#include "image_upload.h"
#include <cstring>

namespace efmi::assets {
ImageUpload::ImageUpload(VkDevice device,const UploadDispatch& dispatch):device_(device),d_(dispatch) {}
ImageUpload::~ImageUpload()
{
    // Cancellation/device shutdown only. The normal readiness path is poll().
    if(in_flight_)d_.WaitForFences(device_,1,&fence_,VK_TRUE,UINT64_MAX);
    release_staging();
    if(view_)d_.DestroyImageView(device_,view_,nullptr);
    if(image_)d_.DestroyImage(device_,image_,nullptr);
    if(image_memory_)d_.FreeMemory(device_,image_memory_,nullptr);
}
bool ImageUpload::result(VkResult value,const char* operation)
{
    if(value==VK_SUCCESS)return true;
    reason_=std::string(operation)+": "+std::to_string(value);state_=UploadState::Failed;return false;
}
VkDeviceMemory ImageUpload::allocate(const VkMemoryRequirements& requirements,VkMemoryPropertyFlags flags)
{
    if(requirements.size>(160ull<<20)){reason_="Image allocation budget exceeded";return VK_NULL_HANDLE;}
    for(uint32_t i=0;i<memory_.memoryTypeCount;++i)
        if((requirements.memoryTypeBits&(1u<<i)) && (memory_.memoryTypes[i].propertyFlags&flags)==flags){
            VkMemoryAllocateInfo info={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            info.allocationSize=requirements.size;info.memoryTypeIndex=i;VkDeviceMemory memory=VK_NULL_HANDLE;
            if(result(d_.AllocateMemory(device_,&info,nullptr,&memory),"allocate image resource memory"))return memory;
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
    pool_=VK_NULL_HANDLE;fence_=VK_NULL_HANDLE;stage_=VK_NULL_HANDLE;stage_memory_=VK_NULL_HANDLE;
}
bool ImageUpload::prepare(const uint8_t* data,size_t size,const VkPhysicalDeviceMemoryProperties& memory,
                          const VkImageFormatProperties& limits,VkFormatFeatureFlags features,
                          bool bc_enabled,bool readback)
{
    if(state_!=UploadState::Empty){reason_="Image ticket cannot be prepared again";return false;}
#define X(name) if(!d_.name){reason_="Missing image upload entry: vk" #name;state_=UploadState::Failed;return false;}
    EFMI_IMAGE_UPLOAD_FUNCS(X)
#undef X
    if(!decode_dds(data,size,decoded_,reason_)){state_=UploadState::Failed;return false;}
    const auto required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|
                        (readback?VK_FORMAT_FEATURE_TRANSFER_SRC_BIT:0);
    const bool compressed=decoded_.format>=VK_FORMAT_BC1_RGB_UNORM_BLOCK && decoded_.format<=VK_FORMAT_BC7_SRGB_BLOCK;
    if((features&required)!=required || (compressed&&!bc_enabled) || decoded_.width>limits.maxExtent.width ||
       decoded_.height>limits.maxExtent.height || decoded_.levels.size()>limits.maxMipLevels ||
       limits.maxArrayLayers<1 || !(limits.sampleCounts&VK_SAMPLE_COUNT_1_BIT)){
        reason_="Image format, enabled features or device limits are incompatible";state_=UploadState::Failed;return false;
    }
    memory_=memory;
    VkImageCreateInfo image={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType=VK_IMAGE_TYPE_2D;image.format=decoded_.format;image.extent={decoded_.width,decoded_.height,1};
    image.mipLevels=static_cast<uint32_t>(decoded_.levels.size());image.arrayLayers=1;
    image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
    image.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(readback?VK_IMAGE_USAGE_TRANSFER_SRC_BIT:0);
    if(!result(d_.CreateImage(device_,&image,nullptr,&image_),"create asset image"))return false;
    VkMemoryRequirements req{};d_.GetImageMemoryRequirements(device_,image_,&req);
    if(req.size>limits.maxResourceSize){reason_="Image exceeds device resource limit";state_=UploadState::Failed;return false;}
    image_memory_=allocate(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if(!image_memory_){state_=UploadState::Failed;return false;}
    if(!result(d_.BindImageMemory(device_,image_,image_memory_,0),"bind asset image"))return false;
    VkImageViewCreateInfo view={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image=image_;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=decoded_.format;
    view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,image.mipLevels,0,1};
    if(!result(d_.CreateImageView(device_,&view,nullptr,&view_),"create asset image view"))return false;
    const auto start=decoded_.levels[0].offset;
    VkBufferCreateInfo buffer={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size=size-start;buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if(!result(d_.CreateBuffer(device_,&buffer,nullptr,&stage_),"create image staging buffer"))return false;
    d_.GetBufferMemoryRequirements(device_,stage_,&req);
    stage_memory_=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(!stage_memory_){state_=UploadState::Failed;return false;}
    if(!result(d_.BindBufferMemory(device_,stage_,stage_memory_,0),"bind image staging memory"))return false;
    void* mapped=nullptr;
    if(!result(d_.MapMemory(device_,stage_memory_,0,buffer.size,0,&mapped),"map image staging memory"))return false;
    std::memcpy(mapped,data+start,size-start);d_.UnmapMemory(device_,stage_memory_);
    state_=UploadState::Prepared;return true;
}
bool ImageUpload::submit(VkQueue queue,uint32_t graphics_family)
{
    if(state_!=UploadState::Prepared || !queue){reason_="Image ticket is not ready for submission";return false;}
    VkCommandPoolCreateInfo pool={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;pool.queueFamilyIndex=graphics_family;
    if(!result(d_.CreateCommandPool(device_,&pool,nullptr,&pool_),"create image upload pool"))return false;
    VkCommandBufferAllocateInfo ai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool=pool_;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    VkCommandBuffer cmd=VK_NULL_HANDLE;
    if(!result(d_.AllocateCommandBuffers(device_,&ai,&cmd),"allocate image upload command"))return false;
    VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if(!result(d_.BeginCommandBuffer(cmd,&begin),"begin image upload"))return false;
    VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=image_;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,static_cast<uint32_t>(decoded_.levels.size()),0,1};
    d_.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    std::vector<VkBufferImageCopy> copies;
    for(uint32_t i=0;i<decoded_.levels.size();++i){
        const auto& level=decoded_.levels[i];VkBufferImageCopy copy{};
        copy.bufferOffset=level.offset-decoded_.levels[0].offset;
        copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,i,0,1};copy.imageExtent={level.width,level.height,1};copies.push_back(copy);
    }
    d_.CmdCopyBufferToImage(cmd,stage_,image_,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,static_cast<uint32_t>(copies.size()),copies.data());
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    d_.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    if(!result(d_.EndCommandBuffer(cmd),"end image upload"))return false;
    VkFenceCreateInfo fi={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if(!result(d_.CreateFence(device_,&fi,nullptr,&fence_),"create image upload fence"))return false;
    VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
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
