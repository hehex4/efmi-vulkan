// Original production INI -> owned Vulkan commands -> GPU readback. Headless.
#include "../frontend/xxmi/gpu_resources.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <type_traits>
using namespace efmi;
static_assert(!std::is_copy_constructible_v<gpu::BufferResource> && !std::is_copy_assignable_v<gpu::ResourceSubmission>,"GPU handles have unique owners");
static unsigned checks=0;
static std::atomic<unsigned> errors{0},warnings{0};
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);++checks;}
static void vkcheck(VkResult r,const char* why){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(why)+": "+std::to_string(r));}
namespace efvk {uint32_t crc32c(uint32_t seed,const void* data,size_t size){
    uint32_t h=~seed;const auto* b=static_cast<const uint8_t*>(data);
    for(size_t i=0;i<size;++i){h^=b[i];for(int j=0;j<8;++j)h=(h>>1)^(0x82f63b78u&(0u-(h&1)));}return ~h;
}}
static VKAPI_ATTR VkBool32 VKAPI_CALL diagnostic(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*){
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)++errors;else ++warnings;
    printf("VALIDATION: %s\n",data->pMessage);return VK_FALSE;
}
struct Device {
    VkInstance instance=VK_NULL_HANDLE;gpu::ResourceDevice resource;VkQueue queue=VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;
    PFN_vkDestroyInstance destroy_instance=nullptr;PFN_vkDestroyDevice destroy_device=nullptr;
    PFN_vkDeviceWaitIdle wait_idle=nullptr;PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug=nullptr;
    ~Device(){if(resource.device){wait_idle(resource.device);destroy_device(resource.device,nullptr);}
        if(messenger)destroy_debug(instance,messenger,nullptr);if(instance)destroy_instance(instance,nullptr);}
    void init(){
        auto loader=LoadLibraryW(L"vulkan-1.dll");check(loader!=nullptr,"Vulkan loader");
        auto gi=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
        auto create=reinterpret_cast<PFN_vkCreateInstance>(gi(nullptr,"vkCreateInstance"));
        VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="efmi_resource_commands";app.apiVersion=VK_API_VERSION_1_1;
        const char* layer="VK_LAYER_KHRONOS_validation";const char* ext=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        VkDebugUtilsMessengerCreateInfoEXT debug={VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;debug.pfnUserCallback=diagnostic;
        VkInstanceCreateInfo ci={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;ci.pNext=&debug;
        ci.enabledLayerCount=ci.enabledExtensionCount=1;ci.ppEnabledLayerNames=&layer;ci.ppEnabledExtensionNames=&ext;
        vkcheck(create(&ci,nullptr,&instance),"Create test instance");
#define GET(name) auto name=reinterpret_cast<PFN_vk##name>(gi(instance,"vk" #name));check(name!=nullptr,"vk" #name);
        GET(DestroyInstance);destroy_instance=DestroyInstance;
        GET(CreateDebugUtilsMessengerEXT);GET(DestroyDebugUtilsMessengerEXT);destroy_debug=DestroyDebugUtilsMessengerEXT;
        vkcheck(CreateDebugUtilsMessengerEXT(instance,&debug,nullptr,&messenger),"Create debug messenger");
        GET(EnumeratePhysicalDevices);GET(GetPhysicalDeviceQueueFamilyProperties);GET(GetPhysicalDeviceMemoryProperties);
        GET(CreateDevice);GET(GetDeviceProcAddr);
#undef GET
        uint32_t count=0;vkcheck(EnumeratePhysicalDevices(instance,&count,nullptr),"Enumerate test devices");
        std::vector<VkPhysicalDevice> physical(count);vkcheck(EnumeratePhysicalDevices(instance,&count,physical.data()),"Read test devices");
        VkPhysicalDevice selected=VK_NULL_HANDLE;
        for(auto p:physical){uint32_t n=0;GetPhysicalDeviceQueueFamilyProperties(p,&n,nullptr);std::vector<VkQueueFamilyProperties> q(n);
            GetPhysicalDeviceQueueFamilyProperties(p,&n,q.data());for(uint32_t i=0;i<n;++i)if(q[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){selected=p;resource.queue_family=i;break;}
            if(selected)break;}
        check(selected!=VK_NULL_HANDLE,"Graphics queue available");GetPhysicalDeviceMemoryProperties(selected,&resource.memory);
        float priority=1;VkDeviceQueueCreateInfo q={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueFamilyIndex=resource.queue_family;q.queueCount=1;q.pQueuePriorities=&priority;
        VkDeviceCreateInfo dc={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&q;
        vkcheck(CreateDevice(selected,&dc,nullptr,&resource.device),"Create test device");
#define GET(name) auto name=reinterpret_cast<PFN_vk##name>(GetDeviceProcAddr(resource.device,"vk" #name));check(name!=nullptr,"vk" #name);
        GET(DestroyDevice);destroy_device=DestroyDevice;GET(DeviceWaitIdle);wait_idle=DeviceWaitIdle;GET(GetDeviceQueue);GetDeviceQueue(resource.device,resource.queue_family,0,&queue);
#undef GET
#define X(name) resource.dispatch.name=reinterpret_cast<PFN_vk##name>(GetDeviceProcAddr(resource.device,"vk" #name));
        EFMI_RESOURCE_COMMAND_FUNCS(X)
#undef X
    }
};
template<class F> static void refuses(F f,const char* why){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,why);}
static float value(const std::vector<uint8_t>& bytes,size_t offset=0){float v;check(offset+4<=bytes.size(),"Readback value in bounds");memcpy(&v,bytes.data()+offset,4);return v;}
static void low_level(Device& d){
    gpu::ResourceBudgets budgets;auto commands=std::make_unique<gpu::ResourceCommands>(d.resource,budgets);
    const uint32_t data[]={0x01020304,0x11223344,0xaabbccdd,0x55667788};
    auto a=commands->create(sizeof(data),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,data,sizeof(data));
    auto b=commands->create(sizeof(data),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);
    commands->copy(a,4,b,0,8);commands->fill(b,8,8,0xdeadbeef);
    auto bytes=commands->readback(d.queue,b,0,sizeof(data));const uint32_t expected[]={data[1],data[2],0xdeadbeef,0xdeadbeef};
    check(bytes.size()==sizeof(expected) && !memcmp(bytes.data(),expected,sizeof(expected)),"GPU copy and fill ordering/readback bytes");
    commands->copy(a,0,a,12,4);bytes=commands->readback(d.queue,a,0,sizeof(data));
    check(!memcmp(bytes.data()+12,data,4),"Non-overlapping same-buffer copy");
    refuses([&]{commands->copy(a,0,a,4,8);},"Overlapping copies refused");
    refuses([&]{commands->copy(a,12,b,0,8);},"Source copy overflow refused");
    refuses([&]{commands->fill(a,1,4,1);},"Misaligned clear refused");
    gpu::ResourceCommands other(d.resource,budgets);refuses([&]{other.fill(a,0,4,1);},"Cross-domain resource refused");
    gpu::ResourceBudgets tiny;tiny.resident=std::make_shared<assets::AllocationBudget>(1);
    {gpu::ResourceCommands denied(d.resource,tiny);refuses([&]{denied.create(16,0);},"Budget failure before oversized allocation");}
    check(tiny.resident->used()==0,"Failed allocation leaves no budget charge");
    commands->fill(b,0,16,0x1234);std::weak_ptr<gpu::BufferResource> weak=b;auto ticket=commands->submit(d.queue);
    a.reset();b.reset();commands.reset();check(!weak.expired(),"Submitted command retains resources after executor retirement");
    ticket->wait();check(ticket->ready(),"Fence-backed completion");ticket.reset();check(weak.expired(),"Completed ticket releases last resource owner");
    check(budgets.resident->used()==0 && budgets.staging->used()==0,"All GPU allocation ledgers released");
}
static const char* fixture=R"ini(namespace = gpu
[Constants]
global $before = 0
global $copied = 0
global $alias = 0
global $description = 0
global $branch = 0
global $null_value = 77
global $bounds = 99
global $typed0 = 0
global $typed3 = 0
global $size = 0
global $stride = 0
global $slot = 2
global $phase = 0
global $fetch = 0
global $pool = 0
global $limit = 0
global $reset = 0
global $small = 0
global $cb = 0
global $srv_noop = 0
global $srv_cache = 0
global $uav_clear = 0
global $view_format = 0
global $view_region = 0
global $raw_float = 0
[ResourceSource]
type = StructuredBuffer
stride = 8
data = R32_FLOAT 1 2 3 4 5 6 7 8
bind_flags = shader_resource unordered_access
[ResourceCopy]
bind_flags = shader_resource unordered_access
[ResourceAlias]
[ResourceSaved]
[ResourceDescription]
bind_flags = shader_resource unordered_access
[ResourceNull]
[ResourceTyped]
type = Buffer
format = R32G32B32A32_FLOAT
array = 2
bind_flags = shader_resource unordered_access
[ResourceFetched]
[ResourceFetched2]
[ResourceLimited]
max_copies_per_frame = 1
bind_flags = shader_resource unordered_access
[ResourceSmall]
type = StructuredBuffer
stride = 12
data = R32_FLOAT 1 2 3 4 5 6
bind_flags = shader_resource unordered_access
[ResourceCB]
type = Buffer
data = R32_FLOAT 10 20 30 40 50 60 70 80
bind_flags = constant_buffer
[PoolBuffers]
pool_size = 3
pool_element_type_switch_reset = false
[ResourceRegion]
[ResourceViewSource]
type = Buffer
format = R32_FLOAT
data = R32_FLOAT 1 2 3 4
bind_flags = shader_resource unordered_access
[ResourceViewResource]
[ResourceViewCache]
[ResourceUav]
[ResourceViewFormat]
format = R32_UINT
[ResourceViewRegion]
[ResourceRawTyped]
type = ByteAddressBuffer
format = R32G32_FLOAT
array = 2
bind_flags = shader_resource unordered_access
[Present]
if $phase == 0
 store = $before, ResourceSource, 5
 ResourceCopy = copy ResourceSource
 ResourceAlias = ref ResourceSource
 cs-u0 = ref ResourceAlias
 clear = cs-u0 0x40000000
 store = $alias, ResourceSource, 5
 store = $copied, ResourceCopy, 2
 if $copied == 3 && $alias == 2
  $branch = 1
 endif
 ResourceSaved = ref ResourceCopy
 ResourceCopy = unless_null ref ResourceNull
 ResourceDescription = copy_desc ResourceCopy
 clear = ResourceDescription 0x40800000
 store = $description, ResourceDescription, 3
 ResourceCopy = null
 store = $null_value, ResourceCopy, 0
 store = $bounds, ResourceSource, 99
 clear = ResourceTyped 1.25 2.5 3.75 4.5
 store = $typed0, ResourceTyped, 0
 store = $typed3, ResourceTyped, 7
 $size = ResourceSource->size
 $stride = ResourceSource->stride
 ps-t0 = ref ResourceSaved
 ps-t1 = ref ResourceDescription
 cs-t[$slot] = ref ResourceTyped
 ResourceFetched = ref ps-t0
 ResourceFetched2 = ref ps-t1
 store = $fetch, ResourceFetched, 7
 PoolBuffers[0] = ref ResourceFetched
 $PoolBuffers[1] = 9
 $PoolBuffers[0] = 5
 store = $pool, PoolBuffers[0], 6
 ResourceLimited = copy ResourceFetched
 ResourceLimited = copy ResourceDescription
 store = $limit, ResourceLimited, 0
 reset_per_frame_limits = ResourceLimited
 ResourceLimited = copy ResourceDescription
 store = $reset, ResourceLimited, 0
 store = $small, ResourceSmall, 1
 cs-cb0 = ref ResourceCB->Region(16, 16)
 store = $cb, cs-cb0, 0
 ResourceRegion = ref ResourceTyped->Region(16, 32)
 clear = ResourceRegion 6.25
 cs-t3 = ref ResourceRegion
 $PoolBuffers[*] = null
 cs-t4 = ref PoolBuffers[0]
 PoolBuffers[*] = null
 cs-t5 = ref PoolBuffers[0]
 ps-t6 = ref ResourceViewSource
 ResourceViewResource = ref ps-t6
 clear = ResourceViewResource 5
 store = $srv_noop, ResourceViewSource, 0
 ResourceViewCache = ref ResourceViewResource
 ResourceViewCache = ref ResourceViewSource
 clear = ResourceViewCache 6
 store = $srv_cache, ResourceViewSource, 0
 cs-u6 = ref ResourceViewResource
 ResourceUav = ref cs-u6
 clear = ResourceUav 7
 store = $uav_clear, ResourceViewSource, 2
 ResourceViewFormat = ref ResourceUav
 clear = ResourceViewFormat 8
 store = $view_format, ResourceViewSource, 2
 ResourceViewRegion = ref cs-u6->Region(0,8)
 clear = ResourceViewRegion 9
 store = $view_region, ResourceViewSource, 3
 clear = ResourceRawTyped 11.5 12.5
 store = $raw_float, ResourceRawTyped, 3
endif
$phase = $phase + 1
)ini";
static void write(const std::filesystem::path& path,const char* text){std::ofstream out(path,std::ios::binary);out<<text;check(bool(out),"Write isolated test INI");}
static void ini_gpu(Device& d,const std::filesystem::path& root){
    using namespace frontend;using T=ResourceCopyTargetType;
    std::filesystem::create_directories(root/"Mods");write(root/"Mods/resource.ini",fixture);
    RuntimeConfig config{root,{},root/"settings","resources",root/"Mods"};
    gpu::ResourceBudgets budgets;auto backend=std::make_shared<GpuResourceRuntime>(d.resource,budgets);std::string why;
    auto ini=IniRuntime::load(config,backend,why);if(!ini)throw std::runtime_error("Load original INI: "+why);++checks;
    refuses([&]{backend->begin(d.queue,GpuResourceRuntime::ExecutionSite::InsideRenderPass);},"Active render-pass scope refused");
    check(!ini->present({},1,0,why) && why.find("scope")!=std::string::npos,"GPU commands cannot run without explicit host scope");
    backend->begin(d.queue,GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
    if(!ini->present({},1,0,why))throw std::runtime_error("Original GPU commands: "+why);++checks;
    auto ticket=backend->finish();if(ticket)ticket->wait();ticket.reset();
    const auto values=ini->status().variables;
    const std::pair<const char*,float> expected[]={{"before",6},{"copied",3},{"alias",2},{"description",4},{"branch",1},{"null_value",77},
        {"bounds",0},{"typed0",1.25f},{"typed3",4.5f},{"size",32},{"stride",8},{"phase",1},
        {"fetch",8},{"pool",7},{"limit",1},{"reset",4},{"small",2},{"cb",10},
        {"srv_noop",1},{"srv_cache",1},{"uav_clear",7},{"view_format",8},{"view_region",9},{"raw_float",12.5f}};
    for(auto pair:expected){const auto name=std::string("$\\gpu\\")+pair.first;const auto it=values.find(name);
        if(it==values.end() || it->second!=pair.second){for(const auto& p:values)printf("VARIABLE %s %.9g\n",p.first.c_str(),p.second);throw std::runtime_error("GPU original variable mismatch: "+name);}++checks;}
    auto saved=backend->binding(L'p',T::SHADER_RESOURCE,0);auto desc=backend->binding(L'p',T::SHADER_RESOURCE,1);
    auto typed=backend->binding(L'c',T::SHADER_RESOURCE,2);
    check(saved.buffer && desc.buffer && typed.buffer,"Optimized batch and dynamic stage bindings retain owned resources");
    auto region=backend->binding(L'c',T::SHADER_RESOURCE,3);
    check(region.buffer==typed.buffer && region.offset==16 && region.visible_size==0 && region.view_offset==16 && region.view_size==16,
          "Original view helper interprets its size metadata as an end boundary");
    auto cb=backend->binding(L'c',T::CONSTANT_BUFFER,0);check(cb.offset==16 && cb.visible_size==16,"Constant-buffer reference region retained");
    check(backend->binding(L'c',T::SHADER_RESOURCE,4).buffer==saved.buffer && !backend->binding(L'c',T::SHADER_RESOURCE,5).buffer,
          "Mixed pool variable reset preserves GPU owner; resource reset unbinds");
    auto region_bytes=backend->commands().readback(d.queue,typed.buffer,0,32);
    check(value(region_bytes)==1.25f && value(region_bytes,16)==6.25f && value(region_bytes,28)==6.25f,"GPU clear affects only referenced region");
    auto read=backend->commands().readback(d.queue,saved.buffer,0,32);check(value(read,8)==3,"Copy survives source clear and destination unbind");
    read=backend->commands().readback(d.queue,desc.buffer,0,32);for(size_t i=0;i<32;i+=4)check(value(read,i)==4,"Description-copy resource was cleared on GPU");
    auto next_backend=std::make_shared<GpuResourceRuntime>(d.resource,budgets);auto next=IniRuntime::load(config,next_backend,why);
    check(bool(next),"Second original INI generation loads");next_backend->begin(d.queue,GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
    if(!next->present({},2,0,why))throw std::runtime_error(why);auto next_ticket=next_backend->finish();if(next_ticket)next_ticket->wait();next_ticket.reset();
    check(next_backend->binding(L'p',T::SHADER_RESOURCE,0).buffer!=saved.buffer,"Reload isolates mutable GPU resources");
    write(root/"Mods/resource.ini","[Present]\nthis_is_not_an_original_command = 1\n");
    auto failed=IniRuntime::load(config,std::make_shared<GpuResourceRuntime>(d.resource,budgets),why);
    check(!failed,"Bad candidate INI refuses without replacing a working generation");
    backend->begin(d.queue,GpuResourceRuntime::ExecutionSite::OutsideRenderPass);check(ini->present({},3,0,why),"Working generation survives bad candidate");
    ticket=backend->finish();if(ticket)ticket->wait();ticket.reset();
    next.reset();next_backend.reset();ini.reset();backend.reset();check(saved.buffer->buffer()!=VK_NULL_HANDLE,"Retained binding survives both runtime retirements");
    saved={};desc={};typed={};region={};cb={};check(budgets.resident->used()==0 && budgets.staging->used()==0,"Reload and retained bindings release shared GPU budgets");
}
static void file_and_failures(Device& d,const std::filesystem::path& root){
    using namespace frontend;using T=ResourceCopyTargetType;
    std::filesystem::create_directories(root/"Mods");
    const float bytes[]={9,8,7,6};{std::ofstream out(root/"Mods/file.buf",std::ios::binary);out.write(reinterpret_cast<const char*>(bytes),sizeof(bytes));}
    const std::string base=R"ini(namespace = files
[Constants]
global $result = 0
global $padding = 1
global $size = 0
global $alias_size = 0
[ResourceSource]
type = Buffer
format = R32_FLOAT
filename = file.buf
byte_width = 32
bind_flags = shader_resource unordered_access
[ResourceOut]
bind_flags = shader_resource unordered_access
[Present]
)ini";
    RuntimeConfig config{root,{},root/"settings","files",root/"Mods"};std::string why;
    write(root/"Mods/files.ini",(base+"store = $result, ResourceSource, 2\nstore = $padding, ResourceSource, 7\n$size = ResourceSource->size\nResourceOut = ref ResourceSource\n$alias_size = ResourceOut->size\ncs-t0 = ref ResourceSource\n").c_str());
    auto backend=std::make_shared<GpuResourceRuntime>(d.resource);auto ini=IniRuntime::load(config,backend,why);
    check(bool(ini),"File-backed original resource declaration");backend->begin(d.queue,GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
    if(!ini->present({},1,0,why))throw std::runtime_error(why);auto ticket=backend->finish();if(ticket)ticket->wait();
    check(ini->status().variables.at("$\\files\\result")==7 && ini->status().variables.at("$\\files\\padding")==0,"File bytes and enlarged zero padding reach GPU store");
    check(ini->status().variables.at("$\\files\\size")==16,"Original file size metadata remains distinct from enlarged allocation");
    check(ini->status().variables.at("$\\files\\alias_size")==32,"Plain reference uses original destination-size metadata, not source file size");
    auto held=backend->binding(L'c',T::SHADER_RESOURCE,0);auto output=backend->commands().readback(d.queue,held.buffer,0,32);
    check(!memcmp(output.data(),bytes,sizeof(bytes)),"Original file bytes round trip exactly");
    struct Failure{const char* command;const char* reason;};
    const Failure bad[]={
        {"cs-t0 = ref ResourceSource->Region(40, 16)\n","region outside"},
        {"clear = ResourceSource 2\ndrawindexed = 3,0,0\n","shader execution"},
        {"ResourceOut = mono copy ResourceSource\n","copy option"},
        {"$result = ResourceSource->hashregion(0,4)\n","source-semantic"},
    };
    for(const auto& item:bad){
        write(root/"Mods/files.ini",(base+item.command).c_str());gpu::ResourceBudgets budgets;
        auto rejected=std::make_shared<GpuResourceRuntime>(d.resource,budgets);auto candidate=IniRuntime::load(config,rejected,why);
        if(!candidate)throw std::runtime_error("Negative fixture did not reach execution: "+why);
        rejected->begin(d.queue,GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
        check(!candidate->present({},2,0,why),"Unsupported/invalid GPU command refuses");
        if(why.find(item.reason)==std::string::npos)throw std::runtime_error("Wrong GPU refusal: "+why);++checks;
        refuses([&]{rejected->finish();},"Partial failed scope cannot submit");rejected->discard();
        refuses([&]{rejected->begin(d.queue,GpuResourceRuntime::ExecutionSite::OutsideRenderPass);},"Discarded generation cannot publish stale data");
        candidate.reset();rejected.reset();check(budgets.resident->used()==0 && budgets.staging->used()==0,"Failure path releases unsubmitted resources");
    }
    output=backend->commands().readback(d.queue,held.buffer,0,32);
    check(!memcmp(output.data(),bytes,sizeof(bytes)),"Rejected generations leave old GPU resource contents intact");
}
int wmain(int argc,wchar_t** argv){
    try{check(argc==2,"Fixture path required");{Device device;device.init();low_level(device);const auto root=std::filesystem::absolute(argv[1]);
        ini_gpu(device,root/"inline");file_and_failures(device,root/"files");}
        check(errors==0 && warnings==0,"Clean validation including device shutdown");
        printf("resource_gpu: %u checks passed; validation errors=%u warnings=%u\n",checks,unsigned(errors),unsigned(warnings));return 0;
    }catch(const std::exception& e){printf("FAIL after %u checks: %s\n",checks,e.what());return 1;}
}
