#include "character_backend.h"
#include "character_module.h"
#include "command_owner.h"
#include "draw_snapshots.h"
#include "variant_cache.h"
#include "character_package.h"
#include "character_program.h"
#include "character_present.h"
#include "character_materials.h"
#include "../../frontend/gpu/source_registers.h"
#include "../../frontend/gpu/texture_roles.h"
#include "texture_identity.h"
#include "merged_skeleton.h"
#include "state.h"
#include "work.h"
#include "json.h"
#include "../../frontend/assets/resource_cache.h"

#include <algorithm>
#include <filesystem>
#include <array>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <set>
#include <future>
#include <fstream>

namespace efvk {
namespace {
// Directory generation (D-078): a character Mod is named by its INI instead of a package file.
bool is_mod_ini(const std::string& path)
{
    const auto extension = std::filesystem::u8path(path).extension().wstring();
    return !_wcsicmp(extension.c_str(), L".ini");
}
std::filesystem::path absolute_folder(const std::string& path)
{
    std::error_code error;
    auto folder = std::filesystem::absolute(std::filesystem::u8path(path), error).lexically_normal();
    if (folder.has_filename()) return folder;
    return folder.parent_path();
}
std::filesystem::path module_folder()
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&module_folder), &module))
        return {};
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}
CharacterProgram::Source mod_source(const std::string& ini)
{
    CharacterProgram::Source source;
    source.ini = std::filesystem::absolute(std::filesystem::u8path(ini)).lexically_normal();
    const auto mods = absolute_folder(g_cfg.m2_mods);
    source.core = g_cfg.m2_core.empty() ? mods.parent_path() / "Core" / "EFMI" : absolute_folder(g_cfg.m2_core);
    source.settings = g_cfg.m2_settings.empty() ? module_folder() / "settings" : absolute_folder(g_cfg.m2_settings);
    source.mod_namespace = CharacterProgram::declared_namespace(source.ini);
    return source;
}
}
namespace {
// Values of the core this follows (SpatialIdentification.ini): cell size of the position hash, base
// radius in cells per 1/120 s, and the number of distinct components that identifies the object.
constexpr float kCellSize = 0.125f;
constexpr float kBaseRadius = 3.0f;
constexpr uint32_t kIdentifyingComponents = 4;
constexpr uint64_t kExpiryFrames = 2;
// Two clocks. The character's frame is one recording of the command buffer its draws are in: the
// game records all draws of a character for one frame into one command buffer, and it starts on
// the next frame before the previous one is presented, so a present is no boundary while
// recording. Presents after which something was drawn are counted too; they pace the periodic work
// and, together with time, the expiry: with frame generation presents come between the game's
// frames, and the game leaves the character out for some frames of a dash, so an identity is given
// up only after the core's two counted presents and this much time without a draw.
constexpr uint64_t kExpiryMicroseconds = 500000;
constexpr size_t kIdentities = 16;
constexpr uint32_t kArenaEntries = 256, kArenaSlices = 64;
constexpr uint64_t kRehashFrames = 256;
// Fragment shaders without a profile in the package whose rules are derived from the module.
constexpr size_t kDerivedProfiles = 256;

float ini_seconds()
{
    static const uint64_t origin = host_time_us();
    return float((host_time_us() - origin) * 1.0e-6);
}

efmi::frontend::RuntimeInput sample_ini_input()
{
    efmi::frontend::RuntimeInput input;
    // Deterministic headless GPU tests supply input inside their own child
    // process only. Never send keyboard events or accept this path in the game.
    static const std::filesystem::path fixture = [] {
        wchar_t executable[32768] = {}, path[32768] = {};
        GetModuleFileNameW(nullptr, executable, 32768);
        if (_wcsicmp(std::filesystem::path(executable).filename().c_str(), L"efmi_vk_variant_gpu.exe")) return std::filesystem::path{};
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

enum Counter : uint32_t {
    C_MATCHED, C_REPLACED, C_ITEMS, C_HIDDEN, C_IDENTIFYING, C_INCOMPLETE, C_PENDING,
    C_STATE, C_INSTANCES, C_SET3, C_PIPELINE, C_VERTEX, C_VARIANT, C_BINDINGS,
    C_ENTRY, C_BONES, C_IMPORT, C_EXHAUSTED, C_IDENTITIES, C_SET3_RESTORED,
    C_MATERIAL, C_IMAGE, C_IMAGE_PENDING, C_MATERIAL_ITEMS, C_COMPAT_ITEMS, C_DERIVED, C_INHERITED,
    C_IDENTITY_NEW, C_IDENTITY_EXPIRED, C_TEXTURED, C_COUNT
};
const char* const kCounterNames[C_COUNT] = {
    "matched", "replaced", "items", "hidden", "identifying", "incomplete", "pending",
    "refused_state", "refused_instances", "refused_set3", "refused_pipeline", "refused_vertex", "refused_variant",
    "refused_bindings", "unreadable_entry", "unreadable_bones", "import_rejected", "exhausted", "identities_full",
    "restored_set3", "refused_material", "refused_image", "image_pending", "material_items", "dimension_zero_items",
    "material_derived", "material_inherited", "identities_created", "identities_expired", "textured"
};

struct GpuMesh {
    VkDevice device;
    SnapshotDispatch d;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    bool device_local = false;
    GpuMesh(VkDevice dev, const SnapshotDispatch& dispatch) : device(dev), d(dispatch) {}
    ~GpuMesh()
    {
        if (buffer) d.DestroyBuffer(device, buffer, nullptr);
        if (memory) d.FreeMemory(device, memory, nullptr);
    }
    bool create(const std::vector<uint8_t>& payload, const VkPhysicalDeviceMemoryProperties& properties, std::string& reason)
    {
        auto good = [&](VkResult result, const char* text) { if (result == VK_SUCCESS) return true; reason = text; return false; };
        VkBufferCreateInfo ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        // Room behind the last stream: an element may reach past its file's stride (kCharacterOvershoot).
        const size_t padding = 256;
        ci.size = payload.size() + padding; ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (!good(d.CreateBuffer(device, &ci, nullptr, &buffer), "create character mesh buffer")) return false;
        VkMemoryRequirements req; d.GetBufferMemoryRequirements(device, buffer, &req);
        if (req.size > (768ull << 20)) { reason = "character mesh allocation budget exceeded"; return false; }
        // A whole character is read by the device in every pass of every frame: it belongs in device
        // memory. Host memory works, but far slower; it is the fallback, and the status says which.
        uint32_t type = host_memory_type(properties, req.memoryTypeBits, true);
        device_local = type != UINT32_MAX;
        if (!device_local) type = host_memory_type(properties, req.memoryTypeBits, false);
        if (type == UINT32_MAX) { reason = "host-coherent character mesh memory unavailable"; return false; }
        VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size; ai.memoryTypeIndex = type;
        void* mapped = nullptr;
        if (!good(d.AllocateMemory(device, &ai, nullptr, &memory), "allocate character mesh memory") ||
            !good(d.BindBufferMemory(device, buffer, memory, 0), "bind character mesh memory") ||
            !good(d.MapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped), "map character mesh memory")) return false;
        std::memcpy(mapped, payload.data(), payload.size());
        std::memset(static_cast<uint8_t*>(mapped) + payload.size(), 0, padding);
        d.UnmapMemory(device, memory);
        return true;
    }
};

struct Identity {
    bool used = false, custom = false;
    int32_t cell[3] = {};
    uint64_t first_frame = 0, last_frame = 0, update_frame = 0; // in frames of the character
    uint64_t last_present = 0;       // counted presents at the last draw
    uint64_t last_us = 0;            // host time of the last draw
    uint64_t id = 0, generation = 0; // generation changes whenever the master's bytes change
    uint32_t seen = 0;               // components met since this identity appeared
    MergedSkeleton skeleton;
    std::vector<uint8_t> coverage;   // per component: bit 0 current pose imported, bit 1 previous
};

struct IndexKey {
    VkBuffer buffer; uint64_t offset; uint32_t bytes;
    bool operator==(const IndexKey& o) const { return buffer == o.buffer && offset == o.offset && bytes == o.bytes; }
};
struct IndexKeyHash {
    size_t operator()(const IndexKey& k) const
    {
        return std::hash<uint64_t>()(reinterpret_cast<uint64_t>(k.buffer) ^ (k.offset * 0x9E3779B97F4A7C15ull) ^ (uint64_t(k.bytes) << 40));
    }
};
struct IndexHash { uint32_t buffer_id = 0; uint64_t gen = 0, next_full = 0; uint32_t hash = 0, quick = 0; };
struct BoneSite { bool ok = false; uint32_t set = 0, binding = 0; };
using StaticMaterial = CharacterMaterial;
// Rules of a fragment shader the package has no profile for, read off its module by the DX11
// register rule (D-076), prepared like a profile.
struct DerivedProfile {
    uint32_t crc = 0;
    std::vector<uint32_t> slots; // the material slots the rules were read for
    std::vector<uint32_t> code;
    std::vector<CharacterMaterialRule> rules;
    std::vector<StaticMaterial> plans;
};
// The character backend's part of a command buffer. The counter outlives the backend: a command
// may still be sealed after the code it came from was exchanged.
struct CharacterCommand : CommandOwner {
    std::shared_ptr<const void> generation;
    std::shared_ptr<DrawSnapshots> snapshots;
    std::shared_ptr<const void> keep; // the mesh
    std::vector<std::shared_ptr<const PipelineVariant>> variants;
    std::map<uint32_t, std::shared_ptr<DrawSnapshots>> material_snapshots;
    std::shared_ptr<std::atomic<uint64_t>> batches;
    void reset_after_completion() override
    {
        used = false;
        variants.clear(); snapshots->recycle_completed();
        for (auto& arena : material_snapshots) arena.second->recycle_completed();
    }
    bool seal() override
    {
        if (snapshots->sealed()) return false;
        snapshots->seal();
        for (auto& arena : material_snapshots) arena.second->seal();
        ++*batches;
        return true;
    }
};
// A graphics queue that may consume the character's images: per publication batch the semaphore it
// waits on before forwarding application work, and the fence of that wait submission.
struct ImageQueue {
    VkQueue queue = VK_NULL_HANDLE;
    std::vector<VkSemaphore> semaphores;
    std::vector<VkFence> fences;
    uint32_t waited = 0; // batches this queue has waited for
};
// Images are published in at most this many batches: each batch as soon as at least one more image
// is Ready, the last one only once every image is Ready or failed.
constexpr uint32_t kPublicationBatches = 16;
} // namespace

class CharacterGeneration {
public:
    Device* device;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
    VariantDispatch draw;
    SnapshotDispatch storage;
    CharacterPackage package;
    std::unique_ptr<CharacterProgram> program;
    // Owned GPU execution of the generation's Present work; destroyed before the program.
    std::unique_ptr<OwnedPresenter> presenter;
    std::atomic<bool> save_requested{false};
    std::unordered_map<uint32_t, std::vector<uint32_t>> by_count; // immutable after init
    // Sections matched by their hash alone (any index count): hidden game meshes. Only an activated
    // character's draws are hashed for them, every indexed draw then. Immutable after init.
    std::vector<uint32_t> hash_only;
    std::vector<BoneComponent> ranges;
    uint32_t bone_bytes = 0;
    std::unique_ptr<VariantCache> cache;
    efmi::assets::ResourceBudgets image_budgets;
    std::unique_ptr<efmi::assets::ResourceCache> images;
    std::vector<efmi::assets::ResourceHandle> image_handles;
    std::vector<std::vector<StaticMaterial>> materials; // profile x package draw, prepared at setup
    std::unordered_map<uint32_t, uint32_t> material_profiles; // pipeline generation -> exact profile
    std::vector<uint32_t> material_slots;                          // DX11 pixel slots the package binds
    // Under the mod lock: the DX11 registers of each pipeline's fragment module (D-076), for texture
    // queries; `readable` false when the register rule could not read the module.
    struct FragmentRegisters { bool readable = false; std::vector<efmi::gpu::SourceRegister> list; };
    std::unordered_map<uint32_t, FragmentRegisters> fragment_registers;
    // Under the mod lock: the image samples of each pipeline's fragment module, for RabbitFX's
    // texture roles (D-082); `readable` false when the module is not read.
    struct FragmentSamples { bool readable = false; std::vector<efmi::gpu::RoleSample> list; };
    std::unordered_map<uint32_t, FragmentSamples> fragment_samples;
    // Under the mod lock: what the original Core reads off each pipeline's vertex shader (D-079):
    // the instance-config cb slot and whether the bones come from the rolling vs-t0 buffer.
    struct SourceFacts { int32_t vs_config_slot = -1; bool rolling_bones = false; };
    std::unordered_map<uint32_t, SourceFacts> source_facts;
    uint64_t facts_unknown = 0;
    uint64_t texture_queries = 0, texture_unknown = 0;
    uint64_t role_queries = 0, role_draws = 0; // RabbitFX role queries, and those that found a diffuse map
    const char* texture_reason = nullptr; // why the latest unknown answer was unknown
    std::unordered_map<uint32_t, uint32_t> derived_for_pipeline;   // pipeline generation -> derived profile or none
    std::vector<std::unique_ptr<DerivedProfile>> derived_profiles;
    uint32_t rule_mismatches = 0;                                  // package profiles the register rule disagrees with
    std::set<uint32_t> image_counts{0};
    std::mutex queue_mx;
    bool queues_stopped = false;
    std::vector<ImageQueue> image_queues;
    VkQueue upload_queue = VK_NULL_HANDLE;
    std::vector<VkFence> publication_fences;      // the signal submission of each batch
    uint32_t publication_batches = 0, publications = 0; // under queue_mx
    // Per image: its publication signal is enqueued, so a recording may retain its view. Every image
    // published: `images_published` (an F10 candidate takes over only then).
    std::unique_ptr<std::atomic<bool>[]> image_published;
    std::atomic<uint32_t> published_images{0};
    std::atomic<bool> images_published{false};
    std::atomic<uint32_t> image_queue_waits{0}, upload_queue_index{UINT32_MAX}, image_publications{0};
    PFN_vkCreateSemaphore CreateSemaphore = nullptr;
    PFN_vkDestroySemaphore DestroySemaphore = nullptr;
    PFN_vkCreateFence CreateFence = nullptr;
    PFN_vkDestroyFence DestroyFence = nullptr;
    PFN_vkWaitForFences WaitForFences = nullptr;
    std::atomic<bool> enabled{false}, ready{false};
    std::atomic<uint64_t> frame{1};
    std::shared_ptr<std::atomic<uint64_t>> batches = std::make_shared<std::atomic<uint64_t>>(0);
    std::shared_ptr<const void> generation_key = std::make_shared<int>(0);
    std::atomic<uint64_t> counters[C_COUNT] = {};
    std::atomic<float> frame_seconds{1.0f / 60};
    uint64_t last_frame_us = 0;

    // Resources prepared off the draw path.
    std::mutex pool_mx;
    std::condition_variable wake;
    bool stopped = false;
    std::thread worker;
    std::shared_ptr<const GpuMesh> mesh;
    std::deque<std::shared_ptr<CharacterCommand>> available;
    std::vector<std::weak_ptr<CharacterCommand>> all;
    std::string last_reason;

    // The mod's state, under the mod lock: one matched draw at a time.
    std::mutex mod_mx;
    uint32_t lod = 0;
    std::array<Identity, kIdentities> identities;
    uint64_t next_identity = 1;
    uint32_t identity_logs = 0;
    std::unordered_map<IndexKey, IndexHash, IndexKeyHash> hashes;
    std::unordered_map<uint32_t, BoneSite> sites;
    std::vector<uint8_t> scratch, current, previous;

    // `lazy`: one of several packages (a directory). Its mesh, arenas and images go to the GPU only
    // when one of its sections first matches a draw, so characters not on screen take no memory there.
    CharacterGeneration(Device* dev, const VkPhysicalDeviceProperties& p, const VkPhysicalDeviceMemoryProperties& m,
                        std::string path = g_cfg.m2_package, bool deferred = false)
        : device(dev), properties(p), memory(m), package_path(std::move(path)), lazy(deferred) {}
    std::string package_path;
    bool lazy = false;
    std::atomic<bool> activated{false};
    ~CharacterGeneration() { stop(); }

    void note(const std::string& reason) { std::lock_guard<std::mutex> lock(pool_mx); last_reason = reason; }
    // A fragment shader without a profile of its own keeps the application's images; logged once per
    // pipeline and slot set (derive caches the answer).
    uint32_t refuse_profile(const std::string& reason, uint32_t result) { note(reason); logf("%s", reason.c_str()); return result; }
    bool refuse(Counter counter, const std::string& reason) { ++counters[counter]; note(reason); return false; }

    bool init()
    {
        const bool discovered = is_mod_ini(package_path);
        if (!discovered && !package.load(package_path, last_reason)) return false;
        if (discovered || CharacterProgram::configured(package_path)) {
            program = std::make_unique<CharacterProgram>();
            const auto gpu = [this](const std::filesystem::path& working, std::string& why) {
                return OwnedPresenter::runtime(device, memory, working, why);
            };
            if (discovered) {
                // The Mod's INI itself: the package is made from what the generation declares (D-078).
                CharacterProgram::Source source;
                try { source = mod_source(package_path); } catch (const std::exception& e) { last_reason = e.what(); return false; }
                if (!program->discover(source, package, last_reason, gpu)) return false;
                // The measured dimension policy, on the device it was measured on only.
                if (g_cfg.m2_dimension_zero_vendor && g_cfg.m2_dimension_zero_vendor == properties.vendorID &&
                    g_cfg.m2_dimension_zero_device == properties.deviceID) {
                    package.material_vendor = properties.vendorID; package.material_device = properties.deviceID;
                }
                logf("character Mod read from its INI: %s: %zu mesh files, %zu images, %zu sections, %zu mesh sections left to the game",
                     package_path.c_str(), package.streams.size(), package.images.size(), package.sections.size(), program->left_to_game());
            } else if (!program->init(package_path, package, last_reason, gpu)) return false;
            if (program->gpu()) presenter = std::make_unique<OwnedPresenter>(device, program->gpu());
            else if (!program->gpu_reason().empty()) logf("owned GPU work unavailable: %s", program->gpu_reason().c_str());
            {
                // What the original overlay would show after a load; the generation carries on (D-067).
                const auto loaded = program->status();
                if (loaded.diagnostics) logf("INI loaded with %zu original warning(s)", loaded.diagnostics);
                for (const auto& text : loaded.diagnostic_messages) logf("INI warning: %s", text.c_str());
            }
        }
#define X(n) draw.n = reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle, "vk" #n));
        EFVK_VARIANT_FUNCS(X)
#undef X
#define X(n) storage.n = reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle, "vk" #n)); \
        if (!storage.n) { last_reason = "missing vk" #n; return false; }
        EFVK_SNAPSHOT_FUNCS(X)
#undef X
        if (!draw.CreateGraphicsPipelines || !draw.CreateRenderPass || !draw.CmdDrawIndexed) { last_reason = "missing graphics entry"; return false; }
        for (uint32_t i = 0; i < package.sections.size(); ++i) {
            if (package.sections[i].match_count == kCharacterNone) hash_only.push_back(i);
            else by_count[package.sections[i].match_count].push_back(i);
        }
        for (const auto& c : package.components) ranges.push_back({c.first, c.count});
        bone_bytes = (MergedSkeleton::kImplicitBones + 2 * package.bones) * static_cast<uint32_t>(MergedSkeleton::kBytesPerBone);
        if (package.materials && !init_materials()) return false;
        if (program || package.materials) {
            // Rules a fragment module gives (D-076) can use any count up to the slots one draw binds; a
            // live INI chooses them at run time, so a program gets every count of the budget.
            // Allocate these arenas before first drawing.
            uint32_t maximum = program ? 8 : 0;
            for (const auto& pixels : package.pixels) maximum = std::max(maximum, static_cast<uint32_t>(std::min<size_t>(pixels.size(), 64)));
            for (const auto& profile : package.profiles) {
                uint32_t count = 0;
                for (const auto& rule : profile.rules) count += rule.kind == CharacterMaterialRule::Image;
                maximum = std::max(maximum, count);
            }
            if (maximum > 8) { last_reason = "runtime material slots exceed the image arena budget"; return false; }
            for (uint32_t i = 0; i <= maximum; ++i) image_counts.insert(i);
        }
        // One variant per pipeline and mesh draw. A character is drawn by some hundred pipelines once
        // the feature variants of its shaders are counted; a full cache would refuse the rest for good.
        cache = std::make_unique<VariantCache>(device->handle, draw, properties.limits.maxBoundDescriptorSets, 2048, properties.limits);
        enabled = g_cfg.m2_enabled;
        if (!lazy) activate();
        return true;
    }

    // Caller holds mod_mx, or init runs alone. The GPU side of the generation: images demanded, mesh
    // and arenas prepared by the worker. Once.
    void activate()
    {
        if (activated.exchange(true)) return;
        if (images)
            for (const auto& handle : image_handles)
                if (!images->demand(handle)) { note("character texture could not be demanded"); break; }
        if (lazy) logf("character package activated on its first match: %s", package_path.c_str());
        worker = std::thread([this] { prepare(); });
    }

    // After a refused init: nothing of the generation runs. The worker was not started yet.
    void abandon()
    {
        enabled = false;
        presenter.reset();
        program.reset();
    }

    void stop()
    {
        { std::lock_guard<std::mutex> lock(pool_mx); stopped = true; enabled = false; wake.notify_all(); }
        if (worker.joinable()) worker.join();
        if (presenter) presenter->stop();
        if (program) { std::string reason; if (!program->save(reason)) note("INI shutdown settings: " + reason); }
        if (cache) cache->stop();
        {
            std::lock_guard<std::mutex> lock(queue_mx);
            queues_stopped = true;
            if (images) images->stop();
            // Device shutdown only. Never wait for a queue or fence on the draw/submit path.
            for (uint32_t b = 0; b < publications; ++b) WaitForFences(device->handle, 1, &publication_fences[b], VK_TRUE, UINT64_MAX);
            for (auto& q : image_queues) {
                for (uint32_t b = 0; b < q.waited; ++b) WaitForFences(device->handle, 1, &q.fences[b], VK_TRUE, UINT64_MAX);
                for (auto s : q.semaphores) if (s) DestroySemaphore(device->handle, s, nullptr);
                for (auto f : q.fences) if (f) DestroyFence(device->handle, f, nullptr);
            }
            image_queues.clear();
            for (auto f : publication_fences) if (f) DestroyFence(device->handle, f, nullptr);
            publication_fences.clear(); publications = publication_batches = 0;
        }
        std::deque<std::shared_ptr<CharacterCommand>> old;
        std::shared_ptr<const GpuMesh> old_mesh;
        { std::lock_guard<std::mutex> lock(pool_mx); old.swap(available); old_mesh.swap(mesh); ready = false; }
    }

    void prepare()
    {
        try {
            auto created = std::make_shared<GpuMesh>(device->handle, storage);
            std::string reason;
            if (!created->create(package.payload, memory, reason)) { note(reason); return; }
            logf("character mesh uploaded: %llu bytes in %s memory", static_cast<unsigned long long>(package.payload.size()),
                 created->device_local ? "device" : "host");
            { std::lock_guard<std::mutex> lock(pool_mx); mesh = created; }
            while (true) {
                if (program && save_requested.exchange(false)) {
                    std::string error;
                    if (!program->save(error)) note("INI settings: " + error);
                }
                {
                    std::unique_lock<std::mutex> lock(pool_mx);
                    all.erase(std::remove_if(all.begin(), all.end(), [](const auto& p) { return p.expired(); }), all.end());
                    if (stopped) return;
                    if (available.size() >= 2 || all.size() >= 8) { wake.wait_for(lock, std::chrono::milliseconds(100)); continue; }
                }
                auto batch = std::make_shared<CharacterCommand>();
                batch->generation = generation_key; batch->backend = generation_key.get(); batch->batches = batches;
                batch->keep = created;
                uint64_t total_bytes = 0;
                for (uint32_t count : image_counts) {
                    auto snapshots = std::make_shared<DrawSnapshots>(device->handle, storage);
                    if (!snapshots->create_shared(memory, properties.limits, kArenaEntries, kArenaSlices, bone_bytes, 65536, count)) {
                        note(snapshots->reason); return;
                    }
                    total_bytes += snapshots->size();
                    if (total_bytes > (64ull << 20)) { note("character material arenas exceed the batch budget"); return; }
                    if (count) batch->material_snapshots.emplace(count, std::move(snapshots));
                    else batch->snapshots = std::move(snapshots);
                }
                {
                    std::lock_guard<std::mutex> lock(pool_mx);
                    if (stopped) return;
                    all.push_back(batch); available.push_back(std::move(batch)); ready = true;
                }
            }
        } catch (const std::exception& e) { note(e.what()); }
    }

    std::shared_ptr<CharacterCommand> command()
    {
        std::lock_guard<std::mutex> lock(pool_mx);
        if (stopped || available.empty() || !mesh) return {};
        auto cmd = available.front(); available.pop_front();
        wake.notify_one();
        return cmd;
    }

    bool init_materials();
    bool refresh_materials()
    {
        // The slots the mod binds are those of every planned draw. A live plan may bind one no earlier
        // draw did (a key, a texture section): rules read for fewer slots stay with what they were made
        // for, and every pipeline derives its rules again for the larger set.
        {
            std::set<uint32_t> slots(material_slots.begin(), material_slots.end());
            for (const auto& draw : package.pixels) for (const auto& pixel : draw) slots.insert(pixel.slot);
            if (slots.size() != material_slots.size()) {
                material_slots.assign(slots.begin(), slots.end());
                derived_for_pipeline.clear();
            }
        }
        std::set<uint32_t> counts;
        std::string reason;
        if (!prepare_character_materials(package, materials, counts, reason)) return refuse(C_MATERIAL, reason);
        for (auto& derived : derived_profiles) {
            if (derived->slots != material_slots) continue;
            CharacterMaterialProfile profile{derived->code, derived->rules};
            if (!prepare_character_profile(package, profile, derived->plans, counts, reason)) return refuse(C_MATERIAL, reason);
        }
        for (auto count : counts) if (!image_counts.count(count)) return refuse(C_MATERIAL, "runtime material lacks a prepared arena");
        return true;
    }
    // `origin`: 0 the package's own profile or nothing to bind, 1 rules derived from the module, 2 the application's images.
    bool material_draws(const PipeInfo&, uint32_t section, const DrawRec&, uint64_t now, const CharacterList&,
                        std::vector<const StaticMaterial*>&, std::vector<std::vector<SnapshotImage>>&, uint32_t& origin);

    // Material rules of a fragment module for the slots the package binds, by the DX11 register rule
    // (D-076): a slot whose t register the module does not declare is absent, as an unread slot is
    // under DX11; a single image is bound there. False for a module the rule cannot read or a slot on
    // something else than one image (left to the application's images, never guessed).
    bool rule_rules(const std::vector<uint32_t>& code, std::vector<CharacterMaterialRule>& rules, std::string& reason) const
    {
        std::vector<efmi::gpu::SourceRegister> registers;
        if (!efmi::gpu::source_registers(code, registers, reason)) return false;
        rules.clear();
        for (uint32_t slot : material_slots) {
            const auto* r = efmi::gpu::find_source_register(registers, 't', slot);
            CharacterMaterialRule rule;
            rule.slot = slot;
            // The slots are those of every draw of the Mod (RabbitFX roles among them, D-082); a slot that
            // holds no single image in this shader (a buffer, an array) takes no file image here, as
            // one it does not declare, and the rest of the shader's slots still do.
            if (!r || r->kind != efmi::gpu::SourceRegister::ImageSrv || r->count != 1) {
                rule.kind = CharacterMaterialRule::Absent; rules.push_back(rule); continue;
            }
            // The device-bound compatibility policy of the package covers a 2D file at a 3D image.
            rule.kind = package.material_vendor && r->dim == 2 ? CharacterMaterialRule::ZeroFile : CharacterMaterialRule::Image;
            rule.set = r->set; rule.binding = r->binding;
            rules.push_back(rule);
        }
        return true;
    }

    // Caller holds mod_mx and res_mx shared. The application's texture at DX11 pixel slot `slot` of the
    // draw, by its original resource hash (D-077): the slot's binding comes from the fragment module's
    // registers (D-076), the image from the tracked descriptor. A slot the module does not declare reads
    // nothing here; a resource whose hash cannot be known answers a bound slot without a hash, so no
    // override matches it and the draw is still replaced (counted in texture_unknown, reason noted).
    bool pixel_texture(const DrawRec& r, unsigned slot, bool& bound, uint32_t& hash, std::string&)
    {
        ++texture_queries;
        bound = false; hash = 0;
        const auto unknown = [&](const char* why) { bound = true; hash = 0; ++texture_unknown; texture_reason = why; return true; };
        const auto pipe = device->pipes.find(r.pipeline);
        if (pipe == device->pipes.end() || !pipe->second.recipe) return unknown("unknown pipeline");
        auto known = fragment_registers.find(pipe->second.id);
        if (known == fragment_registers.end()) {
            if (fragment_registers.size() >= 8192) fragment_registers.clear();
            FragmentRegisters registers;
            registers.readable = true; // a pipeline without a fragment stage reads no texture
            for (const auto& stage : pipe->second.recipe->stages)
                if (stage.stage == VK_SHADER_STAGE_FRAGMENT_BIT && stage.shader) {
                    std::string why;
                    registers.readable = efmi::gpu::source_registers(stage.shader->code, registers.list, why);
                }
            known = fragment_registers.emplace(pipe->second.id, std::move(registers)).first;
        }
        if (!known->second.readable) return unknown("the fragment module is not read by the register rule");
        const auto* reg = efmi::gpu::find_source_register(known->second.list, 't', slot);
        if (!reg) return true;
        if (reg->kind != efmi::gpu::SourceRegister::ImageSrv || reg->count != 1 || reg->set >= kMaxSets)
            return unknown("a pixel slot is not a single image");
        const char* why = nullptr;
        bool empty = false;
        const ImageInfo* image = bound_image(r, reg->set, reg->binding, empty, why);
        if (empty) return true; // a null view: an empty slot
        if (!image) return unknown(why);
        if (!dx11_texture_hash(*image, hash)) return unknown("an image without a known original hash");
        bound = true;
        return true;
    }

    // Caller holds res_mx. The image the draw binds at element 0 of (set, binding): null with `empty`
    // for a null view, null with `why` when the layer cannot tell.
    const ImageInfo* bound_image(const DrawRec& r, uint32_t set_index, uint32_t binding, bool& empty, const char*& why)
    {
        empty = false;
        if (set_index >= kMaxSets) { why = "a set outside the tracked slots"; return nullptr; }
        const SetBind& bind = r.sets[set_index];
        const DescriptorSetInfo* contents = bind.pushed.get();
        if (!contents && bind.id) {
            const auto set = device->descriptor_sets.find(bind.set);
            if (set != device->descriptor_sets.end() && set->second.id == bind.id) contents = &set->second;
        }
        if (bind.unsupported || !contents) { why = "the descriptor set of a pixel slot is not tracked"; return nullptr; }
        const auto value = contents->values.find({binding, 0});
        if (value == contents->values.end() || !value->second.known) { why = "a pixel slot's descriptor is unknown"; return nullptr; }
        if (!value->second.image_view) { empty = true; return nullptr; }
        const auto view = device->image_views.find(value->second.image_view);
        if (view == device->image_views.end() || view->second.id != value->second.image_view_id) { why = "unknown image view"; return nullptr; }
        const auto image = device->images.find(view->second.image);
        if (image == device->images.end() || image->second.id != view->second.image_id) { why = "unknown image"; return nullptr; }
        return &image->second;
    }

    // Caller holds res_mx and mod_mx. RabbitFX's roles in this draw (D-082): the fragment module's
    // samples and the images the draw binds at them. False when the module is not read.
    bool texture_roles(const DrawRec& r, int32_t roles[3], std::string& reason)
    {
        roles[0] = roles[1] = roles[2] = -1;
        const auto pipe = device->pipes.find(r.pipeline);
        if (pipe == device->pipes.end() || !pipe->second.recipe) { reason = "unknown pipeline"; return false; }
        auto known = fragment_samples.find(pipe->second.id);
        if (known == fragment_samples.end()) {
            if (fragment_samples.size() >= 8192) fragment_samples.clear();
            FragmentSamples samples;
            samples.readable = true; // a pipeline without a fragment stage samples nothing
            for (const auto& stage : pipe->second.recipe->stages)
                if (stage.stage == VK_SHADER_STAGE_FRAGMENT_BIT && stage.shader) {
                    std::string why;
                    samples.readable = efmi::gpu::role_samples(stage.shader->code, samples.list, why);
                }
            known = fragment_samples.emplace(pipe->second.id, std::move(samples)).first;
        }
        if (!known->second.readable) { reason = "the fragment module is not read"; return false; }
        const auto found = efmi::gpu::texture_roles(known->second.list, [&](const efmi::gpu::RoleSample& s) {
            efmi::gpu::RoleTexture texture;
            const char* why = nullptr;
            bool empty = false;
            if (const ImageInfo* image = bound_image(r, s.set, s.binding, empty, why)) {
                texture.known = true; texture.format = image->format;
                texture.width = image->extent.width; texture.height = image->extent.height; texture.levels = image->levels;
            }
            return texture;
        });
        roles[0] = found.diffuse; roles[1] = found.lightmap; roles[2] = found.normal;
        ++role_queries;
        if (found.diffuse >= 0) ++role_draws;
        return true;
    }

    // Caller holds res_mx. The derived profile of a fragment shader the package has no profile for,
    // or none: then the mesh is the mod's and the images stay the application's.
    uint32_t derive(const PipeInfo& pipe)
    {
        const auto known = derived_for_pipeline.find(pipe.id);
        if (known != derived_for_pipeline.end()) return known->second;
        if (derived_for_pipeline.size() >= 8192) derived_for_pipeline.clear();
        uint32_t& result = derived_for_pipeline[pipe.id];
        result = kCharacterNone;
        const std::vector<uint32_t>* code = nullptr;
        for (const auto& stage : pipe.recipe->stages)
            if (stage.stage == VK_SHADER_STAGE_FRAGMENT_BIT && stage.shader) code = &stage.shader->code;
        if (!code) return result;
        const uint32_t crc = crc32c(0, code->data(), code->size() * 4);
        for (uint32_t i = 0; i < derived_profiles.size(); ++i)
            if (derived_profiles[i]->crc == crc && derived_profiles[i]->slots == material_slots && derived_profiles[i]->code == *code) return result = i;
        std::string reason;
        auto created = std::make_unique<DerivedProfile>();
        if (!rule_rules(*code, created->rules, reason)) {
            char name[16];
            snprintf(name, sizeof(name), "%08x", crc);
            return refuse_profile(std::string("character material: fragment ") + name + " keeps the application's images: " + reason, result);
        }
        if (derived_profiles.size() >= kDerivedProfiles) return refuse_profile("character material: too many derived profiles", result);
        created->crc = crc; created->code = *code; created->slots = material_slots;
        CharacterMaterialProfile source;
        source.code = created->code; source.rules = created->rules;
        std::set<uint32_t> counts;
        if (!prepare_character_profile(package, source, created->plans, counts, reason)) {
            return refuse_profile("character material: pipeline " + std::to_string(pipe.id) + " derived profile refused: " + reason, result);
        }
        // The arenas of a command are made for the image counts known at setup.
        for (uint32_t count : counts)
            if (!image_counts.count(count))
                return refuse_profile("character material: derived profile of pipeline " + std::to_string(pipe.id) + " needs " +
                                      std::to_string(count) + " images, no arena has that count", result);
        logf("character material: fragment %08x (pipeline %u) takes %zu rules from the DX11 register rule",
             crc, pipe.id, created->rules.size());
        derived_profiles.push_back(std::move(created));
        return result = static_cast<uint32_t>(derived_profiles.size() - 1);
    }

    // Caller holds res_mx. The index bytes of one draw, hashed once and then recognized by a short
    // fingerprint; the full hash is repeated now and then because mapped pools change unseen.
    bool region_hash(const DrawRec& r, uint64_t now, uint32_t& hash)
    {
        auto ib = device->bufs.find(r.ib);
        if (ib == device->bufs.end() || (r.index_size != 2 && r.index_size != 4)) return false;
        const uint64_t start = uint64_t(r.first) * r.index_size, bytes = uint64_t(r.count) * r.index_size;
        const BufInfo& info = ib->second;
        if (r.ib_off > info.size || start > info.size - r.ib_off || bytes > info.size - r.ib_off - start || bytes > (64u << 20)) return false;
        const uint64_t offset = r.ib_off + start;
        uint8_t edge[128];
        const size_t head = static_cast<size_t>(std::min<uint64_t>(bytes, 64)), tail = static_cast<size_t>(std::min<uint64_t>(bytes - head, 64));
        if (read_buffer_locked(device, info, offset, head, edge) != SRC_MAPPED ||
            (tail && read_buffer_locked(device, info, offset + bytes - tail, tail, edge + head) != SRC_MAPPED)) return false;
        const uint32_t quick = crc32c(0, edge, head + tail);
        const IndexKey key = {r.ib, offset, static_cast<uint32_t>(bytes)};
        auto found = hashes.find(key);
        if (found != hashes.end() && found->second.buffer_id == info.id && found->second.gen == info.gen &&
            found->second.quick == quick && now < found->second.next_full) {
            hash = found->second.hash;
            return true;
        }
        scratch.resize(static_cast<size_t>(bytes));
        if (read_buffer_locked(device, info, offset, bytes, scratch.data()) != SRC_MAPPED) return false;
        hash = crc32c(0, scratch.data(), scratch.size());
        if (hashes.size() >= 4096) hashes.clear();
        // Regions first seen in one frame are not all hashed again in one later frame.
        hashes[key] = {info.id, info.gen, now + kRehashFrames + (IndexKeyHash()(key) >> 7) % kRehashFrames, hash, quick};
        return true;
    }

    // `now`: the character's frame; `present`: counted presents.
    Identity* identify(const InstanceEntry& entry, uint64_t now, uint64_t present)
    {
        float position[3];
        std::memcpy(position, entry.data() + 48, sizeof(position));
        int32_t cell[3];
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(position[i]) || std::fabs(position[i]) > 1.0e6f) return nullptr;
            cell[i] = static_cast<int32_t>(std::floor(position[i] / kCellSize));
        }
        const float scaled = std::ceil(kBaseRadius * (frame_seconds.load(std::memory_order_relaxed) * 120.0f));
        const int32_t radius = static_cast<int32_t>(std::min(std::max(scaled, kBaseRadius), 64.0f));
        const uint64_t clock = host_time_us();
        Identity* best = nullptr;
        int32_t best_distance = INT32_MAX;
        Identity* vacant = nullptr;
        Identity* weakest = nullptr;
        Identity* stale = nullptr;
        uint32_t weakest_seen = UINT32_MAX;
        // For the log when a new identity is made: the replaced object nearest to this place.
        int32_t near_distance = INT32_MAX;
        uint64_t near_frames = 0, near_us = 0;
        bool near_expired = false;
        for (auto& identity : identities) {
            int32_t distance = 0;
            for (int i = 0; i < 3; ++i) distance = std::max(distance, std::abs(cell[i] - identity.cell[i]));
            // Recordings of two threads may interleave, so a frame number can be behind the last one.
            const uint64_t gap = now > identity.last_frame ? now - identity.last_frame : identity.last_frame - now;
            const bool unseen = present - identity.last_present > kExpiryFrames;
            bool expired = false;
            if (identity.used && unseen && clock - identity.last_us > kExpiryMicroseconds) {
                identity.used = false; expired = true;
                ++counters[C_IDENTITY_EXPIRED];
            }
            if ((identity.used || expired) && identity.custom && distance < near_distance) {
                near_distance = distance; near_frames = gap; near_us = clock - identity.last_us; near_expired = expired;
            }
            if (!identity.used) { if (!vacant) vacant = &identity; continue; }
            // Every component of one object has the same position within a frame: an identity
            // already met in this frame is only the same object at the very same place. Across
            // frames the object may have moved, by at most the radius for each frame it was not drawn in.
            const int32_t allowed = gap ? static_cast<int32_t>(std::min<uint64_t>(uint64_t(radius) * gap, 64)) : 0;
            if (distance <= allowed && distance < best_distance) { best = &identity; best_distance = distance; }
            uint32_t bits = 0;
            for (uint32_t value = identity.seen; value; value &= value - 1) ++bits;
            if (!identity.custom && bits < weakest_seen) { weakest = &identity; weakest_seen = bits; }
            if (unseen && (!stale || identity.last_present < stale->last_present)) stale = &identity;
        }
        if (!best) {
            // Objects that merely share a mesh with the character must not keep it out: when every
            // slot is taken, the unidentified object seen with the fewest components gives way, and
            // failing that the object that has gone longest without a draw.
            if (!vacant) vacant = weakest;
            if (!vacant) vacant = stale;
            if (!vacant) return nullptr;
            ++counters[C_IDENTITY_NEW];
            if (near_distance != INT32_MAX && identity_logs < 200) {
                ++identity_logs;
                logf("character identity %llu at a new place: the nearest replaced object is %d cells away, last drawn %llu frames and %llu ms ago%s (radius %d)",
                     static_cast<unsigned long long>(next_identity), near_distance, static_cast<unsigned long long>(near_frames),
                     static_cast<unsigned long long>(near_us / 1000), near_expired ? ", expired" : "", radius);
            }
            std::string reason;
            if (!vacant->skeleton.configure(package.bones, 1, ranges, reason)) { note(reason); return nullptr; }
            vacant->used = true; vacant->custom = false; vacant->seen = 0;
            vacant->first_frame = now; vacant->update_frame = 0;
            vacant->id = next_identity++; vacant->generation = 0;
            vacant->coverage.assign(package.components.size(), 0);
            best = vacant;
        }
        std::memcpy(best->cell, cell, sizeof(cell));
        best->last_frame = now;
        best->last_present = present;
        best->last_us = clock;
        return best;
    }

    // Caller holds res_mx. Where the vertex shader of a pipeline reads its bones, found once per pipeline.
    BoneSite site_of(const PipeInfo& pipe)
    {
        auto found = sites.find(pipe.id);
        if (found != sites.end()) return found->second;
        BoneSite site;
        if (pipe.recipe && pipe.layout) {
            for (const auto& stage : pipe.recipe->stages) {
                if (stage.stage != VK_SHADER_STAGE_VERTEX_BIT || !stage.shader) continue;
                VertexRepoint out;
                if (repoint_vertex(stage.shader->code, static_cast<uint32_t>(pipe.layout->sets.size()), out) && out.bone_set < kMaxSets)
                    site = {true, out.bone_set, out.bone_binding};
            }
        }
        if (sites.size() >= 4096) sites.clear();
        sites[pipe.id] = site;
        return site;
    }

    // Caller holds mod_mx and res_mx. The Core's facts by its own rules on the vertex module's DX11
    // registers (D-076), kept only where they name what the renderer itself reads: the window at
    // set 2, binding 0 (the instance entry) and the bone buffer the bone site found. Otherwise none,
    // and the original Core leaves the mesh to the game.
    SourceFacts facts_of(const PipeInfo& pipe, const BoneSite& site)
    {
        const auto known = source_facts.find(pipe.id);
        if (known != source_facts.end()) return known->second;
        if (source_facts.size() >= 4096) source_facts.clear();
        SourceFacts facts;
        if (pipe.recipe)
            for (const auto& stage : pipe.recipe->stages) {
                if (stage.stage != VK_SHADER_STAGE_VERTEX_BIT || !stage.shader) continue;
                std::vector<efmi::gpu::SourceRegister> registers;
                std::string why;
                if (!efmi::gpu::source_registers(stage.shader->code, registers, why)) break;
                const auto core = efmi::gpu::core_source_facts(registers);
                if (core.config && core.config->set == 2 && core.config->binding == 0) {
                    facts.vs_config_slot = core.vs_config_slot;
                    facts.rolling_bones = core.rolling_bones && site.ok && core.bones->set == site.set && core.bones->binding == site.binding;
                }
            }
        if (!facts.rolling_bones) ++facts_unknown;
        source_facts[pipe.id] = facts;
        return facts;
    }

    bool record(CmdBuf* cb, const DrawRec& r);
    // The application's draw made again with the Mod's textures (D-083). Caller holds res_mx and
    // mod_mx; `list` is a texture-only list of the program.
    bool record_textures(CmdBuf* cb, const DrawRec& r, const PipeInfo& pipe, uint32_t section, uint64_t present, const CharacterList& list);
};

bool CharacterGeneration::init_materials()
{
    using namespace efmi;
    if (device->material_queues.empty()) { last_reason = device->material_queue_reason; return false; }
    if (package.material_vendor && (package.material_vendor != properties.vendorID || package.material_device != properties.deviceID)) {
        last_reason = "material dimension compatibility belongs to a different device"; return false;
    }
    if (!prepare_character_materials(package, materials, image_counts, last_reason)) return false;
    {
        std::set<uint32_t> slots;
        for (const auto& draw : package.pixels) for (const auto& pixel : draw) slots.insert(pixel.slot);
        material_slots.assign(slots.begin(), slots.end());
    }
    // The package's own profiles came from paired captures; the register rule must give the same
    // bindings for the slots they place. A difference is reported, the profile stays in use.
    for (uint32_t i = 0; i < package.profiles.size(); ++i) {
        std::vector<CharacterMaterialRule> rules;
        std::string why;
        if (!rule_rules(package.profiles[i].code, rules, why)) { ++rule_mismatches; logf("character material: profile %u not read by the register rule: %s", i, why.c_str()); continue; }
        for (const auto& own : package.profiles[i].rules)
            for (const auto& rule : rules)
                if (rule.slot == own.slot && (rule.kind != own.kind || rule.set != own.set || rule.binding != own.binding)) {
                    ++rule_mismatches;
                    logf("character material: profile %u slot %u is %u.%u by capture, %u.%u by the register rule", i, own.slot,
                         own.set, own.binding, rule.set, rule.binding);
                }
    }
#define X(n) n = reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle, "vk" #n)); \
    if (!n) { last_reason = "missing material vk" #n; return false; }
    X(CreateSemaphore) X(DestroySemaphore) X(CreateFence) X(DestroyFence) X(WaitForFences)
#undef X
    assets::ResourceDevice resource;
    resource.device = device->handle; resource.physical = device->phys; resource.memory = memory;
    resource.graphics_family = device->material_family; resource.bc_enabled = device->material_bc_enabled;
    resource.format_properties = device->material_format_properties; resource.image_properties = device->material_image_properties;
    resource.dispatch.SetDeviceLoaderData = device->set_loader_data;
    resource.buffer_dispatch.SetDeviceLoaderData = device->set_loader_data;
#define X(n) resource.dispatch.n = reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle, "vk" #n)); \
    if (!resource.dispatch.n) { last_reason = "missing material vk" #n; return false; }
    EFMI_IMAGE_UPLOAD_FUNCS(X)
#undef X
#define X(n) resource.buffer_dispatch.n = reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle, "vk" #n));
    EFMI_BUFFER_UPLOAD_FUNCS(X)
#undef X
    {
        // The images of one character may hold a quarter of the largest device-local heap, between
        // 512 MiB and 2 GiB. Installed Mods ship textures of 64 MiB (4096 x 4096 uncompressed); a fixed
        // 512 MiB left a character's last images unloaded and its materials waiting for good.
        uint64_t heap = 0;
        for (uint32_t i = 0; i < memory.memoryHeapCount; ++i)
            if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) heap = std::max<uint64_t>(heap, memory.memoryHeaps[i].size);
        image_budgets.resident = std::make_shared<assets::AllocationBudget>(std::clamp<uint64_t>(heap / 4, 512ull << 20, 2048ull << 20));
    }
    images = std::make_unique<assets::ResourceCache>(resource, image_budgets, 64);
    for (const auto& path : package.images) {
        auto handle = images->declare_image(std::filesystem::u8path(path), last_reason);
        if (!handle) return false;
        image_handles.push_back(std::move(handle));
    }
    // Publication objects for every batch on every graphics queue, allocated here and kept until
    // device shutdown (docs/character-materials.md).
    image_published.reset(new std::atomic<bool>[image_handles.size() + 1]());
    publication_batches = std::min<uint32_t>(kPublicationBatches, std::max<uint32_t>(1, static_cast<uint32_t>(image_handles.size())));
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    publication_fences.assign(publication_batches, VK_NULL_HANDLE);
    for (auto& fence : publication_fences)
        if (CreateFence(device->handle, &fi, nullptr, &fence) != VK_SUCCESS) { last_reason = "cannot allocate material publication fence"; return false; }
    for (auto queue : device->material_queues) {
        image_queues.push_back({queue});
        auto& q = image_queues.back();
        q.semaphores.assign(publication_batches, VK_NULL_HANDLE); q.fences.assign(publication_batches, VK_NULL_HANDLE);
        for (uint32_t b = 0; b < publication_batches; ++b)
            if (CreateSemaphore(device->handle, &si, nullptr, &q.semaphores[b]) != VK_SUCCESS ||
                CreateFence(device->handle, &fi, nullptr, &q.fences[b]) != VK_SUCCESS) {
                last_reason = "cannot allocate material queue publication"; return false;
            }
    }
    return true;
}

bool CharacterGeneration::material_draws(const PipeInfo& pipe, uint32_t section, const DrawRec& r, uint64_t now,
                                      const CharacterList& list,
                                      std::vector<const StaticMaterial*>& plans, std::vector<std::vector<SnapshotImage>>& views,
                                      uint32_t& origin)
{
    static const StaticMaterial inherited{true, false, {}, {}, {}};
    const uint32_t pipeline_id = pipe.id;
    const GraphicsRecipe& recipe = *pipe.recipe;
    const PipelineLayoutInfo& layout = *pipe.layout;
    origin = 0;
    uint32_t profile = kCharacterNone;
    bool no_fragment = true;
    if (package.materials) {
        const auto found = material_profiles.find(pipeline_id);
        if (found != material_profiles.end()) profile = found->second;
        else {
            for (const auto& stage : recipe.stages) if (stage.stage == VK_SHADER_STAGE_FRAGMENT_BIT && stage.shader) {
                no_fragment = false;
                for (uint32_t i = 0; i < package.profiles.size(); ++i)
                    if (package.profiles[i].code == stage.shader->code) { profile = i; break; }
            }
            if (no_fragment) profile = kCharacterNone - 1;
            if (profile == kCharacterNone && layout.complete) {
                // A fragment shader whose layout holds no image for it cannot read a texture: every
                // slot the mod binds is absent there, as for the pass that tests visibility with
                // depth only. Nothing to replace; the mesh is still the mod's.
                bool images = false, known = true;
                for (const auto& set : layout.sets) {
                    if (!set || !set->complete) { known = false; break; }
                    for (const auto& b : set->bindings) {
                        const uint32_t type = b.second.type;
                        if ((b.second.stages & VK_SHADER_STAGE_FRAGMENT_BIT) &&
                            (type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                             type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT ||
                             type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER ||
                             type > VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)) images = true;
                    }
                }
                if (known && !images) profile = kCharacterNone - 1;
            }
            if (material_profiles.size() >= 4096) material_profiles.clear();
            material_profiles[pipeline_id] = profile;
        }
    }
    // The table of plans: the package's own profile; for a fragment shader without one the rules
    // the DX11 register rule reads off its module; failing that none, and the application's images
    // stay on the mod's mesh. Keeping the game's mesh there instead would mix the two models on screen.
    const std::vector<StaticMaterial>* table = nullptr;
    bool binds = false;
    for (uint32_t i = 0; i < list.count; ++i) binds |= package.materials && !package.pixels[list.first + i].empty();
    if (binds && profile < package.profiles.size()) {
        table = &materials[profile];
    } else if (binds && profile == kCharacterNone) {
        const uint32_t index = derive(pipe);
        if (index != kCharacterNone) table = &derived_profiles[index]->plans;
        origin = table ? 1 : 2;
    }
    // Decide the entire logical transaction before obtaining any views or issuing a command.
    for (uint32_t i = 0; i < list.count; ++i) {
        const uint32_t index = list.first + i;
        const StaticMaterial* plan = &inherited;
        if (table && !package.pixels[index].empty()) {
            plan = &(*table)[index];
            if (!plan->supported) return refuse(C_MATERIAL, plan->reason);
        }
        plans.push_back(plan);
    }
    // No command can retain a file image until its publication signal is enqueued; every consumer
    // queue inserts its waits before forwarding application work. An image the draw waits for is
    // loaded next.
    bool pending = false;
    for (const auto* plan : plans) {
        std::vector<SnapshotImage> draw_views;
        for (uint32_t asset : plan->assets) {
            auto result = image_handles[asset]->snapshot();
            if (result.state == efmi::assets::ResourceState::Failed || result.state == efmi::assets::ResourceState::Stopped)
                return refuse(C_IMAGE, "character texture: " + result.reason);
            if (!image_published[asset].load(std::memory_order_acquire) || result.state != efmi::assets::ResourceState::Ready ||
                !result.image || !result.image->view()) {
                pending = true; images->prioritize(image_handles[asset]); continue;
            }
            draw_views.push_back({result.image->view(), result.image});
        }
        views.push_back(std::move(draw_views));
    }
    if (pending) { ++counters[C_IMAGE_PENDING]; return false; }
    return true;
}

bool CharacterGeneration::record(CmdBuf* cb, const DrawRec& r)
{
    if (!enabled.load(std::memory_order_relaxed) || !cache) return false;
    const auto candidates = by_count.find(r.count);
    const bool by_hash = !hash_only.empty() && activated.load(std::memory_order_relaxed);
    bool possible = by_hash;
    if (candidates != by_count.end())
        for (uint32_t index : candidates->second)
            possible |= package.sections[index].match_first == kCharacterNone || package.sections[index].match_first == r.first;
    if (!possible) return false;

    std::lock_guard<std::mutex> mod_lock(mod_mx);
    const uint64_t present = frame.load(std::memory_order_relaxed);
    InstanceEntry entry, patched;
    const CharacterList* list = nullptr;
    CharacterList runtime_list;
    Identity* identity = nullptr;
    std::vector<std::shared_ptr<const PipelineVariant>> variants;
    std::vector<const StaticMaterial*> material_plans;
    std::vector<std::vector<SnapshotImage>> material_views;
    uint32_t material_origin = 0;
    DrawGeometry original;
    // The application's own set 3, when it has one bound: the replacement uses that slot, so it is
    // bound again afterwards exactly as the application bound it.
    uint32_t restore_slot = 0;
    VkPipelineLayout restore_layout = VK_NULL_HANDLE;
    VkDescriptorSet restore_set = VK_NULL_HANDLE;
    std::vector<uint32_t> restore_offsets;
    {
        std::shared_lock<std::shared_mutex> lock(device->res_mx);
        uint32_t hash = 0;
        if (!region_hash(r, present, hash)) return false;
        const CharacterSection* section = nullptr;
        if (candidates != by_count.end())
            for (uint32_t index : candidates->second) {
                const auto& s = package.sections[index];
                if (s.hash == hash && (s.match_first == kCharacterNone || s.match_first == r.first)) { section = &s; break; }
            }
        if (!section && by_hash)
            for (uint32_t index : hash_only)
                if (package.sections[index].hash == hash) { section = &package.sections[index]; break; }
        if (!section) return false;
        if (!activated.load(std::memory_order_relaxed)) { activate(); return false; }
        ++counters[C_MATCHED];
        if (section->component == kCharacterNone) {
            // Exact IB-only override: no fabricated identity or bone import.
            // Live conditions still run; a changed rule may only emit an empty skip.
            if (program) {
                efmi::frontend::RuntimeDraw request;
                request.hash = section->hash; request.count = r.count; request.first = r.first;
                request.base = r.vertex_offset; request.instances = r.instances; request.first_instance = r.first_instance;
                request.frame = present; request.seconds = ini_seconds();
                request.frame_seconds = frame_seconds.load(std::memory_order_relaxed);
                request.pixel_texture = [this, &r](unsigned slot, bool& bound, uint32_t& value, std::string& why) {
                    return pixel_texture(r, slot, bound, value, why); };
                request.texture_roles = [this, &r](int32_t roles[3], std::string& why) { return texture_roles(r, roles, why); };
                bool skip = false, added = false, textures = false; std::string reason;
                if (!program->plan(request, package, runtime_list, skip, added, reason, nullptr, &textures)) return refuse(C_STATE, "INI hide: " + reason);
                if (!skip) {
                    if (!textures) return false;
                    if (added && !refresh_materials()) return false;
                    const auto pipe = device->pipes.find(r.pipeline);
                    if (pipe == device->pipes.end() || !pipe->second.recipe || !pipe->second.layout)
                        return refuse(C_PIPELINE, "texture-only draw pipeline metadata missing");
                    return record_textures(cb, r, pipe->second, static_cast<uint32_t>(section - package.sections.data()), present, runtime_list);
                }
                if (runtime_list.count) return refuse(C_STATE, "INI hide override unexpectedly emits geometry");
            }
            ++counters[C_HIDDEN]; return true;
        }
        // The character's frame is this recording of the command buffer (see kExpiryMicroseconds).
        if (!cb->character_frame) cb->character_frame = ++device->character_recordings;
        const uint64_t now = cb->character_frame;
        // The mod's LOD variable: a section that assigns it changes it for the sections that do not.
        if (section->lod != kCharacterNone) lod = section->lod;
        const CharacterComponent& component = package.components[section->component];
        if (cb->secondary || !cb->in_rp || cb->dyn_rp_count || cb->m2_extended_bind)
            return refuse(C_STATE, "character draw outside a primary classic render pass or after extended binds");
        if (r.instances != 1 || r.first_instance != 0) return refuse(C_INSTANCES, "character draw is instanced");
        auto pipe = device->pipes.find(r.pipeline);
        if (pipe == device->pipes.end() || !pipe->second.recipe || !pipe->second.layout)
            return refuse(C_PIPELINE, "character draw pipeline metadata missing");

        // The instance entry is at set 2 binding 0 in every pipeline of the game's characters.
        std::vector<BoundRange> ranges_bound;
        const auto& window = r.sets[2];
        if (window.dyn_count && uint64_t(window.dyn_first) + window.dyn_count > cb->dyn.size())
            return refuse(C_ENTRY, "character dynamic offsets incomplete");
        collect_buffer_ranges_locked(device, window, window.dyn_count ? cb->dyn.data() + window.dyn_first : nullptr, window.dyn_count, 2, ranges_bound);
        auto read_range = [&](const BoundRange* range, uint64_t offset, size_t size, uint8_t* output) {
            if (!range || offset > range->size || size > range->size - offset) return false;
            auto buffer = device->bufs.find(range->buffer);
            return buffer != device->bufs.end() && read_buffer_locked(device, buffer->second, range->off + offset, size, output) == SRC_MAPPED;
        };
        const BoundRange* instance = nullptr;
        for (const auto& range : ranges_bound)
            if (range.element == 0 && range.binding == 0 &&
                (range.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || range.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)) instance = &range;
        if (!read_range(instance, 0, entry.size(), entry.data())) return refuse(C_ENTRY, "character instance entry is not mapped");
        identity = identify(entry, now, present);
        if (!identity) return refuse(C_IDENTITIES, "character position invalid or all identities in use");
        identity->seen |= 1u << section->component;
        if (now > identity->update_frame) {
            std::string reason;
            if (!identity->skeleton.begin_update(now, reason)) return refuse(C_IMPORT, reason);
            identity->update_frame = now;
        }

        // Import this component's poses into the identity's master, whatever happens to the draw.
        uint32_t offsets[2];
        std::memcpy(offsets, entry.data() + 80, 8);
        const uint8_t dynamic = (offsets[0] >= MergedSkeleton::kStaticRows ? 1 : 0) | (offsets[1] >= MergedSkeleton::kStaticRows ? 2 : 0);
        const BoneSite site = site_of(pipe->second);
        if (site.ok && dynamic && identity->skeleton.needs_import(0, section->component, lod)) {
            const bool remapped = lod == 1 && component.has_remap;
            uint32_t last = component.count - 1;
            if (remapped) last = *std::max_element(component.remap_lod1.begin(), component.remap_lod1.end());
            const size_t needed = (size_t(last) + 2) * MergedSkeleton::kBytesPerBone;
            current.resize(needed); previous.resize(needed);
            std::vector<BoundRange> bone_ranges;
            const auto& bound = r.sets[site.set];
            bool readable = !(bound.dyn_count && uint64_t(bound.dyn_first) + bound.dyn_count > cb->dyn.size());
            if (readable) collect_buffer_ranges_locked(device, bound, bound.dyn_count ? cb->dyn.data() + bound.dyn_first : nullptr, bound.dyn_count, site.set, bone_ranges);
            const BoundRange* bones = nullptr;
            for (const auto& range : bone_ranges)
                if (range.element == 0 && range.binding == site.binding &&
                    (range.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || range.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)) bones = &range;
            readable = readable && (!(dynamic & 1) || read_range(bones, uint64_t(offsets[0]) * 16, needed, current.data())) &&
                       (!(dynamic & 2) || read_range(bones, uint64_t(offsets[1]) * 16, needed, previous.data()));
            if (!readable) {
                ++counters[C_BONES];
            } else {
                std::string reason;
                static const std::vector<uint16_t> identity_map;
                const auto result = identity->skeleton.import_component(0, section->component, lod, entry,
                    {current.data(), needed}, {previous.data(), needed}, remapped ? component.remap_lod1 : identity_map, 1.0f, reason);
                if (result == BoneImportResult::Imported) {
                    identity->coverage[section->component] |= dynamic;
                    ++identity->generation;
                } else if (result == BoneImportResult::Rejected) {
                    ++counters[C_IMPORT];
                    note("character bone import: " + reason);
                }
            }
        }

        // As in the core: the object is the mod's once enough of its components were seen at this
        // place and a frame has passed. Until then the game draws it.
        if (!identity->custom) {
            uint32_t bits = 0;
            for (uint32_t value = identity->seen; value; value &= value - 1) ++bits;
            // The original Core's identification_min_components as the Mod sets it (EFMI exports: 3).
            const uint32_t wanted = program && program->identify_components() ? program->identify_components() : kIdentifyingComponents;
            if (bits >= std::min<uint32_t>(wanted, static_cast<uint32_t>(package.components.size())) && now > identity->first_frame)
                identity->custom = true;
            else { ++counters[C_IDENTIFYING]; return false; }
        }
        list = &package.lists[component.lists[lod]];
        if (program) {
            efmi::frontend::RuntimeDraw request;
            request.hash = section->hash; request.count = r.count; request.first = r.first; request.base = r.vertex_offset;
            request.component = section->component; request.lod = lod; request.identity = identity->id; request.frame = now;
            request.seconds = ini_seconds();
            request.frame_seconds = frame_seconds.load(std::memory_order_relaxed);
            request.pixel_texture = [this, &r](unsigned slot, bool& bound, uint32_t& value, std::string& why) {
                return pixel_texture(r, slot, bound, value, why); };
            request.texture_roles = [this, &r](int32_t roles[3], std::string& why) { return texture_roles(r, roles, why); };
            {
                const SourceFacts facts = facts_of(pipe->second, site);
                request.source_vs_config_slot = facts.vs_config_slot;
                request.rolling_bone_source = facts.rolling_bones;
            }
            bool skip = false, added = false, textures = false; std::string reason;
            if (!program->plan(request, package, runtime_list, skip, added, reason, &pipe->second.recipe->vertex, &textures))
                return refuse(C_STATE, "INI plan: " + reason);
            if (!skip) {
                if (!textures) return false;
                if (added && !refresh_materials()) return false;
                return record_textures(cb, r, pipe->second, static_cast<uint32_t>(section - package.sections.data()), present, runtime_list);
            }
            if (added && !refresh_materials()) return false;
            list = &runtime_list;
        }
        if (!list->count) { ++counters[C_HIDDEN]; return true; }
        for (uint32_t c = 0; c < package.components.size(); ++c)
            if ((list->required >> c & 1) && (identity->coverage[c] & dynamic) != dynamic) { ++counters[C_INCOMPLETE]; return false; }
        // The private set is bound right above the pipeline's own sets: slot 3 for most character
        // pipelines, slot 4 for the four-set ones (the game's visibility pass among them).
        const uint32_t own = static_cast<uint32_t>(pipe->second.layout->sets.size());
        if (own >= kMaxSets) {
            // Not a tracked slot: usable only if no pipeline layout of the application reaches it.
            if (device->max_layout_sets > own) return refuse(C_SET3, "character draw: the slot above the pipeline's sets is in use and not tracked");
        } else if (r.sets[own].pushed) {
            return refuse(C_SET3, "character draw with pushed descriptors at the private set's slot");
        } else if (r.sets[own].set) {
            // The tracker keeps a set only while later binds left it valid, so the layout it was
            // bound with agrees with the current one for the lower sets, and binding it again there
            // disturbs nothing. Vulkan keeps that layout alive while this recording lasts.
            const SetBind& app = r.sets[own];
            restore_slot = own;
            auto live = device->pipeline_layouts.find(app.layout_handle);
            if (!app.layout_handle || !app.layout || app.unsupported || live == device->pipeline_layouts.end() ||
                live->second != app.layout || (app.dyn_count && uint64_t(app.dyn_first) + app.dyn_count > cb->dyn.size()))
                return refuse(C_SET3, "character draw with an application set that cannot be bound again");
            restore_layout = app.layout_handle; restore_set = app.set;
            restore_offsets.assign(cb->dyn.begin() + app.dyn_first, cb->dyn.begin() + app.dyn_first + app.dyn_count);
        }

        // Restore exactly the vertex bindings the pipeline can read among the four tracked ones.
        uint32_t slots = 0;
        for (const auto& b : pipe->second.recipe->vertex.bindings) if (b.binding < kMaxVb) slots = std::max(slots, b.binding + 1);
        for (uint32_t slot = 0; slot < 4; ++slot)
            for (uint32_t i = 0; i < list->count; ++i)
                if (package.draws[list->first + i].vb[slot] != kCharacterNone) slots = std::max(slots, slot + 1);
        if (!slots || !r.ib) return refuse(C_BINDINGS, "character draw without tracked vertex or index bindings");
        for (uint32_t slot = 0; slot < slots; ++slot) {
            if (!r.vb[slot].buf) return refuse(C_BINDINGS, "character original vertex bindings incomplete");
            original.buffers.push_back(r.vb[slot].buf); original.offsets.push_back(r.vb[slot].off);
        }
        original.index = r.ib; original.index_offset = r.ib_off;
        original.index_type = r.index_size == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

        bool pending = false;
        if (!material_draws(pipe->second, static_cast<uint32_t>(section - package.sections.data()), r, present, *list,
                            material_plans, material_views, material_origin)) return false;
        for (uint32_t i = 0; i < list->count; ++i) {
            VertexInputRecipe vertex;
            std::string reason;
            if (!package.vertex_input(package.draws[list->first + i], pipe->second.recipe->vertex, vertex, reason))
                return refuse(C_VERTEX, "character vertex declaration: " + reason);
            const auto result = cache->request(pipe->second.id, pipe->second.recipe, pipe->second.layout, vertex, material_plans[i]->patch);
            if (result.variant) { variants.push_back(result.variant); continue; }
            if (result.state == VariantState::Queued || result.state == VariantState::Building) { pending = true; continue; }
            return refuse(C_VARIANT, "character variant: " + result.reason);
        }
        if (pending) { ++counters[C_PENDING]; return false; }
        std::string reason;
        if (!identity->skeleton.patch_entry(0, entry, patched, reason)) return refuse(C_IMPORT, reason);
    }

    // A recording that holds work of other replacement code keeps it to its end. What that code
    // left in a command buffer from earlier recordings gives way.
    if (cb->m2 && cb->m2->backend != generation_key.get()) {
        if (cb->m2->used) return refuse(C_STATE, "recording holds another backend's work");
        cb->m2.reset();
    }
    if (!cb->m2) cb->m2 = command();
    if (!cb->m2) return refuse(C_EXHAUSTED, "character snapshot pool is at capacity or refilling");
    CharacterCommand* const command_owner = static_cast<CharacterCommand*>(cb->m2.get());
    command_owner->used = true;
    const auto mesh_owner = std::static_pointer_cast<const GpuMesh>(command_owner->keep);
    if (!mesh_owner) return refuse(C_EXHAUSTED, "character command owner has no mesh");
    // Everything is decided; from here nothing can fail after a command was emitted.
    std::vector<DrawGeometry> geometry(list->count);
    std::vector<VariantDraw> items(list->count);
    const std::vector<uint8_t>& master = identity->skeleton.bytes();
    const uint64_t key = identity->id << 32 ^ identity->generation;
    for (uint32_t i = 0; i < list->count; ++i) {
        const CharacterDraw& d = package.draws[list->first + i];
        SnapshotSlot slot;
        const uint32_t count = static_cast<uint32_t>(material_views[i].size());
        auto arena = count ? command_owner->material_snapshots.at(count) : command_owner->snapshots;
        if (!arena->append_shared(master.data(), master.size(), key, patched, material_views[i], slot))
            return refuse(C_EXHAUSTED, arena->reason);
        DrawGeometry& g = geometry[i];
        g = original;
        for (uint32_t s = 0; s < g.buffers.size(); ++s)
            if (d.vb[s] != kCharacterNone) { g.buffers[s] = mesh_owner->buffer; g.offsets[s] = package.streams[d.vb[s]].offset; }
        const auto& index = package.streams[d.index_stream];
        g.index = mesh_owner->buffer; g.index_offset = index.offset;
        g.index_type = index.stride == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        IndexedDraw args; args.count = d.count; args.first = d.first; args.vertex_offset = d.base;
        items[i] = {variants[i].get(), slot.set, slot.offset, &g, args};
    }
    command_owner->variants.insert(command_owner->variants.end(), variants.begin(), variants.end());
    if (!emit_variant_draws(draw, cb->handle, items.data(), items.size(), r.pipeline, original))
        return refuse(C_VARIANT, "character draw transaction refused");
    if (restore_layout) {
        draw.CmdBindDescriptorSets(cb->handle, VK_PIPELINE_BIND_POINT_GRAPHICS, restore_layout, restore_slot, 1, &restore_set,
                                   static_cast<uint32_t>(restore_offsets.size()), restore_offsets.empty() ? nullptr : restore_offsets.data());
        ++counters[C_SET3_RESTORED];
    }
    ++counters[C_REPLACED];
    if (material_origin) ++counters[material_origin == 1 ? C_DERIVED : C_INHERITED];
    counters[C_ITEMS] += list->count;
    for (const auto* plan : material_plans) {
        if (!plan->patch.images.empty() || !plan->patch.nulls.empty()) ++counters[C_MATERIAL_ITEMS];
        if (plan->compatibility) ++counters[C_COMPAT_ITEMS];
    }
    return true;
}

bool CharacterGeneration::record_textures(CmdBuf* cb, const DrawRec& r, const PipeInfo& pipe, uint32_t section, uint64_t present,
                                          const CharacterList& list)
{
    if (list.count != 1 || !package.draws[list.first].texture_only) return refuse(C_STATE, "texture-only list expected");
    if (cb->secondary || !cb->in_rp || cb->dyn_rp_count || cb->m2_extended_bind)
        return refuse(C_STATE, "texture-only draw outside a primary classic render pass or after extended binds");
    std::vector<const StaticMaterial*> plans;
    std::vector<std::vector<SnapshotImage>> views;
    uint32_t origin = 0;
    if (!material_draws(pipe, section, r, present, list, plans, views, origin)) return false;
    // Nothing the module can take: the application's draw stays as it is.
    if (plans.size() != 1 || (plans[0]->patch.images.empty() && plans[0]->patch.nulls.empty())) return false;
    // The private set above the pipeline's own sets, and an application set there bound again after.
    uint32_t restore_slot = 0;
    VkPipelineLayout restore_layout = VK_NULL_HANDLE;
    VkDescriptorSet restore_set = VK_NULL_HANDLE;
    std::vector<uint32_t> restore_offsets;
    const uint32_t own = static_cast<uint32_t>(pipe.layout->sets.size());
    if (own >= kMaxSets) {
        if (device->max_layout_sets > own) return refuse(C_SET3, "texture-only draw: the slot above the pipeline's sets is in use and not tracked");
    } else if (r.sets[own].pushed) {
        return refuse(C_SET3, "texture-only draw with pushed descriptors at the private set's slot");
    } else if (r.sets[own].set) {
        const SetBind& app = r.sets[own];
        restore_slot = own;
        auto live = device->pipeline_layouts.find(app.layout_handle);
        if (!app.layout_handle || !app.layout || app.unsupported || live == device->pipeline_layouts.end() ||
            live->second != app.layout || (app.dyn_count && uint64_t(app.dyn_first) + app.dyn_count > cb->dyn.size()))
            return refuse(C_SET3, "texture-only draw with an application set that cannot be bound again");
        restore_layout = app.layout_handle; restore_set = app.set;
        restore_offsets.assign(cb->dyn.begin() + app.dyn_first, cb->dyn.begin() + app.dyn_first + app.dyn_count);
    }
    // The application's own geometry: the vertex bindings the pipeline reads among the tracked ones,
    // bound again as they are; higher ones are not touched and stay the application's.
    DrawGeometry original;
    uint32_t slots = 0;
    for (const auto& b : pipe.recipe->vertex.bindings) if (b.binding < kMaxVb) slots = std::max(slots, b.binding + 1);
    if (!r.ib) return refuse(C_BINDINGS, "texture-only draw without a tracked index binding");
    for (uint32_t slot = 0; slot < slots; ++slot) {
        if (!r.vb[slot].buf) return refuse(C_BINDINGS, "texture-only draw's vertex bindings incomplete");
        original.buffers.push_back(r.vb[slot].buf); original.offsets.push_back(r.vb[slot].off);
    }
    if (original.buffers.empty()) return refuse(C_BINDINGS, "texture-only draw without vertex bindings");
    original.index = r.ib; original.index_offset = r.ib_off;
    original.index_type = r.index_size == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    FragmentPatch patch = plans[0]->patch;
    patch.keep_vertex = true;
    const auto result = cache->request(pipe.id, pipe.recipe, pipe.layout, pipe.recipe->vertex, patch);
    if (!result.variant) {
        if (result.state == VariantState::Queued || result.state == VariantState::Building) { ++counters[C_PENDING]; return false; }
        return refuse(C_VARIANT, "texture-only variant: " + result.reason);
    }
    if (cb->m2 && cb->m2->backend != generation_key.get()) {
        if (cb->m2->used) return refuse(C_STATE, "recording holds another backend's work");
        cb->m2.reset();
    }
    if (!cb->m2) cb->m2 = command();
    if (!cb->m2) return refuse(C_EXHAUSTED, "character snapshot pool is at capacity or refilling");
    CharacterCommand* const command_owner = static_cast<CharacterCommand*>(cb->m2.get());
    command_owner->used = true;
    const uint32_t count = static_cast<uint32_t>(views[0].size());
    auto arena = count ? command_owner->material_snapshots.at(count) : command_owner->snapshots;
    static const InstanceEntry unused{};
    SnapshotSlot slot;
    if (!arena->append_images(unused, views[0], slot)) return refuse(C_EXHAUSTED, arena->reason);
    IndexedDraw args;
    args.count = r.count; args.instances = r.instances; args.first = r.first; args.vertex_offset = r.vertex_offset;
    args.first_instance = r.first_instance;
    const VariantDraw item{result.variant.get(), slot.set, slot.offset, &original, args};
    command_owner->variants.push_back(result.variant);
    if (!emit_variant_draws(draw, cb->handle, &item, 1, r.pipeline, original))
        return refuse(C_VARIANT, "texture-only draw transaction refused");
    if (restore_layout) {
        draw.CmdBindDescriptorSets(cb->handle, VK_PIPELINE_BIND_POINT_GRAPHICS, restore_layout, restore_slot, 1, &restore_set,
                                   static_cast<uint32_t>(restore_offsets.size()), restore_offsets.empty() ? nullptr : restore_offsets.data());
        ++counters[C_SET3_RESTORED];
    }
    ++counters[C_TEXTURED];
    if (origin) ++counters[origin == 1 ? C_DERIVED : C_INHERITED];
    ++counters[C_MATERIAL_ITEMS];
    if (plans[0]->compatibility) ++counters[C_COMPAT_ITEMS];
    return true;
}

void character_device_setup(Device* device, const VkDeviceCreateInfo* info, VkInstance instance, PFN_vkGetInstanceProcAddr gipa)
{
    device->material_queue_reason = "material queue discovery unavailable";
    if (!gipa) return;
    const auto families = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    const auto get_queue = reinterpret_cast<PFN_vkGetDeviceQueue>(device->next_gdpa(device->handle, "vkGetDeviceQueue"));
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
    if (effective >= VK_API_VERSION_1_1)
        device->format_properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties2>(gipa(instance, "vkGetPhysicalDeviceFormatProperties2"));
    device->material_format_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(gipa(instance, "vkGetPhysicalDeviceFormatProperties"));
    device->material_image_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties>(gipa(instance, "vkGetPhysicalDeviceImageFormatProperties"));
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

VkResult generation_submit(CharacterGeneration& backend, VkQueue queue)
{
    if (!backend.images || !backend.cache) return VK_SUCCESS;
    std::lock_guard<std::mutex> lock(backend.queue_mx);
    if (backend.queues_stopped) return VK_SUCCESS;
    auto found = std::find_if(backend.image_queues.begin(), backend.image_queues.end(),
                              [&](const ImageQueue& q) { return q.queue == queue; });
    if (found == backend.image_queues.end()) return VK_SUCCESS;
    if (!backend.upload_queue) {
        backend.upload_queue = queue;
        backend.upload_queue_index = static_cast<uint32_t>(found - backend.image_queues.begin());
    }
    if (queue == backend.upload_queue) {
        backend.images->submit_prepared(queue);
        // A batch of the images that became Ready since the last one; the last batch waits until
        // every image is Ready or failed. Its signal covers the upload queue's earlier work.
        const uint32_t count = static_cast<uint32_t>(backend.image_handles.size());
        if (backend.publications < backend.publication_batches && backend.published_images.load() < count) {
            std::vector<uint32_t> fresh;
            bool settled = true;
            for (uint32_t i = 0; i < count; ++i) {
                if (backend.image_published[i].load(std::memory_order_relaxed)) continue;
                const auto state = backend.image_handles[i]->snapshot().state;
                if (state == efmi::assets::ResourceState::Ready) fresh.push_back(i);
                else if (state != efmi::assets::ResourceState::Failed && state != efmi::assets::ResourceState::Stopped) settled = false;
            }
            if (!fresh.empty() && (backend.publications + 1 < backend.publication_batches || settled)) {
                const uint32_t batch = backend.publications;
                std::vector<VkSemaphore> signals;
                for (const auto& q : backend.image_queues) signals.push_back(q.semaphores[batch]);
                VkSubmitInfo publish{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                publish.signalSemaphoreCount = static_cast<uint32_t>(signals.size());
                publish.pSignalSemaphores = signals.data();
                const auto result = backend.device->d.QueueSubmit(queue, 1, &publish, backend.publication_fences[batch]);
                if (result != VK_SUCCESS) { backend.note("material publication submission failed"); return result; }
                ++backend.publications; ++backend.image_publications;
                for (uint32_t i : fresh) backend.image_published[i].store(true, std::memory_order_release);
                if (backend.published_images.fetch_add(static_cast<uint32_t>(fresh.size())) + fresh.size() == count)
                    backend.images_published.store(true, std::memory_order_release);
            }
        }
    }
    while (found->waited < backend.publications) {
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo wait{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        wait.waitSemaphoreCount = 1; wait.pWaitSemaphores = &found->semaphores[found->waited]; wait.pWaitDstStageMask = &stage;
        const auto result = backend.device->d.QueueSubmit(queue, 1, &wait, found->fences[found->waited]);
        if (result != VK_SUCCESS) { backend.note("material consumer queue wait failed"); return result; }
        ++found->waited;
        ++backend.image_queue_waits;
    }
    return VK_SUCCESS;
}

std::shared_ptr<CharacterGeneration> generation_create(Device* device, const VkPhysicalDeviceProperties& properties,
                                                   const VkPhysicalDeviceMemoryProperties& memory,
                                                   const std::string& path, bool lazy)
{
    auto backend = std::make_shared<CharacterGeneration>(device, properties, memory, path, lazy);
    try {
        if (!backend->init()) {
            // BUG-026: a refusal can come after the INI program or its presenter were made; a refused
            // backend keeps neither, so no later presentation or draw reaches a half-made program.
            backend->abandon();
            logf("character package refused: %s", backend->last_reason.c_str()); return backend;
        }
        logf("character package configured: %u sections, %u components, %u bones, %u draws, enabled=%u",
             static_cast<uint32_t>(backend->package.sections.size()), static_cast<uint32_t>(backend->package.components.size()),
             backend->package.bones, static_cast<uint32_t>(backend->package.draws.size()), backend->enabled.load() ? 1 : 0);
        if (backend->program && backend->program->outside_meshes())
            logf("character package: %zu referenced mesh file(s) outside the package stay with the game's draws",
                 backend->program->outside_meshes());
    } catch (const std::exception& e) {
        backend->enabled = false; backend->note(e.what());
        logf("character package exception: %s", e.what());
    }
    return backend;
}

void generation_stop(CharacterGeneration& backend) { backend.stop(); }

bool generation_record(CharacterGeneration& backend, CmdBuf* cb, const DrawRec& r)
{
    try { return backend.record(cb, r); }
    catch (const std::exception& e) { ++backend.counters[C_STATE]; backend.note(e.what()); return false; }
}

void generation_retire(CharacterGeneration& backend, uint32_t pipeline_id)
{
    if (backend.cache) backend.cache->retire(pipeline_id);
    std::lock_guard<std::mutex> lock(backend.mod_mx);
    backend.sites.erase(pipeline_id);
    backend.material_profiles.erase(pipeline_id);
    backend.derived_for_pipeline.erase(pipeline_id);
    backend.fragment_registers.erase(pipeline_id);
    backend.source_facts.erase(pipeline_id);
}

void generation_frame(CharacterGeneration& backend, bool had_draws, PresentWork* present)
{
    if (!had_draws) return;
    const uint64_t now = host_time_us();
    if (backend.last_frame_us && now > backend.last_frame_us) {
        const float seconds = static_cast<float>(now - backend.last_frame_us) * 1.0e-6f;
        backend.frame_seconds.store(std::min(std::max(seconds, 0.001f), 0.25f), std::memory_order_relaxed);
    }
    backend.last_frame_us = now;
    backend.frame.fetch_add(1, std::memory_order_relaxed);
    if (backend.program) {
        auto input = sample_ini_input();
        input.frame_seconds = backend.frame_seconds.load(std::memory_order_relaxed);
        input.ticks_us = now;
        OwnedPresenter::sample(backend.device, present, input);
        std::lock_guard<std::mutex> lock(backend.mod_mx);
        std::string reason, owned_reason;
        // With a presentation the generation's GPU commands run in a scope ordered before it.
        const bool owned = backend.presenter && backend.presenter->begin(present, owned_reason);
        if (!owned_reason.empty()) backend.note(owned_reason);
        const bool executed = backend.program->present(input, backend.frame.load(), ini_seconds(), reason);
        if (!executed) backend.note("INI Present: " + reason);
        // The first refusal of owned Present work ends the generation's GPU side; keep its reason.
        static std::atomic<bool> logged{false};
        if (owned && !executed && !logged.exchange(true)) logf("INI Present refused with owned GPU work: %s", reason.c_str());
        if (owned) {
            std::string submission;
            backend.presenter->finish(present, executed, submission);
            if (!submission.empty()) backend.note(submission);
        }
        backend.save_requested = true; backend.wake.notify_one();
    }
}

bool generation_usable(CharacterGeneration& backend, std::string& reason)
{
    std::lock_guard<std::mutex> lock(backend.pool_mx);
    reason = backend.last_reason;
    return bool(backend.cache);
}

bool generation_ready(CharacterGeneration& backend)
{
    return backend.cache && backend.ready.load() && (!backend.package.materials || backend.images_published.load());
}

void generation_enable(CharacterGeneration&, bool);
void generation_status(CharacterGeneration&, JsonWriter&);

// F10 owns another complete resource generation. Its CPU parsing/validation and
// Vulkan preparation run while the current generation continues to draw. Only
// a Ready candidate takes over; command buffers retain their immutable owners.
class CharacterController {
public:
    std::string path;
    bool lazy = false;
    std::shared_mutex mutex;
    std::shared_ptr<CharacterGeneration> active, staged;
    std::future<std::shared_ptr<CharacterGeneration>> loading;
    std::vector<std::future<void>> retiring;
    uint64_t generation = 0, reloads = 0, rejected = 0, staged_at = 0;
    std::string reload_error;
    bool stopped = false;
    ~CharacterController() { stop(); }
    void retire(std::shared_ptr<CharacterGeneration> old)
    {
        if (old) retiring.push_back(std::async(std::launch::async, [old = std::move(old)] { old->stop(); }));
    }
    void stop()
    {
        std::unique_lock<std::shared_mutex> lock(mutex);
        if (stopped) return;
        stopped = true;
        if (loading.valid()) { auto candidate = loading.get(); if (candidate) candidate->stop(); }
        if (staged) staged->stop();
        if (active) active->stop();
        for (auto& future : retiring) future.get();
        retiring.clear(); staged.reset();
    }
    void frame(bool had_draws, PresentWork* present)
    {
        std::unique_lock<std::shared_mutex> lock(mutex);
        if (stopped || !active) return;
        generation_frame(*active, had_draws, present);
        for (auto it = retiring.begin(); it != retiring.end();) {
            if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) { it->get(); it = retiring.erase(it); }
            else ++it;
        }
        if (loading.valid() && loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto candidate = loading.get(); std::string reason;
            if (!candidate || !generation_usable(*candidate, reason)) {
                ++rejected; reload_error = reason.empty() ? "INI generation preparation failed" : reason;
                logf("INI reload refused; current generation retained: %s", reload_error.c_str()); retire(std::move(candidate));
            } else {
                staged = std::move(candidate); staged_at = host_time_us();
                // A lazy package already on the GPU brings its successor up at once.
                if (active->activated.load()) { std::lock_guard<std::mutex> hold(staged->mod_mx); staged->activate(); }
            }
        }
        if (staged) {
            // A lazy package never matched yet needs nothing on the GPU to take over.
            if (generation_ready(*staged) || (staged->lazy && !staged->activated.load())) {
                staged->enabled = active->enabled.load();
                if (active->program && staged->program) {
                    active->program->suspend_persistence();
                    staged->program->inherit_persistent(*active->program);
                }
                staged->save_requested = true; staged->wake.notify_one();
                auto old = std::move(active); active = std::move(staged); ++generation; ++reloads; reload_error.clear();
                logf("INI generation %llu Ready; F10 exchange complete", static_cast<unsigned long long>(generation));
                retire(std::move(old));
            } else if ((staged->images && staged->images->stats().failed) || host_time_us() - staged_at > 30000000) {
                ++rejected; reload_error = "INI candidate resource upload failed or timed out; current generation retained";
                logf("%s", reload_error.c_str()); retire(std::move(staged));
            }
        }
        if (active->program && active->program->take_reload_request() && !loading.valid() && !staged) {
            auto previous = active;
            loading = std::async(std::launch::async, [previous] {
                std::string reason;
                if (!previous->program->save(reason)) {
                    auto failed = std::make_shared<CharacterGeneration>(previous->device, previous->properties, previous->memory,
                                                                        previous->package_path, previous->lazy);
                    failed->note("Cannot preserve settings before reload: " + reason); return failed;
                }
                return generation_create(previous->device, previous->properties, previous->memory, previous->package_path,
                                         previous->lazy);
            });
        }
    }
};

// The module's backend: one controller per configured package. A directory names every live
// package in it (a package with its .runtime.json sidecar); they share the device and the frame,
// each matches only its own sections, and each goes to the GPU on its first match.
class CharacterBackend {
public:
    std::vector<std::unique_ptr<CharacterController>> controllers;
    std::string reason; // why there is no package
    ~CharacterBackend() { for (auto& c : controllers) c->stop(); }
};

namespace {
// The character Mods of an EFMI Mods folder (D-078).
std::vector<std::string> mods_characters(std::string& reason)
{
    std::vector<std::string> found;
    for (const auto& path : CharacterProgram::mods_characters(absolute_folder(g_cfg.m2_mods), reason)) found.push_back(path.u8string());
    if (found.size() > 64) { found.resize(64); logf("character Mods: more than 64 in %s, the first 64 are used", g_cfg.m2_mods.c_str()); }
    if (found.empty() && reason.empty()) reason = "no character Mod in " + g_cfg.m2_mods;
    return found;
}
std::vector<std::string> configured_packages(std::string& reason)
{
    if (!g_cfg.m2_mods.empty()) return mods_characters(reason);
    const std::string& path = g_cfg.m2_package;
    std::error_code error;
    const auto where = std::filesystem::u8path(path);
    if (!std::filesystem::is_directory(where, error)) return {path};
    std::vector<std::string> found;
    for (const auto& entry : std::filesystem::directory_iterator(where, error)) {
        if (!entry.is_regular_file(error) || entry.path().extension() != ".bin") continue;
        std::filesystem::path sidecar = entry.path();
        sidecar += ".runtime.json";
        if (std::filesystem::is_regular_file(sidecar, error)) found.push_back(entry.path().u8string());
    }
    std::sort(found.begin(), found.end());
    if (found.size() > 32) { found.resize(32); logf("character packages: more than 32 in %s, the first 32 are used", path.c_str()); }
    if (found.empty()) reason = "no live character package in " + path;
    return found;
}
}

std::shared_ptr<CharacterBackend> character_create(Device* d, const VkPhysicalDeviceProperties& p, const VkPhysicalDeviceMemoryProperties& m)
{
    auto backend = std::make_shared<CharacterBackend>();
    std::error_code error;
    const bool directory = !g_cfg.m2_mods.empty() || std::filesystem::is_directory(std::filesystem::u8path(g_cfg.m2_package), error);
    for (const auto& path : configured_packages(backend->reason)) {
        auto controller = std::make_unique<CharacterController>();
        controller->path = path; controller->lazy = directory;
        controller->active = generation_create(d, p, m, path, directory);
        backend->controllers.push_back(std::move(controller));
    }
    if (directory) logf("character packages: %zu from %s", backend->controllers.size(),
                        g_cfg.m2_mods.empty() ? g_cfg.m2_package.c_str() : g_cfg.m2_mods.c_str());
    return backend;
}
void character_stop(CharacterBackend& b) { for (auto& c : b.controllers) c->stop(); }
bool character_record(CharacterBackend& b, CmdBuf* cb, const DrawRec& r)
{
    // The first package whose section matches records the draw; the others never see it.
    for (auto& c : b.controllers) {
        std::shared_lock<std::shared_mutex> lock(c->mutex);
        if (!c->stopped && c->active && generation_record(*c->active, cb, r)) return true;
    }
    return false;
}
void character_retire(CharacterBackend& b, uint32_t id)
{
    for (auto& c : b.controllers) {
        std::shared_lock<std::shared_mutex> lock(c->mutex);
        if (c->active) generation_retire(*c->active, id);
        if (c->staged) generation_retire(*c->staged, id);
    }
}
VkResult character_submit(CharacterBackend& b, VkQueue q)
{
    for (auto& c : b.controllers) {
        std::shared_lock<std::shared_mutex> lock(c->mutex); if (c->stopped) continue;
        auto result = c->active ? generation_submit(*c->active, q) : VK_SUCCESS;
        if (result == VK_SUCCESS && c->staged) result = generation_submit(*c->staged, q);
        if (result != VK_SUCCESS) return result;
    }
    return VK_SUCCESS;
}
// Each package runs its Present work in turn; a presenter waits for what the one before handed on.
void character_frame(CharacterBackend& b, bool had_draws, PresentWork* present) { for (auto& c : b.controllers) c->frame(had_draws, present); }
bool character_usable(CharacterBackend& b, std::string& reason)
{
    reason = b.reason;
    bool any = false;
    for (auto& c : b.controllers) {
        std::shared_lock<std::shared_mutex> lock(c->mutex);
        std::string why;
        if (c->active && generation_usable(*c->active, why)) any = true;
        else if (reason.empty()) reason = why;
    }
    if (any) reason.clear();
    return any;
}
// Every usable package is ready; a lazy one counts as ready, it prepares on its first match.
bool character_ready(CharacterBackend& b)
{
    bool any = false;
    for (auto& c : b.controllers) {
        std::shared_lock<std::shared_mutex> lock(c->mutex);
        std::string why;
        if (!c->active || !generation_usable(*c->active, why)) continue;
        any = true;
        if (!c->lazy && !generation_ready(*c->active)) return false;
    }
    return any;
}
void character_enable(CharacterBackend& b, bool enabled)
{
    for (auto& c : b.controllers) { std::shared_lock<std::shared_mutex> lock(c->mutex); if (c->active) generation_enable(*c->active, enabled); }
}
static void controller_status(CharacterController& c, JsonWriter& w)
{
    std::shared_lock<std::shared_mutex> lock(c.mutex);
    w.kn("ini_generation", c.generation); w.kn("ini_reloads", c.reloads); w.kn("ini_reload_rejected", c.rejected);
    w.kb("ini_reload_pending", c.loading.valid() || bool(c.staged)); w.kv("ini_reload_error", c.reload_error);
    if (c.active) generation_status(*c.active, w);
}
// One package's fields at the top as before: the first one on the GPU, else the first. Every package
// under "packages" when there are more.
void character_status(CharacterBackend& b, JsonWriter& w)
{
    CharacterController* primary = b.controllers.empty() ? nullptr : b.controllers.front().get();
    for (auto& c : b.controllers) {
        std::shared_lock<std::shared_mutex> lock(c->mutex);
        if (c->active && c->active->activated.load()) { primary = c.get(); break; }
    }
    if (primary) controller_status(*primary, w);
    else w.kv("package_reason", b.reason);
    if (b.controllers.size() > 1) {
        w.key("packages"); w.begin_array();
        for (auto& c : b.controllers) {
            w.begin_object(); w.kv("path", c->path); controller_status(*c, w); w.end_object();
        }
        w.end_array();
    }
}

const CharacterModule* builtin_character_module()
{
    static const CharacterModule module = [] {
        CharacterModule m;
        m.size = sizeof(CharacterModule); m.abi = EFVK_ABI; m.version = EFVK_VERSION;
        m.create = character_create; m.usable = character_usable; m.ready = character_ready; m.stop = character_stop;
        m.record = character_record; m.retire = character_retire; m.frame = character_frame; m.status = character_status;
        m.enable = character_enable; m.submit = character_submit;
        return m;
    }();
    return &module;
}

void generation_enable(CharacterGeneration& backend, bool enabled) { backend.enabled = enabled && bool(backend.cache); }

void generation_status(CharacterGeneration& backend, JsonWriter& w)
{
    if (backend.presenter) backend.presenter->status(w);
    if (backend.program && !backend.program->gpu_reason().empty()) w.kv("owned_gpu_unavailable", backend.program->gpu_reason());
    if (backend.program) {
        const auto status = backend.program->status();
        w.key("ini"); w.begin_object();
        w.kn("draws", status.draws); w.kn("presents", status.presents); w.kn("key_events", status.key_events);
        w.kn("bindings", status.bindings); w.kn("saves", status.saves); w.kb("dirty", status.dirty);
        w.kb("reload_requested", status.reload_requested);
        w.kn("diagnostics", status.diagnostics);
        w.key("diagnostic_messages"); w.begin_array();
        for (const auto& text : status.diagnostic_messages) w.str(text.c_str());
        w.end_array();
        w.key("variables"); w.begin_object();
        for (const auto& value : status.variables) { w.key(value.first.c_str()); if (std::isfinite(value.second)) w.raw(std::to_string(value.second)); else w.null(); }
        w.end_object(); w.end_object();
    }
    w.kv("kind", "character");
    w.kb("configured", bool(backend.cache));
    w.kb("enabled", backend.enabled.load());
    w.kb("ready", backend.ready.load());
    // The names the single-component backend reports, so that one reader serves both.
    uint64_t unsupported = 0, unreadable = backend.counters[C_ENTRY].load() + backend.counters[C_BONES].load();
    for (uint32_t c = C_STATE; c <= C_BINDINGS; ++c) unsupported += backend.counters[c].load();
    unsupported += backend.counters[C_MATERIAL].load() + backend.counters[C_IMAGE].load();
    w.kn("matched", backend.counters[C_MATCHED].load());
    w.kn("replaced", backend.counters[C_REPLACED].load());
    w.kn("pending", backend.counters[C_PENDING].load());
    w.kn("unsupported", unsupported);
    w.kn("unreadable", unreadable);
    w.kn("exhausted", backend.counters[C_EXHAUSTED].load());
    w.kn("sealed_batches", backend.batches->load());
    w.key("counters"); w.begin_object();
    for (uint32_t c = 0; c < C_COUNT; ++c) w.kn(kCounterNames[c], backend.counters[c].load());
    w.end_object();
    w.kn("frame", backend.frame.load());
    w.kn("sections", backend.package.sections.size());
    w.kn("package_bytes", backend.package.payload.size());
    w.kb("materials", backend.package.materials);
    w.kn("material_profiles", backend.package.profiles.size());
    w.kn("image_count", backend.image_handles.size());
    w.kn("image_resident_bytes", backend.image_budgets.resident->used());
    w.kb("dimension_zero_policy", backend.package.material_vendor != 0);
    w.kn("material_graphics_queues", backend.device->material_queues.size());
    w.kn("material_graphics_family", backend.device->material_family);
    w.kn("image_upload_queue_index", backend.upload_queue_index.load());
    w.kb("images_published", backend.images_published.load());
    w.kn("image_publications", backend.image_publications.load());
    w.kn("images_published_count", backend.published_images.load());
    w.kn("image_queue_waits", backend.image_queue_waits.load());
    if (backend.images) {
        const auto stats = backend.images->stats();
        w.kn("images_ready", stats.ready); w.kn("images_failed", stats.failed);
        w.kn("image_file_reads", stats.file_reads); w.kn("image_submissions", stats.submissions);
    }
    {
        std::lock_guard<std::mutex> lock(backend.pool_mx);
        w.kv("last_reason", backend.last_reason);
        w.kn("available_batches", backend.available.size());
        w.kb("mesh_in_device_memory", backend.mesh && backend.mesh->device_local);
    }
    {
        std::lock_guard<std::mutex> lock(backend.mod_mx);
        uint32_t used = 0, custom = 0;
        for (const auto& identity : backend.identities) { used += identity.used; custom += identity.used && identity.custom; }
        w.kn("identities", used);
        w.kn("identities_replaced", custom);
        w.kn("lod", backend.lod);
        w.kn("material_derived_profiles", backend.derived_profiles.size());
        w.kb("lazy", backend.lazy);
        w.kn("texture_queries", backend.texture_queries);
        w.kn("texture_role_queries", backend.role_queries);
        w.kn("texture_role_draws", backend.role_draws);
        w.kn("source_facts_unknown", backend.facts_unknown);
        w.kn("identify_components", backend.program ? backend.program->identify_components() : 0);
        w.kn("texture_unknown", backend.texture_unknown);
        if (backend.texture_reason) w.kv("texture_unknown_reason", backend.texture_reason);
        w.kb("activated", backend.activated.load());
        w.kn("material_rule_mismatches", backend.rule_mismatches);
    }
}

} // namespace efvk
