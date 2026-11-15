// I-01: real pending work on two application queues, with no inter-submit idle.
// The optional third queue belongs only to the readiness scheduler experiment.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <vector>

#define INSTANCE_FUNCS(X) \
    X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) \
    X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceMemoryProperties) \
    X(GetPhysicalDeviceFeatures2) X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE_FUNCS(X) \
    X(DestroyDevice) X(GetDeviceQueue) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) \
    X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) \
    X(EndCommandBuffer) X(CmdFillBuffer) X(CmdCopyBuffer) X(CmdPipelineBarrier) \
    X(CreateSemaphore) X(DestroySemaphore) X(GetSemaphoreCounterValue) X(SignalSemaphore) \
    X(CreateFence) X(DestroyFence) X(GetFenceStatus) X(WaitForFences) X(QueueSubmit)
#define X(n) static PFN_vk##n vk##n;
INSTANCE_FUNCS(X)
DEVICE_FUNCS(X)
#undef X
static std::atomic<unsigned> errors{0}, warnings{0};
static std::atomic<unsigned> binary_canaries{0};
static std::atomic<bool> expect_binary_canary{false};
static unsigned passed=0, failed=0;
static void check(bool good,const char* label)
{
    if(good)++passed;else{++failed;printf("FAIL: %s\n",label);}
}
static void require(VkResult result,const char* label)
{
    if(result!=VK_SUCCESS){printf("FAILED: %s returned %d\n",label,result);throw std::runtime_error(label);}
}
#define VK(call) require((call),#call)
static VKAPI_ATTR VkBool32 VKAPI_CALL message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*)
{
    const bool error=(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)!=0;
    if(error && expect_binary_canary && data->pMessageIdName &&
       !strcmp(data->pMessageIdName,"VUID-vkQueueSubmit-pWaitSemaphores-03238")){
        ++binary_canaries;printf("EXPECTED BINARY CANARY: %s\n",data->pMessage);return VK_TRUE;
    }
    if(!error && data->pMessageIdName && !strcmp(data->pMessageIdName,"Loader Message") &&
       (strstr(data->pMessage,"forced disabled because name matches filter of env var 'VK_LOADER_LAYERS_DISABLE'") ||
        !strcmp(data->pMessage,"env var 'VK_INSTANCE_LAYERS' defined and adding layers \"VK_LAYER_EFMI_probe\"")))return VK_FALSE;
    if(error)++errors;else ++warnings;
    printf("VALIDATION %s [%s]: %s\n",error?"ERROR":"WARNING",data->pMessageIdName,data->pMessage);
    return error?VK_TRUE:VK_FALSE;
}
struct Buffer {VkBuffer handle;VkDeviceMemory memory;uint32_t* data;};
struct Point {VkSemaphore semaphore;uint64_t value;};
struct Fixture {
    VkInstance instance{};VkPhysicalDevice physical{};VkDevice device{};
    VkDebugUtilsMessengerEXT messenger{};PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger{};
    VkPhysicalDeviceMemoryProperties memory{};uint32_t family{};VkQueue queues[3]{};VkCommandPool pool{};
    std::vector<Buffer> buffers;std::vector<VkSemaphore> semaphores;std::vector<VkFence> fences;

    void init()
    {
        auto library=LoadLibraryW(L"vulkan-1.dll");if(!library)throw std::runtime_error("Vulkan loader");
        auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(library,"vkGetInstanceProcAddr"));
        auto create=reinterpret_cast<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE,"vkCreateInstance"));
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="efmi_vk_queue_overlap";app.apiVersion=VK_API_VERSION_1_2;
        VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug.pfnUserCallback=message;
        const char* layer="VK_LAYER_KHRONOS_validation";const char* extension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;ci.pNext=&debug;
        ci.enabledLayerCount=ci.enabledExtensionCount=1;ci.ppEnabledLayerNames=&layer;ci.ppEnabledExtensionNames=&extension;
        VK(create(&ci,nullptr,&instance));
#define X(n) vk##n=reinterpret_cast<PFN_vk##n>(gipa(instance,"vk" #n));if(!vk##n)throw std::runtime_error("vk" #n);
        INSTANCE_FUNCS(X)
#undef X
        auto create_debug=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(gipa(instance,"vkCreateDebugUtilsMessengerEXT"));
        destroy_messenger=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(gipa(instance,"vkDestroyDebugUtilsMessengerEXT"));
        VK(create_debug(instance,&debug,nullptr,&messenger));
        uint32_t count=0;VK(vkEnumeratePhysicalDevices(instance,&count,nullptr));std::vector<VkPhysicalDevice> devices(count);
        VK(vkEnumeratePhysicalDevices(instance,&count,devices.data()));int best=-1;
        for(auto candidate:devices){
            VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(candidate,&properties);
            if(properties.apiVersion<VK_API_VERSION_1_2)continue;
            VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&timeline;
            vkGetPhysicalDeviceFeatures2(candidate,&features);if(!timeline.timelineSemaphore)continue;
            uint32_t n=0;vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,families.data());
            for(uint32_t i=0;i<n;++i)if((families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)&&families[i].queueCount>=3){
                const int score=properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?2:1;
                if(score>best){best=score;physical=candidate;family=i;}break;
            }
        }
        if(!physical)throw std::runtime_error("three graphics queues and timeline semaphores required");
        VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(physical,&properties);
        printf("device: %s; two application queues and one private experiment queue\n",properties.deviceName);
        vkGetPhysicalDeviceMemoryProperties(physical,&memory);
        const float priorities[3]{1,1,1};VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex=family;queue.queueCount=3;queue.pQueuePriorities=priorities;
        VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};timeline.timelineSemaphore=VK_TRUE;
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.pNext=&timeline;dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&queue;
        VK(vkCreateDevice(physical,&dc,nullptr,&device));
#define X(n) vk##n=reinterpret_cast<PFN_vk##n>(vkGetDeviceProcAddr(device,"vk" #n));if(!vk##n)throw std::runtime_error("vk" #n);
        DEVICE_FUNCS(X)
#undef X
        for(uint32_t i=0;i<3;++i)vkGetDeviceQueue(device,family,i,&queues[i]);
        check(queues[0]!=queues[1]&&queues[1]!=queues[2]&&queues[0]!=queues[2],"distinct queue handles");
        VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pc.queueFamilyIndex=family;
        VK(vkCreateCommandPool(device,&pc,nullptr,&pool));
    }
    Buffer buffer()
    {
        Buffer b{};VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=256;
        bc.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;bc.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        VK(vkCreateBuffer(device,&bc,nullptr,&b.handle));VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(device,b.handle,&requirements);
        uint32_t type=memory.memoryTypeCount;const auto flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((requirements.memoryTypeBits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags){type=i;break;}
        if(type==memory.memoryTypeCount)throw std::runtime_error("coherent memory required");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;
        VK(vkAllocateMemory(device,&allocation,nullptr,&b.memory));VK(vkBindBufferMemory(device,b.handle,b.memory,0));
        VK(vkMapMemory(device,b.memory,0,256,0,reinterpret_cast<void**>(&b.data)));memset(b.data,0,256);
        buffers.push_back(b);return b;
    }
    VkSemaphore timeline(bool binary=false)
    {
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};type.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};info.pNext=binary?nullptr:&type;VkSemaphore semaphore{};
        VK(vkCreateSemaphore(device,&info,nullptr,&semaphore));semaphores.push_back(semaphore);return semaphore;
    }
    VkFence fence()
    {
        VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};
        VK(vkCreateFence(device,&info,nullptr,&fence));fences.push_back(fence);return fence;
    }
    template<class F>VkCommandBuffer commands(F record)
    {
        VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};info.commandPool=pool;
        info.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;info.commandBufferCount=1;VkCommandBuffer command{};
        VK(vkAllocateCommandBuffers(device,&info,&command));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;VK(vkBeginCommandBuffer(command,&begin));record(command);
        VK(vkEndCommandBuffer(command));return command;
    }
    VkCommandBuffer fill(Buffer b,uint32_t value)
    {
        return commands([&](VkCommandBuffer command){vkCmdFillBuffer(command,b.handle,0,256,value);});
    }
    VkCommandBuffer copy(Buffer source,Buffer destination)
    {
        return commands([&](VkCommandBuffer command){
            VkBufferCopy range{0,0,256};vkCmdCopyBuffer(command,source.handle,destination.handle,1,&range);
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        });
    }
    VkResult submit(unsigned queue,VkCommandBuffer command,std::initializer_list<Point> waits,
                std::initializer_list<Point> signals,VkFence fence=VK_NULL_HANDLE,bool allow_refusal=false)
    {
        std::vector<VkSemaphore> wait_handles,signal_handles;std::vector<uint64_t> wait_values,signal_values;
        for(auto point:waits){wait_handles.push_back(point.semaphore);wait_values.push_back(point.value);}
        for(auto point:signals){signal_handles.push_back(point.semaphore);signal_values.push_back(point.value);}
        std::vector<VkPipelineStageFlags> stages(waits.size(),VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
        timeline.waitSemaphoreValueCount=uint32_t(waits.size());timeline.pWaitSemaphoreValues=wait_values.data();
        timeline.signalSemaphoreValueCount=uint32_t(signals.size());timeline.pSignalSemaphoreValues=signal_values.data();
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};info.pNext=&timeline;info.commandBufferCount=1;info.pCommandBuffers=&command;
        info.waitSemaphoreCount=uint32_t(waits.size());info.pWaitSemaphores=wait_handles.data();info.pWaitDstStageMask=stages.data();
        info.signalSemaphoreCount=uint32_t(signals.size());info.pSignalSemaphores=signal_handles.data();
        const auto result=vkQueueSubmit(queues[queue],1,&info,fence);
        if(!allow_refusal)require(result,"vkQueueSubmit");return result;
    }
    uint64_t value(VkSemaphore semaphore){uint64_t value=0;VK(vkGetSemaphoreCounterValue(device,semaphore,&value));return value;}
    void wait_value(VkSemaphore semaphore,uint64_t target)
    {
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(value(semaphore)<target){if(std::chrono::steady_clock::now()>=end)throw std::runtime_error("readiness timeout");Sleep(1);}
    }
    void completed(VkFence fence){VK(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ull));}
    // Called only after every submitted fence completes. An exceptional pending
    // graph is bounded by the runner's process timeout, never an unbounded idle.
    void close()
    {
        vkDestroyCommandPool(device,pool,nullptr);
        for(auto fence:fences)vkDestroyFence(device,fence,nullptr);
        for(auto semaphore:semaphores)vkDestroySemaphore(device,semaphore,nullptr);
        for(auto b:buffers){vkUnmapMemory(device,b.memory);vkDestroyBuffer(device,b.handle,nullptr);vkFreeMemory(device,b.memory,nullptr);}
        vkDestroyDevice(device,nullptr);destroy_messenger(instance,messenger,nullptr);vkDestroyInstance(instance,nullptr);
    }
};

static constexpr uint32_t pattern=0x71a24b9du;
static bool all(Buffer buffer,uint32_t value){return std::all_of(buffer.data,buffer.data+64,[&](uint32_t word){return word==value;});}
static void future_wait(Fixture& g,bool global_chain)
{
    const auto source=g.buffer(),readback=g.buffer();const auto app=g.timeline(),serial=g.timeline();
    const auto consumer=g.fence(),producer=g.fence();
    g.submit(0,g.copy(source,readback),{{app,1}},{{serial,1}},consumer);
    check(vkGetFenceStatus(g.device,consumer)==VK_NOT_READY,"consumer is pending before its producer is submitted");
    if(global_chain)g.submit(1,g.fill(source,pattern),{{serial,1}},{{app,2},{serial,2}},producer);
    else g.submit(1,g.fill(source,pattern),{},{{app,2}},producer);
    if(global_chain){
        check(vkWaitForFences(g.device,1,&consumer,VK_TRUE,100000000ull)==VK_TIMEOUT,"global completion chain stalls a valid future-wait graph");
        check(g.value(app)==0&&g.value(serial)==0,"neither side of the introduced cycle makes progress");
        // 1 is below the pending GPU signal 2: legal host recovery, never a GPU reset.
        VkSemaphoreSignalInfo release{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};release.semaphore=app;release.value=1;
        VK(vkSignalSemaphore(g.device,&release));
    }
    g.completed(consumer);g.completed(producer);
    check(all(readback,global_chain?0:pattern),global_chain?"recovered cycle consumed the initial data":"future producer bytes reach every consumer word");
    check(g.value(app)==2,"producer eventually completed its application signal");
    printf("%s: pending consumer observed; readback=%08x\n",global_chain?"global-chain counterexample":"future wait",readback.data[0]);
}
static void readiness_gate(Fixture& g)
{
    const auto source=g.buffer(),readback=g.buffer();const auto app=g.timeline();
    const auto ready0=g.timeline(),ready1=g.timeline(),gate0=g.timeline(),gate1=g.timeline(),done0=g.timeline(),done1=g.timeline();
    const auto consumer=g.fence(),producer=g.fence(),release0=g.fence(),release1=g.fence();
    // Real marker commands join wait and signal scopes in readiness/release batches.
    g.submit(0,g.fill(g.buffer(),1),{{app,1}},{{ready0,1}});
    g.submit(0,g.copy(source,readback),{{gate0,1}},{{done0,1}},consumer);
    g.submit(1,g.fill(g.buffer(),1),{},{{ready1,1}});
    g.submit(1,g.fill(source,pattern),{{gate1,1}},{{app,2},{done1,1}},producer);
    g.wait_value(ready1,1);
    check(g.value(ready0)==0&&g.value(done0)==0&&g.value(done1)==0,"both work batches are pending while only the producer is ready");
    // CPU readiness chooses work; GPU waits carry memory dependencies. A host
    // counter query alone is not the publication edge used by the consumer.
    g.submit(2,g.fill(g.buffer(),1),{{ready1,1}},{{gate1,1}},release1);
    g.wait_value(done1,1);g.wait_value(ready0,1);
    check(vkGetFenceStatus(g.device,consumer)==VK_NOT_READY,"producer finishes without prematurely releasing the consumer gate");
    g.submit(2,g.fill(g.buffer(),1),{{ready0,1},{done1,1}},{{gate0,1}},release0);
    g.completed(consumer);g.completed(producer);g.completed(release0);g.completed(release1);
    check(all(readback,pattern),"readiness-selected GPU dependency chain publishes all producer bytes");
    check(g.value(done0)==1&&g.value(done1)==1,"both reordered work batches complete");
    printf("readiness gates: producer released before earlier consumer; readback=%08x\n",readback.data[0]);
}
static void binary_boundary(Fixture& g)
{
    const auto source=g.buffer(),readback=g.buffer();const auto gate=g.timeline(),binary=g.timeline(true);
    const auto producer=g.fence(),consumer=g.fence(),release=g.fence();
    const auto copy=g.copy(source,readback);
    g.submit(0,g.fill(source,pattern),{{gate,1}},{{binary,0}},producer);
    // The binary signal is submitted, but its timeline gate signal is not. Core
    // validation must abort this call before it reaches the driver.
    expect_binary_canary=true;
    const auto rejected=g.submit(1,copy,{{binary,0}},{},consumer,true);
    expect_binary_canary=false;
    check(rejected==VK_ERROR_VALIDATION_FAILED_EXT&&binary_canaries==1,
          "binary wait refuses a producer whose gate signal is not yet submitted");
    g.submit(2,g.fill(g.buffer(),1),{},{{gate,1}},release);
    if(rejected==VK_ERROR_VALIDATION_FAILED_EXT)g.submit(1,copy,{{binary,0}},{},consumer);
    else if(rejected!=VK_SUCCESS)require(rejected,"binary canary result");
    g.completed(producer);g.completed(consumer);g.completed(release);
    check(all(readback,pattern),"binary consumer succeeds after the producer gate signal is submitted");
    printf("binary forwarding: missing gate detected; complete dependency chain accepted\n");
}
int wmain(int argc,wchar_t** argv)
{
    setvbuf(stdout,nullptr,_IONBF,0);
    for(int i=1;i<argc;++i)if(!wcscmp(argv[i],L"--inject")&&i+1<argc)if(!LoadLibraryW(argv[++i]))return 2;
    try{Fixture g;g.init();future_wait(g,false);future_wait(g,true);readiness_gate(g);binary_boundary(g);g.close();}
    catch(const std::exception& e){printf("FAILED: %s\n",e.what());return 2;}
    printf("validation: errors=%u warnings=%u\n",errors.load(),warnings.load());
    check(errors==0&&warnings==0,"core and synchronization validation clean");
    printf("queue_overlap: %u/%u checks passed\n",passed,passed+failed);return failed?1:0;
}
