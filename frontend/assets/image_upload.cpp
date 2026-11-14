#include "image_upload.h"
#include <algorithm>
#include <cstring>

namespace efmi::assets {
namespace {
struct PayloadRegion { size_t source=0,offset=0,bytes=0;uint32_t mip=0,layer=0; };
struct Payload { size_t source_bytes=0,bytes=0;std::vector<PayloadRegion> regions; };
Payload payload_plan(const DdsImage& image,uint32_t requested_skip,bool all_layers)
{
    Payload result;if(image.levels.empty())return result;
    const auto skipped=ImageUpload::levels_skipped(image,requested_skip);
    const size_t base=image.levels[skipped].offset,end=image.levels.back().offset+image.levels.back().bytes;
    const size_t stride=end-image.levels.front().offset;
    const uint32_t layers=all_layers?image.layers:1;
    size_t padding=0;
    for(uint32_t layer=0;layer<layers;++layer)for(uint32_t mip=skipped;mip<image.levels.size();++mip){
        const auto& level=image.levels[mip];const size_t source=layer*stride+level.offset-base;
        padding+=(4-(source+padding)%4)%4;
        result.regions.push_back({source,source+padding,level.bytes,mip,layer});
    }
    result.source_bytes=layers?size_t(layers-1)*stride+end-base:0;
    result.bytes=result.source_bytes+padding;return result;
}
}
size_t ImageUpload::staging_bytes(const DdsImage& image,uint32_t skip,bool all_layers)
{
    return payload_plan(image,skip,all_layers).bytes;
}
void ImageUpload::pad_staging(const DdsImage& image,uint32_t skip,bool all_layers,uint8_t* mapped)
{
    const auto plan=payload_plan(image,skip,all_layers);
    // Every destination is at or above its source. Moving backwards preserves unread regions.
    for(auto i=plan.regions.rbegin();i!=plan.regions.rend();++i)
        if(i->source!=i->offset)std::memmove(mapped+i->offset,mapped+i->source,i->bytes);
}

ImageUpload::ImageUpload(VkDevice device,const UploadDispatch& dispatch,
                         std::shared_ptr<AllocationBudget> resident,std::shared_ptr<AllocationBudget> staging)
    :device_(device),d_(dispatch),resident_budget_(std::move(resident)),staging_budget_(std::move(staging)) {}
ImageUpload::~ImageUpload()
{
    // Cancellation/device shutdown only. The normal readiness path is poll().
    if(in_flight_)d_.WaitForFences(device_,1,&fence_,VK_TRUE,UINT64_MAX);
    release_staging();
    if(view_)d_.DestroyImageView(device_,view_,nullptr);
    if(image_)d_.DestroyImage(device_,image_,nullptr);
    if(image_memory_)d_.FreeMemory(device_,image_memory_,nullptr);
    if(resident_budget_)resident_budget_->release(resident_charged_);
}
bool ImageUpload::result(VkResult value,const char* operation)
{
    if(value==VK_SUCCESS)return true;
    reason_=std::string(operation)+": "+std::to_string(value);state_=UploadState::Failed;return false;
}
VkDeviceMemory ImageUpload::allocate(const VkMemoryRequirements& requirements,VkMemoryPropertyFlags flags,
                                     const std::shared_ptr<AllocationBudget>& budget,uint64_t& charged,float priority)
{
    if(requirements.size>(160ull<<20)){reason_="Image allocation budget exceeded";return VK_NULL_HANDLE;}
    for(uint32_t i=0;i<memory_.memoryTypeCount;++i)
        if((requirements.memoryTypeBits&(1u<<i)) && (memory_.memoryTypes[i].propertyFlags&flags)==flags){
            if(budget && !budget->acquire(requirements.size)){reason_="Aggregate image memory budget exceeded";return VK_NULL_HANDLE;}
            VkMemoryAllocateInfo info={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            info.allocationSize=requirements.size;info.memoryTypeIndex=i;VkDeviceMemory memory=VK_NULL_HANDLE;
            VkMemoryPriorityAllocateInfoEXT priority_info={VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT};
            if(priority>=0.0f){priority_info.priority=priority;info.pNext=&priority_info;}
            if(result(d_.AllocateMemory(device_,&info,nullptr,&memory),"allocate image resource memory")){
                charged=requirements.size;return memory;
            }
            if(budget)budget->release(requirements.size);
            return VK_NULL_HANDLE;
        }
    reason_="Image resource memory type unavailable";return VK_NULL_HANDLE;
}
void ImageUpload::release_staging()
{
    if(pool_)d_.DestroyCommandPool(device_,pool_,nullptr);
    if(fence_)d_.DestroyFence(device_,fence_,nullptr);
    if(stage_)d_.DestroyBuffer(device_,stage_,nullptr);
    if(stage_memory_)d_.FreeMemory(device_,stage_memory_,nullptr);
    if(staging_budget_)staging_budget_->release(staging_charged_);
    staging_charged_=0;command_=VK_NULL_HANDLE;
    pool_=VK_NULL_HANDLE;fence_=VK_NULL_HANDLE;stage_=VK_NULL_HANDLE;stage_memory_=VK_NULL_HANDLE;
}
bool ImageUpload::check_format(VkFormatFeatureFlags features,const VkImageFormatProperties& limits,bool bc_enabled,bool readback)
{
    if(all_layers_ && (!decoded_.layers || (decoded_.cube && (decoded_.width!=decoded_.height || decoded_.layers%6 ||
        (decoded_.layers>6 && !cube_arrays_))))){
        reason_="Image layers or enabled cube-array feature are incompatible";state_=UploadState::Failed;return false;
    }
    const auto required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|
                        (readback?VK_FORMAT_FEATURE_TRANSFER_SRC_BIT:0);
    const bool compressed=decoded_.format>=VK_FORMAT_BC1_RGB_UNORM_BLOCK && decoded_.format<=VK_FORMAT_BC7_SRGB_BLOCK;
    if((features&required)!=required || (compressed&&!bc_enabled) || decoded_.width>limits.maxExtent.width ||
       decoded_.height>limits.maxExtent.height || decoded_.levels.size()>limits.maxMipLevels ||
       limits.maxArrayLayers<layers() || !(limits.sampleCounts&VK_SAMPLE_COUNT_1_BIT)){
        reason_="Image format, enabled features or device limits are incompatible";state_=UploadState::Failed;return false;
    }
    return true;
}
uint32_t ImageUpload::levels_skipped(const DdsImage& image,uint32_t skip_levels)
{
    if(image.levels.empty())return 0;
    uint32_t skipped=std::min<uint32_t>(skip_levels,static_cast<uint32_t>(image.levels.size())-1);
    while(skipped && (image.levels[skipped].width<4 || image.levels[skipped].height<4))--skipped;
    return skipped;
}
bool ImageUpload::create_image(const VkImageFormatProperties& limits,bool readback,uint32_t skip_levels,float priority)
{
    // The DDS's levels from skipped_ on become the image's levels 0..n: the texture at a quarter of
    // its memory per level left out.
    skipped_=levels_skipped(decoded_,skip_levels);
    const auto& base=decoded_.levels[skipped_];
    VkImageCreateInfo image={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType=VK_IMAGE_TYPE_2D;image.format=decoded_.format;image.extent={base.width,base.height,1};
    image.mipLevels=static_cast<uint32_t>(decoded_.levels.size())-skipped_;image.arrayLayers=layers();
    image.flags=all_layers_ && decoded_.cube?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0;
    image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
    image.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(readback?VK_IMAGE_USAGE_TRANSFER_SRC_BIT:0);
    if(shared_){image.sharingMode=VK_SHARING_MODE_CONCURRENT;image.queueFamilyIndexCount=2;image.pQueueFamilyIndices=families_;}
    if(!result(d_.CreateImage(device_,&image,nullptr,&image_),"create asset image"))return false;
    VkMemoryRequirements req{};d_.GetImageMemoryRequirements(device_,image_,&req);
    if(req.size>limits.maxResourceSize){reason_="Image exceeds device resource limit";state_=UploadState::Failed;return false;}
    image_memory_=allocate(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,resident_budget_,resident_charged_,priority);
    if(!image_memory_){state_=UploadState::Failed;return false;}
    if(!result(d_.BindImageMemory(device_,image_,image_memory_,0),"bind asset image"))return false;
    VkImageViewCreateInfo view={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image=image_;view.viewType=view_type();view.format=decoded_.format;
    view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,image.mipLevels,0,layers()};
    return result(d_.CreateImageView(device_,&view,nullptr,&view_),"create asset image view");
}
bool ImageUpload::prepare_image(const DdsImage& decoded,const VkPhysicalDeviceMemoryProperties& memory,
                                const VkImageFormatProperties& limits,VkFormatFeatureFlags features,
                                bool bc_enabled,uint32_t skip_levels,float priority,bool readback)
{
    if(state_!=UploadState::Empty){reason_="Image ticket cannot be prepared again";return false;}
#define X(name) if(!d_.name){reason_="Missing image upload entry: vk" #name;state_=UploadState::Failed;return false;}
    EFMI_IMAGE_UPLOAD_FUNCS(X)
#undef X
    if(decoded.levels.empty()){reason_="DDS image has no levels";state_=UploadState::Failed;return false;}
    decoded_=decoded;memory_=memory;
    if(!check_format(features,limits,bc_enabled,readback) || !create_image(limits,readback,skip_levels,priority))return false;
    state_=UploadState::Prepared;return true;
}
bool ImageUpload::prepare(const uint8_t* data,size_t size,const VkPhysicalDeviceMemoryProperties& memory,
                          const VkImageFormatProperties& limits,VkFormatFeatureFlags features,
                          bool bc_enabled,bool readback,uint32_t skip_levels,float priority)
{
    if(state_!=UploadState::Empty){reason_="Image ticket cannot be prepared again";return false;}
    DdsImage decoded;
    if(!decode_dds(data,size,decoded,reason_)){state_=UploadState::Failed;return false;}
    return prepare_decoded(decoded,data,size,memory,limits,features,bc_enabled,readback,skip_levels,priority);
}
bool ImageUpload::prepare_decoded(const DdsImage& decoded,const uint8_t* data,size_t size,const VkPhysicalDeviceMemoryProperties& memory,
                                  const VkImageFormatProperties& limits,VkFormatFeatureFlags features,
                                  bool bc_enabled,bool readback,uint32_t skip_levels,float priority)
{
    if(state_!=UploadState::Empty){reason_="Image ticket cannot be prepared again";return false;}
#define X(name) if(!d_.name){reason_="Missing image upload entry: vk" #name;state_=UploadState::Failed;return false;}
    EFMI_IMAGE_UPLOAD_FUNCS(X)
#undef X
    if(decoded.levels.empty() || decoded.levels.back().offset+decoded.levels.back().bytes>size){
        reason_="Decoded image levels outside its bytes";state_=UploadState::Failed;return false;
    }
    decoded_=decoded;memory_=memory;
    const uint64_t span=decoded.levels.back().offset+decoded.levels.back().bytes-decoded.levels.front().offset;
    if(all_layers_ && (!span || !decoded.layers || decoded.levels.front().offset>size ||
        uint64_t(decoded.layers)>(size-decoded.levels.front().offset)/span)){
        reason_="Decoded image layers outside its bytes";state_=UploadState::Failed;return false;
    }
    if(!check_format(features,limits,bc_enabled,readback) || !create_image(limits,readback,skip_levels,priority))return false;
    const auto start=base_offset();const auto payload=payload_plan(decoded_,skipped_,all_layers_);
    VkBufferCreateInfo buffer={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size=payload.bytes;buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if(!result(d_.CreateBuffer(device_,&buffer,nullptr,&stage_),"create image staging buffer"))return false;
    VkMemoryRequirements req{};d_.GetBufferMemoryRequirements(device_,stage_,&req);
    stage_memory_=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,staging_budget_,staging_charged_);
    if(!stage_memory_){state_=UploadState::Failed;return false;}
    if(!result(d_.BindBufferMemory(device_,stage_,stage_memory_,0),"bind image staging memory"))return false;
    void* mapped=nullptr;
    if(!result(d_.MapMemory(device_,stage_memory_,0,buffer.size,0,&mapped),"map image staging memory"))return false;
    std::memcpy(mapped,data+start,payload.source_bytes);
    pad_staging(decoded_,skipped_,all_layers_,static_cast<uint8_t*>(mapped));d_.UnmapMemory(device_,stage_memory_);
    state_=UploadState::Prepared;return true;
}
bool ImageUpload::record_copy(VkCommandBuffer cmd,VkBuffer stage,VkDeviceSize offset)
{
    if(state_!=UploadState::Prepared){reason_="Image ticket is not ready for recording";return false;}
    const uint32_t levels=static_cast<uint32_t>(decoded_.levels.size())-skipped_;
    VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=image_;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,levels,0,layers()};
    d_.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    std::vector<VkBufferImageCopy> copies;
    const auto payload=payload_plan(decoded_,skipped_,all_layers_);
    for(const auto& region:payload.regions){
        const auto& level=decoded_.levels[region.mip];VkBufferImageCopy copy{};
        copy.bufferOffset=offset+region.offset;
        copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,region.mip-skipped_,region.layer,1};copy.imageExtent={level.width,level.height,1};copies.push_back(copy);
    }
    d_.CmdCopyBufferToImage(cmd,stage,image_,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,static_cast<uint32_t>(copies.size()),copies.data());
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    d_.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    state_=UploadState::Recorded;return true;
}
void ImageUpload::loaded(bool ok,const std::string& reason)
{
    if(ok && state_==UploadState::Recorded){state_=UploadState::Ready;return;}
    reason_=reason.empty()?std::string("Image upload not completed"):reason;state_=UploadState::Failed;
}
bool ImageUpload::record(uint32_t graphics_family)
{
    if(state_!=UploadState::Prepared){reason_="Image ticket is not ready for recording";return false;}
    VkCommandPoolCreateInfo pool={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;pool.queueFamilyIndex=graphics_family;
    if(!result(d_.CreateCommandPool(device_,&pool,nullptr,&pool_),"create image upload pool"))return false;
    VkCommandBufferAllocateInfo ai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool=pool_;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    if(!result(d_.AllocateCommandBuffers(device_,&ai,&command_),"allocate image upload command"))return false;
    if(d_.SetDeviceLoaderData && !result(d_.SetDeviceLoaderData(device_,command_),"initialize image upload dispatch"))return false;
    VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if(!result(d_.BeginCommandBuffer(command_,&begin),"begin image upload"))return false;
    if(!record_copy(command_,stage_,0))return false;
    if(!result(d_.EndCommandBuffer(command_),"end image upload"))return false;
    VkFenceCreateInfo fi={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if(!result(d_.CreateFence(device_,&fi,nullptr,&fence_),"create image upload fence"))return false;
    state_=UploadState::Recorded;return true;
}
bool ImageUpload::submit(VkQueue queue,VkSemaphore timeline,uint64_t value)
{
    if(state_!=UploadState::Recorded || !queue){reason_="Image ticket is not ready for submission";return false;}
    VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command_;
    VkTimelineSemaphoreSubmitInfo values={VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    if(timeline){values.signalSemaphoreValueCount=1;values.pSignalSemaphoreValues=&value;submit.pNext=&values;
        submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&timeline;}
    if(!result(d_.QueueSubmit(queue,1,&submit,fence_),"submit image upload"))return false;
    in_flight_=true;state_=UploadState::Submitted;return true;
}
UploadState ImageUpload::poll()
{
    if(state_!=UploadState::Submitted)return state_;
    const auto status=d_.GetFenceStatus(device_,fence_);
    if(status==VK_NOT_READY)return state_;
    if(result(status,"poll image upload fence")){in_flight_=false;release_staging();state_=UploadState::Ready;}
    return state_;
}
}
