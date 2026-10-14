#include "resource_commands.h"
#include "image_formats.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace efmi::gpu {
namespace {
void check(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+": "+std::to_string(result));
}
const ImageFormatInfo& info(VkFormat format) {
    if(format<=VK_FORMAT_UNDEFINED || uint32_t(format)>=185)throw std::runtime_error("Image format requires an extension adapter");
    return image_formats[uint32_t(format)];
}
VkDeviceSize multiply(VkDeviceSize a,VkDeviceSize b) {
    if(b && a>UINT64_MAX/b)throw std::runtime_error("Image transfer size overflow");
    return a*b;
}
VkDeviceSize add(VkDeviceSize a,VkDeviceSize b) {
    if(a>UINT64_MAX-b)throw std::runtime_error("Image transfer size overflow");
    return a+b;
}
VkDeviceSize blocks(uint32_t size,uint32_t block) {return (VkDeviceSize(size)+block-1)/block;}
bool same_extent(VkExtent3D a,VkExtent3D b) {return a.width==b.width && a.height==b.height && a.depth==b.depth;}
constexpr VkImageUsageFlags view_usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
VkFormatFeatureFlags required_features(VkImageUsageFlags usage,VkImageAspectFlags aspects) {
    VkFormatFeatureFlags required=0;
    if(usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)required|=VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if(usage&VK_IMAGE_USAGE_TRANSFER_DST_BIT)required|=VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if(usage&VK_IMAGE_USAGE_SAMPLED_BIT)required|=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if(usage&VK_IMAGE_USAGE_STORAGE_BIT)required|=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if(usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)required|=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if(usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)required|=VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if(usage&VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)required|=aspects==VK_IMAGE_ASPECT_COLOR_BIT?
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT:VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    return required;
}
}
ImageResource::~ImageResource() {
    if(image_)device_.dispatch.DestroyImage(device_.device,image_,nullptr);
    if(memory_)device_.dispatch.FreeMemory(device_.device,memory_,nullptr);
    if(allocation_)budget_->release(allocation_);
}
ImageViewResource::~ImageViewResource() {
    if(view_)image_->device_.dispatch.DestroyImageView(image_->device_.device,view_,nullptr);
}
VkExtent3D ImageResource::extent(uint32_t mip) const {
    if(mip>=description_.mips)throw std::runtime_error("Image mip outside allocation");
    return {std::max(1u,description_.extent.width>>mip),std::max(1u,description_.extent.height>>mip),
            std::max(1u,description_.extent.depth>>mip)};
}
VkImageSubresourceRange ImageResource::full_range() const {
    return {info(description_.format).aspects,0,description_.mips,0,description_.layers};
}
Image ResourceCommands::create_image(ImageDescription desc) {
    if(!device_.physical || !device_.GetImageFormatProperties || !device_.GetFormatProperties ||
       !(device_.queue_flags&VK_QUEUE_GRAPHICS_BIT) || device_.api_version<VK_API_VERSION_1_1)
        throw std::runtime_error("Images require Vulkan 1.1 capabilities and a serialized graphics queue");
    const auto& f=info(desc.format);
    if(desc.type!=VK_IMAGE_TYPE_1D && desc.type!=VK_IMAGE_TYPE_2D && desc.type!=VK_IMAGE_TYPE_3D)
        throw std::runtime_error("Invalid image dimension");
    if(!desc.extent.width || !desc.extent.height || !desc.extent.depth || !desc.layers || !desc.mips ||
       (desc.type==VK_IMAGE_TYPE_1D && desc.extent.height!=1) ||
       (desc.type!=VK_IMAGE_TYPE_3D && desc.extent.depth!=1) || (desc.type==VK_IMAGE_TYPE_3D && desc.layers!=1))
        throw std::runtime_error("Invalid image extent or layer count");
    uint32_t max_mips=1;for(auto n=std::max({desc.extent.width,desc.extent.height,desc.extent.depth});n>1;n>>=1)++max_mips;
    if(desc.mips>max_mips)throw std::runtime_error("Image mip chain exceeds extent");
    if(desc.flags&~(VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT|VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT))
        throw std::runtime_error("Image creation flags require another adapter");
    if((desc.flags&VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
       (desc.type!=VK_IMAGE_TYPE_2D || desc.extent.width!=desc.extent.height || desc.layers<6))
        throw std::runtime_error("Invalid cube-compatible image");
    const uint32_t samples=uint32_t(desc.samples);
    if(!samples || samples>64 || (samples&(samples-1)) || (samples!=1 &&
       (desc.type!=VK_IMAGE_TYPE_2D || desc.mips!=1 || (desc.flags&VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT))))
        throw std::runtime_error("Invalid image sample count or multisample dimensions");
    if(desc.usage&~(view_usage|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        throw std::runtime_error("Image usage requires another adapter");
    if(samples!=1 && (desc.usage&VK_IMAGE_USAGE_STORAGE_BIT) && !device_.enabled_features.shaderStorageImageMultisample)
        throw std::runtime_error("Multisampled storage images were not enabled");
    if((f.compression==1 && !device_.enabled_features.textureCompressionBC) ||
       (f.compression==2 && !device_.enabled_features.textureCompressionETC2) ||
       (f.compression==3 && !device_.enabled_features.textureCompressionASTC_LDR))
        throw std::runtime_error("Image compression feature was not enabled");
    desc.usage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const auto required=required_features(desc.usage,f.aspects);VkFormatProperties properties{};
    device_.GetFormatProperties(device_.physical,desc.format,&properties);
    if((properties.optimalTilingFeatures&required)!=required)throw std::runtime_error("Image format does not support requested usage");
    VkImageFormatProperties limits{};
    check(device_.GetImageFormatProperties(device_.physical,desc.format,desc.type,VK_IMAGE_TILING_OPTIMAL,desc.usage,desc.flags,&limits),
          "Query image description support");
    if(desc.extent.width>limits.maxExtent.width || desc.extent.height>limits.maxExtent.height || desc.extent.depth>limits.maxExtent.depth ||
       desc.mips>limits.maxMipLevels || desc.layers>limits.maxArrayLayers || !(samples&limits.sampleCounts))
        throw std::runtime_error("Image description exceeds device capabilities");
    VkDeviceSize minimum=0;
    for(uint32_t mip=0;mip<desc.mips;++mip) {
        auto size=multiply(blocks(std::max(1u,desc.extent.width>>mip),f.block.width),blocks(std::max(1u,desc.extent.height>>mip),f.block.height));
        size=multiply(size,blocks(std::max(1u,desc.extent.depth>>mip),f.block.depth));
        minimum=add(minimum,multiply(multiply(multiply(size,desc.layers),samples),f.bytes));
    }
    if(minimum>budgets_.resident->limit() || minimum>limits.maxResourceSize)throw std::runtime_error("Image allocation exceeds budget or device limit");
    auto image=std::make_shared<ImageResource>();image->device_=device_;image->domain_=domain_;
    image->description_=desc;image->budget_=budgets_.resident;
    VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.flags=desc.flags;ci.imageType=desc.type;ci.format=desc.format;
    ci.extent=desc.extent;ci.mipLevels=desc.mips;ci.arrayLayers=desc.layers;ci.samples=desc.samples;ci.usage=desc.usage;
    ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;ci.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    const auto& d=device_.dispatch;check(d.CreateImage(device_.device,&ci,nullptr,&image->image_),"Create resource image");
    VkMemoryDedicatedRequirements dedicated={VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    VkMemoryRequirements2 requirements={VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};requirements.pNext=&dedicated;
    VkImageMemoryRequirementsInfo2 request={VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};request.image=image->image_;
    d.GetImageMemoryRequirements2(device_.device,&request,&requirements);const auto& req=requirements.memoryRequirements;
    uint32_t chosen=UINT32_MAX;int best=-1;
    for(uint32_t i=0;i<device_.memory.memoryTypeCount;++i)if(req.memoryTypeBits&(1u<<i)) {
        int rank=(device_.memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)?2:1;
        if(rank>best){chosen=i;best=rank;}
    }
    if(chosen==UINT32_MAX)throw std::runtime_error("Image memory type unavailable");
    if(req.size>limits.maxResourceSize || !image->budget_->acquire(req.size))throw std::runtime_error("Image allocation budget exhausted");
    image->allocation_=req.size;VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=chosen;
    VkMemoryDedicatedAllocateInfo allocation={VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};allocation.image=image->image_;
    if(dedicated.requiresDedicatedAllocation || dedicated.prefersDedicatedAllocation)ai.pNext=&allocation;
    check(d.AllocateMemory(device_.device,&ai,nullptr,&image->memory_),"Allocate image memory");
    check(d.BindImageMemory(device_.device,image->image_,image->memory_,0),"Bind image memory");return image;
}
void ResourceCommands::validate(const Image& image) const {
    if(!image || image->domain_!=domain_)throw std::runtime_error("Image belongs to another execution domain");
}
VkImageSubresourceRange ResourceCommands::range(const Image& image,VkImageSubresourceRange r) const {
    validate(image);const auto& desc=image->description_;
    if(!r.aspectMask || (r.aspectMask&~info(desc.format).aspects))throw std::runtime_error("Invalid image aspects");
    if(r.baseMipLevel>=desc.mips || r.baseArrayLayer>=desc.layers)throw std::runtime_error("Image range base outside allocation");
    if(r.levelCount==VK_REMAINING_MIP_LEVELS)r.levelCount=desc.mips-r.baseMipLevel;
    if(r.layerCount==VK_REMAINING_ARRAY_LAYERS)r.layerCount=desc.layers-r.baseArrayLayer;
    if(!r.levelCount || !r.layerCount || r.levelCount>desc.mips-r.baseMipLevel || r.layerCount>desc.layers-r.baseArrayLayer)
        throw std::runtime_error("Image range outside allocation");
    return r;
}
void ResourceCommands::region(const Image& image,VkImageSubresourceLayers sub,VkOffset3D offset,VkExtent3D size) const {
    range(image,{sub.aspectMask,sub.mipLevel,1,sub.baseArrayLayer,sub.layerCount});
    if(sub.layerCount==VK_REMAINING_ARRAY_LAYERS)throw std::runtime_error("Image copies require an explicit layer count");
    const auto extent=image->extent(sub.mipLevel);const auto block=info(image->description_.format).block;
    auto axis=[](int32_t offset,uint32_t size,uint32_t extent,uint32_t block) {
        if(offset<0 || !size || uint32_t(offset)>=extent || size>extent-uint32_t(offset) || uint32_t(offset)%block ||
           (size%block && size!=extent-uint32_t(offset)))throw std::runtime_error("Image copy region is out of bounds or block alignment");
    };
    axis(offset.x,size.width,extent.width,block.width);axis(offset.y,size.height,extent.height,block.height);
    axis(offset.z,size.depth,extent.depth,block.depth);
}
ImageView ResourceCommands::view(const Image& image,VkImageViewType type,VkFormat format,VkImageSubresourceRange r,VkImageUsageFlags usage) {
    r=range(image,r);const auto& desc=image->description_;
    if(format==VK_FORMAT_UNDEFINED)format=desc.format;
    const auto& f=info(format);const auto& original=info(desc.format);
    if(format!=desc.format && (!(desc.flags&VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) || f.compatibility!=original.compatibility ||
       original.aspects!=VK_IMAGE_ASPECT_COLOR_BIT))throw std::runtime_error("Image view format is not compatible with its allocation");
    bool match=false;
    switch(type) {
    case VK_IMAGE_VIEW_TYPE_1D:match=desc.type==VK_IMAGE_TYPE_1D && r.layerCount==1;break;
    case VK_IMAGE_VIEW_TYPE_1D_ARRAY:match=desc.type==VK_IMAGE_TYPE_1D;break;
    case VK_IMAGE_VIEW_TYPE_2D:match=desc.type==VK_IMAGE_TYPE_2D && r.layerCount==1;break;
    case VK_IMAGE_VIEW_TYPE_2D_ARRAY:match=desc.type==VK_IMAGE_TYPE_2D;break;
    case VK_IMAGE_VIEW_TYPE_3D:match=desc.type==VK_IMAGE_TYPE_3D;break;
    case VK_IMAGE_VIEW_TYPE_CUBE:match=(desc.flags&VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) && r.layerCount==6;break;
    case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:match=(desc.flags&VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) && r.layerCount%6==0 && device_.enabled_features.imageCubeArray;break;
    default:break;
    }
    if(!match)throw std::runtime_error("Image view dimension or feature is incompatible");
    if(!usage)usage=desc.usage;
    if(!(usage&view_usage) || (usage&~desc.usage))throw std::runtime_error("Image view usage is not supported by its allocation");
    VkFormatProperties properties{};device_.GetFormatProperties(device_.physical,format,&properties);
    const auto required=required_features(usage,f.aspects);
    if((properties.optimalTilingFeatures&required)!=required)throw std::runtime_error("Image view format does not support its usage");
    auto result=std::make_shared<ImageViewResource>();result->image_=image;result->format_=format;result->type_=type;result->range_=r;result->usage_=usage;
    VkImageViewUsageCreateInfo use={VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO};use.usage=usage;
    VkImageViewCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};ci.pNext=&use;ci.image=image->image_;ci.viewType=type;ci.format=format;ci.subresourceRange=r;
    check(device_.dispatch.CreateImageView(device_.device,&ci,nullptr,&result->view_),"Create resource image view");return result;
}
void ResourceCommands::transition(const Image& image,VkImageLayout layout) {
    auto cb=command();auto found=pending_->image_layouts_.find(image);
    const auto old=found==pending_->image_layouts_.end()?(image->initialized_?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED):found->second;
    // Insertion retains the owner before a command can refer to its image.
    pending_->image_layouts_[image]=layout;if(old==layout)return;
    VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image->image_;b.subresourceRange=image->full_range();
    b.oldLayout=old;b.newLayout=layout;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.srcAccessMask=old==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask=layout==VK_IMAGE_LAYOUT_GENERAL?VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT:
        (layout==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL?VK_ACCESS_TRANSFER_READ_BIT:VK_ACCESS_TRANSFER_WRITE_BIT);
    device_.dispatch.CmdPipelineBarrier(cb,old==VK_IMAGE_LAYOUT_UNDEFINED?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        layout==VK_IMAGE_LAYOUT_GENERAL?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
}
VkDeviceSize ResourceCommands::transfer_size(const Image& image,const VkBufferImageCopy& r) const {
    region(image,r.imageSubresource,r.imageOffset,r.imageExtent);const auto& f=info(image->description_.format);
    const auto aspect=r.imageSubresource.aspectMask;
    if(aspect&(aspect-1))throw std::runtime_error("Buffer-image transfers require exactly one aspect");
    if(image->description_.samples!=VK_SAMPLE_COUNT_1_BIT)throw std::runtime_error("Buffer-image transfers require a resolved image");
    const auto unit=aspect==VK_IMAGE_ASPECT_DEPTH_BIT?f.depth_bytes:(aspect==VK_IMAGE_ASPECT_STENCIL_BIT?1:f.bytes);
    if(r.bufferOffset%4 || r.bufferOffset%unit)throw std::runtime_error("Image buffer offset requires four-byte and texel-block alignment");
    if((r.bufferRowLength && (r.bufferRowLength<r.imageExtent.width || r.bufferRowLength%f.block.width)) ||
       (r.bufferImageHeight && (r.bufferImageHeight<r.imageExtent.height || r.bufferImageHeight%f.block.height)))
        throw std::runtime_error("Image transfer pitch is too small or not block aligned");
    const auto row=blocks(r.bufferRowLength?r.bufferRowLength:r.imageExtent.width,f.block.width);
    const auto height=blocks(r.bufferImageHeight?r.bufferImageHeight:r.imageExtent.height,f.block.height);
    if(multiply(row,unit)>INT32_MAX)throw std::runtime_error("Image transfer row pitch exceeds Vulkan limit");
    auto slices=multiply(blocks(r.imageExtent.depth,f.block.depth),r.imageSubresource.layerCount);
    auto last=add(multiply(slices-1,height),blocks(r.imageExtent.height,f.block.height)-1);
    return multiply(add(multiply(last,row),blocks(r.imageExtent.width,f.block.width)),unit);
}
void ResourceCommands::copy(const Buffer& source,const Image& destination,const VkBufferImageCopy& r) {
    const auto size=transfer_size(destination,r);validate(source,r.bufferOffset,size);
    if(!(source->usage_&VK_BUFFER_USAGE_TRANSFER_SRC_BIT))throw std::runtime_error("Image upload buffer usage missing");
    barrier();transition(destination,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);pending_->retained_.push_back(source);
    device_.dispatch.CmdCopyBufferToImage(command(),source->buffer_,destination->image_,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&r);
}
void ResourceCommands::copy(const Image& source,const Buffer& destination,const VkBufferImageCopy& r) {
    const auto size=transfer_size(source,r);validate(destination,r.bufferOffset,size);
    if(!(destination->usage_&VK_BUFFER_USAGE_TRANSFER_DST_BIT))throw std::runtime_error("Image download buffer usage missing");
    barrier();transition(source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);pending_->retained_.push_back(destination);
    device_.dispatch.CmdCopyImageToBuffer(command(),source->image_,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,destination->buffer_,1,&r);
}
void ResourceCommands::copy(const Image& source,const Image& destination,const VkImageCopy& r) {
    region(source,r.srcSubresource,r.srcOffset,r.extent);region(destination,r.dstSubresource,r.dstOffset,r.extent);
    const auto& src=source->description_;const auto& dst=destination->description_;const auto& a=info(src.format);const auto& b=info(dst.format);
    if(src.type!=dst.type || src.samples!=dst.samples || r.srcSubresource.layerCount!=r.dstSubresource.layerCount ||
       r.srcSubresource.aspectMask!=r.dstSubresource.aspectMask || a.bytes!=b.bytes || !same_extent(a.block,b.block) ||
       (a.aspects!=VK_IMAGE_ASPECT_COLOR_BIT && src.format!=dst.format))
        throw std::runtime_error("Image copies require compatible dimensions, aspects, sample counts and texel blocks");
    if(source==destination && r.srcSubresource.mipLevel==r.dstSubresource.mipLevel) {
        auto overlaps=[](uint32_t x,uint32_t y,uint32_t count){return uint64_t(x)<uint64_t(y)+count && uint64_t(y)<uint64_t(x)+count;};
        if(overlaps(r.srcSubresource.baseArrayLayer,r.dstSubresource.baseArrayLayer,r.srcSubresource.layerCount) &&
           overlaps(uint32_t(r.srcOffset.x),uint32_t(r.dstOffset.x),r.extent.width) &&
           overlaps(uint32_t(r.srcOffset.y),uint32_t(r.dstOffset.y),r.extent.height) &&
           overlaps(uint32_t(r.srcOffset.z),uint32_t(r.dstOffset.z),r.extent.depth))throw std::runtime_error("Overlapping image copy");
    }
    barrier();const auto sl=source==destination?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const auto dl=source==destination?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    transition(source,sl);transition(destination,dl);device_.dispatch.CmdCopyImage(command(),source->image_,sl,destination->image_,dl,1,&r);
}
void ResourceCommands::clear(const Image& image,VkImageSubresourceRange r,const VkClearColorValue& value) {
    r=range(image,r);if(!info(image->description_.format).color_clear || r.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT)
        throw std::runtime_error("Image format does not support a color clear");
    barrier();transition(image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    device_.dispatch.CmdClearColorImage(command(),image->image_,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&value,1,&r);
}
void ResourceCommands::clear(const Image& image,VkImageSubresourceRange r,const VkClearDepthStencilValue& value) {
    r=range(image,r);if(r.aspectMask&VK_IMAGE_ASPECT_COLOR_BIT)throw std::runtime_error("Depth/stencil clear requires a depth/stencil image");
    if((r.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT) && (!std::isfinite(value.depth) || value.depth<0 || value.depth>1))
        throw std::runtime_error("Depth clear value is outside the enabled range");
    barrier();transition(image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    device_.dispatch.CmdClearDepthStencilImage(command(),image->image_,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&value,1,&r);
}
void ResourceCommands::resolve(const Image& source,const Image& destination,const VkImageResolve& r) {
    region(source,r.srcSubresource,r.srcOffset,r.extent);region(destination,r.dstSubresource,r.dstOffset,r.extent);
    const auto& src=source->description_;const auto& dst=destination->description_;
    if(src.format!=dst.format || src.type!=VK_IMAGE_TYPE_2D || dst.type!=VK_IMAGE_TYPE_2D ||
       src.samples==VK_SAMPLE_COUNT_1_BIT || dst.samples!=VK_SAMPLE_COUNT_1_BIT ||
       r.srcSubresource.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT || r.dstSubresource.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT ||
       r.srcSubresource.layerCount!=r.dstSubresource.layerCount)throw std::runtime_error("Invalid color multisample resolve");
    VkFormatProperties properties{};device_.GetFormatProperties(device_.physical,dst.format,&properties);
    if(!(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))throw std::runtime_error("Resolve format lacks color attachment support");
    barrier();transition(source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);transition(destination,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    device_.dispatch.CmdResolveImage(command(),source->image_,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,destination->image_,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&r);
}
void ResourceCommands::write(const Image& image,VkBufferImageCopy r,const void* bytes,size_t size) {
    if(r.bufferOffset)throw std::runtime_error("Host image upload requires zero buffer offset");
    const auto required=transfer_size(image,r);
    if(!bytes || size!=required)throw std::runtime_error("Image upload byte count does not match region and pitch");
    auto stage=allocate(required,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,true);write_stage(stage,bytes,size);copy(stage,image,r);
}
std::vector<uint8_t> ResourceCommands::readback(VkQueue queue,const Image& image,VkBufferImageCopy r) {
    if(r.bufferOffset)throw std::runtime_error("Host image readback requires zero buffer offset");
    const auto size=transfer_size(image,r);auto stage=allocate(size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true);copy(image,stage,r);
    barrier(VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);auto ticket=submit(queue);ticket->wait();return read_stage(stage,size);
}
}
