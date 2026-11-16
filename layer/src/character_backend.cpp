// Device setup and lifecycle of the configuration execution module.
#include "character_backend_internal.h"
#include "native_images.h"

namespace efvk {
namespace {
efmi::frontend::RuntimeInput sample_ini_input()
{
    efmi::frontend::RuntimeInput input;
    // Deterministic headless GPU tests supply input inside their own child
    // process only. Never send keyboard events or accept this path in the game.
    static const std::filesystem::path fixture = [] {
        wchar_t executable[32768] = {}, path[32768] = {};
        GetModuleFileNameW(nullptr, executable, 32768);
        const auto name = std::filesystem::path(executable).filename();
        if (_wcsicmp(name.c_str(), L"efmi_vk_variant_gpu.exe") && _wcsicmp(name.c_str(), L"efmi_vk_sandbox.exe")) return std::filesystem::path{};
        const DWORD length = GetEnvironmentVariableW(L"EFMI_VK_TEST_KEYS", path, 32768);
        return length && length < 32768 ? std::filesystem::path(path) : std::filesystem::path{};
    }();
    if (!fixture.empty()) {
        std::ifstream in(fixture); int foreground = 0, key = 0, count = 0;
        if (in >> foreground) input.foreground = foreground == 1;
        while (count++ < 256 && in >> key) if (key >= 0 && key < 256) input.keys[key] = true;
        return input;
    }
    DWORD foreground = 0; GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
    input.foreground = foreground == GetCurrentProcessId();
    if (input.foreground) for (int key = 0; key < 256; ++key) input.keys[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
    return input;
}

} // namespace

namespace character_internal {
// Directory generation (D-078): a character Mod is named by its INI instead of a package file.

// A configured folder; a relative one is taken from the layer's own folder, where its configuration
// is (the release names `../Mods` from EFMI\EFMI-Vulkan), never from the game's working directory.
std::filesystem::path absolute_folder(const std::string& path)
{
    std::error_code error;
    auto named = std::filesystem::u8path(path);
    if (named.is_relative()) named = module_folder() / named;
    auto folder = std::filesystem::absolute(named, error).lexically_normal();
    if (folder.has_filename()) return folder;
    return folder.parent_path();
}
// The configured Mods folder. `auto` (the release's) is the Mods folder of the EFMI folder the layer sits
// in or under: the first folder upward from the DLL's own that holds Mods and Core\EFMI, so the release
// works unpacked into EFMI (EFMI\EFMI-Vulkan) and into EFMI\Mods alike.
std::filesystem::path configuration_root()
{
    if (g_cfg.configuration_root != "auto") return absolute_folder(g_cfg.configuration_root);
    std::error_code error;
    for (auto at = module_folder(); !at.empty() && at.has_relative_path(); at = at.parent_path())
        if (std::filesystem::is_regular_file(at / "d3dx.ini", error)) return at;
    return {};
}
std::filesystem::path module_folder()
{
    // Configuration belongs to the injected host, even when replacement code lives elsewhere.
    return std::filesystem::path(module_dir());
}


} // namespace character_internal

void CharacterBackend::open_pipeline_cache()
{
    if (!device) return;
    const auto create = reinterpret_cast<PFN_vkCreatePipelineCache>(device->owned_gdpa(device->handle, "vkCreatePipelineCache"));
    destroy_pipeline_cache = reinterpret_cast<PFN_vkDestroyPipelineCache>(device->owned_gdpa(device->handle, "vkDestroyPipelineCache"));
    pipeline_cache_data = reinterpret_cast<PFN_vkGetPipelineCacheData>(device->owned_gdpa(device->handle, "vkGetPipelineCacheData"));
    if (!create || !destroy_pipeline_cache || !pipeline_cache_data) return;
    char name[64];
    snprintf(name, sizeof(name), "pipeline-cache-%04x-%04x.bin", device->physical_properties.vendorID, device->physical_properties.deviceID);
    const auto folder = g_cfg.m2_settings.empty() ? module_folder() / "settings" : absolute_folder(g_cfg.m2_settings);
    pipeline_cache_path = folder / name;
    std::vector<char> initial;
    {
        std::ifstream in(pipeline_cache_path, std::ios::binary);
        if (in) initial.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    // The driver checks the header (its UUID among it) and ignores data that is not its own.
    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    info.initialDataSize = initial.size(); info.pInitialData = initial.empty() ? nullptr : initial.data();
    if (create(device->handle, &info, nullptr, &pipeline_cache) != VK_SUCCESS && !initial.empty()) {
        info.initialDataSize = 0; info.pInitialData = nullptr;
        if (create(device->handle, &info, nullptr, &pipeline_cache) != VK_SUCCESS) pipeline_cache = VK_NULL_HANDLE;
        else initial.clear();
    }
    pipeline_cache_loaded = pipeline_cache ? initial.size() : 0;
    if (pipeline_cache) logf("variant pipeline cache: %llu bytes from %s", static_cast<unsigned long long>(pipeline_cache_loaded),
                             pipeline_cache_path.u8string().c_str());
}

// After every generation stopped (no variant is being built): the cache written beside the settings.
void CharacterBackend::close_pipeline_cache()
{
    if (!pipeline_cache) return;
    size_t size = 0;
    if (pipeline_cache_data(device->handle, pipeline_cache, &size, nullptr) == VK_SUCCESS && size) {
        std::vector<char> data(size);
        if (pipeline_cache_data(device->handle, pipeline_cache, &size, data.data()) == VK_SUCCESS) {
            std::error_code error;
            std::filesystem::create_directories(pipeline_cache_path.parent_path(), error);
            const auto temporary = pipeline_cache_path.wstring() + L".tmp";
            { std::ofstream out(temporary, std::ios::binary | std::ios::trunc); out.write(data.data(), static_cast<std::streamsize>(size)); }
            std::filesystem::rename(temporary, pipeline_cache_path, error);
            if (!error) pipeline_cache_saved = size;
            logf("variant pipeline cache: %llu bytes written to %s%s", static_cast<unsigned long long>(size),
                 pipeline_cache_path.u8string().c_str(), error ? " failed" : "");
        }
    }
    destroy_pipeline_cache(device->handle, pipeline_cache, nullptr);
    pipeline_cache = VK_NULL_HANDLE;
}

CharacterBackend::~CharacterBackend()
{
    if (twins_sweep.valid()) twins_sweep.get();
    if (folder_loading.valid()) { auto candidate = folder_loading.get(); if (candidate) candidate->stop(); }
    if (folder_staged) folder_staged->stop();
    if (folder) folder->stop();
    for (auto& f : folder_retiring) f.get();
    if (textures) textures->stop(); if (variants) variants->stop(); close_pipeline_cache();
}

void character_device_setup(Device* device, const VkDeviceCreateInfo* info, VkInstance instance, PFN_vkGetInstanceProcAddr gipa)
{
    for(uint32_t i=0;i<info->queueCreateInfoCount;++i)device->application_queue_count+=info->pQueueCreateInfos[i].queueCount;
    device->material_queue_reason = "material queue discovery unavailable";
    if (!gipa) return;
    const auto families = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    const auto get_queue = reinterpret_cast<PFN_vkGetDeviceQueue>(device->owned_gdpa(device->handle, "vkGetDeviceQueue"));
    if (!families || !get_queue) return;
    uint32_t count = 0; families(device->phys, &count, nullptr);
    if (!count || count > 256) return;
    std::vector<VkQueueFamilyProperties> properties(count); families(device->phys, &count, properties.data());
    const VkDeviceQueueCreateInfo* chosen = nullptr;
    for (uint32_t i = 0; i < info->queueCreateInfoCount; ++i) {
        const auto& q = info->pQueueCreateInfos[i];
        if (q.queueFamilyIndex < properties.size() && (properties[q.queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            logf("character graphics queue request: family=%u count=%u flags=%u", q.queueFamilyIndex, q.queueCount, q.flags);
            if (chosen || q.flags || !q.queueCount || q.queueCount > 32) {
                device->material_queue_reason = "materials require one unprotected graphics queue family, at most 32 queues";
                return;
            }
            chosen = &q;
        }
    }
    if (!chosen) { device->material_queue_reason = "no enabled graphics queue family"; return; }
    device->material_family = chosen->queueFamilyIndex;
    device->material_bc_enabled = info->pEnabledFeatures && info->pEnabledFeatures->textureCompressionBC;
    for (auto p = static_cast<const VkBaseInStructure*>(info->pNext); p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
            device->material_bc_enabled = reinterpret_cast<const VkPhysicalDeviceFeatures2*>(p)->features.textureCompressionBC != 0;
    // What owned GPU work may rely on: the core features the application enabled, and float
    // controls when core 1.2 or the extension is enabled on this device (D-072).
    if (info->pEnabledFeatures) device->enabled_features = *info->pEnabledFeatures;
    for (auto p = static_cast<const VkBaseInStructure*>(info->pNext); p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
            device->enabled_features = reinterpret_cast<const VkPhysicalDeviceFeatures2*>(p)->features;
    const uint32_t effective = std::min(device->app_api_version ? device->app_api_version : VK_API_VERSION_1_0, device->api_version);
    bool float_extension = false;
    for (uint32_t i = 0; i < info->enabledExtensionCount; ++i)
        float_extension |= !strcmp(info->ppEnabledExtensionNames[i], VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
    const auto properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(gipa(instance, "vkGetPhysicalDeviceProperties2"));
    if (properties2 && (effective >= VK_API_VERSION_1_2 || float_extension)) {
        VkPhysicalDeviceProperties2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        query.pNext = &device->float_properties;
        properties2(device->phys, &query);
        device->float_controls = true;
    }
    if (properties2 && effective >= VK_API_VERSION_1_3) {
        VkPhysicalDeviceProperties2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        query.pNext = &device->texel_alignment_properties;
        properties2(device->phys, &query);
        device->texel_alignment = true;
    }
    if (effective >= VK_API_VERSION_1_1)
        device->format_properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties2>(gipa(instance, "vkGetPhysicalDeviceFormatProperties2"));
    device->material_format_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(gipa(instance, "vkGetPhysicalDeviceFormatProperties"));
    device->material_image_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties>(gipa(instance, "vkGetPhysicalDeviceImageFormatProperties"));
    // The device-local memory still free, for activating character packages (D-092).
    if (effective >= VK_API_VERSION_1_1) {
        device->memory_properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(gipa(instance, "vkGetPhysicalDeviceMemoryProperties2"));
        const auto enumerate = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(gipa(instance, "vkEnumerateDeviceExtensionProperties"));
        uint32_t count = 0;
        if (enumerate && device->memory_properties2 && enumerate(device->phys, nullptr, &count, nullptr) == VK_SUCCESS && count) {
            std::vector<VkExtensionProperties> found(count);
            if (enumerate(device->phys, nullptr, &count, found.data()) == VK_SUCCESS)
                for (uint32_t i = 0; i < count; ++i)
                    device->memory_budget |= !strcmp(found[i].extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
        }
    }
    std::vector<VkQueue> queues;
    for (uint32_t i = 0; i < chosen->queueCount; ++i) {
        VkQueue queue = VK_NULL_HANDLE;
        get_queue(device->handle, device->material_family, i, &queue);
        if (!queue || (device->set_loader_data && device->set_loader_data(device->handle, queue) != VK_SUCCESS)) {
            device->material_queue_reason = "cannot initialize material queue dispatch"; return;
        }
        queues.push_back(queue);
    }
    device->material_queues = std::move(queues);
    device->material_queue_reason.clear();
}

std::shared_ptr<CharacterBackend> character_create(Device* d, const VkPhysicalDeviceProperties& p, const VkPhysicalDeviceMemoryProperties& m)
{
    auto backend = std::make_shared<CharacterBackend>();
    backend->device = d;
    backend->enabled.store(g_cfg.m2_enabled, std::memory_order_relaxed);
    backend->open_pipeline_cache();
    {
        VariantDispatch draw;
#define X(n) draw.n = reinterpret_cast<PFN_vk##n>(d->owned_gdpa(d->handle, "vk" #n));
        EFVK_VARIANT_FUNCS(X)
#undef X
        if (draw.CreateGraphicsPipelines && draw.CreateRenderPass && draw.CmdDrawIndexed)
            backend->variants = std::make_shared<VariantCache>(d->handle, draw, p.limits.maxBoundDescriptorSets, 4096, p.limits, backend->pipeline_cache,
                                                               VariantCache::builders_for_device());
        else logf("character variants unavailable: missing graphics entry");
    }
    backend->textures = std::make_unique<TextureResidency>(d, m);
    if (!backend->textures->usable()) logf("character textures unavailable: %s", backend->textures->reason().c_str());
    backend->properties = p; backend->memory = m;
    backend->folder = folder_create(d, m, backend->variants);
    backend->reason = backend->folder->reason;
    backend->folder->announce();
    shader_hooks_frame(*backend, false);
    g_dither_off.store(std::any_of(backend->folder->gates.begin(), backend->folder->gates.end(),
        [](const auto& gate) { return gate.value != 0; }), std::memory_order_relaxed);
    g_dither_gated.store(!backend->folder->gates.empty(), std::memory_order_relaxed);
    prepare_twins(*backend);
    backend->twins_swept = g_dither_gated.load(std::memory_order_relaxed);
    return backend;
}

void character_stop(CharacterBackend& b)
{
    if (b.twins_sweep.valid()) b.twins_sweep.get();
    { std::unique_lock<std::shared_mutex> lock(b.index_mx); b.shader_hooks.reset(); }
    if (b.folder_loading.valid()) { auto candidate = b.folder_loading.get(); if (candidate) candidate->stop(); }
    if (b.folder_staged) { b.folder_staged->stop(); b.folder_staged.reset(); }
    if (b.folder) b.folder->stop();
    for (auto& f : b.folder_retiring) f.get();
    b.folder_retiring.clear();
    if (b.textures) b.textures->stop(); if (b.variants) b.variants->stop(); b.close_pipeline_cache();
}

void character_retire(CharacterBackend& b, uint32_t id)
{
    if (b.variants) {
        b.variants->retire(id);
        for (uint32_t k = 0; k < kCaptureKinds; ++k) b.variants->retire(capture_identity(id, CaptureKind(k)));
        b.variants->retire(twin_identity(id));
    }
    {
        std::unique_lock<std::shared_mutex> lock(b.twins_mx);
        for (auto it = b.twins.begin(); it != b.twins.end();)
            if (std::get<1>(it->first) == id) it = b.twins.erase(it); else ++it;
        for (auto it = b.twins_failed.begin(); it != b.twins_failed.end();)
            if (std::get<1>(*it) == id) it = b.twins_failed.erase(it); else ++it;
    }
    { std::unique_lock<std::shared_mutex> lock(g_dither_mx); g_dither_pipelines.erase(id); }
}
// After the application ended a classic render pass (D-113). The library's depth-copy hook: every
// draw of the game's copy shader in the pass is told to the generations that loaded EFMITTL (its pre
// list: `$copy_after_lit` when the lit pass was seen). Then the pass's captures and composite.
void character_pass_end(CharacterBackend&, CmdBuf*) {}

VkResult character_submit(CharacterBackend& b, VkQueue q, SubmitWork&)
{
    if (b.textures) { const auto result = b.textures->before_submit(q); if (result != VK_SUCCESS) return result; }
    return VK_SUCCESS;
}
void character_submitted(CharacterBackend&, VkQueue, SubmitWork&, VkResult) {}
// Each package runs its Present work in turn; a presenter waits for what the one before handed on.
void character_frame(CharacterBackend& b, bool had_draws, PresentWork* present)
{
    // The presentation's work of every package (or of the folder generation, D-131), timed for the status.
    struct Clock {
        CharacterBackend& b; uint64_t began;
        ~Clock() { b.frame_us += host_time_us() - began; ++b.frame_calls; }
    } clock{b, host_time_us()};
    if (had_draws) b.frames.fetch_add(1, std::memory_order_relaxed);
    if (b.textures) b.textures->frame();
    // The keys, the window and the cursor, once for every package (they cost per package before).
    auto input = sample_ini_input();
    OwnedPresenter::sample(b.device, present, input);
    folder_frame_and_exchange(b, had_draws, present, input);
    // Route 2: the ShaderOverride hashes of the generation in place, the hunting keys of this presentation.
    shader_hooks_frame(b);
}
bool character_usable(CharacterBackend& b, std::string& reason)
{
    const bool loaded = b.folder && b.folder->program;
    reason = loaded ? std::string{} : b.folder ? b.folder->reason : b.reason;
    return loaded;
}
// Every usable package is ready; a lazy one counts as ready, it prepares on its first match.
bool character_ready(CharacterBackend& b) { return b.folder && b.folder->program; }
void character_enable(CharacterBackend& b, bool enabled) { b.enabled.store(enabled, std::memory_order_relaxed); }

const CharacterModule* builtin_character_module()
{
    static const CharacterModule module = [] {
        CharacterModule m;
        m.size = sizeof(CharacterModule); m.abi = EFVK_ABI; m.version = EFVK_VERSION;
        m.create = character_create; m.usable = character_usable; m.ready = character_ready; m.activate = character_activate; m.stop = character_stop;
        m.record = character_record; m.retire = character_retire; m.frame = character_frame; m.status = character_status;
        m.enable = character_enable; m.submit = character_submit; m.submitted = character_submitted; m.pass_end = character_pass_end;
        m.bind = [](CharacterBackend& b, CmdBuf* cb, VkPipeline pipeline) { return character_bind(b, cb, pipeline); };
        m.created = character_created;
        m.shader_draw = character_shader_draw;
        m.native_bind = native_target_bind; m.native_call = native_images_call;
        m.native_begin_classic = native_images_begin; m.native_begin_rendering = native_images_begin;
        m.native_end_images = native_images_end; m.native_end_targets = native_targets_end;
        return m;
    }();
    return &module;
}

} // namespace efvk
