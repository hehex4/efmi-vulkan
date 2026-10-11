// A deterministic Vulkan program for testing the layer without the game.
//
// It draws five small meshes every frame:
//   - three from index buffer A (16-bit, device-local, uploaded through a staging copy),
//   - two from index buffer B (32-bit, host-visible and persistently mapped, bound at a byte offset).
// Each draw selects its uniform block with a dynamic offset. With --threads N the draws are
// recorded on N threads into secondary command buffers, the way an engine with graphics jobs does.
// The expected draws and their CRC-32C region hashes are written to a JSON file for the tests.
//
//   efmi_vk_sandbox [--seconds S] [--threads N] [--expected FILE] [--trigger FILE] [--primary-screen]
//                   [--inject DLL | --inject-late DLL | --inject-late-exports DLL]
//
// --inject loads a DLL before the program touches Vulkan, the way a launcher injects one at
// process start. --inject-late loads it after the device exists, which is too late to intercept.
// --inject-late-exports does the same and then looks every device function up again through the
// exports of vulkan-1.dll, like a second Vulkan user in the process that resolves its functions late.
//
// When the trigger file appears, the indices of the second mesh are rewritten through a staging
// buffer that is unmapped before the copy is recorded, and FILE.done is created.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef CreateSemaphore

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "spirv.h"

// ---------------------------------------------------------------------------
// Vulkan functions, loaded at run time from vulkan-1.dll

#define VK_INSTANCE_FUNCS(X)                                                                                           \
    X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties)                                      \
    X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceMemoryProperties) X(CreateDevice)                     \
    X(CreateWin32SurfaceKHR) X(DestroySurfaceKHR) X(GetPhysicalDeviceSurfaceSupportKHR)                                \
    X(GetPhysicalDeviceSurfaceCapabilitiesKHR) X(GetPhysicalDeviceSurfaceFormatsKHR) X(GetDeviceProcAddr)

#define VK_DEVICE_FUNCS(X)                                                                                             \
    X(DestroyDevice) X(GetDeviceQueue) X(DeviceWaitIdle) X(CreateSwapchainKHR) X(DestroySwapchainKHR)                  \
    X(GetSwapchainImagesKHR) X(AcquireNextImageKHR) X(QueuePresentKHR) X(CreateImageView) X(DestroyImageView)          \
    X(CreateRenderPass) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateShaderModule)          \
    X(DestroyShaderModule) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout)          \
    X(DestroyPipelineLayout) X(CreateGraphicsPipelines) X(DestroyPipeline) X(CreateDescriptorPool)                     \
    X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(FreeDescriptorSets) X(UpdateDescriptorSets) X(CreateBuffer) X(DestroyBuffer) \
    X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) X(CreateSampler) X(DestroySampler) \
    X(CmdPipelineBarrier) X(CmdClearColorImage)                                                                         \
    X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory)     \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer)     \
    X(ResetCommandBuffer) X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdBindPipeline) X(CmdBindVertexBuffers)         \
    X(CmdBindIndexBuffer) X(CmdBindDescriptorSets) X(CmdDrawIndexed) X(CmdCopyBuffer) X(CmdExecuteCommands)            \
    X(CmdSetViewport) X(CmdSetScissor) X(QueueSubmit) X(QueueWaitIdle) X(CreateSemaphore) X(DestroySemaphore)          \
    X(CreateFence) X(DestroyFence) X(WaitForFences) X(ResetFences)

static HMODULE g_vulkan_lib;
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
static PFN_vkCreateInstance vkCreateInstance;
#define X(n) static PFN_vk##n vk##n;
VK_INSTANCE_FUNCS(X)
VK_DEVICE_FUNCS(X)
#undef X

#define CHECK(call)                                                                                                    \
    do {                                                                                                               \
        VkResult check_result = (call);                                                                                \
        if (check_result != VK_SUCCESS) {                                                                              \
            printf("FAILED line %d: %s = %d\n", __LINE__, #call, static_cast<int>(check_result));                      \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

// ---------------------------------------------------------------------------
// Scene data

static const float kVertices[17][2] = {
    {-0.90f, 0.10f}, {-0.70f, 0.10f}, {-0.80f, -0.10f},                   // mesh0, triangle
    {-0.50f, -0.10f}, {-0.30f, -0.10f}, {-0.30f, 0.10f}, {-0.50f, 0.10f}, // mesh1, quad
    {-0.10f, 0.10f}, {0.10f, 0.10f}, {0.00f, -0.10f},                     // mesh2, triangle
    {0.30f, -0.10f}, {0.50f, -0.10f}, {0.50f, 0.10f}, {0.30f, 0.10f},     // mesh3, quad
    {0.70f, 0.10f}, {0.90f, 0.10f}, {0.80f, -0.10f},                      // mesh4, triangle
};

static const uint16_t kIndicesA[12] = {0, 1, 2, 3, 4, 5, 3, 5, 6, 7, 8, 9};
static const uint16_t kRewriteA[6] = {3, 5, 4, 3, 6, 5}; // replaces indices 3..8 of buffer A
static const uint32_t kPadB = 16;                        // bytes before the first index of buffer B
static const uint32_t kIndicesB[9] = {10, 11, 12, 10, 12, 13, 14, 15, 16};
static const uint32_t kUboSlot = 256;

struct DrawDef {
    const char* label;
    int ib; // 0 = A, 1 = B
    uint32_t first, count, instances;
};

static const DrawDef kDraws[5] = {
    {"mesh0", 0, 0, 3, 1}, {"mesh1", 0, 3, 6, 1}, {"mesh2", 0, 9, 3, 2}, {"mesh3", 1, 0, 6, 1}, {"mesh4", 1, 6, 3, 1},
};
static const int kDrawCount = 5;
static const int kRewriteDraw = 1;

// ---------------------------------------------------------------------------
// CRC-32C, table driven; independent of the implementation in the layer

static uint32_t crc32c_sw(const void* data, size_t size)
{
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (c >> 1) ^ 0x82F63B78u : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    while (size--)
        crc = (crc >> 8) ^ table[(crc ^ *p++) & 0xFF];
    return ~crc;
}

// ---------------------------------------------------------------------------
// State

struct Buffer {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* map = nullptr;
};

struct Worker {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    int first = 0, count = 0;
};

struct App {
    int seconds = 30;
    int threads = 2;
    bool primary_screen = false;
    std::wstring expected_path;
    std::wstring trigger_path;
    std::wstring inject_path;
    bool inject_late = false;
    bool reresolve_exports = false;

    HWND window = nullptr;
    bool quit = false;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_props = {};

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass render_pass = VK_NULL_HANDLE;

    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout material_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    VkDescriptorSet desc_set = VK_NULL_HANDLE;
    VkDescriptorSet material_set = VK_NULL_HANDLE;
    VkImage texture = VK_NULL_HANDLE;
    VkDeviceMemory texture_mem = VK_NULL_HANDLE;
    VkImageView texture_view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    uint32_t shader_hashes[2] = {}, shader_sizes[2] = {};

    Buffer vb, ib_a, ib_b, ubo, storage, staging, staging2;

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer primary = VK_NULL_HANDLE;
    std::vector<Worker> workers;

    VkSemaphore sem_acquire = VK_NULL_HANDLE;
    VkSemaphore sem_render = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    bool rewritten = false;
    uint64_t frame = 0;
};

static App g;

// ---------------------------------------------------------------------------
// Window

struct MonitorPick {
    RECT primary = {};
    RECT secondary = {};
    bool have_secondary = false;
};

static BOOL CALLBACK monitor_cb(HMONITOR mon, HDC, LPRECT, LPARAM param)
{
    MonitorPick* pick = reinterpret_cast<MonitorPick*>(param);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(mon, &info))
        return TRUE;
    if (info.dwFlags & MONITORINFOF_PRIMARY)
        pick->primary = info.rcWork;
    else if (!pick->have_secondary) {
        pick->secondary = info.rcWork;
        pick->have_secondary = true;
    }
    return TRUE;
}

static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        g.quit = true;
        return 0;
    }
    if (msg == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    return DefWindowProcW(wnd, msg, wp, lp);
}

static bool create_window()
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"EfmiVkSandbox";
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    if (!RegisterClassW(&wc))
        return false;

    MonitorPick pick;
    EnumDisplayMonitors(nullptr, nullptr, monitor_cb, reinterpret_cast<LPARAM>(&pick));
    // Test windows go to the secondary screen and never take the focus.
    RECT area = (pick.have_secondary && !g.primary_screen) ? pick.secondary : pick.primary;

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    DWORD ex_style = WS_EX_NOACTIVATE;
    RECT rc = {0, 0, 640, 360};
    AdjustWindowRectEx(&rc, style, FALSE, ex_style);
    g.window = CreateWindowExW(ex_style, wc.lpszClassName, L"EFMI Vulkan sandbox", style, area.left + 40, area.top + 40,
                               rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g.window)
        return false;
    ShowWindow(g.window, SW_SHOWNOACTIVATE);
    return true;
}

static void pump_messages()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT)
            g.quit = true;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// ---------------------------------------------------------------------------
// Vulkan setup

static bool load_injected()
{
    if (!LoadLibraryW(g.inject_path.c_str())) {
        printf("FAILED: cannot load the DLL to inject, error %lu\n", GetLastError());
        return false;
    }
    return true;
}

// Replaces the device function pointers with the addresses vulkan-1.dll exports now.
static void resolve_from_exports()
{
    int replaced = 0;
#define X(n) \
    if (FARPROC f = GetProcAddress(g_vulkan_lib, "vk" #n)) { \
        vk##n = reinterpret_cast<PFN_vk##n>(f); \
        replaced++; \
    }
    VK_DEVICE_FUNCS(X)
#undef X
    printf("%d device functions resolved again through the exports\n", replaced);
}

static bool load_vulkan()
{
    HMODULE lib = LoadLibraryW(L"vulkan-1.dll");
    g_vulkan_lib = lib;
    if (!lib) {
        printf("FAILED: vulkan-1.dll not found\n");
        return false;
    }
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(lib, "vkGetInstanceProcAddr"));
    if (!vkGetInstanceProcAddr)
        return false;
    vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    return vkCreateInstance != nullptr;
}

static bool create_instance()
{
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "efmi_vk_sandbox";
    app.pEngineName = "sandbox";
    app.apiVersion = VK_API_VERSION_1_1;

    const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = exts;
    CHECK(vkCreateInstance(&info, nullptr, &g.instance));

#define X(n)                                                                                                           \
    vk##n = reinterpret_cast<PFN_vk##n>(vkGetInstanceProcAddr(g.instance, "vk" #n));                                   \
    if (!vk##n) {                                                                                                      \
        printf("FAILED: instance function vk" #n " missing\n");                                                        \
        return false;                                                                                                  \
    }
    VK_INSTANCE_FUNCS(X)
#undef X
    return true;
}

static bool create_device()
{
    VkWin32SurfaceCreateInfoKHR sinfo = {};
    sinfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sinfo.hinstance = GetModuleHandleW(nullptr);
    sinfo.hwnd = g.window;
    CHECK(vkCreateWin32SurfaceKHR(g.instance, &sinfo, nullptr, &g.surface));

    uint32_t count = 0;
    CHECK(vkEnumeratePhysicalDevices(g.instance, &count, nullptr));
    std::vector<VkPhysicalDevice> devices(count);
    if (count)
        vkEnumeratePhysicalDevices(g.instance, &count, devices.data());

    int best_score = -1;
    for (VkPhysicalDevice pd : devices) {
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> families(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, families.data());
        for (uint32_t i = 0; i < qcount; i++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, g.surface, &present);
            if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present)
                continue;
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(pd, &props);
            int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (score > best_score) {
                best_score = score;
                g.phys = pd;
                g.queue_family = i;
            }
            break;
        }
    }
    if (!g.phys) {
        printf("FAILED: no device with a graphics queue that can present\n");
        return false;
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(g.phys, &props);
    printf("device: %s\n", props.deviceName);
    vkGetPhysicalDeviceMemoryProperties(g.phys, &g.mem_props);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qinfo = {};
    qinfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qinfo.queueFamilyIndex = g.queue_family;
    qinfo.queueCount = 1;
    qinfo.pQueuePriorities = &priority;

    const char* exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dinfo = {};
    dinfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dinfo.queueCreateInfoCount = 1;
    dinfo.pQueueCreateInfos = &qinfo;
    dinfo.enabledExtensionCount = 1;
    dinfo.ppEnabledExtensionNames = exts;
    CHECK(vkCreateDevice(g.phys, &dinfo, nullptr, &g.device));

#define X(n)                                                                                                           \
    vk##n = reinterpret_cast<PFN_vk##n>(vkGetDeviceProcAddr(g.device, "vk" #n));                                       \
    if (!vk##n) {                                                                                                      \
        printf("FAILED: device function vk" #n " missing\n");                                                          \
        return false;                                                                                                  \
    }
    VK_DEVICE_FUNCS(X)
#undef X
    vkGetDeviceQueue(g.device, g.queue_family, 0, &g.queue);
    return true;
}

static bool create_swapchain()
{
    VkSurfaceCapabilitiesKHR caps;
    CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g.phys, g.surface, &caps));
    uint32_t fcount = 0;
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(g.phys, g.surface, &fcount, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(fcount);
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(g.phys, g.surface, &fcount, formats.data()));
    if (formats.empty())
        return false;
    VkSurfaceFormatKHR chosen = formats[0];
    for (const VkSurfaceFormatKHR& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            chosen = f;
    g.format = chosen.format;

    g.extent = caps.currentExtent;
    if (g.extent.width == 0xFFFFFFFFu) {
        RECT rc;
        GetClientRect(g.window, &rc);
        g.extent.width = static_cast<uint32_t>(rc.right - rc.left);
        g.extent.height = static_cast<uint32_t>(rc.bottom - rc.top);
    }
    if (g.extent.width == 0 || g.extent.height == 0) {
        printf("FAILED: the window has no client area\n");
        return false;
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount && image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;

    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha)) {
        for (uint32_t bit = 1; bit <= 8; bit <<= 1)
            if (caps.supportedCompositeAlpha & bit) {
                alpha = static_cast<VkCompositeAlphaFlagBitsKHR>(bit);
                break;
            }
    }

    VkSwapchainCreateInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = g.surface;
    info.minImageCount = image_count;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = g.extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    CHECK(vkCreateSwapchainKHR(g.device, &info, nullptr, &g.swapchain));

    uint32_t n = 0;
    CHECK(vkGetSwapchainImagesKHR(g.device, g.swapchain, &n, nullptr));
    g.images.resize(n);
    CHECK(vkGetSwapchainImagesKHR(g.device, g.swapchain, &n, g.images.data()));

    VkAttachmentDescription att = {};
    att.format = g.format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency dep = {};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &att;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    CHECK(vkCreateRenderPass(g.device, &rp, nullptr, &g.render_pass));

    g.views.resize(n);
    g.framebuffers.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        VkImageViewCreateInfo v = {};
        v.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        v.image = g.images[i];
        v.viewType = VK_IMAGE_VIEW_TYPE_2D;
        v.format = g.format;
        v.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        v.subresourceRange.levelCount = 1;
        v.subresourceRange.layerCount = 1;
        CHECK(vkCreateImageView(g.device, &v, nullptr, &g.views[i]));

        VkFramebufferCreateInfo fb = {};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = g.render_pass;
        fb.attachmentCount = 1;
        fb.pAttachments = &g.views[i];
        fb.width = g.extent.width;
        fb.height = g.extent.height;
        fb.layers = 1;
        CHECK(vkCreateFramebuffer(g.device, &fb, nullptr, &g.framebuffers[i]));
    }
    return true;
}

static bool create_pipeline()
{
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo sl = {};
    sl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sl.bindingCount = 1;
    sl.pBindings = &binding;
    CHECK(vkCreateDescriptorSetLayout(g.device, &sl, nullptr, &g.set_layout));

    VkSamplerCreateInfo sampler = {};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 0;
    CHECK(vkCreateSampler(g.device, &sampler, nullptr, &g.sampler));

    // Deliberately unsorted bindings: dynamic offsets follow binding numbers, not creation order.
    VkDescriptorSetLayoutBinding material[2] = {};
    material[0] = {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &g.sampler};
    material[1] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 2, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    sl.bindingCount = 2;
    sl.pBindings = material;
    CHECK(vkCreateDescriptorSetLayout(g.device, &sl, nullptr, &g.material_layout));

    VkPipelineLayoutCreateInfo pl = {};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout layouts[] = {g.set_layout, g.material_layout};
    pl.setLayoutCount = 2;
    pl.pSetLayouts = layouts;
    CHECK(vkCreatePipelineLayout(g.device, &pl, nullptr, &g.layout));

    std::vector<uint32_t> vs_code = spv::vertex_shader();
    std::vector<uint32_t> fs_code = spv::fragment_shader();
    g.shader_hashes[0] = crc32c_sw(vs_code.data(), vs_code.size() * 4);
    g.shader_hashes[1] = crc32c_sw(fs_code.data(), fs_code.size() * 4);
    g.shader_sizes[0] = static_cast<uint32_t>(vs_code.size() * 4);
    g.shader_sizes[1] = static_cast<uint32_t>(fs_code.size() * 4);
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo sm = {};
    sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sm.codeSize = vs_code.size() * 4;
    sm.pCode = vs_code.data();
    CHECK(vkCreateShaderModule(g.device, &sm, nullptr, &vs));
    sm.codeSize = fs_code.size() * 4;
    sm.pCode = fs_code.data();
    CHECK(vkCreateShaderModule(g.device, &sm, nullptr, &fs));

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription vbind = {0, 8, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription vattr = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vbind;
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions = &vattr;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba = {};
    cba.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn = {};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dyn;
    info.layout = g.layout;
    info.renderPass = g.render_pass;
    info.subpass = 0;
    CHECK(vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &info, nullptr, &g.pipeline));

    vkDestroyShaderModule(g.device, vs, nullptr);
    vkDestroyShaderModule(g.device, fs, nullptr);
    return true;
}

static bool find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid, uint32_t* out)
{
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t i = 0; i < g.mem_props.memoryTypeCount; i++) {
            VkMemoryPropertyFlags flags = g.mem_props.memoryTypes[i].propertyFlags;
            if (!(type_bits & (1u << i)) || (flags & want) != want)
                continue;
            if (pass == 0 && (flags & avoid))
                continue;
            *out = i;
            return true;
        }
    }
    return false;
}

static bool make_buffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage, bool host, bool keep_mapped)
{
    VkBufferCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CHECK(vkCreateBuffer(g.device, &info, nullptr, &b.buf));
    b.size = size;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g.device, b.buf, &req);
    uint32_t type = 0;
    bool found = host ? find_memory_type(req.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, &type)
                      : find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &type);
    if (!found) {
        printf("FAILED: no suitable memory type\n");
        return false;
    }
    VkMemoryAllocateInfo alloc = {};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type;
    CHECK(vkAllocateMemory(g.device, &alloc, nullptr, &b.mem));
    CHECK(vkBindBufferMemory(g.device, b.buf, b.mem, 0));
    if (host && keep_mapped)
        CHECK(vkMapMemory(g.device, b.mem, 0, VK_WHOLE_SIZE, 0, &b.map));
    return true;
}

static void destroy_buffer(Buffer& b)
{
    if (b.buf)
        vkDestroyBuffer(g.device, b.buf, nullptr);
    if (b.mem)
        vkFreeMemory(g.device, b.mem, nullptr);
    b = Buffer();
}

static bool create_resources()
{
    if (!make_buffer(g.vb, sizeof(kVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true, true))
        return false;
    memcpy(g.vb.map, kVertices, sizeof(kVertices));

    if (!make_buffer(g.ib_a, sizeof(kIndicesA), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false,
                     false))
        return false;
    if (!make_buffer(g.staging, sizeof(kIndicesA), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, true))
        return false;
    memcpy(g.staging.map, kIndicesA, sizeof(kIndicesA));
    if (!make_buffer(g.staging2, sizeof(kRewriteA), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, false))
        return false;

    if (!make_buffer(g.ib_b, kPadB + sizeof(kIndicesB), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, true, true))
        return false;
    memset(g.ib_b.map, 0xFF, kPadB);
    memcpy(static_cast<uint8_t*>(g.ib_b.map) + kPadB, kIndicesB, sizeof(kIndicesB));

    if (!make_buffer(g.ubo, kUboSlot * kDrawCount, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, true))
        return false;
    memset(g.ubo.map, 0, static_cast<size_t>(g.ubo.size));
    for (int i = 0; i < kDrawCount; i++) {
        float block[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.2f + 0.2f * i, 1.0f - 0.2f * i, 0.5f, 1.0f};
        memcpy(static_cast<uint8_t*>(g.ubo.map) + kUboSlot * i, block, sizeof(block));
    }

    if (!make_buffer(g.storage, kUboSlot * (kDrawCount + 1), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, true))
        return false;
    memset(g.storage.map, 0, static_cast<size_t>(g.storage.size));
    for (int i = 0; i <= kDrawCount; i++) {
        float tint[] = {0.5f + 0.05f * i, 0.75f, 1.0f, 1.0f};
        memcpy(static_cast<uint8_t*>(g.storage.map) + kUboSlot * i, tint, sizeof(tint));
    }

    VkImageCreateInfo image = {};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_R8G8B8A8_UNORM;
    image.extent = {1, 1, 1};
    image.mipLevels = image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    CHECK(vkCreateImage(g.device, &image, nullptr, &g.texture));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g.device, g.texture, &req);
    uint32_t type = 0;
    if (!find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &type)) return false;
    VkMemoryAllocateInfo mem = {};
    mem.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mem.allocationSize = req.size;
    mem.memoryTypeIndex = type;
    CHECK(vkAllocateMemory(g.device, &mem, nullptr, &g.texture_mem));
    CHECK(vkBindImageMemory(g.device, g.texture, g.texture_mem, 0));
    VkImageViewCreateInfo view = {};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = g.texture;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = image.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    CHECK(vkCreateImageView(g.device, &view, nullptr, &g.texture_view));

    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1},
                                  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 4},
                                  {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}};
    VkDescriptorPoolCreateInfo dp = {};
    dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dp.maxSets = 3;
    dp.poolSizeCount = 3;
    dp.pPoolSizes = sizes;
    CHECK(vkCreateDescriptorPool(g.device, &dp, nullptr, &g.desc_pool));
    VkDescriptorSetAllocateInfo da = {};
    da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    da.descriptorPool = g.desc_pool;
    VkDescriptorSetLayout layouts[] = {g.set_layout, g.material_layout, g.material_layout};
    VkDescriptorSet sets[3];
    da.descriptorSetCount = 3;
    da.pSetLayouts = layouts;
    CHECK(vkAllocateDescriptorSets(g.device, &da, sets));
    g.desc_set = sets[0];
    g.material_set = sets[1];
    VkDescriptorBufferInfo bi = {g.ubo.buf, 0, 32};
    VkDescriptorBufferInfo storage[] = {{g.storage.buf, kUboSlot, 16}, {g.storage.buf, 0, 16}};
    VkDescriptorImageInfo texture = {VK_NULL_HANDLE, g.texture_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet writes[3] = {};
    for (auto& w : writes) w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = g.desc_set;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[0].pBufferInfo = &bi;
    writes[1].dstSet = sets[2];
    writes[1].dstBinding = 2;
    writes[1].descriptorCount = 2;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    writes[1].pBufferInfo = storage;
    writes[2].dstSet = sets[2];
    writes[2].dstBinding = 5;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &texture;
    VkCopyDescriptorSet copies[2] = {};
    for (auto& c : copies) {
        c.sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
        c.srcSet = sets[2]; c.dstSet = g.material_set;
    }
    copies[0].srcBinding = copies[0].dstBinding = 2;
    copies[0].descriptorCount = 2;
    copies[1].srcBinding = copies[1].dstBinding = 5;
    copies[1].descriptorCount = 1;
    vkUpdateDescriptorSets(g.device, 3, writes, 2, copies);
    CHECK(vkFreeDescriptorSets(g.device, g.desc_pool, 1, &sets[2]));

    VkCommandPoolCreateInfo cp = {};
    cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cp.queueFamilyIndex = g.queue_family;
    CHECK(vkCreateCommandPool(g.device, &cp, nullptr, &g.pool));
    VkCommandBufferAllocateInfo ca = {};
    ca.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ca.commandPool = g.pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    CHECK(vkAllocateCommandBuffers(g.device, &ca, &g.primary));

    g.workers.resize(static_cast<size_t>(g.threads));
    int next = 0;
    for (int t = 0; t < g.threads; t++) {
        Worker& w = g.workers[static_cast<size_t>(t)];
        CHECK(vkCreateCommandPool(g.device, &cp, nullptr, &w.pool));
        ca.commandPool = w.pool;
        ca.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        CHECK(vkAllocateCommandBuffers(g.device, &ca, &w.cb));
        // Contiguous shares keep the order of the draws after vkCmdExecuteCommands.
        int share = (kDrawCount - next + (g.threads - t) - 1) / (g.threads - t);
        w.first = next;
        w.count = share;
        next += share;
    }

    VkSemaphoreCreateInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    CHECK(vkCreateSemaphore(g.device, &si, nullptr, &g.sem_acquire));
    CHECK(vkCreateSemaphore(g.device, &si, nullptr, &g.sem_render));
    VkFenceCreateInfo fi = {};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    CHECK(vkCreateFence(g.device, &fi, nullptr, &g.fence));

    // Initial upload of index buffer A. The staging memory stays mapped.
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CHECK(vkBeginCommandBuffer(g.primary, &begin));
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = g.texture;
    barrier.subresourceRange = view.subresourceRange;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(g.primary, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkClearColorValue white = {{1.0f, 1.0f, 1.0f, 1.0f}};
    vkCmdClearColorImage(g.primary, g.texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &white, 1, &barrier.subresourceRange);
    barrier.oldLayout = barrier.newLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(g.primary, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferCopy region = {0, 0, sizeof(kIndicesA)};
    vkCmdCopyBuffer(g.primary, g.staging.buf, g.ib_a.buf, 1, &region);
    CHECK(vkEndCommandBuffer(g.primary));
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &g.primary;
    CHECK(vkQueueSubmit(g.queue, 1, &submit, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(g.queue));
    return true;
}

// ---------------------------------------------------------------------------
// Expected values for the tests

static void region_of(const DrawDef& d, bool rewritten, const uint8_t** data, size_t* size)
{
    static uint16_t a_now[12];
    if (d.ib == 0) {
        memcpy(a_now, kIndicesA, sizeof(kIndicesA));
        if (rewritten)
            memcpy(a_now + 3, kRewriteA, sizeof(kRewriteA));
        *data = reinterpret_cast<const uint8_t*>(a_now + d.first);
        *size = d.count * sizeof(uint16_t);
    } else {
        *data = reinterpret_cast<const uint8_t*>(kIndicesB + d.first);
        *size = d.count * sizeof(uint32_t);
    }
}

static bool write_expected()
{
    if (g.expected_path.empty())
        return true;
    std::string out = "{\n";
    char line[512];
    snprintf(line, sizeof(line), "  \"pid\": %lu,\n  \"threads\": %d,\n  \"rewrite_draw\": %d,\n  \"draws\": [\n",
             GetCurrentProcessId(), g.threads, kRewriteDraw);
    out += line;
    for (int i = 0; i < kDrawCount; i++) {
        const DrawDef& d = kDraws[i];
        const uint8_t* data;
        size_t size;
        region_of(d, false, &data, &size);
        uint32_t before = crc32c_sw(data, size);
        region_of(d, true, &data, &size);
        uint32_t after = crc32c_sw(data, size);
        snprintf(line, sizeof(line),
                 "    {\"label\": \"%s\", \"ib\": \"%s\", \"index_size\": %d, \"bind_offset\": %u, \"first\": %u, "
                 "\"count\": %u, \"instances\": %u, \"dyn\": %u, \"hash\": \"%08x\", \"hash_after_rewrite\": \"%08x\", "
                 "\"src\": \"%s\"}%s\n",
                 d.label, d.ib == 0 ? "A" : "B", d.ib == 0 ? 2 : 4, d.ib == 0 ? 0u : kPadB, d.first, d.count, d.instances,
                 kUboSlot * static_cast<uint32_t>(i), before, after, d.ib == 0 ? "shadow" : "mapped",
                 i + 1 < kDrawCount ? "," : "");
        out += line;
    }
    snprintf(line, sizeof(line),
             "  ],\n  \"shaders\": [{\"stage\": 1, \"hash\": \"%08x\", \"size\": %u}, "
             "{\"stage\": 16, \"hash\": \"%08x\", \"size\": %u}]\n}\n",
             g.shader_hashes[0], g.shader_sizes[0], g.shader_hashes[1], g.shader_sizes[1]);
    out += line;

    std::wstring tmp = g.expected_path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    WriteFile(f, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
    CloseHandle(f);
    return MoveFileExW(tmp.c_str(), g.expected_path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

// ---------------------------------------------------------------------------
// Frame

static void record_setup(VkCommandBuffer cb)
{
    VkViewport viewport = {0.0f, 0.0f, static_cast<float>(g.extent.width), static_cast<float>(g.extent.height), 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, g.extent};
    vkCmdSetViewport(cb, 0, 1, &viewport);
    vkCmdSetScissor(cb, 0, 1, &scissor);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeline);
    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &g.vb.buf, &zero);
}

static void record_draw(VkCommandBuffer cb, int i)
{
    const DrawDef& d = kDraws[i];
    if (d.ib == 0)
        vkCmdBindIndexBuffer(cb, g.ib_a.buf, 0, VK_INDEX_TYPE_UINT16);
    else
        vkCmdBindIndexBuffer(cb, g.ib_b.buf, kPadB, VK_INDEX_TYPE_UINT32);
    uint32_t dyn[] = {kUboSlot * static_cast<uint32_t>(i), kUboSlot * static_cast<uint32_t>((i + 1) % kDrawCount),
                      kUboSlot * static_cast<uint32_t>(kDrawCount - i - 1)};
    VkDescriptorSet sets[] = {g.desc_set, g.material_set};
    // Exercise both a multi-set bind and a nonzero firstSet.
    if (i % 2 == 0)
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, 0, 2, sets, 3, dyn);
    else {
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, 0, 1, sets, 1, dyn);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, 1, 1, sets + 1, 2, dyn + 1);
    }
    vkCmdDrawIndexed(cb, d.count, d.instances, d.first, 0, 0);
}

static void record_worker(Worker* w, VkFramebuffer fb)
{
    VkCommandBufferInheritanceInfo inherit = {};
    inherit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inherit.renderPass = g.render_pass;
    inherit.subpass = 0;
    inherit.framebuffer = fb;
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin.pInheritanceInfo = &inherit;
    vkBeginCommandBuffer(w->cb, &begin);
    record_setup(w->cb);
    for (int i = 0; i < w->count; i++)
        record_draw(w->cb, w->first + i);
    vkEndCommandBuffer(w->cb);
}

static bool trigger_present()
{
    return !g.trigger_path.empty() && GetFileAttributesW(g.trigger_path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool render_frame()
{
    CHECK(vkWaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX));
    uint32_t image = 0;
    VkResult acquired = vkAcquireNextImageKHR(g.device, g.swapchain, UINT64_MAX, g.sem_acquire, VK_NULL_HANDLE, &image);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        g.quit = true;
        return true;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        printf("FAILED: vkAcquireNextImageKHR = %d\n", static_cast<int>(acquired));
        return false;
    }
    CHECK(vkResetFences(g.device, 1, &g.fence));

    bool rewrite_now = !g.rewritten && trigger_present();
    if (rewrite_now) {
        // Map, write and unmap before the copy is recorded: the layer cannot read the
        // staging memory any more when it sees the copy.
        void* p = nullptr;
        CHECK(vkMapMemory(g.device, g.staging2.mem, 0, VK_WHOLE_SIZE, 0, &p));
        memcpy(p, kRewriteA, sizeof(kRewriteA));
        vkUnmapMemory(g.device, g.staging2.mem);
        VkDescriptorBufferInfo bi = {g.storage.buf, 0, 16};
        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = g.material_set;
        write.dstBinding = 2;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        write.pBufferInfo = &bi;
        vkUpdateDescriptorSets(g.device, 1, &write, 0, nullptr);
    }

    CHECK(vkResetCommandBuffer(g.primary, 0));
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CHECK(vkBeginCommandBuffer(g.primary, &begin));

    if (rewrite_now) {
        VkBufferCopy region = {0, 3 * sizeof(uint16_t), sizeof(kRewriteA)};
        vkCmdCopyBuffer(g.primary, g.staging2.buf, g.ib_a.buf, 1, &region);
    }

    VkClearValue clear = {};
    clear.color.float32[0] = 0.05f;
    clear.color.float32[1] = 0.05f;
    clear.color.float32[2] = 0.08f;
    clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = g.render_pass;
    rp.framebuffer = g.framebuffers[image];
    rp.renderArea.extent = g.extent;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;

    if (g.threads == 0) {
        vkCmdBeginRenderPass(g.primary, &rp, VK_SUBPASS_CONTENTS_INLINE);
        record_setup(g.primary);
        for (int i = 0; i < kDrawCount; i++)
            record_draw(g.primary, i);
    } else {
        vkCmdBeginRenderPass(g.primary, &rp, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
        std::vector<std::thread> threads;
        for (Worker& w : g.workers)
            threads.emplace_back(record_worker, &w, g.framebuffers[image]);
        for (std::thread& t : threads)
            t.join();
        std::vector<VkCommandBuffer> list;
        for (Worker& w : g.workers)
            list.push_back(w.cb);
        vkCmdExecuteCommands(g.primary, static_cast<uint32_t>(list.size()), list.data());
    }
    vkCmdEndRenderPass(g.primary);
    CHECK(vkEndCommandBuffer(g.primary));

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &g.sem_acquire;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &g.primary;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &g.sem_render;
    CHECK(vkQueueSubmit(g.queue, 1, &submit, g.fence));

    VkPresentInfoKHR present = {};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &g.sem_render;
    present.swapchainCount = 1;
    present.pSwapchains = &g.swapchain;
    present.pImageIndices = &image;
    VkResult presented = vkQueuePresentKHR(g.queue, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR)
        g.quit = true;
    else if (presented != VK_SUCCESS && presented != VK_SUBOPTIMAL_KHR) {
        printf("FAILED: vkQueuePresentKHR = %d\n", static_cast<int>(presented));
        return false;
    }

    if (rewrite_now) {
        g.rewritten = true;
        std::wstring done = g.trigger_path + L".done";
        HANDLE f = CreateFileW(done.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE)
            CloseHandle(f);
        printf("index buffer A rewritten in frame %llu\n", static_cast<unsigned long long>(g.frame));
    }
    g.frame++;
    return true;
}

static void cleanup()
{
    if (!g.device)
        return;
    vkDeviceWaitIdle(g.device);
    vkDestroyFence(g.device, g.fence, nullptr);
    vkDestroySemaphore(g.device, g.sem_render, nullptr);
    vkDestroySemaphore(g.device, g.sem_acquire, nullptr);
    for (Worker& w : g.workers)
        if (w.pool)
            vkDestroyCommandPool(g.device, w.pool, nullptr);
    if (g.pool)
        vkDestroyCommandPool(g.device, g.pool, nullptr);
    if (g.desc_pool)
        vkDestroyDescriptorPool(g.device, g.desc_pool, nullptr);
    destroy_buffer(g.vb);
    destroy_buffer(g.ib_a);
    destroy_buffer(g.ib_b);
    destroy_buffer(g.ubo);
    destroy_buffer(g.storage);
    destroy_buffer(g.staging);
    destroy_buffer(g.staging2);
    if (g.pipeline)
        vkDestroyPipeline(g.device, g.pipeline, nullptr);
    if (g.layout)
        vkDestroyPipelineLayout(g.device, g.layout, nullptr);
    if (g.set_layout)
        vkDestroyDescriptorSetLayout(g.device, g.set_layout, nullptr);
    if (g.material_layout) vkDestroyDescriptorSetLayout(g.device, g.material_layout, nullptr);
    if (g.texture_view) vkDestroyImageView(g.device, g.texture_view, nullptr);
    if (g.texture) vkDestroyImage(g.device, g.texture, nullptr);
    if (g.texture_mem) vkFreeMemory(g.device, g.texture_mem, nullptr);
    if (g.sampler) vkDestroySampler(g.device, g.sampler, nullptr);
    for (VkFramebuffer fb : g.framebuffers)
        vkDestroyFramebuffer(g.device, fb, nullptr);
    for (VkImageView v : g.views)
        vkDestroyImageView(g.device, v, nullptr);
    if (g.render_pass)
        vkDestroyRenderPass(g.device, g.render_pass, nullptr);
    if (g.swapchain)
        vkDestroySwapchainKHR(g.device, g.swapchain, nullptr);
    vkDestroyDevice(g.device, nullptr);
    if (g.surface)
        vkDestroySurfaceKHR(g.instance, g.surface, nullptr);
    vkDestroyInstance(g.instance, nullptr);
}

int wmain(int argc, wchar_t** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"--seconds" && i + 1 < argc)
            g.seconds = _wtoi(argv[++i]);
        else if (a == L"--threads" && i + 1 < argc)
            g.threads = _wtoi(argv[++i]);
        else if (a == L"--expected" && i + 1 < argc)
            g.expected_path = argv[++i];
        else if (a == L"--trigger" && i + 1 < argc)
            g.trigger_path = argv[++i];
        else if (a == L"--primary-screen")
            g.primary_screen = true;
        else if (a == L"--inject" && i + 1 < argc)
            g.inject_path = argv[++i];
        else if (a == L"--inject-late" && i + 1 < argc) {
            g.inject_path = argv[++i];
            g.inject_late = true;
        } else if (a == L"--inject-late-exports" && i + 1 < argc) {
            g.inject_path = argv[++i];
            g.inject_late = true;
            g.reresolve_exports = true;
        } else {
            printf("unknown argument\n");
            return 2;
        }
    }
    if (g.threads < 0)
        g.threads = 0;
    if (g.threads > kDrawCount)
        g.threads = kDrawCount;

    bool ok = true;
    if (!g.inject_path.empty() && !g.inject_late)
        ok = load_injected();
    ok = ok && load_vulkan() && create_instance() && create_window() && create_device() && create_swapchain() &&
         create_pipeline() && create_resources();
    if (ok && !g.inject_path.empty() && g.inject_late)
        ok = load_injected();
    if (ok && g.reresolve_exports)
        resolve_from_exports();
    if (ok)
        ok = render_frame() && write_expected();
    if (ok)
        printf("ready pid=%lu threads=%d\n", GetCurrentProcessId(), g.threads);

    ULONGLONG start = GetTickCount64();
    while (ok && !g.quit) {
        pump_messages();
        if (g.seconds > 0 && GetTickCount64() - start > static_cast<ULONGLONG>(g.seconds) * 1000)
            break;
        ok = render_frame();
    }
    printf("%s after %llu frames\n", ok ? "done" : "FAILED", static_cast<unsigned long long>(g.frame));
    cleanup();
    return ok ? 0 : 1;
}
