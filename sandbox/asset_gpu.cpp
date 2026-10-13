// Headless texture upload/readback. No game, window, layer injection or settings write.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../frontend/assets/image_upload.h"
#include "../frontend/assets/buffer_upload.h"
#include "xxmi_host/dds_test.cpp"
#include <atomic>
#include <stdexcept>

using namespace efmi::assets;
static int passed=0,failed=0;
static std::atomic<unsigned> validation_errors{0},validation_warnings{0};
static void check(bool ok,const char* text){if(ok)++passed;else{++failed;printf("FAIL: %s\n",text);}}
static void require(VkResult r,const char* where){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(where)+": "+std::to_string(r));}
#define CHECK_VK(call) require((call),#call)
#define INSTANCE_CALLS(X) X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) \
    X(GetPhysicalDeviceFeatures) X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceQueueFamilyProperties) \
    X(GetPhysicalDeviceFormatProperties) X(GetPhysicalDeviceImageFormatProperties) X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE_CALLS(X) EFMI_IMAGE_UPLOAD_FUNCS(X) X(DestroyDevice) X(GetDeviceQueue) X(QueueWaitIdle) \
    X(DeviceWaitIdle) X(CmdCopyImageToBuffer) X(CmdBlitImage) \
    X(CreateShaderModule) X(DestroyShaderModule) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
    X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateGraphicsPipelines) X(DestroyPipeline) \
    X(CreateRenderPass) X(CreateRenderPass2) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
    X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdBindVertexBuffers) \
    X(CmdDraw) X(CmdSetViewport) X(CmdSetScissor) X(CreateSampler) X(DestroySampler) \
    X(CmdCopyBuffer) X(CmdBindIndexBuffer) X(CmdDrawIndexed)
#define X(n) static PFN_vk##n p##n=nullptr;
INSTANCE_CALLS(X) DEVICE_CALLS(X)
#undef X
static VKAPI_ATTR VkBool32 VKAPI_CALL diagnostic(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*)
{
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)++validation_errors;else ++validation_warnings;
    printf("VALIDATION: %s\n",data->pMessage);return VK_FALSE;
}
struct Buffer {VkBuffer buffer{};VkDeviceMemory memory{};void* mapped=nullptr;};
struct Destination {VkImage image{};VkDeviceMemory memory{};};
struct Gpu {
    VkInstance instance{};VkPhysicalDevice physical{};VkDevice device{};VkQueue queue{};
    uint32_t family=0;bool bc=false;VkPhysicalDeviceMemoryProperties memory{};
    VkDebugUtilsMessengerEXT messenger{};PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug=nullptr;
    std::vector<Buffer> buffers;std::vector<Destination> images;std::vector<VkCommandPool> pools;
    UploadDispatch dispatch;
    ~Gpu(){
        if(device){
            pDeviceWaitIdle(device);
            for(auto pool:pools)pDestroyCommandPool(device,pool,nullptr);
            for(auto b:buffers){if(b.mapped)pUnmapMemory(device,b.memory);pDestroyBuffer(device,b.buffer,nullptr);pFreeMemory(device,b.memory,nullptr);}
            for(auto i:images){pDestroyImage(device,i.image,nullptr);pFreeMemory(device,i.memory,nullptr);}
            pDestroyDevice(device,nullptr);
        }
        if(messenger)destroy_debug(instance,messenger,nullptr);
        if(instance)pDestroyInstance(instance,nullptr);
    }
    void init(){
        auto loader=LoadLibraryW(L"vulkan-1.dll");if(!loader)throw std::runtime_error("Vulkan loader missing");
        auto gi=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
        auto create=reinterpret_cast<PFN_vkCreateInstance>(gi(nullptr,"vkCreateInstance"));
        VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="efmi_asset_gpu";app.apiVersion=VK_API_VERSION_1_2;
        const char* layer="VK_LAYER_KHRONOS_validation";const char* ext=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        VkDebugUtilsMessengerCreateInfoEXT debug={VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug.pfnUserCallback=diagnostic;
        VkInstanceCreateInfo info={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;info.pNext=&debug;
        info.enabledLayerCount=info.enabledExtensionCount=1;info.ppEnabledLayerNames=&layer;info.ppEnabledExtensionNames=&ext;
        CHECK_VK(create(&info,nullptr,&instance));
#define X(n) p##n=reinterpret_cast<PFN_vk##n>(gi(instance,"vk" #n));if(!p##n)throw std::runtime_error("vk" #n);
        INSTANCE_CALLS(X)
#undef X
        auto create_debug=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(gi(instance,"vkCreateDebugUtilsMessengerEXT"));
        destroy_debug=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(gi(instance,"vkDestroyDebugUtilsMessengerEXT"));
        CHECK_VK(create_debug(instance,&debug,nullptr,&messenger));
        uint32_t count=0;CHECK_VK(pEnumeratePhysicalDevices(instance,&count,nullptr));std::vector<VkPhysicalDevice> devices(count);
        CHECK_VK(pEnumeratePhysicalDevices(instance,&count,devices.data()));int score=-1;
        for(auto pd:devices){
            VkPhysicalDeviceProperties props{};pGetPhysicalDeviceProperties(pd,&props);
            uint32_t n=0;pGetPhysicalDeviceQueueFamilyProperties(pd,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);
            pGetPhysicalDeviceQueueFamilyProperties(pd,&n,families.data());
            for(uint32_t i=0;i<n;++i)if(families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){
                int rank=props.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?2:1;
                if(rank>score){score=rank;physical=pd;family=i;}break;
            }
        }
        if(!physical)throw std::runtime_error("No graphics device");
        VkPhysicalDeviceFeatures available{},enabled{};pGetPhysicalDeviceFeatures(physical,&available);
        enabled.textureCompressionBC=available.textureCompressionBC;bc=enabled.textureCompressionBC!=0;
        float priority=1;VkDeviceQueueCreateInfo q={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueFamilyIndex=family;q.queueCount=1;q.pQueuePriorities=&priority;
        VkDeviceCreateInfo dc={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&q;dc.pEnabledFeatures=&enabled;
        CHECK_VK(pCreateDevice(physical,&dc,nullptr,&device));
#define X(n) p##n=reinterpret_cast<PFN_vk##n>(pGetDeviceProcAddr(device,"vk" #n));if(!p##n)throw std::runtime_error("vk" #n);
        DEVICE_CALLS(X)
#undef X
#define X(n) dispatch.n=p##n;
        EFMI_IMAGE_UPLOAD_FUNCS(X)
#undef X
        pGetDeviceQueue(device,family,0,&queue);pGetPhysicalDeviceMemoryProperties(physical,&memory);
    }
    VkDeviceMemory allocate(VkMemoryRequirements req,VkMemoryPropertyFlags flags){
        for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags){
            VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=i;
            VkDeviceMemory out{};CHECK_VK(pAllocateMemory(device,&ai,nullptr,&out));return out;
        }
        throw std::runtime_error("Test memory type missing");
    }
    Buffer buffer(size_t bytes,VkBufferUsageFlags usage){
        buffers.emplace_back();auto& b=buffers.back();VkBufferCreateInfo ci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=bytes;ci.usage=usage;
        CHECK_VK(pCreateBuffer(device,&ci,nullptr,&b.buffer));VkMemoryRequirements req{};pGetBufferMemoryRequirements(device,b.buffer,&req);
        b.memory=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);CHECK_VK(pBindBufferMemory(device,b.buffer,b.memory,0));
        CHECK_VK(pMapMemory(device,b.memory,0,bytes,0,&b.mapped));memset(b.mapped,0,bytes);return b;
    }
    Buffer readback(size_t bytes){return buffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);}
    Destination destination(){
        images.emplace_back();auto& i=images.back();VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;
        ci.format=VK_FORMAT_R8G8B8A8_UNORM;ci.extent={1,1,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;
        ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        CHECK_VK(pCreateImage(device,&ci,nullptr,&i.image));VkMemoryRequirements req{};pGetImageMemoryRequirements(device,i.image,&req);
        i.memory=allocate(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);CHECK_VK(pBindImageMemory(device,i.image,i.memory,0));return i;
    }
    VkCommandBuffer begin(){
        VkCommandPoolCreateInfo ci={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};ci.queueFamilyIndex=family;VkCommandPool pool{};
        CHECK_VK(pCreateCommandPool(device,&ci,nullptr,&pool));pools.push_back(pool);
        VkCommandBufferAllocateInfo ai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.commandBufferCount=1;VkCommandBuffer cb{};
        CHECK_VK(pAllocateCommandBuffers(device,&ai,&cb));VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK_VK(pBeginCommandBuffer(cb,&bi));return cb;
    }
    void finish(VkCommandBuffer cb,Buffer b){
        VkBufferMemoryBarrier barrier={VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=b.buffer;barrier.size=VK_WHOLE_SIZE;
        pCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
        CHECK_VK(pEndCommandBuffer(cb));VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cb;
        CHECK_VK(pQueueSubmit(queue,1,&si,VK_NULL_HANDLE));CHECK_VK(pQueueWaitIdle(queue));
    }
};
static void barrier(VkCommandBuffer cb,VkImage image,uint32_t mips,VkImageLayout before,VkImageLayout after,
                    VkAccessFlags source,VkAccessFlags destination)
{
    VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=source;b.dstAccessMask=destination;
    b.oldLayout=before;b.newLayout=after;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,mips,0,1};
    pCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
}
static void run(Gpu& g,std::vector<uint8_t> data,bool color)
{
    DdsImage meta;std::string reason;if(!decode_dds(data.data(),data.size(),meta,reason))throw std::runtime_error(reason);
    VkFormatProperties features{};pGetPhysicalDeviceFormatProperties(g.physical,meta.format,&features);
    VkImageFormatProperties limits{};CHECK_VK(pGetPhysicalDeviceImageFormatProperties(g.physical,meta.format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,0,&limits));
    if(!color){
        ImageUpload disabled(g.device,g.dispatch);
        check(!disabled.prepare(data.data(),data.size(),g.memory,limits,features.optimalTilingFeatures,false,true) && !disabled.view(),"BC must be enabled on the device");
        ImageUpload limited(g.device,g.dispatch);auto limited_caps=limits;limited_caps.maxExtent.width=1;
        check(!limited.prepare(data.data(),data.size(),g.memory,limited_caps,features.optimalTilingFeatures,g.bc,true) && !limited.image(),"device extent refusal publishes nothing");
        auto failing=g.dispatch;
        failing.CreateBuffer=[](VkDevice,const VkBufferCreateInfo*,const VkAllocationCallbacks*,VkBuffer* output)->VkResult {
            *output=VK_NULL_HANDLE;return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        };
        ImageUpload partial(g.device,failing);
        check(!partial.prepare(data.data(),data.size(),g.memory,limits,features.optimalTilingFeatures,g.bc,true) && !partial.view(),
              "staging allocation refusal leaves no published partial image");
    }
    ImageUpload upload(g.device,g.dispatch);
    if(!upload.prepare(data.data(),data.size(),g.memory,limits,features.optimalTilingFeatures,g.bc,true))throw std::runtime_error(upload.reason());
    check(upload.state()==UploadState::Prepared && !upload.view() && upload.poll()==UploadState::Prepared,"prepared texture is not exposed before upload");
    std::vector<uint8_t> expected(data.begin()+meta.levels[0].offset,data.end());
    memset(data.data(),0,data.size());
    check(upload.record(g.family) && upload.state()==UploadState::Recorded && !upload.view(),"recorded texture remains private without queue submission");
    check(upload.submit(g.queue) && !upload.view(),"submitted texture remains private until fence polling");
    check(!upload.submit(g.queue),"one upload ticket cannot submit twice");
    CHECK_VK(pQueueWaitIdle(g.queue));
    check(upload.poll()==UploadState::Ready && upload.view() && upload.image(),"completed fence publishes the texture and releases staging");
    auto b=g.readback(color?4:expected.size());auto cb=g.begin();
    barrier(cb,upload.image(),static_cast<uint32_t>(meta.levels.size()),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    if(color){
        auto destination=g.destination();barrier(cb,destination.image,1,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageBlit blit{};blit.srcSubresource=blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};blit.srcOffsets[1]=blit.dstOffsets[1]={1,1,1};
        pCmdBlitImage(cb,upload.image(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,destination.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
        barrier(cb,destination.image,1,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};
        pCmdCopyImageToBuffer(cb,destination.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,b.buffer,1,&copy);
    }else{
        std::vector<VkBufferImageCopy> copies;
        for(uint32_t i=0;i<meta.levels.size();++i){const auto& level=meta.levels[i];VkBufferImageCopy copy{};
            copy.bufferOffset=level.offset-meta.levels[0].offset;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,i,0,1};copy.imageExtent={level.width,level.height,1};copies.push_back(copy);}
        pCmdCopyImageToBuffer(cb,upload.image(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,b.buffer,static_cast<uint32_t>(copies.size()),copies.data());
    }
    barrier(cb,upload.image(),static_cast<uint32_t>(meta.levels.size()),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
    g.finish(cb,b);
    if(color){auto pixel=static_cast<uint8_t*>(b.mapped);int value=meta.format==VK_FORMAT_R8G8B8A8_SRGB?55:128;
        check(abs(int(pixel[0])-value)<=1 && pixel[0]==pixel[1] && pixel[1]==pixel[2] && pixel[3]==255,"GPU blit observes preserved linear versus sRGB interpretation");
    }else check(!memcmp(b.mapped,expected.data(),expected.size()),"all compressed mip bytes survive upload after CPU source destruction");
}
#include "asset_cache_test.cpp"
#include "asset_buffer_test.cpp"
#include "asset_file_plan_test.cpp"
#include "asset_material_test.cpp"
#include "asset_transaction_test.cpp"
int wmain(int argc,wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc>=8 && !wcscmp(argv[1],L"--repoint-fragment"))return rewrite_fragment_file(argc,argv);
    if(argc>=6 && !wcscmp(argv[1],L"--null-fragment"))return rewrite_fragment_file(argc,argv);
    if(argc==5 && !wcscmp(argv[1],L"--material-profile"))return material_profile_file(argv[2],argv[3],argv[4]);
    try{Gpu g;g.init();
        if(argc==3 && !wcscmp(argv[1],L"--dds")){
            std::ifstream file(std::filesystem::path(argv[2]),std::ios::binary|std::ios::ate);
            if(!file || file.tellg()<0 || file.tellg()>(128ull<<20))throw std::runtime_error("DDS file outside budget");
            std::vector<uint8_t> data(static_cast<size_t>(file.tellg()));file.seekg(0);file.read(reinterpret_cast<char*>(data.data()),data.size());
            if(!file)throw std::runtime_error("DDS file read failed");run(g,std::move(data),false);
        }else if(argc==3 && !wcscmp(argv[1],L"--buffer")){
            std::ifstream file(std::filesystem::path(argv[2]),std::ios::binary|std::ios::ate);
            if(!file || file.tellg()<=0 || file.tellg()>(128ull<<20))throw std::runtime_error("Buffer file outside budget");
            std::vector<uint8_t> bytes(static_cast<size_t>(file.tellg()));file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
            if(!file)throw std::runtime_error("Buffer file read failed");buffer_roundtrip(g,std::move(bytes));
        }else if(argc>=3 && !wcscmp(argv[1],L"--cache-dds")){
            cache_sample(g,argc-2,argv+2);
        }else if(argc>=3 && !wcscmp(argv[1],L"--cache-resources")){
            resource_sample(g,argc-2,argv+2);
        }else if(argc==1){
            run(g,fixture(99),false);auto bc5=fixture(83);run(g,bc5,false);
            for(auto format:{28u,29u}){auto rgba=fixture(format);rgba.resize(152);put(rgba,12,1);put(rgba,16,1);put(rgba,28,1);
                rgba[148]=rgba[149]=rgba[150]=128;rgba[151]=255;run(g,rgba,true);}
            cache_tests(g);
            buffer_tests(g);
            resource_cache_tests(g);
            file_plan_tests(g);
            file_draw_tests(g);
            fragment_tests();material_test(g);
            transaction_fixture::run(g);
        }else throw std::runtime_error("Invalid asset fixture arguments");
    }catch(const std::exception& e){++failed;printf("FAILED: %s\n",e.what());}
    check(!validation_errors && !validation_warnings,"core and synchronization validation remain clean");
    printf("asset_gpu: %d checks passed, %d failed; validation errors=%u warnings=%u\n",passed,failed,validation_errors.load(),validation_warnings.load());
    return failed?1:0;
}
