// First-use texture load timing (docs/design-review-1.9.md, cause C). No game, window or layer.
//   --d3d11 FILES...           the original's load: DirectXTK CreateDDSTextureFromFileEx as
//                              CustomResource::LoadFromFile calls it, one file after another, on a
//                              hardware D3D11 device; the call's time and the GPU's completion after it.
//   --vulkan ROUNDS FILES...   the layer's loader (ResourceCache with three workers and an upload queue
//                              of its own, as TextureResidency builds it): every file demanded at once,
//                              each one's time to Ready, the wall time, the loader's step counters;
//                              then each file alone (what one draw that needs one texture waits).
// Prints one JSON object per line.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include "DDSTextureLoader.h"
#include "../frontend/assets/resource_cache.h"
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace efmi::assets;
namespace {
uint64_t now_us()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
std::string narrow(const std::wstring& text)
{
    std::string out;
    for (wchar_t c : text) out += (c >= 32 && c < 127 && c != L'"' && c != L'\\') ? char(c) : '?';
    return out;
}
void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }

int d3d11(int count, wchar_t** files)
{
    ID3D11Device* device = nullptr; ID3D11DeviceContext* context = nullptr;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &device, nullptr, &context)),
            "D3D11CreateDevice");
    D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
    uint64_t total_call = 0, total_gpu = 0;
    for (int i = 0; i < count; ++i) {
        ID3D11Resource* resource = nullptr; ID3D11Query* query = nullptr;
        const uint64_t began = now_us();
        const HRESULT hr = DirectX::CreateDDSTextureFromFileEx(device, files[i], 0, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0,
                                                               false, &resource, nullptr, nullptr);
        const uint64_t called = now_us();
        require(SUCCEEDED(hr), "CreateDDSTextureFromFileEx");
        // A draw would follow on the GPU after the driver's copy; when that copy completed.
        require(SUCCEEDED(device->CreateQuery(&qd, &query)), "CreateQuery");
        context->End(query); context->Flush();
        BOOL done = FALSE;
        while (context->GetData(query, &done, sizeof(done), 0) != S_OK) Sleep(0);
        const uint64_t completed = now_us();
        total_call += called - began; total_gpu += completed - began;
        printf("{\"path\":\"d3d11\",\"file\":\"%s\",\"call_us\":%llu,\"gpu_done_us\":%llu}\n", narrow(files[i]).c_str(),
               static_cast<unsigned long long>(called - began), static_cast<unsigned long long>(completed - began));
        query->Release(); resource->Release();
    }
    printf("{\"path\":\"d3d11\",\"files\":%d,\"sum_call_us\":%llu,\"sum_gpu_done_us\":%llu}\n", count,
           static_cast<unsigned long long>(total_call), static_cast<unsigned long long>(total_gpu));
    context->Release(); device->Release();
    return 0;
}

#define BENCH_INSTANCE_CALLS(X) X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) \
    X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceFormatProperties) \
    X(GetPhysicalDeviceImageFormatProperties) X(CreateDevice) X(GetDeviceProcAddr) X(GetPhysicalDeviceFeatures)
#define X(n) PFN_vk##n p##n = nullptr;
BENCH_INSTANCE_CALLS(X)
#undef X

struct Vulkan {
    VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{};
    VkQueue upload{}; uint32_t graphics = 0, transfer = 0;
    VkPhysicalDeviceMemoryProperties memory{};
    VkSemaphore timeline{};
    PFN_vkDestroyDevice DestroyDevice = nullptr; PFN_vkCreateSemaphore CreateSemaphore = nullptr;
    PFN_vkDestroySemaphore DestroySemaphore = nullptr; PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
    PFN_vkDeviceWaitIdle DeviceWaitIdle = nullptr;
    ResourceDevice resource;
    std::mutex upload_mx; std::atomic<uint64_t> serial{0};
    ~Vulkan()
    {
        if (device) { DeviceWaitIdle(device); if (timeline) DestroySemaphore(device, timeline, nullptr); DestroyDevice(device, nullptr); }
        if (instance) pDestroyInstance(instance, nullptr);
    }
    void init()
    {
        auto loader = LoadLibraryW(L"vulkan-1.dll"); require(loader, "Vulkan loader");
        auto gi = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr"));
        auto create = reinterpret_cast<PFN_vkCreateInstance>(gi(nullptr, "vkCreateInstance"));
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName = "efmi_texture_bench"; app.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo = &app;
        require(create(&info, nullptr, &instance) == VK_SUCCESS, "vkCreateInstance");
#define X(n) p##n = reinterpret_cast<PFN_vk##n>(gi(instance, "vk" #n)); require(p##n, "vk" #n);
        BENCH_INSTANCE_CALLS(X)
#undef X
        uint32_t count = 0; pEnumeratePhysicalDevices(instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count); pEnumeratePhysicalDevices(instance, &count, devices.data());
        for (auto pd : devices) {
            VkPhysicalDeviceProperties props{}; pGetPhysicalDeviceProperties(pd, &props);
            if (!physical || props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) physical = pd;
        }
        require(physical, "no Vulkan device");
        pGetPhysicalDeviceMemoryProperties(physical, &memory);
        uint32_t families = 0; pGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
        std::vector<VkQueueFamilyProperties> props(families); pGetPhysicalDeviceQueueFamilyProperties(physical, &families, props.data());
        // As the layer (D-103): the graphics family, and for uploads a dedicated transfer family first, then compute.
        int best = -1;
        for (uint32_t f = 0; f < families; ++f) {
            const auto flags = props[f].queueFlags;
            if ((flags & VK_QUEUE_GRAPHICS_BIT) && best < 0) { graphics = f; best = 0; }
        }
        require(best == 0, "no graphics family");
        transfer = graphics; int score = 1;
        for (uint32_t f = 0; f < families; ++f) {
            const auto flags = props[f].queueFlags;
            if (f == graphics || !(flags & (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT))) continue;
            const int s = (flags & VK_QUEUE_GRAPHICS_BIT) ? 1 : (flags & VK_QUEUE_COMPUTE_BIT) ? 2 : 3;
            if (s > score) { score = s; transfer = f; }
        }
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queues[2]{{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}, {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}};
        queues[0].queueFamilyIndex = graphics; queues[0].queueCount = 1; queues[0].pQueuePriorities = &priority;
        queues[1].queueFamilyIndex = transfer; queues[1].queueCount = 1; queues[1].pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures supported{}; pGetPhysicalDeviceFeatures(physical, &supported);
        VkPhysicalDeviceFeatures features{}; features.textureCompressionBC = supported.textureCompressionBC;
        VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; v12.timelineSemaphore = VK_TRUE;
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.pNext = &v12;
        dci.queueCreateInfoCount = transfer == graphics ? 1 : 2; dci.pQueueCreateInfos = queues; dci.pEnabledFeatures = &features;
        require(pCreateDevice(physical, &dci, nullptr, &device) == VK_SUCCESS, "vkCreateDevice");
        auto get = [&](const char* name) { return pGetDeviceProcAddr(device, name); };
        DestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(get("vkDestroyDevice"));
        CreateSemaphore = reinterpret_cast<PFN_vkCreateSemaphore>(get("vkCreateSemaphore"));
        DestroySemaphore = reinterpret_cast<PFN_vkDestroySemaphore>(get("vkDestroySemaphore"));
        GetDeviceQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(get("vkGetDeviceQueue"));
        DeviceWaitIdle = reinterpret_cast<PFN_vkDeviceWaitIdle>(get("vkDeviceWaitIdle"));
        GetDeviceQueue(device, transfer, 0, &upload);
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO}; type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; sci.pNext = &type;
        require(CreateSemaphore(device, &sci, nullptr, &timeline) == VK_SUCCESS, "timeline");
        resource.device = device; resource.physical = physical; resource.memory = memory;
        resource.graphics_family = graphics; resource.bc_enabled = supported.textureCompressionBC;
        resource.format_properties = pGetPhysicalDeviceFormatProperties; resource.image_properties = pGetPhysicalDeviceImageFormatProperties;
#define X(n) resource.dispatch.n = reinterpret_cast<PFN_vk##n>(get("vk" #n)); require(resource.dispatch.n, "vk" #n);
        EFMI_IMAGE_UPLOAD_FUNCS(X)
#undef X
#define X(n) resource.buffer_dispatch.n = reinterpret_cast<PFN_vk##n>(get("vk" #n));
        EFMI_BUFFER_UPLOAD_FUNCS(X)
#undef X
        resource.upload_queue = upload; resource.upload_family = transfer; resource.upload_mutex = &upload_mx;
        resource.timeline = timeline; resource.serial = &serial;
    }
};

ResourceBudgets layer_budgets(const Vulkan& vk)
{
    // As TextureResidency: the ledger large, staging 256 MiB, source 192 MiB.
    uint64_t heap = 0;
    for (uint32_t i = 0; i < vk.memory.memoryHeapCount; ++i)
        if (vk.memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) heap = std::max<uint64_t>(heap, vk.memory.memoryHeaps[i].size);
    ResourceBudgets budgets;
    budgets.resident = std::make_shared<AllocationBudget>(std::max<uint64_t>(heap, 1ull << 30));
    budgets.staging = std::make_shared<AllocationBudget>(256ull << 20);
    budgets.source = std::make_shared<AllocationBudget>(192ull << 20);
    return budgets;
}

void print_stats(const char* mode, int round, const ResourceCacheStats& s, uint64_t wall)
{
    printf("{\"path\":\"vulkan\",\"mode\":\"%s\",\"round\":%d,\"wall_us\":%llu,\"ready\":%llu,\"failed\":%llu,\"read_us\":%llu,"
           "\"prepare_us\":%llu,\"complete_us\":%llu,\"load_max_us\":%llu}\n", mode, round,
           static_cast<unsigned long long>(wall), static_cast<unsigned long long>(s.ready), static_cast<unsigned long long>(s.failed),
           static_cast<unsigned long long>(s.read_us), static_cast<unsigned long long>(s.prepare_us),
           static_cast<unsigned long long>(s.complete_us), static_cast<unsigned long long>(s.load_max_us));
}

int vulkan(int rounds, int count, wchar_t** files)
{
    Vulkan vk; vk.init();
    for (int round = 0; round < rounds; ++round) {
        {
            // A character's textures all at once, as a character's first frame demands them.
            ResourceCache cache(vk.resource, layer_budgets(vk), 4096, 3);
            std::vector<ResourceHandle> handles; std::string reason;
            for (int i = 0; i < count; ++i) {
                handles.push_back(cache.declare_image(std::filesystem::path(files[i]), reason, uint64_t(round) * 2 + 1));
                require(bool(handles.back()), "declare");
            }
            const uint64_t began = now_us();
            for (auto& h : handles) require(cache.demand(h), "demand");
            std::vector<std::shared_ptr<const ImageUpload>> owners;
            for (int i = 0; i < count; ++i) {
                const auto s = handles[i]->wait_until(std::chrono::steady_clock::now() + std::chrono::seconds(60));
                require(s.state == ResourceState::Ready, "load did not become Ready");
                printf("{\"path\":\"vulkan\",\"mode\":\"together\",\"round\":%d,\"file\":\"%s\",\"ready_us\":%llu}\n", round,
                       narrow(files[i]).c_str(), static_cast<unsigned long long>(now_us() - began));
                owners.push_back(s.image);
            }
            print_stats("together", round, cache.stats(), now_us() - began);
            cache.stop();
        }
        {
            // One texture at a time: what a draw that needs one waits.
            ResourceCache cache(vk.resource, layer_budgets(vk), 4096, 3);
            std::string reason; uint64_t total = 0;
            for (int i = 0; i < count; ++i) {
                auto h = cache.declare_image(std::filesystem::path(files[i]), reason, uint64_t(round) * 2 + 2);
                const uint64_t began = now_us();
                require(h && cache.demand(h), "demand");
                const auto s = h->wait_until(std::chrono::steady_clock::now() + std::chrono::seconds(60));
                require(s.state == ResourceState::Ready, "load did not become Ready");
                total += now_us() - began;
                printf("{\"path\":\"vulkan\",\"mode\":\"alone\",\"round\":%d,\"file\":\"%s\",\"ready_us\":%llu}\n", round,
                       narrow(files[i]).c_str(), static_cast<unsigned long long>(now_us() - began));
            }
            print_stats("alone", round, cache.stats(), total);
            cache.stop();
        }
    }
    return 0;
}
}

int wmain(int argc, wchar_t** argv)
{
    try {
        if (argc >= 3 && !wcscmp(argv[1], L"--d3d11")) return d3d11(argc - 2, argv + 2);
        if (argc >= 4 && !wcscmp(argv[1], L"--vulkan")) return vulkan(_wtoi(argv[2]), argc - 3, argv + 3);
        fprintf(stderr, "usage: --d3d11 FILES... | --vulkan ROUNDS FILES...\n"); return 2;
    } catch (const std::exception& e) { fprintf(stderr, "texture_load_bench: %s\n", e.what()); return 1; }
}
