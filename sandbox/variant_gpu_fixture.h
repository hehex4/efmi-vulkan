#pragma once
// Shared headless Vulkan fixture for variant and native pass tests.
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
#include <fstream>

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
    X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceFeatures) X(GetPhysicalDeviceFeatures2) X(CreateDevice) X(GetDeviceProcAddr)
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
    std::vector<VkQueue> queues;
    uint32_t submit_kind = 0;
    PFN_vkQueueSubmit2 Submit2 = nullptr;
    VkPhysicalDeviceProperties properties = {};
    VkPhysicalDeviceMemoryProperties memory = {};
    uint32_t family = 0;
    VkQueueFlags queue_flags = 0;
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

    void init(bool enable_bc=false, uint32_t queue_count=1, uint32_t queue_index=0, uint32_t kind=0, bool dynamic=false, bool draw_parameters=false)
    {
        HMODULE lib = LoadLibraryW(L"vulkan-1.dll");
        if (!lib) throw std::runtime_error("Vulkan loader");
        auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(lib, "vkGetInstanceProcAddr"));
        auto create = reinterpret_cast<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE, "vkCreateInstance"));
        VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "efmi_vk_variant_gpu"; app.apiVersion = (kind || dynamic) ? VK_API_VERSION_1_3 : VK_API_VERSION_1_2;
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
            if (props.apiVersion < app.apiVersion) continue;
            uint32_t n = 0; vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, nullptr);
            std::vector<VkQueueFamilyProperties> families(n); vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, families.data());
            for (uint32_t i = 0; i < n; i++) if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                if (families[i].queueCount < queue_count) continue;
                int candidate = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
                if (candidate > score) { physical = pd; family = i; queue_flags = families[i].queueFlags; properties = props; score = candidate; }
                break;
            }
        }
        if (!physical) throw std::runtime_error("graphics device");
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        if (!queue_count || queue_count > 32 || queue_index >= queue_count) throw std::runtime_error("graphics queue selection");
        std::vector<float> priorities(queue_count, 1.f);
        VkDeviceQueueCreateInfo q = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = family; q.queueCount = queue_count; q.pQueuePriorities = priorities.data();
        VkDeviceCreateInfo dc = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dc.queueCreateInfoCount = 1; dc.pQueueCreateInfos = &q;
        VkPhysicalDeviceFeatures supported{},features{};
        if(enable_bc){vkGetPhysicalDeviceFeatures(physical,&supported);features.textureCompressionBC=supported.textureCompressionBC;dc.pEnabledFeatures=&features;}
        VkPhysicalDeviceSynchronization2Features sync{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
        const char* sync_extension = VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
        if (kind) {
            VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; query.pNext = &sync;
            vkGetPhysicalDeviceFeatures2(physical, &query);
            if (!sync.synchronization2) throw std::runtime_error("synchronization2 unsupported");
            dc.pNext = &sync;
            if (kind == 2) { dc.enabledExtensionCount = 1; dc.ppEnabledExtensionNames = &sync_extension; }
        }
        VkPhysicalDeviceDynamicRenderingFeatures rendering{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
        if (dynamic) {
            VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; query.pNext = &rendering;
            vkGetPhysicalDeviceFeatures2(physical, &query);
            if (!rendering.dynamicRendering) throw std::runtime_error("dynamic rendering unsupported");
            rendering.pNext = const_cast<void*>(dc.pNext); dc.pNext = &rendering;
        }
        VkPhysicalDeviceShaderDrawParametersFeatures draw{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES};
        if (draw_parameters) {
            VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; query.pNext = &draw;
            vkGetPhysicalDeviceFeatures2(physical, &query);
            if (!draw.shaderDrawParameters) throw std::runtime_error("shader draw parameters unsupported");
            draw.pNext = const_cast<void*>(dc.pNext); dc.pNext = &draw;
        }
        VK(vkCreateDevice(physical, &dc, nullptr, &device));
#define X(n) vk##n = reinterpret_cast<PFN_vk##n>(vkGetDeviceProcAddr(device, "vk" #n)); if (!vk##n) throw std::runtime_error("vk" #n);
        DEVICE_FUNCS(X)
#undef X
        for (uint32_t i = 0; i < queue_count; ++i) {
            VkQueue handle = VK_NULL_HANDLE; vkGetDeviceQueue(device, family, i, &handle); queues.push_back(handle);
        }
        queue = queues[queue_index]; submit_kind = kind;
        if (kind) {
            Submit2 = reinterpret_cast<PFN_vkQueueSubmit2>(vkGetDeviceProcAddr(device, kind == 1 ? "vkQueueSubmit2" : "vkQueueSubmit2KHR"));
            if (!Submit2) throw std::runtime_error("submission2 entry missing");
        }
        printf("device: %s\n", properties.deviceName);
    }

    VkResult submit(VkCommandBuffer command = VK_NULL_HANDLE)
    {
        if (submit_kind) {
            VkCommandBufferSubmitInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO}; cb.commandBuffer = command;
            VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
            submit.commandBufferInfoCount = command ? 1u : 0u; submit.pCommandBufferInfos = command ? &cb : nullptr;
            return Submit2(queue, 1, &submit, VK_NULL_HANDLE);
        }
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = command ? 1u : 0u; submit.pCommandBuffers = command ? &command : nullptr;
        return vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
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

