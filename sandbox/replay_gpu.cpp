// Replays the character draws of a captured game frame through the loaded layer
// (scripts/replay_capture.py generates replay_case.inc and replay.bin). Headless: frames end with the
// probe's "advance" command, as in the character scene. Every colour and depth target of every
// render pass instance is read back after the last frame. No window, game or settings write.
#define wmain unused_asset_main
#include "asset_gpu.cpp"
#undef wmain
#include "../layer/src/json.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <map>
#include <dbghelp.h>

struct HistoricalCase {
    efvk::GraphicsRecipe recipe;
    efvk::PipelineLayoutInfo layout;
    efvk::VertexInputRecipe vertex;
    efvk::FragmentPatch fragment;
    std::string expected_recipe,expected_layout,expected_vertex,expected_patch,expected_source_vertex,expected_dynamic;
};
template<class T>static void field(T& target,int64_t value){target=static_cast<T>(value);}
static float float_bits(uint32_t value){float f;memcpy(&f,&value,4);return f;}
static std::shared_ptr<efvk::ShaderInfo> shader_file(const char* path,uint32_t id){
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);const auto size=file.tellg();
    if(!file || size<20 || size>(16<<20) || size%4)throw std::runtime_error("Invalid archived shader");
    auto shader=std::make_shared<efvk::ShaderInfo>();shader->id=id;shader->size=static_cast<uint32_t>(size);shader->code.resize(size_t(size)/4);
    file.seekg(0);file.read(reinterpret_cast<char*>(shader->code.data()),size);
    if(!file)throw std::runtime_error("Archived shader read failed");return shader;
}
struct ReplayFill{uint64_t dst,src,size;};
struct ReplayBones{uint64_t buffer,offset,size;}; // a stand-in bone region (animate-bones)
struct ReplayBuffer{uint64_t size;std::vector<ReplayFill> fills;};
struct ReplayImage{uint32_t format,view_format,type,view_type,layers,width,height,mips;};
struct ReplayDescriptor{uint32_t set,binding,type,buffer,offset,range,image;};
struct ReplayVertex{uint32_t binding,buffer;};
struct ReplayDraw{uint32_t pipeline,group,count,instances,first,first_instance;int32_t vertex_offset;uint32_t index_buffer,index_type;
    std::vector<ReplayDescriptor> descriptors;std::vector<ReplayVertex> vertex;uint32_t n;};
#include "replay_case.inc"

static std::string self_probe(const char* request,const char* output)
{
    wchar_t name[80];swprintf(name,80,L"\\\\.\\pipe\\efmi-vk-%lu",GetCurrentProcessId());
    HANDLE pipe=INVALID_HANDLE_VALUE;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(pipe==INVALID_HANDLE_VALUE && std::chrono::steady_clock::now()<deadline){
        pipe=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        if(pipe==INVALID_HANDLE_VALUE)Sleep(10);
    }
    if(pipe==INVALID_HANDLE_VALUE)throw std::runtime_error("self probe connect");
    auto transfer=[&](void* bytes,uint32_t size,bool write){
        auto* p=static_cast<uint8_t*>(bytes);
        while(size){DWORD n=0;BOOL ok=write?WriteFile(pipe,p,size,&n,nullptr):ReadFile(pipe,p,size,&n,nullptr);
            if(!ok || !n){CloseHandle(pipe);throw std::runtime_error("self probe transfer");}p+=n;size-=n;}
    };
    uint32_t header[]={static_cast<uint32_t>(strlen(request)),0};transfer(header,8,true);
    transfer(const_cast<char*>(request),header[0],true);transfer(header,8,false);
    if(header[0]>(8u<<20) || header[1]){CloseHandle(pipe);throw std::runtime_error("self probe lengths");}
    std::string result(header[0],'\0');transfer(&result[0],header[0],false);CloseHandle(pipe);
    if(output){FILE* file=nullptr;fopen_s(&file,output,"wb");if(!file)throw std::runtime_error("report file");
        fwrite(result.data(),1,result.size(),file);fclose(file);}
    return result;
}

static bool depth_format(VkFormat f){return f==VK_FORMAT_D16_UNORM || f==VK_FORMAT_X8_D24_UNORM_PACK32 || f==VK_FORMAT_D32_SFLOAT ||
    f==VK_FORMAT_S8_UINT || f==VK_FORMAT_D16_UNORM_S8_UINT || f==VK_FORMAT_D24_UNORM_S8_UINT || f==VK_FORMAT_D32_SFLOAT_S8_UINT;}
static bool stencil_format(VkFormat f){return f==VK_FORMAT_S8_UINT || f==VK_FORMAT_D16_UNORM_S8_UINT || f==VK_FORMAT_D24_UNORM_S8_UINT ||
    f==VK_FORMAT_D32_SFLOAT_S8_UINT;}
static bool block_format(VkFormat f){return f>=VK_FORMAT_BC1_RGB_UNORM_BLOCK && f<=VK_FORMAT_BC7_SRGB_BLOCK;}
static VkImageAspectFlags aspect_of(VkFormat f){
    if(!depth_format(f))return VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageAspectFlags a=0;if(f!=VK_FORMAT_S8_UINT)a|=VK_IMAGE_ASPECT_DEPTH_BIT;if(stencil_format(f))a|=VK_IMAGE_ASPECT_STENCIL_BIT;return a;
}
static uint32_t texel_bytes(VkFormat f){
    switch(f){
    case VK_FORMAT_R8G8B8A8_UNORM:case VK_FORMAT_R8G8B8A8_SRGB:case VK_FORMAT_B8G8R8A8_UNORM:case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:case VK_FORMAT_B10G11R11_UFLOAT_PACK32:case VK_FORMAT_R32_SFLOAT:case VK_FORMAT_R32_UINT:
    case VK_FORMAT_D32_SFLOAT:case VK_FORMAT_X8_D24_UNORM_PACK32:case VK_FORMAT_D24_UNORM_S8_UINT:case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R16G16_UNORM:case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT:case VK_FORMAT_R16G16B16A16_UNORM:case VK_FORMAT_R32G32_SFLOAT:case VK_FORMAT_D32_SFLOAT_S8_UINT:return 8;
    case VK_FORMAT_R32G32B32A32_SFLOAT:return 16;
    case VK_FORMAT_R8_UNORM:case VK_FORMAT_R8_UINT:case VK_FORMAT_S8_UINT:return 1;
    case VK_FORMAT_R8G8_UNORM:case VK_FORMAT_R16_SFLOAT:case VK_FORMAT_R16_UNORM:case VK_FORMAT_D16_UNORM:return 2;
    default:return 0;
    }
}

// A window and its swap chain: frames end with a presentation, as in the game, so that the layer's
// Present path (the Mods' [Present] lists and their owned GPU work) runs as it does there.
// Test windows go to the secondary screen and never take the focus.
static LRESULT CALLBACK replay_window_proc(HWND wnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    return DefWindowProcW(wnd,msg,wp,lp);
}
static BOOL CALLBACK replay_monitor(HMONITOR mon,HDC,LPRECT,LPARAM param){
    auto* pick=reinterpret_cast<std::pair<RECT,bool>*>(param);MONITORINFO info={sizeof(info)};
    if(GetMonitorInfoW(mon,&info) && !(info.dwFlags&MONITORINFOF_PRIMARY) && !pick->second){pick->first=info.rcWork;pick->second=true;}
    return TRUE;
}
static HWND replay_window(uint32_t width,uint32_t height){
    WNDCLASSW wc={};wc.lpfnWndProc=replay_window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"EfmiVkReplay";
    RegisterClassW(&wc);
    std::pair<RECT,bool> pick{{0,0,0,0},false};EnumDisplayMonitors(nullptr,nullptr,replay_monitor,reinterpret_cast<LPARAM>(&pick));
    const DWORD style=WS_OVERLAPPED|WS_CAPTION,ex=WS_EX_NOACTIVATE;RECT rc={0,0,LONG(width),LONG(height)};AdjustWindowRectEx(&rc,style,FALSE,ex);
    const int x=pick.second?pick.first.left+40:40,y=pick.second?pick.first.top+40:40;
    HWND w=CreateWindowExW(ex,wc.lpszClassName,L"EFMI Vulkan replay",style,x,y,rc.right-rc.left,rc.bottom-rc.top,nullptr,nullptr,wc.hInstance,nullptr);
    if(!w)throw std::runtime_error("replay window");
    ShowWindow(w,SW_SHOWNOACTIVATE);return w;
}
static void pump(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
struct Swap {
    VkSurfaceKHR surface{};VkSwapchainKHR swapchain{};std::vector<VkImage> images;VkSemaphore acquired{},rendered{};
    PFN_vkDestroySurfaceKHR DestroySurface=nullptr;PFN_vkDestroySwapchainKHR DestroySwapchain=nullptr;
    PFN_vkAcquireNextImageKHR Acquire=nullptr;PFN_vkQueuePresentKHR Present=nullptr;PFN_vkDestroySemaphore DestroySemaphore=nullptr;
};
// Gpu::init of the asset fixture, with the surface and swap chain extensions and a queue that presents.
// two_queues: two queues of the graphics family (D-119); queues[1] stays null otherwise.
static void init_with_window(Gpu& g,HWND window,Swap& swap,bool two_queues,VkQueue queues[2]){
    auto loader=LoadLibraryW(L"vulkan-1.dll");if(!loader)throw std::runtime_error("Vulkan loader missing");
    auto gi=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
    auto create=reinterpret_cast<PFN_vkCreateInstance>(gi(nullptr,"vkCreateInstance"));
    VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="efmi_vk_replay";app.apiVersion=VK_API_VERSION_1_2;
    const char* layer="VK_LAYER_KHRONOS_validation";
    const char* exts[]={VK_EXT_DEBUG_UTILS_EXTENSION_NAME,VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkDebugUtilsMessengerCreateInfoEXT debug={VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback=diagnostic;
    // EFMI_VK_REPLAY_NO_VALIDATION=1: no validation layer, for profiling the layer's own CPU work (the
    // validation layer's calls dominated a profile otherwise). The diagnostics then count nothing.
    char no_validation[4]={};
    const bool validated=!(GetEnvironmentVariableA("EFMI_VK_REPLAY_NO_VALIDATION",no_validation,sizeof no_validation) && no_validation[0]=='1');
    VkInstanceCreateInfo info={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;info.pNext=validated?&debug:nullptr;
    info.enabledLayerCount=validated?1:0;info.ppEnabledLayerNames=&layer;
    info.enabledExtensionCount=validated?3:2;info.ppEnabledExtensionNames=validated?exts:exts+1;
    CHECK_VK(create(&info,nullptr,&g.instance));
#define X(n) p##n=reinterpret_cast<PFN_vk##n>(gi(g.instance,"vk" #n));if(!p##n)throw std::runtime_error("vk" #n);
    INSTANCE_CALLS(X)
#undef X
    auto create_debug=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(gi(g.instance,"vkCreateDebugUtilsMessengerEXT"));
    g.destroy_debug=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(gi(g.instance,"vkDestroyDebugUtilsMessengerEXT"));
    if(validated)CHECK_VK(create_debug(g.instance,&debug,nullptr,&g.messenger));
    auto surface_create=reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(gi(g.instance,"vkCreateWin32SurfaceKHR"));
    auto supports=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(gi(g.instance,"vkGetPhysicalDeviceSurfaceSupportKHR"));
    auto caps_of=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(gi(g.instance,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    auto formats_of=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(gi(g.instance,"vkGetPhysicalDeviceSurfaceFormatsKHR"));
    swap.DestroySurface=reinterpret_cast<PFN_vkDestroySurfaceKHR>(gi(g.instance,"vkDestroySurfaceKHR"));
    VkWin32SurfaceCreateInfoKHR si={VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};si.hinstance=GetModuleHandleW(nullptr);si.hwnd=window;
    CHECK_VK(surface_create(g.instance,&si,nullptr,&swap.surface));
    uint32_t count=0;CHECK_VK(pEnumeratePhysicalDevices(g.instance,&count,nullptr));std::vector<VkPhysicalDevice> devices(count);
    CHECK_VK(pEnumeratePhysicalDevices(g.instance,&count,devices.data()));int score=-1;uint32_t family_queues=1;
    for(auto pd:devices){
        VkPhysicalDeviceProperties props{};pGetPhysicalDeviceProperties(pd,&props);
        uint32_t n=0;pGetPhysicalDeviceQueueFamilyProperties(pd,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);
        pGetPhysicalDeviceQueueFamilyProperties(pd,&n,families.data());
        for(uint32_t i=0;i<n;++i){
            VkBool32 present=VK_FALSE;supports(pd,i,swap.surface,&present);
            if(!(families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT) || !present)continue;
            int rank=props.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?2:1;
            if(rank>score){score=rank;g.physical=pd;g.family=i;family_queues=families[i].queueCount;}break;
        }
    }
    if(!g.physical)throw std::runtime_error("No graphics device that presents");
    VkPhysicalDeviceFeatures available{},enabled{};pGetPhysicalDeviceFeatures(g.physical,&available);
    enabled.textureCompressionBC=available.textureCompressionBC;g.bc=enabled.textureCompressionBC!=0;
    enabled.independentBlend=available.independentBlend;
    // Features the game's shaders rely on (fragment stores in its G-buffer variants and the like).
    enabled.fragmentStoresAndAtomics=available.fragmentStoresAndAtomics;
    enabled.vertexPipelineStoresAndAtomics=available.vertexPipelineStoresAndAtomics;
    enabled.shaderImageGatherExtended=available.shaderImageGatherExtended;
    enabled.shaderStorageImageWriteWithoutFormat=available.shaderStorageImageWriteWithoutFormat;
    enabled.depthClamp=available.depthClamp;enabled.depthBiasClamp=available.depthBiasClamp;
    enabled.samplerAnisotropy=available.samplerAnisotropy;
    if(two_queues && family_queues<2)throw std::runtime_error("two-queues needs a graphics family with two queues");
    const float priorities[2]={1,1};VkDeviceQueueCreateInfo q={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueFamilyIndex=g.family;q.queueCount=two_queues?2:1;q.pQueuePriorities=priorities;
    const char* device_ext=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo dc={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&q;dc.pEnabledFeatures=&enabled;
    dc.enabledExtensionCount=1;dc.ppEnabledExtensionNames=&device_ext;
    CHECK_VK(pCreateDevice(g.physical,&dc,nullptr,&g.device));
#define X(n) p##n=reinterpret_cast<PFN_vk##n>(pGetDeviceProcAddr(g.device,"vk" #n));if(!p##n)throw std::runtime_error("vk" #n);
    DEVICE_CALLS(X)
#undef X
#define X(n) g.dispatch.n=p##n;
    EFMI_IMAGE_UPLOAD_FUNCS(X)
#undef X
    queues[0]=queues[1]=VK_NULL_HANDLE;pGetDeviceQueue(g.device,g.family,0,&queues[0]);if(two_queues)pGetDeviceQueue(g.device,g.family,1,&queues[1]);
    g.queue=queues[0];pGetPhysicalDeviceMemoryProperties(g.physical,&g.memory);
    VkSurfaceCapabilitiesKHR caps{};CHECK_VK(caps_of(g.physical,swap.surface,&caps));
    uint32_t nf=0;formats_of(g.physical,swap.surface,&nf,nullptr);std::vector<VkSurfaceFormatKHR> formats(nf);formats_of(g.physical,swap.surface,&nf,formats.data());
    if(formats.empty())throw std::runtime_error("surface has no format");
    VkSurfaceFormatKHR chosen=formats[0];for(const auto& f:formats)if(f.format==VK_FORMAT_B8G8R8A8_UNORM)chosen=f;
    auto swap_create=reinterpret_cast<PFN_vkCreateSwapchainKHR>(pGetDeviceProcAddr(g.device,"vkCreateSwapchainKHR"));
    auto swap_images=reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(pGetDeviceProcAddr(g.device,"vkGetSwapchainImagesKHR"));
    swap.DestroySwapchain=reinterpret_cast<PFN_vkDestroySwapchainKHR>(pGetDeviceProcAddr(g.device,"vkDestroySwapchainKHR"));
    swap.Acquire=reinterpret_cast<PFN_vkAcquireNextImageKHR>(pGetDeviceProcAddr(g.device,"vkAcquireNextImageKHR"));
    swap.Present=reinterpret_cast<PFN_vkQueuePresentKHR>(pGetDeviceProcAddr(g.device,"vkQueuePresentKHR"));
    swap.DestroySemaphore=reinterpret_cast<PFN_vkDestroySemaphore>(pGetDeviceProcAddr(g.device,"vkDestroySemaphore"));
    auto semaphore_create=reinterpret_cast<PFN_vkCreateSemaphore>(pGetDeviceProcAddr(g.device,"vkCreateSemaphore"));
    VkSwapchainCreateInfoKHR sc={VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};sc.surface=swap.surface;
    sc.minImageCount=std::max(2u,caps.minImageCount);if(caps.maxImageCount)sc.minImageCount=std::min(sc.minImageCount,caps.maxImageCount);
    sc.imageFormat=chosen.format;sc.imageColorSpace=chosen.colorSpace;sc.imageExtent=caps.currentExtent;sc.imageArrayLayers=1;
    sc.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;sc.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;
    sc.preTransform=caps.currentTransform;sc.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;sc.presentMode=VK_PRESENT_MODE_FIFO_KHR;sc.clipped=VK_TRUE;
    CHECK_VK(swap_create(g.device,&sc,nullptr,&swap.swapchain));
    uint32_t ni=0;swap_images(g.device,swap.swapchain,&ni,nullptr);swap.images.resize(ni);swap_images(g.device,swap.swapchain,&ni,swap.images.data());
    VkSemaphoreCreateInfo sem={VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    CHECK_VK(semaphore_create(g.device,&sem,nullptr,&swap.acquired));CHECK_VK(semaphore_create(g.device,&sem,nullptr,&swap.rendered));
}

static PFN_vkCmdSetStencilReference pCmdSetStencilReference=nullptr;
static PFN_vkCmdSetDepthBias pCmdSetDepthBias=nullptr;
static PFN_vkCmdClearColorImage pCmdClearColorImage=nullptr;
static PFN_vkCmdClearDepthStencilImage pCmdClearDepthStencilImage=nullptr;

struct Pipe {
    HistoricalCase c;
    std::vector<VkDescriptorSetLayout> sets;
    VkPipelineLayout layout{};VkRenderPass pass{},replay{},keep{},cont{};VkPipeline pipeline{};
    std::vector<VkShaderModule> modules;
    bool reversed_depth=false;
};
struct Target {VkImage image{};VkDeviceMemory memory{};VkImageView view{};VkFormat format{};bool shared=false;};
// `keep`: the group's depth is the one an earlier group wrote; `reversed`: a pipeline of the group
// tests depth with GREATER or GREATER_OR_EQUAL, so the depth starts at 0.
struct Group {uint32_t pipe=0;std::vector<Target> targets;VkFramebuffer framebuffer{};bool keep=false,reversed=false;};

// A fatal exception in the process (the layer, its module, a driver) is written to stderr with the
// faulting stack, symbolized from the modules' own debug information, before the process ends.
static LONG WINAPI fatal_exception(EXCEPTION_POINTERS* info){
    const DWORD code=info->ExceptionRecord->ExceptionCode;
    if(code!=EXCEPTION_BREAKPOINT && code!=EXCEPTION_ACCESS_VIOLATION && code!=0xC0000409 && code!=0xC0000374 &&
       code!=EXCEPTION_ILLEGAL_INSTRUCTION && code!=EXCEPTION_STACK_OVERFLOW)return EXCEPTION_CONTINUE_SEARCH;
    static LONG once=0;if(InterlockedExchange(&once,1))return EXCEPTION_CONTINUE_SEARCH;
    const HANDLE process=GetCurrentProcess(),thread=GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_LOAD_LINES);SymInitialize(process,nullptr,TRUE);
    fprintf(stderr,"fatal exception 0x%08lx at %p\n",code,info->ExceptionRecord->ExceptionAddress);
    CONTEXT context=*info->ContextRecord;STACKFRAME64 frame{};
    frame.AddrPC.Offset=context.Rip;frame.AddrPC.Mode=AddrModeFlat;frame.AddrFrame.Offset=context.Rbp;frame.AddrFrame.Mode=AddrModeFlat;
    frame.AddrStack.Offset=context.Rsp;frame.AddrStack.Mode=AddrModeFlat;
    for(int i=0;i<48 && StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr);++i){
        const DWORD64 address=frame.AddrPC.Offset;char buffer[sizeof(SYMBOL_INFO)+512]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=511;DWORD64 displacement=0;
        IMAGEHLP_MODULE64 module{};module.SizeOfStruct=sizeof(module);SymGetModuleInfo64(process,address,&module);
        IMAGEHLP_LINE64 line{};line.SizeOfStruct=sizeof(line);DWORD column=0;
        const bool named=SymFromAddr(process,address,&displacement,symbol)!=0;const bool lined=SymGetLineFromAddr64(process,address,&column,&line)!=0;
        fprintf(stderr,"  %2d %s!%s+0x%llx %s:%lu\n",i,module.ModuleName[0]?module.ModuleName:"?",named?symbol->Name:"?",
                (unsigned long long)displacement,lined?line.FileName:"",lined?line.LineNumber:0ul);
    }
    fflush(stderr);return EXCEPTION_CONTINUE_SEARCH;
}

int wmain(int argc,wchar_t** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);setvbuf(stdout,nullptr,_IONBF,0);
    AddVectoredExceptionHandler(1,fatal_exception);
    unsigned drawn=0,frames=argc>2?unsigned(_wtoi(argv[2])):4;
    // Diagnosis only: "open" turns off depth, stencil and culling in every replayed pipeline, so
    // that what reaches the rasterizer shows whatever those tests would have kept of it.
    // "two-queues": the game's use of its two graphics queues (D-119): a frame's passes split over two
    // command buffers submitted to the two queues with a semaphore between them, the queues swapping
    // roles every frame, a semaphore chain from frame to frame, three frames in flight. The layer's
    // work in front of a submission must then hold on either queue with nothing crossing them.
    // "split-passes" (with two queues): every pass's draws alternate between the two command buffers, the
    // first command buffer's part of each pass where the pass begins, the second's in a pass instance that
    // loads every attachment; a recording then draws components whose bones only the other recording
    // imports, as the game records one character over several command buffers (BUG-054).
    // "animate-bones": the stand-in bones move every frame (a translation that changes with the frame),
    // written while every queue is idle, so the CPU import (at recording) and the GPU import (at
    // execution) read the same pose; a draw that reads another recording's older import then shows
    // a different pose than the CPU import (BUG-054).
    bool open_tests=false,two_queues=false,split_passes=false,animate_bones=false;
    for(int a=3;a<argc;++a){const std::wstring flag=argv[a];if(flag==L"open")open_tests=true;else if(flag==L"two-queues")two_queues=true;
        else if(flag==L"split-passes")two_queues=split_passes=true;else if(flag==L"animate-bones")animate_bones=true;}
    const uint32_t W=960,H=600;
    try{
        if(argc<2)throw std::runtime_error("usage: replay_gpu replay.bin [frames]");
        std::ifstream in(argv[1],std::ios::binary|std::ios::ate);const auto size=in.tellg();
        if(!in || size<0)throw std::runtime_error("replay data missing");
        std::vector<uint8_t> payload(static_cast<size_t>(size));in.seekg(0);in.read(reinterpret_cast<char*>(payload.data()),size);
        HWND window=replay_window(640,360);
        Swap swap;
        VkQueue queues[2]={};
        Gpu g;init_with_window(g,window,swap,two_queues,queues);
#define X(n) p##n=reinterpret_cast<PFN_vk##n>(pGetDeviceProcAddr(g.device,"vk" #n));if(!p##n)throw std::runtime_error("vk" #n);
        X(CmdSetStencilReference) X(CmdSetDepthBias) X(CmdClearColorImage) X(CmdClearDepthStencilImage)
#undef X
        VkPhysicalDeviceProperties properties{};pGetPhysicalDeviceProperties(g.physical,&properties);
        efvk::JsonWriter device;device.begin_object();device.kv("device",properties.deviceName);device.end_object();printf("%s\n",device.out.c_str());

        // Pipelines of the frame as the game created them; a compatible pass of the same attachments
        // that clears and keeps everything for the replay's own render pass instances.
        std::vector<Pipe> pipes(replay_case_count);
        for(unsigned i=0;i<replay_case_count;++i){
            auto& p=pipes[i];p.c=replay_case(i);
            for(const auto& s:p.c.layout.sets){
                std::vector<VkDescriptorSetLayoutBinding> bindings;
                for(const auto& b:s->bindings)bindings.push_back({b.first,static_cast<VkDescriptorType>(b.second.type),b.second.count,b.second.stages,nullptr});
                VkDescriptorSetLayoutCreateInfo ci={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};ci.flags=s->flags;
                ci.bindingCount=static_cast<uint32_t>(bindings.size());ci.pBindings=bindings.data();
                VkDescriptorSetLayout l{};CHECK_VK(pCreateDescriptorSetLayout(g.device,&ci,nullptr,&l));p.sets.push_back(l);
            }
            VkPipelineLayoutCreateInfo li={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.flags=p.c.layout.flags;
            li.setLayoutCount=static_cast<uint32_t>(p.sets.size());li.pSetLayouts=p.sets.data();
            CHECK_VK(pCreatePipelineLayout(g.device,&li,nullptr,&p.layout));
            CHECK_VK(p.c.recipe.render_pass->create(g.device,pCreateRenderPass,pCreateRenderPass2,&p.pass));
            auto replay=*p.c.recipe.render_pass;
            for(auto& a:replay.attachments){
                a.loadOp=a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;
                a.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;a.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            }
            CHECK_VK(replay.create(g.device,pCreateRenderPass,pCreateRenderPass2,&p.replay));
            // The same pass for a later instance: depth and stencil as the earlier instance left them.
            for(auto& a:replay.attachments)if(depth_format(a.format)){
                a.loadOp=a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD;a.initialLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            }
            CHECK_VK(replay.create(g.device,pCreateRenderPass,pCreateRenderPass2,&p.keep));
            // A pass continued in another command buffer: every attachment as the first part left it.
            for(auto& a:replay.attachments){a.loadOp=a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD;a.initialLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;}
            CHECK_VK(replay.create(g.device,pCreateRenderPass,pCreateRenderPass2,&p.cont));
            for(const auto& s:p.c.recipe.stages){
                VkShaderModuleCreateInfo ci={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.codeSize=s.shader->code.size()*4;ci.pCode=s.shader->code.data();
                VkShaderModule m{};CHECK_VK(pCreateShaderModule(g.device,&ci,nullptr,&m));p.modules.push_back(m);
            }
            if(open_tests){
                p.c.recipe.depth.depthTestEnable=VK_FALSE;p.c.recipe.depth.stencilTestEnable=VK_FALSE;
                p.c.recipe.raster.cullMode=VK_CULL_MODE_NONE;
            }
            CHECK_VK(p.c.recipe.create(g.device,pCreateGraphicsPipelines,p.layout,p.pass,p.modules,&p.pipeline));
            p.reversed_depth=p.c.recipe.has_depth && (p.c.recipe.depth.depthCompareOp==VK_COMPARE_OP_GREATER ||
                                                      p.c.recipe.depth.depthCompareOp==VK_COMPARE_OP_GREATER_OR_EQUAL);
        }
        // Buffers: host-visible and mapped, as the game's are where the layer reads them.
        const auto specs=replay_buffers();
        std::vector<Buffer> buffers;
        for(const auto& b:specs){
            auto made=g.buffer(size_t(b.size),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|
                                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            for(const auto& f:b.fills){
                if(f.src+f.size>payload.size() || f.dst+f.size>b.size)throw std::runtime_error("replay fill outside its data");
                memcpy(static_cast<uint8_t*>(made.mapped)+f.dst,payload.data()+f.src,size_t(f.size));
            }
            buffers.push_back(made);
        }
        // Textures: zero or grey images of the recorded format, size, mip chain and view type.
        const auto image_specs=replay_images();
        std::vector<VkImageView> views;
        {
            auto cb=g.begin();
            auto zero=g.buffer(size_t(32)<<20,VK_BUFFER_USAGE_TRANSFER_SRC_BIT); // largest level: 4096 x 4096 at 2 bytes per texel
            for(const auto& s:image_specs){
                const auto format=static_cast<VkFormat>(s.format),view_format=static_cast<VkFormat>(s.view_format);
                const auto view_type=static_cast<VkImageViewType>(s.view_type);
                const bool cube=view_type==VK_IMAGE_VIEW_TYPE_CUBE || view_type==VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
                VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=static_cast<VkImageType>(s.type);ci.format=format;
                ci.extent={s.width,ci.imageType==VK_IMAGE_TYPE_1D?1u:s.height,ci.imageType==VK_IMAGE_TYPE_3D?4u:1u};
                ci.mipLevels=s.mips;ci.arrayLayers=std::max(cube?6u:1u,s.layers);ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;
                ci.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                if(cube)ci.flags|=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
                if(format!=view_format)ci.flags|=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
                g.images.emplace_back();auto& image=g.images.back();
                CHECK_VK(pCreateImage(g.device,&ci,nullptr,&image.image));VkMemoryRequirements req{};pGetImageMemoryRequirements(g.device,image.image,&req);
                image.memory=g.allocate(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);CHECK_VK(pBindImageMemory(g.device,image.image,image.memory,0));
                const auto aspect=aspect_of(format);
                VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image.image;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                b.subresourceRange={aspect,0,ci.mipLevels,0,ci.arrayLayers};b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
                pCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
                if(depth_format(format)){
                    VkClearDepthStencilValue v{0.5f,0};pCmdClearDepthStencilImage(cb,image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&v,1,&b.subresourceRange);
                }else if(block_format(format) || !texel_bytes(format)){
                    for(uint32_t level=0;level<ci.mipLevels;++level){
                        VkBufferImageCopy copy{};copy.imageSubresource={aspect,level,0,ci.arrayLayers};
                        copy.imageExtent={std::max(1u,ci.extent.width>>level),std::max(1u,ci.extent.height>>level),std::max(1u,ci.extent.depth>>level)};
                        pCmdCopyBufferToImage(cb,zero.buffer,image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
                    }
                }else{
                    VkClearColorValue grey{{0.5f,0.5f,0.5f,1.0f}};pCmdClearColorImage(cb,image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&grey,1,&b.subresourceRange);
                }
                b.oldLayout=b.newLayout;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcAccessMask=b.dstAccessMask;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                pCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,0,0,nullptr,0,nullptr,1,&b);
                VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image.image;vi.viewType=view_type;vi.format=view_format;
                vi.subresourceRange={depth_format(format)?VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT):aspect,0,ci.mipLevels,0,
                                     view_type==VK_IMAGE_VIEW_TYPE_CUBE?6u:(view_type==VK_IMAGE_VIEW_TYPE_2D || view_type==VK_IMAGE_VIEW_TYPE_1D ||
                                                                           view_type==VK_IMAGE_VIEW_TYPE_3D?1u:ci.arrayLayers)};
                VkImageView view{};CHECK_VK(pCreateImageView(g.device,&vi,nullptr,&view));views.push_back(view);
            }
            g.finish(cb,zero);
        }
        VkSamplerCreateInfo si={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_LINEAR;si.maxLod=VK_LOD_CLAMP_NONE;
        VkSampler sampler{};CHECK_VK(pCreateSampler(g.device,&si,nullptr,&sampler));

        // Descriptor sets of every draw, from its recorded contents.
        const auto draws=replay_draws();
        std::map<VkDescriptorType,uint32_t> totals;uint32_t set_total=0;
        for(const auto& d:draws){set_total+=uint32_t(pipes[d.pipeline].sets.size());for(const auto& x:d.descriptors)++totals[static_cast<VkDescriptorType>(x.type)];}
        std::vector<VkDescriptorPoolSize> sizes;for(const auto& t:totals)sizes.push_back({t.first,t.second});
        VkDescriptorPoolCreateInfo pi={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=std::max(1u,set_total);
        pi.poolSizeCount=static_cast<uint32_t>(sizes.size());pi.pPoolSizes=sizes.data();
        VkDescriptorPool pool{};CHECK_VK(pCreateDescriptorPool(g.device,&pi,nullptr,&pool));
        std::vector<std::vector<VkDescriptorSet>> sets(draws.size());
        std::vector<std::vector<uint32_t>> dynamic_counts(draws.size());
        for(size_t i=0;i<draws.size();++i){
            const auto& d=draws[i];auto& p=pipes[d.pipeline];
            sets[i].resize(p.sets.size());
            VkDescriptorSetAllocateInfo ai={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=pool;
            ai.descriptorSetCount=static_cast<uint32_t>(p.sets.size());ai.pSetLayouts=p.sets.data();
            if(!p.sets.empty())CHECK_VK(pAllocateDescriptorSets(g.device,&ai,sets[i].data()));
            dynamic_counts[i].assign(p.sets.size(),0);
            std::vector<VkWriteDescriptorSet> writes;std::vector<VkDescriptorBufferInfo> infos;std::vector<VkDescriptorImageInfo> images;
            infos.reserve(d.descriptors.size());images.reserve(d.descriptors.size());
            for(const auto& x:d.descriptors){
                VkWriteDescriptorSet w={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=sets[i][x.set];w.dstBinding=x.binding;w.descriptorCount=1;
                w.descriptorType=static_cast<VkDescriptorType>(x.type);
                if(x.type==VK_DESCRIPTOR_TYPE_SAMPLER){images.push_back({sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED});w.pImageInfo=&images.back();}
                else if(x.type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE){images.push_back({VK_NULL_HANDLE,views[x.image],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});w.pImageInfo=&images.back();}
                else{infos.push_back({buffers[x.buffer].buffer,x.offset,x.range});w.pBufferInfo=&infos.back();}
                if(x.type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || x.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)++dynamic_counts[i][x.set];
                writes.push_back(w);
            }
            pUpdateDescriptorSets(g.device,static_cast<uint32_t>(writes.size()),writes.data(),0,nullptr);
        }
        // One framebuffer per recorded render pass instance, at a fixed size.
        // The capture does not name the images of a render pass instance. The frame's passes of one
        // depth format are taken to share their depth, as a deferred renderer's G-buffer and forward
        // passes do: the first instance clears it, the later ones keep it.
        std::map<uint32_t,Group> groups;std::map<VkFormat,Target> depths;
        for(const auto& d:draws)groups[d.group].reversed|=pipes[d.pipeline].reversed_depth;
        for(const auto& d:draws)if(!groups[d.group].framebuffer){
            Group& gr=groups[d.group];gr.pipe=d.pipeline;const auto& rp=*pipes[d.pipeline].c.recipe.render_pass;
            std::vector<VkImageView> attachments;
            for(const auto& a:rp.attachments){
                Target t;t.format=a.format;const bool depth=depth_format(a.format);
                if(depth && depths.count(a.format)){
                    t=depths[a.format];t.shared=true;gr.keep=true;attachments.push_back(t.view);gr.targets.push_back(t);continue;
                }
                VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=a.format;ci.extent={W,H,1};
                ci.mipLevels=ci.arrayLayers=1;ci.samples=a.samples;ci.tiling=VK_IMAGE_TILING_OPTIMAL;
                // Sampled as well: the game reads its targets in later passes (the copy of the depth, D-113).
                ci.usage=(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
                CHECK_VK(pCreateImage(g.device,&ci,nullptr,&t.image));VkMemoryRequirements req{};pGetImageMemoryRequirements(g.device,t.image,&req);
                t.memory=g.allocate(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);CHECK_VK(pBindImageMemory(g.device,t.image,t.memory,0));
                VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=t.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=a.format;
                vi.subresourceRange={aspect_of(a.format),0,1,0,1};CHECK_VK(pCreateImageView(g.device,&vi,nullptr,&t.view));
                attachments.push_back(t.view);gr.targets.push_back(t);
                if(depth)depths[a.format]=t;
            }
            VkFramebufferCreateInfo fi={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fi.renderPass=pipes[d.pipeline].replay;
            fi.attachmentCount=static_cast<uint32_t>(attachments.size());fi.pAttachments=attachments.data();fi.width=W;fi.height=H;fi.layers=1;
            CHECK_VK(pCreateFramebuffer(g.device,&fi,nullptr,&gr.framebuffer));
        }
        // Frames: the recorded draws in their order, each render pass instance its own pass.
        std::vector<Buffer> readbacks;std::vector<std::pair<uint32_t,uint32_t>> readback_of;
        // Three command buffers reset and recorded again in turn, as a game reuses its own: the layer
        // recycles what it retained for a buffer when the buffer is reset.
        auto reset_pool=reinterpret_cast<PFN_vkResetCommandPool>(pGetDeviceProcAddr(g.device,"vkResetCommandPool"));
        if(!reset_pool)throw std::runtime_error("vkResetCommandPool");
        VkCommandPool ring_pools[3]{};VkCommandBuffer ring[3]{};
        for(unsigned r=0;r<3;++r){
            VkCommandPoolCreateInfo ci={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};ci.queueFamilyIndex=g.family;
            CHECK_VK(pCreateCommandPool(g.device,&ci,nullptr,&ring_pools[r]));g.pools.push_back(ring_pools[r]);
            VkCommandBufferAllocateInfo ai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=ring_pools[r];ai.commandBufferCount=1;
            CHECK_VK(pAllocateCommandBuffers(g.device,&ai,&ring[r]));
        }
        // Two-queue mode: the groups (passes) of a frame in two command buffers, the first on queue
        // A (frame f: f%2), the second on queue B, which waits for the first through a semaphore and
        // for the previous frame's second half (the other queue) through the chain; fences keep three
        // frames in flight; the presentation goes on queue B.
        std::vector<uint32_t> group_order;for(const auto& d:draws)if(group_order.empty() || group_order.back()!=d.group)group_order.push_back(d.group);
        const size_t split_index=group_order.size()/2;
        auto cb_of_group=[&](uint32_t group){for(size_t k=0;k<group_order.size();++k)if(group_order[k]==group)return k<split_index?0u:1u;return 1u;};
        // split-passes: each draw's half of its pass (0 the first half of the pass's draws, 1 the rest).
        std::vector<uint32_t> part_of(draws.size(),0);
        if(split_passes){std::map<uint32_t,std::vector<size_t>> members;for(size_t i=0;i<draws.size();++i)members[draws[i].group].push_back(i);
            for(const auto& m:members)for(size_t k=0;k<m.second.size();++k)part_of[m.second[k]]=uint32_t(k%2);}
        // The recording order: with split passes, the first command buffer is recorded whole before the
        // second, as each of the game's threads records its own command buffer through.
        std::vector<size_t> record_order;
        for(uint32_t part=0;part<(split_passes?2u:1u);++part)
            for(size_t i=0;i<draws.size();++i)if(!split_passes || part_of[i]==part)record_order.push_back(i);
        VkCommandBuffer second[3]={};VkFence fences[3]={};VkSemaphore acquired[3]={},chain[3]={},halves[3]={};
        // The presentation's semaphore per swap chain image: a presentation may still hold the one a frame
        // slot would reuse.
        std::vector<VkSemaphore> rendered(swap.images.size(),VK_NULL_HANDLE);
        auto fence_create=reinterpret_cast<PFN_vkCreateFence>(pGetDeviceProcAddr(g.device,"vkCreateFence"));
        auto fence_wait=reinterpret_cast<PFN_vkWaitForFences>(pGetDeviceProcAddr(g.device,"vkWaitForFences"));
        auto fence_reset=reinterpret_cast<PFN_vkResetFences>(pGetDeviceProcAddr(g.device,"vkResetFences"));
        auto fence_destroy=reinterpret_cast<PFN_vkDestroyFence>(pGetDeviceProcAddr(g.device,"vkDestroyFence"));
        auto semaphore_make=reinterpret_cast<PFN_vkCreateSemaphore>(pGetDeviceProcAddr(g.device,"vkCreateSemaphore"));
        if(two_queues){
            if(!fence_create || !fence_wait || !fence_reset || !fence_destroy || !semaphore_make)throw std::runtime_error("two-queues: fence and semaphore entry points");
            for(unsigned r=0;r<3;++r){
                VkCommandBufferAllocateInfo ai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=ring_pools[r];ai.commandBufferCount=1;
                CHECK_VK(pAllocateCommandBuffers(g.device,&ai,&second[r]));
                VkFenceCreateInfo fi={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fi.flags=VK_FENCE_CREATE_SIGNALED_BIT;CHECK_VK(fence_create(g.device,&fi,nullptr,&fences[r]));
                VkSemaphoreCreateInfo si={VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                CHECK_VK(semaphore_make(g.device,&si,nullptr,&acquired[r]));
                CHECK_VK(semaphore_make(g.device,&si,nullptr,&chain[r]));CHECK_VK(semaphore_make(g.device,&si,nullptr,&halves[r]));
            }
            VkSemaphoreCreateInfo si={VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            for(auto& sem:rendered)CHECK_VK(semaphore_make(g.device,&si,nullptr,&sem));
        }
        for(unsigned f=0;f<frames;++f){
            pump();
            const unsigned slot=f%3;
            if(two_queues){CHECK_VK(fence_wait(g.device,1,&fences[slot],VK_TRUE,UINT64_MAX));CHECK_VK(fence_reset(g.device,1,&fences[slot]));}
            if(animate_bones){
                CHECK_VK(pDeviceWaitIdle(g.device));
                const float t=float(f);
                const float row[12]={1,0,0,0.04f*std::sin(0.7f*t), 0,0,1,0.03f*std::cos(0.5f*t), 0,-1,0,0.02f*std::sin(0.3f*t)};
                for(const auto& region:replay_bones())
                    for(uint64_t at=0;at+sizeof(row)<=region.size;at+=sizeof(row))
                        memcpy(static_cast<uint8_t*>(buffers[size_t(region.buffer)].mapped)+region.offset+at,row,sizeof(row));
            }
            const VkSemaphore acquire_sem=two_queues?acquired[slot]:swap.acquired;
            uint32_t image=0;CHECK_VK(swap.Acquire(g.device,swap.swapchain,UINT64_MAX,acquire_sem,VK_NULL_HANDLE,&image));
            const VkSemaphore render_sem=two_queues?rendered[image]:swap.rendered;
            CHECK_VK(reset_pool(g.device,ring_pools[slot],0));
            VkCommandBuffer halves_cb[2]={ring[slot],two_queues?second[slot]:ring[slot]};
            auto cb=halves_cb[0];
            {VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK_VK(pBeginCommandBuffer(halves_cb[0],&bi));
             if(two_queues)CHECK_VK(pBeginCommandBuffer(halves_cb[1],&bi));}
            uint32_t current=UINT32_MAX,current_part=UINT32_MAX;
            auto end_pass=[&]{if(current!=UINT32_MAX)pCmdEndRenderPass(cb);current=current_part=UINT32_MAX;};
            for(const size_t i:record_order){
                const auto& d=draws[i];auto& p=pipes[d.pipeline];
                if(d.group!=current || part_of[i]!=current_part){
                    end_pass();current=d.group;current_part=part_of[i];auto& gr=groups[d.group];
                    if(split_passes)cb=halves_cb[current_part];
                    else if(two_queues)cb=halves_cb[cb_of_group(d.group)];
                    std::vector<VkClearValue> clears(gr.targets.size());
                    for(size_t a=0;a<gr.targets.size();++a){
                        if(depth_format(gr.targets[a].format))clears[a].depthStencil={gr.reversed?0.0f:1.0f,0};
                        else clears[a].color={{0,0,0,0}};
                    }
                    VkRenderPassBeginInfo bi={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                    bi.renderPass=current_part==1?pipes[gr.pipe].cont:gr.keep?pipes[gr.pipe].keep:pipes[gr.pipe].replay;bi.framebuffer=gr.framebuffer;
                    bi.renderArea.extent={W,H};bi.clearValueCount=static_cast<uint32_t>(clears.size());bi.pClearValues=clears.data();
                    pCmdBeginRenderPass(cb,&bi,VK_SUBPASS_CONTENTS_INLINE);
                }
                pCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,p.pipeline);
                for(auto state:p.c.recipe.dynamic){
                    if(state==VK_DYNAMIC_STATE_VIEWPORT){VkViewport vp{0,0,float(W),float(H),0,1};pCmdSetViewport(cb,0,1,&vp);}
                    else if(state==VK_DYNAMIC_STATE_SCISSOR){VkRect2D sc{{0,0},{W,H}};pCmdSetScissor(cb,0,1,&sc);}
                    else if(state==VK_DYNAMIC_STATE_STENCIL_REFERENCE)pCmdSetStencilReference(cb,VK_STENCIL_FACE_FRONT_AND_BACK,0);
                    else if(state==VK_DYNAMIC_STATE_DEPTH_BIAS)pCmdSetDepthBias(cb,0,0,0);
                }
                for(uint32_t s=0;s<sets[i].size();++s){
                    std::vector<uint32_t> offsets(dynamic_counts[i][s],0);
                    pCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,p.layout,s,1,&sets[i][s],uint32_t(offsets.size()),offsets.data());
                }
                for(const auto& v:d.vertex){VkDeviceSize zero=0;pCmdBindVertexBuffers(cb,v.binding,1,&buffers[v.buffer].buffer,&zero);}
                if(d.index_buffer==UINT32_MAX)pCmdDraw(cb,d.count,d.instances,d.first,d.first_instance); // a full-screen pass of the game
                else{
                    pCmdBindIndexBuffer(cb,buffers[d.index_buffer].buffer,0,d.index_type?VK_INDEX_TYPE_UINT32:VK_INDEX_TYPE_UINT16);
                    pCmdDrawIndexed(cb,d.count,d.instances,d.first,d.vertex_offset,d.first_instance);
                }
                if(f+1==frames)++drawn;
            }
            end_pass();
            cb=halves_cb[1];
            if(f+1==frames)
                for(auto& item:groups)for(uint32_t a=0;a<item.second.targets.size();++a){
                    auto& t=item.second.targets[a];const uint32_t bytes=texel_bytes(t.format);if(!bytes)continue;
                    auto b=g.readback(size_t(W)*H*bytes);
                    VkBufferImageCopy copy{};copy.imageSubresource={depth_format(t.format)?VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT):VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
                    copy.imageExtent={W,H,1};
                    if(t.format==VK_FORMAT_D32_SFLOAT_S8_UINT){b=g.readback(size_t(W)*H*4);}
                    pCmdCopyImageToBuffer(cb,t.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,b.buffer,1,&copy);
                    readbacks.push_back(b);readback_of.push_back({item.first,a});
                }
            {
                // The presented image: cleared and handed to the presentation engine.
                VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=swap.images[image];
                b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
                pCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
                VkClearColorValue black{{0,0,0,1}};pCmdClearColorImage(cb,b.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&black,1,&b.subresourceRange);
                b.oldLayout=b.newLayout;b.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;b.srcAccessMask=b.dstAccessMask;b.dstAccessMask=0;
                pCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&b);
                CHECK_VK(pEndCommandBuffer(halves_cb[1]));
                if(!two_queues){
                    const VkPipelineStageFlags wait=VK_PIPELINE_STAGE_TRANSFER_BIT;
                    VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&swap.acquired;
                    submit.pWaitDstStageMask=&wait;submit.commandBufferCount=1;submit.pCommandBuffers=&cb;
                    submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&swap.rendered;
                    CHECK_VK(pQueueSubmit(g.queue,1,&submit,VK_NULL_HANDLE));
                    VkPresentInfoKHR present={VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&swap.rendered;
                    present.swapchainCount=1;present.pSwapchains=&swap.swapchain;present.pImageIndices=&image;
                    CHECK_VK(swap.Present(g.queue,&present));
                    CHECK_VK(pQueueWaitIdle(g.queue));
                }else{
                    CHECK_VK(pEndCommandBuffer(halves_cb[0]));
                    VkQueue queue_a=queues[f%2],queue_b=queues[(f+1)%2];
                    // The first half on queue A signals the halves semaphore; the second half on queue B
                    // waits for it, for the swap chain image and (from the second frame) for the previous
                    // frame's second half on the other queue, and signals the chain and the presentation.
                    {
                        VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&halves_cb[0];
                        submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&halves[slot];
                        CHECK_VK(pQueueSubmit(queue_a,1,&submit,VK_NULL_HANDLE));
                    }
                    {
                        VkSemaphore waits[3]={halves[slot],acquire_sem,chain[(f+2)%3]};
                        const VkPipelineStageFlags stages[3]={VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                                              VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT};
                        VkSemaphore signals[2]={render_sem,chain[slot]};
                        VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=f?3u:2u;submit.pWaitSemaphores=waits;submit.pWaitDstStageMask=stages;
                        submit.commandBufferCount=1;submit.pCommandBuffers=&halves_cb[1];submit.signalSemaphoreCount=2;submit.pSignalSemaphores=signals;
                        CHECK_VK(pQueueSubmit(queue_b,1,&submit,fences[slot]));
                    }
                    VkPresentInfoKHR present={VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&render_sem;
                    present.swapchainCount=1;present.pSwapchains=&swap.swapchain;present.pImageIndices=&image;
                    CHECK_VK(swap.Present(queue_b,&present));
                }
            }
            // Optional benchmark checkpoint; normal regression runs keep their original path.
            if(f==299 && std::getenv("EFMI_VK_REPLAY_TIMING")) self_probe("{\"cmd\":\"status\"}","warm-status.json");
        }
        CHECK_VK(pDeviceWaitIdle(g.device));
        if(two_queues)for(unsigned r=0;r<3;++r){fence_destroy(g.device,fences[r],nullptr);
            for(auto sem:{acquired[r],chain[r],halves[r]})swap.DestroySemaphore(g.device,sem,nullptr);}
        for(auto sem:rendered)if(sem)swap.DestroySemaphore(g.device,sem,nullptr);
        // Targets and their formats for the caller to convert.
        efvk::JsonWriter index;index.begin_object();index.kn("width",W);index.kn("height",H);index.key("targets");index.begin_array();
        for(size_t i=0;i<readbacks.size();++i){
            const auto& t=groups[readback_of[i].first].targets[readback_of[i].second];
            const std::string name="target-"+std::to_string(readback_of[i].first)+"-"+std::to_string(readback_of[i].second)+".raw";
            const uint32_t bytes=t.format==VK_FORMAT_D32_SFLOAT_S8_UINT?4:texel_bytes(t.format);
            std::ofstream out(name,std::ios::binary);out.write(static_cast<const char*>(readbacks[i].mapped),std::streamsize(size_t(W)*H*bytes));
            index.begin_object();index.kn("group",readback_of[i].first);index.kn("attachment",readback_of[i].second);index.kn("format",t.format);
            index.kn("bytes",bytes);index.kv("file",name);index.end_object();
        }
        index.end_array();index.end_object();
        {std::ofstream out("targets.json");out<<index.out;}
        self_probe("{\"cmd\":\"status\"}","status.json");
        pDeviceWaitIdle(g.device);
        for(auto& item:groups){pDestroyFramebuffer(g.device,item.second.framebuffer,nullptr);
            for(auto& t:item.second.targets)if(!t.shared){pDestroyImageView(g.device,t.view,nullptr);pDestroyImage(g.device,t.image,nullptr);pFreeMemory(g.device,t.memory,nullptr);}}
        pDestroyDescriptorPool(g.device,pool,nullptr);pDestroySampler(g.device,sampler,nullptr);
        for(auto v:views)pDestroyImageView(g.device,v,nullptr);
        for(auto& p:pipes){pDestroyPipeline(g.device,p.pipeline,nullptr);for(auto m:p.modules)pDestroyShaderModule(g.device,m,nullptr);
            pDestroyRenderPass(g.device,p.pass,nullptr);pDestroyRenderPass(g.device,p.replay,nullptr);pDestroyRenderPass(g.device,p.keep,nullptr);pDestroyRenderPass(g.device,p.cont,nullptr);pDestroyPipelineLayout(g.device,p.layout,nullptr);
            for(auto s:p.sets)pDestroyDescriptorSetLayout(g.device,s,nullptr);}
        swap.DestroySemaphore(g.device,swap.acquired,nullptr);swap.DestroySemaphore(g.device,swap.rendered,nullptr);
        swap.DestroySwapchain(g.device,swap.swapchain,nullptr);swap.DestroySurface(g.instance,swap.surface,nullptr);
    }catch(const std::exception& error){++failed;printf("FAIL: %s\n",error.what());}
    printf("replay: %u draws per frame, %u frames, %d failed; validation errors=%u warnings=%u\n",
           drawn,frames,failed,validation_errors.load(),validation_warnings.load());
    return failed?1:0;
}
