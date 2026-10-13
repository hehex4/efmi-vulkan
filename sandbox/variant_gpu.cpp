// Headless Vulkan proof: rewritten VS, unchanged FS, wider mod vertices/indices, restored next draw.
// All images are offscreen. No window, global layer setting, game, or installed mod is touched.
#include "variant_fixture.h"
#include "../layer/src/variant_cache.h"
#include "../layer/src/draw_snapshots.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

using namespace efvk;
using variant_fixture::RecipeFixture;
static std::atomic<uint32_t> errors{0}, warnings{0};
static int passed = 0, failed = 0;
static void check(bool ok, const char* label)
{
    if (ok) passed++; else { failed++; printf("FAIL: %s\n", label); }
}
static void vkcheck(VkResult r, const char* name)
{
    if (r != VK_SUCCESS) { printf("FAILED %s: %d\n", name, r); throw std::runtime_error(name); }
}
#define VK(call) vkcheck((call), #call)

#define INSTANCE_FUNCS(X) \
    X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceQueueFamilyProperties) \
    X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceFeatures) X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE_FUNCS(X) \
    EFVK_VARIANT_FUNCS(X) \
    X(DestroyDevice) X(GetDeviceQueue) X(DeviceWaitIdle) X(CreateImage) X(DestroyImage) \
    X(GetImageMemoryRequirements) X(BindImageMemory) X(CreateImageView) X(DestroyImageView) \
    X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateDescriptorPool) X(DestroyDescriptorPool) \
    X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) \
    X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) \
    X(CreateCommandPool) X(DestroyCommandPool) X(ResetCommandPool) X(ResetCommandBuffer) \
    X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdSetViewport) X(CmdSetScissor) X(CmdCopyImageToBuffer) \
    X(CmdPipelineBarrier) X(QueueSubmit) X(QueueWaitIdle) X(CmdClearColorImage)
#define X(n) static PFN_vk##n vk##n;
INSTANCE_FUNCS(X)
DEVICE_FUNCS(X)
#undef X

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
    bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
    if (!error && data->pMessageIdName && !strcmp(data->pMessageIdName, "Loader Message") &&
        (strstr(data->pMessage, "forced disabled because name matches filter of env var 'VK_LOADER_LAYERS_DISABLE'") ||
         !strcmp(data->pMessage, "env var 'VK_INSTANCE_LAYERS' defined and adding layers \"VK_LAYER_EFMI_probe\""))) {
        printf("LOADER NOTICE: %s\n", data->pMessage); return VK_FALSE;
    }
    if (error) ++errors; else ++warnings;
    printf("VALIDATION %s [%s]: %s\n", error ? "ERROR" : "WARNING", data->pMessageIdName, data->pMessage);
    return error ? VK_TRUE : VK_FALSE;
}

struct Buffer { VkBuffer handle; VkDeviceMemory memory; void* mapped; VkDeviceSize size; };
struct Image { VkImage handle; VkDeviceMemory memory; VkImageView view; };

class Gpu {
public:
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties = {};
    VkPhysicalDeviceMemoryProperties memory = {};
    uint32_t family = 0;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger = nullptr;
    std::vector<Buffer> buffers;
    std::vector<Image> images;
    std::vector<VkDescriptorSetLayout> sets;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkDescriptorPool descriptors = VK_NULL_HANDLE;

    ~Gpu()
    {
        if (device) {
            vkDeviceWaitIdle(device);
            if (pool) vkDestroyCommandPool(device, pool, nullptr);
            if (descriptors) vkDestroyDescriptorPool(device, descriptors, nullptr);
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
            if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
            if (pass) vkDestroyRenderPass(device, pass, nullptr);
            for (auto s : sets) vkDestroyDescriptorSetLayout(device, s, nullptr);
            for (auto& b : buffers) {
                if (b.mapped) vkUnmapMemory(device, b.memory);
                if (b.handle) vkDestroyBuffer(device, b.handle, nullptr);
                if (b.memory) vkFreeMemory(device, b.memory, nullptr);
            }
            for (auto& i : images) {
                if (i.view) vkDestroyImageView(device, i.view, nullptr);
                if (i.handle) vkDestroyImage(device, i.handle, nullptr);
                if (i.memory) vkFreeMemory(device, i.memory, nullptr);
            }
            vkDestroyDevice(device, nullptr);
        }
        if (messenger) destroy_messenger(instance, messenger, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }

    void init(bool enable_bc=false)
    {
        HMODULE lib = LoadLibraryW(L"vulkan-1.dll");
        if (!lib) throw std::runtime_error("Vulkan loader");
        auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(lib, "vkGetInstanceProcAddr"));
        auto create = reinterpret_cast<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE, "vkCreateInstance"));
        VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "efmi_vk_variant_gpu"; app.apiVersion = VK_API_VERSION_1_2;
        const char* vvl = "VK_LAYER_KHRONOS_validation";
        const char* debug_ext = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        VkDebugUtilsMessengerCreateInfoEXT debug = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug.pfnUserCallback = debug_message;
        VkInstanceCreateInfo ci = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app; ci.enabledLayerCount = 1; ci.ppEnabledLayerNames = &vvl;
        ci.enabledExtensionCount = 1; ci.ppEnabledExtensionNames = &debug_ext; ci.pNext = &debug;
        VK(create(&ci, nullptr, &instance));
#define X(n) vk##n = reinterpret_cast<PFN_vk##n>(gipa(instance, "vk" #n)); if (!vk##n) throw std::runtime_error("vk" #n);
        INSTANCE_FUNCS(X)
#undef X
        auto create_debug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(gipa(instance, "vkCreateDebugUtilsMessengerEXT"));
        destroy_messenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(gipa(instance, "vkDestroyDebugUtilsMessengerEXT"));
        VK(create_debug(instance, &debug, nullptr, &messenger));
        uint32_t count = 0; VK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
        std::vector<VkPhysicalDevice> devices(count); VK(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
        int score = -1;
        for (auto pd : devices) {
            VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(pd, &props);
            if (props.apiVersion < VK_API_VERSION_1_2) continue;
            uint32_t n = 0; vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, nullptr);
            std::vector<VkQueueFamilyProperties> families(n); vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, families.data());
            for (uint32_t i = 0; i < n; i++) if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                int candidate = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
                if (candidate > score) { physical = pd; family = i; properties = props; score = candidate; }
                break;
            }
        }
        if (!physical) throw std::runtime_error("graphics device");
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        float priority = 1;
        VkDeviceQueueCreateInfo q = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = family; q.queueCount = 1; q.pQueuePriorities = &priority;
        VkDeviceCreateInfo dc = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dc.queueCreateInfoCount = 1; dc.pQueueCreateInfos = &q;
        VkPhysicalDeviceFeatures supported{},features{};
        if(enable_bc){vkGetPhysicalDeviceFeatures(physical,&supported);features.textureCompressionBC=supported.textureCompressionBC;dc.pEnabledFeatures=&features;}
        VK(vkCreateDevice(physical, &dc, nullptr, &device));
#define X(n) vk##n = reinterpret_cast<PFN_vk##n>(vkGetDeviceProcAddr(device, "vk" #n)); if (!vk##n) throw std::runtime_error("vk" #n);
        DEVICE_FUNCS(X)
#undef X
        vkGetDeviceQueue(device, family, 0, &queue);
        printf("device: %s\n", properties.deviceName);
    }

    VkDeviceMemory allocate(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags flags)
    {
        for (uint32_t i = 0; i < memory.memoryTypeCount; i++)
            if ((requirements.memoryTypeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) {
                VkMemoryAllocateInfo a = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                a.allocationSize = requirements.size; a.memoryTypeIndex = i;
                VkDeviceMemory result; VK(vkAllocateMemory(device, &a, nullptr, &result)); return result;
            }
        throw std::runtime_error("memory type");
    }

    Buffer buffer(VkDeviceSize size, VkBufferUsageFlags usage)
    {
        buffers.push_back({}); auto& b = buffers.back(); b.size = size;
        VkBufferCreateInfo ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; ci.size = size; ci.usage = usage;
        VK(vkCreateBuffer(device, &ci, nullptr, &b.handle));
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(device, b.handle, &req);
        b.memory = allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK(vkBindBufferMemory(device, b.handle, b.memory, 0)); VK(vkMapMemory(device, b.memory, 0, size, 0, &b.mapped));
        memset(b.mapped, 0, static_cast<size_t>(size)); return b;
    }

    Image image(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect)
    {
        images.push_back({}); auto& img = images.back();
        VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = format; ci.extent = {64, 64, 1}; ci.mipLevels = ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.usage = usage;
        VK(vkCreateImage(device, &ci, nullptr, &img.handle));
        VkMemoryRequirements req; vkGetImageMemoryRequirements(device, img.handle, &req);
        img.memory = allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); VK(vkBindImageMemory(device, img.handle, img.memory, 0));
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = img.handle;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format; vi.subresourceRange = {aspect, 0, 1, 0, 1};
        VK(vkCreateImageView(device, &vi, nullptr, &img.view)); return img;
    }
};

#include "m2_gpu_case.h"
#include "character_gpu_case.h"

static void run_character_assets(Gpu& g,uint32_t expected)
{
    const std::string marker="\"images_ready\":"+std::to_string(expected);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    bool ready=false;
    do{
        VK(vkQueueSubmit(g.queue,0,nullptr,VK_NULL_HANDLE));
        Sleep(10);
        const auto status=self_probe("{\"cmd\":\"status\"}","character-assets.json");
        ready=status.find(marker)!=std::string::npos;
    }while(!ready && std::chrono::steady_clock::now()<deadline);
    check(ready,"actual character DDS files reach Ready through the layer's queue service");
    VK(vkDeviceWaitIdle(g.device));
}

static void run(Gpu& g, bool use_rp2,bool material)
{
    VariantDispatch dispatch;
#define X(n) dispatch.n = vk##n;
    EFVK_VARIANT_FUNCS(X)
#undef X
    GraphicsRecipe recipe;
    PipelineLayoutInfo source_layout;
    {
        RecipeFixture f(material);
        if(material)f.ba.blendEnable=VK_FALSE;
        recipe = f.capture(); source_layout = f.layout;
    }
    if (use_rp2) {
        auto pass2 = std::make_shared<RenderPassRecipe>(*recipe.render_pass);
        pass2->version2 = true; recipe.render_pass = pass2;
    }
    check(recipe.complete, "source recipe complete after source arrays die");
    VK(recipe.render_pass->create(g.device, vkCreateRenderPass, vkCreateRenderPass2, &g.pass));
    for (const auto& set : source_layout.sets) {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (const auto& pair : set->bindings)
            bindings.push_back({pair.first, static_cast<VkDescriptorType>(pair.second.type), pair.second.count, pair.second.stages, nullptr});
        VkDescriptorSetLayoutCreateInfo ci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = static_cast<uint32_t>(bindings.size()); ci.pBindings = bindings.data();
        VkDescriptorSetLayout handle; VK(vkCreateDescriptorSetLayout(g.device, &ci, nullptr, &handle)); g.sets.push_back(handle);
    }
    VkPipelineLayoutCreateInfo pl = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 3; pl.pSetLayouts = g.sets.data(); VK(vkCreatePipelineLayout(g.device, &pl, nullptr, &g.layout));
    std::vector<VkShaderModule> modules;
    for (const auto& s : recipe.stages) {
        VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; ci.codeSize = s.shader->code.size() * 4;
        ci.pCode = s.shader->code.data(); VkShaderModule m; VK(vkCreateShaderModule(g.device, &ci, nullptr, &m)); modules.push_back(m);
    }
    VkResult result = recipe.create(g.device, vkCreateGraphicsPipelines, g.layout, g.pass, modules, &g.pipeline);
    for (auto m : modules) vkDestroyShaderModule(g.device, m, nullptr);
    VK(result);
    // Real creation handles die before the variant starts on a worker. It uses only owned recipes.
    vkDestroyRenderPass(g.device, g.pass, nullptr); g.pass = VK_NULL_HANDLE;
    VariantCache cache(g.device, dispatch, g.properties.limits.maxBoundDescriptorSets,128,g.properties.limits);
    FragmentPatch material_patch;if(material)material_patch.images={{1,6}};
    auto owned_recipe = std::make_shared<GraphicsRecipe>(recipe);
    auto owned_layout = std::make_shared<PipelineLayoutInfo>(source_layout);
    auto pending = cache.request(1, owned_recipe, owned_layout, RecipeFixture::mod_vertex(),material_patch);
    check(pending.state == VariantState::Queued && !pending.variant, "first live GPU cache request is pending");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        pending = cache.request(1, owned_recipe, owned_layout, RecipeFixture::mod_vertex(),material_patch);
    } while ((pending.state == VariantState::Queued || pending.state == VariantState::Building) &&
             std::chrono::steady_clock::now() < deadline);
    check(pending.state == VariantState::Ready, "cache publishes a driver-built variant from owned state");
    if (!pending.variant) { printf("variant failed: %s\n", pending.reason.c_str()); return; }
    const auto& variant = *pending.variant;
    VariantResult null_variant;
    if(material){
        FragmentPatch patch;patch.nulls={{1,6}};
        do{
            null_variant=cache.request(1,owned_recipe,owned_layout,RecipeFixture::mod_vertex(),patch);
            if(null_variant.state==VariantState::Queued || null_variant.state==VariantState::Building)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));else break;
        }while(std::chrono::steady_clock::now()<deadline);
        check(null_variant.state==VariantState::Ready && null_variant.variant->image_count==0,"null material has an independently compiled buffer-only private layout");
        if(!null_variant.variant)throw std::runtime_error(null_variant.reason);
    }
    // A separate compatible pass is used for drawing, rather than the already destroyed creation pass.
    VK(recipe.render_pass->create(g.device, vkCreateRenderPass, vkCreateRenderPass2, &g.pass));
    Image color = g.image(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    Image depth = g.image(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    VkImageView views[] = {color.view, depth.view};
    VkFramebufferCreateInfo fb = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fb.renderPass = g.pass;
    fb.attachmentCount = 2; fb.pAttachments = views; fb.width = fb.height = 64; fb.layers = 1;
    VK(vkCreateFramebuffer(g.device, &fb, nullptr, &g.framebuffer));

    struct OriginalVertex { float pos[2]; uint8_t blend[4]; };
    struct ModVertex { float pos[2]; uint16_t blend[4]; };
    static_assert(sizeof(OriginalVertex) == 12 && sizeof(ModVertex) == 16, "fixture vertex strides");
    const OriginalVertex ov[] = {{{-.22f, -.2f}, {1, 0, 0, 0}}, {{.22f, -.2f}, {1, 0, 0, 0}}, {{0, .22f}, {1, 0, 0, 0}}};
    const ModVertex mv[] = {{{-.22f, -.2f}, {257, 0, 0, 0}}, {{.22f, -.2f}, {257, 0, 0, 0}}, {{0, .22f}, {257, 0, 0, 0}},
                           {{-.22f, -.2f}, {257, 0, 0, 0}}, {{.22f, -.2f}, {257, 0, 0, 0}}, {{0, .22f}, {257, 0, 0, 0}}};
    const uint16_t oi[] = {0, 1, 2}; const uint32_t mi[] = {0, 1, 2, 0, 1, 2};
    Buffer original_vb = g.buffer(sizeof(ov), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT); memcpy(original_vb.mapped, ov, sizeof(ov));
    Buffer mod_vb = g.buffer(sizeof(mv), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT); memcpy(mod_vb.mapped, mv, sizeof(mv));
    Buffer original_ib = g.buffer(sizeof(oi) + 16, VK_BUFFER_USAGE_INDEX_BUFFER_BIT); memcpy(static_cast<char*>(original_ib.mapped) + 16, oi, sizeof(oi));
    Buffer mod_ib = g.buffer(sizeof(mi) + 32, VK_BUFFER_USAGE_INDEX_BUFFER_BIT); memcpy(static_cast<char*>(mod_ib.mapped) + 32, mi, sizeof(mi));
    Buffer bones = g.buffer(259 * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    std::vector<uint8_t> own_bones(259 * 16);
    float* original_bones = static_cast<float*>(bones.mapped); original_bones[1 * 4] = -.5f;
    float move = .5f; memcpy(own_bones.data() + 257 * 16, &move, 4);
    const float game_tint[] = {material?1.f:0.f, 1, 1, 1}, own_tint[] = {1, 0, 0, 1};
    memcpy(original_bones + 258 * 4, game_tint, 16); memcpy(own_bones.data() + 258 * 16, own_tint, 16);
    uint32_t offset = static_cast<uint32_t>(g.properties.limits.minUniformBufferOffsetAlignment);
    if (offset < 32) offset = 32;
    Buffer instance = g.buffer(offset + 32, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    InstanceEntry own_instance = {};
    const float original_instance[] = {0, -.35f, 0, 0, 1, 1, material?1.f:0.f, material?1.f:.5f};
    const float replacement_instance[] = {0, .35f, 0, 0, 1, 0, 1, 1};
    memcpy(static_cast<char*>(instance.mapped) + offset, original_instance, 32);
    memcpy(own_instance.data(), replacement_instance, 32);
    // Offset zero is deliberately wrong for both draws.
    static_cast<float*>(instance.mapped)[0] = 10;
    SnapshotDispatch snapshot_dispatch;
#define X(n) snapshot_dispatch.n = vk##n;
    EFVK_SNAPSHOT_FUNCS(X)
#undef X
    std::vector<std::shared_ptr<Image>> material_images;std::weak_ptr<Image> retained_image;
    if(material)for(unsigned i=0;i<3;++i){
        auto image=g.image(VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        auto owner=std::shared_ptr<Image>(new Image(image),[device=g.device](Image* img){
            vkDestroyImageView(device,img->view,nullptr);vkDestroyImage(device,img->handle,nullptr);vkFreeMemory(device,img->memory,nullptr);delete img;});
        g.images.pop_back();material_images.push_back(std::move(owner));
    }
    if(material)retained_image=material_images[1];
    auto image_list=[&](unsigned i){return material?std::vector<SnapshotImage>{{material_images[i]->view,material_images[i]}}:std::vector<SnapshotImage>{};};
    DrawSnapshots snapshots(g.device, snapshot_dispatch);
    check(snapshots.create(g.memory, g.properties.limits, 3, static_cast<uint32_t>(own_bones.size()),65536,material?1:0),
          "private snapshot storage with full 64 KiB uniform ranges created");
    if (!snapshots.size()) throw std::runtime_error(snapshots.reason);
    SnapshotSlot bad_slot, good_slot, second_slot;
    check(!snapshots.append({},own_instance,bad_slot) && snapshots.used()==0, "wrong bone size does not consume a slot");
    auto wrong_instance=own_instance;float failed_x=-.5f,failed_y=-.35f;
    memcpy(wrong_instance.data(),&failed_x,4);memcpy(wrong_instance.data()+4,&failed_y,4);
    check(snapshots.append(own_bones,wrong_instance,image_list(1),bad_slot) && snapshots.append(own_bones,own_instance,image_list(1),good_slot),
          "each recorded draw gets a separate bone and entry slice");
    auto second_instance=own_instance;float second_x=-1;memcpy(second_instance.data(),&second_x,4);
    check(snapshots.append(own_bones,second_instance,image_list(2),second_slot), "second replacement uses its own entry and images");
    check(second_slot.offset>good_slot.offset && good_slot.offset>=256 && snapshots.used()==3,
          "replacement entries use distinct nonzero dynamic offsets");
    check(!snapshots.append(own_bones,own_instance,bad_slot), "full snapshot storage leaves original draw available");
    snapshots.seal();
    check(!snapshots.append(own_bones,own_instance,bad_slot) &&
          !snapshots.create(g.memory,g.properties.limits,2,static_cast<uint32_t>(own_bones.size())),
          "sealed storage cannot be changed or recreated");
    DrawSnapshots null_snapshots(g.device,snapshot_dispatch);SnapshotSlot null_slot;
    if(material){
        auto null_entry=own_instance;float nx=-.5f,ny=-.7f;memcpy(null_entry.data(),&nx,4);memcpy(null_entry.data()+4,&ny,4);
        check(null_snapshots.create(g.memory,g.properties.limits,1,static_cast<uint32_t>(own_bones.size())) &&
              null_snapshots.append(own_bones,null_entry,null_slot),"explicit null material uses no image descriptor or owner");
        null_snapshots.seal();
    }
    std::fill(own_bones.begin(),own_bones.end(),0); own_instance.fill(0);
    Buffer readback = g.buffer(64 * 64 * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1}};
    VkDescriptorPoolCreateInfo dp = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dp.maxSets = 3; dp.poolSizeCount = material?3:2; dp.pPoolSizes = sizes;
    VK(vkCreateDescriptorPool(g.device, &dp, nullptr, &g.descriptors));
    VkDescriptorSetLayout set_layouts[] = {g.sets[0], g.sets[1], g.sets[2]};
    VkDescriptorSet sets[3];
    VkDescriptorSetAllocateInfo da = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; da.descriptorPool = g.descriptors;
    da.descriptorSetCount = 3; da.pSetLayouts = set_layouts; VK(vkAllocateDescriptorSets(g.device, &da, sets));
    VkDescriptorBufferInfo infos[] = {{bones.handle, 0, bones.size}, {instance.handle, 0, 32}};
    VkWriteDescriptorSet writes[2] = {};
    for (uint32_t i = 0; i < 2; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet = sets[i == 0 ? 0 : 2];
        writes[i].dstBinding = i == 0 ? 7 : 0; writes[i].descriptorCount = 1;
        writes[i].descriptorType = i % 2 == 0 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(g.device, 2, writes, 0, nullptr);
    if(material){
        VkDescriptorImageInfo image{VK_NULL_HANDLE,material_images[0]->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=sets[1];write.dstBinding=6;
        write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.descriptorCount=1;write.pImageInfo=&image;
        vkUpdateDescriptorSets(g.device,1,&write,0,nullptr);
    }
    // The live descriptor sets and pipeline layout no longer need the creation handles either.
    for (auto s : g.sets) vkDestroyDescriptorSetLayout(g.device, s, nullptr); g.sets.clear();
    VkCommandPoolCreateInfo pc = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pc.queueFamilyIndex = g.family;
    VK(vkCreateCommandPool(g.device, &pc, nullptr, &g.pool));
    VkCommandBufferAllocateInfo ca = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool = g.pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount = 1; VkCommandBuffer cmd;
    VK(vkAllocateCommandBuffers(g.device, &ca, &cmd));
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; VK(vkBeginCommandBuffer(cmd, &begin));
    if(material)for(unsigned i=0;i<material_images.size();++i){
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=material_images[i]->handle;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
        VkClearColorValue color{};color.float32[i==0?2:(i==1?0:1)]=1;color.float32[3]=1;
        vkCmdClearColorImage(cmd,b.image,b.newLayout,&color,1,&b.subresourceRange);
        b.oldLayout=b.newLayout;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcAccessMask=b.dstAccessMask;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
    }
    VkClearValue clear[2] = {}; clear[0].color.float32[2] = clear[0].color.float32[3] = 1; clear[1].depthStencil.depth = 1;
    VkRenderPassBeginInfo bi = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; bi.renderPass = g.pass; bi.framebuffer = g.framebuffer;
    bi.renderArea.extent = {64, 64}; bi.clearValueCount = 2; bi.pClearValues = clear;
    vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport = {0, 0, 64, 64, 0, 1}; VkRect2D scissor = {{0, 0}, {64, 64}};
    vkCmdSetViewport(cmd, 0, 1, &viewport); vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeline);
    VkDeviceSize zero = 0; vkCmdBindVertexBuffers(cmd, 0, 1, &original_vb.handle, &zero);
    vkCmdBindIndexBuffer(cmd, original_ib.handle, 16, VK_INDEX_TYPE_UINT16);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, 0, 3, sets, 1, &offset);
    DrawGeometry original = {0, {original_vb.handle}, {0}, original_ib.handle, 16, VK_INDEX_TYPE_UINT16};
    DrawGeometry replacement = {0, {mod_vb.handle}, {0}, mod_ib.handle, 32, VK_INDEX_TYPE_UINT32};
    IndexedDraw draw; draw.count = 3;
    // No-op failure is followed by the real transaction, then an app draw with no state rebound.
    check(!emit_variant_draw(dispatch, cmd, variant, VK_NULL_HANDLE, offset, replacement, draw, g.pipeline, original),
          "unready transaction emits nothing");
    VariantDraw sequence[]={{&variant,good_slot.set,good_slot.offset,&replacement,draw},
                            {&variant,second_slot.set,second_slot.offset,&replacement,draw}};
    sequence[1].draw.first=3;sequence[1].draw.vertex_offset=3;
    // If the emitter draws the first item before rejecting the second, this writes an extra
    // visible triangle. Move it away from both valid draws so depth cannot hide the defect.
    auto invalid=sequence[0];invalid.own=VK_NULL_HANDLE;
    auto poison=sequence[0];poison.own=bad_slot.set;poison.instance_offset=bad_slot.offset;
    VariantDraw rejected[]={poison,invalid};
    check(!emit_variant_draws(dispatch,cmd,rejected,2,g.pipeline,original), "late failure rejects the entire draw sequence");
    check(emit_variant_draws(dispatch,cmd,nullptr,0,g.pipeline,original), "resolved empty draw sequence is an intentional skip");
    check(emit_variant_draws(dispatch,cmd,sequence,2,g.pipeline,original), "multiple resolved ranges emitted by one transaction");
    if(material)check(emit_variant_draw(dispatch,cmd,*null_variant.variant,null_slot.set,null_slot.offset,replacement,draw,g.pipeline,original),
                      "explicit null fragment variant restores original application state");
    vkCmdDrawIndexed(cmd, 3, 1, 0, 0, 0);
    vkCmdEndRenderPass(cmd);
    VkBufferImageCopy copy = {}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {64, 64, 1};
    vkCmdCopyImageToBuffer(cmd, color.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle, 1, &copy);
    VkBufferMemoryBarrier barrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT; barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = readback.handle; barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    VK(vkEndCommandBuffer(cmd)); VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
    cache.retire(1); cache.stop();
    if(material){material_images[1].reset();material_images[2].reset();check(!retained_image.expired(),"recorded material owns retired images until GPU completion and resubmission");}
    check(pending.variant->pipeline != VK_NULL_HANDLE, "recorded command owner retains the retired GPU pipeline");
    VK(vkQueueSubmit(g.queue, 1, &submit, VK_NULL_HANDLE)); VK(vkQueueWaitIdle(g.queue));
    // Resubmitting executable commands reads the same sealed slices, with no host overwrite.
    VK(vkQueueSubmit(g.queue, 1, &submit, VK_NULL_HANDLE)); VK(vkQueueWaitIdle(g.queue));
    auto pixel = [&](uint32_t x, uint32_t y, bool mesh, const char* label) {
        auto p = static_cast<const uint8_t*>(readback.mapped) + 4 * (y * 64 + x);
        printf("pixel %s: %u %u %u %u\n", label, p[0], p[1], p[2], p[3]);
        check(mesh ? p[0] == 0 && p[1] >= 127 && p[1] <= 128 && p[2] >= 127 && p[2] <= 128 && p[3] >= 127 && p[3] <= 128 :
                     p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255, label);
    };
    if(material){
        auto sample=[&](uint32_t x,uint32_t y,const std::array<uint8_t,4>& expected,const char* label){
            const auto p=static_cast<const uint8_t*>(readback.mapped)+4*(y*64+x);check(!memcmp(p,expected.data(),4),label);};
        sample(48,42,{255,0,0,255},"first material draw reads retained private red image");
        sample(16,42,{0,255,0,255},"second material draw reads its private green image and original fragment buffers");
        sample(32,8,{0,0,0,0},"null variant produces zero without a bound null descriptor");
        sample(16,19,{0,0,255,255},"following original draw keeps original blue material");
    }else{
        pixel(48, 42, true, "mod geometry moved by both own buffers, original fragment data and blending");
        pixel(16, 42, true, "second range uses its first index, vertex offset and private instance entry");
        pixel(16, 19, true, "next draw restores original pipeline, VB, IB, sets and dynamic offsets");
    }
    pixel(32, 19, false, "rejected transaction did not emit its valid first item");
    pixel(32, 32, false, "background outside both draws");
    FILE* file = nullptr; fopen_s(&file, "pixels.rgba", "wb");
    if (file) { fwrite(readback.mapped, 1, 64 * 64 * 4, file); fclose(file); }
}

int wmain(int argc, wchar_t** argv)
{
    bool rp2 = false, canary = false, m2 = false, material=false, character=false, reject_late=false;
    uint32_t character_assets=0;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--inject") && i + 1 < argc) {
            if (!LoadLibraryW(argv[++i])) return 2;
        } else if (!wcscmp(argv[i], L"--renderpass2")) rp2 = true;
        else if (!wcscmp(argv[i], L"--canary")) canary = true;
        else if (!wcscmp(argv[i], L"--m2")) m2 = true;
        else if (!wcscmp(argv[i], L"--character")) character = true;
        else if (!wcscmp(argv[i], L"--material")) material = true;
        else if (!wcscmp(argv[i], L"--character-reject-late")) reject_late = true;
        else if (!wcscmp(argv[i], L"--character-assets") && i+1<argc) character_assets=static_cast<uint32_t>(_wtoi(argv[++i]));
    }
    try {
        Gpu g; g.init(character_assets!=0);
        if (canary) {
            VkBufferCreateInfo bad = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bad.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            VkBuffer handle = VK_NULL_HANDLE;
            check(vkCreateBuffer(g.device, &bad, nullptr, &handle) == VK_ERROR_VALIDATION_FAILED_EXT,
                  "validation aborts the deliberate zero-size buffer");
        } else if (character_assets) run_character_assets(g,character_assets);
        else if (m2) run_m2(g, rp2);
        else if (character) run_character(g, rp2,material,reject_late);
        else run(g, rp2,material);
    } catch (const std::exception& e) { printf("FAILED: %s\n", e.what()); failed++; }
    printf("validation: errors=%u warnings=%u\n", errors.load(), warnings.load());
    check(errors == (canary ? 1u : 0u) && warnings == 0, "validation diagnostics match expectation");
    printf("variant_gpu: %d/%d checks passed\n", passed, passed + failed);
    return failed ? 1 : 0;
}
