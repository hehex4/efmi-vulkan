#pragma once
#include "../gpu/image_formats.h"

namespace efmi::frontend {
namespace application_images {
inline void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class Map,class Expired> void prune(Map& values,Expired expired){
    if(values.size()<65536)return;
    for(auto it=values.begin();it!=values.end();)if(expired(it->second))it=values.erase(it);else ++it;
    require(values.size()<65536,"Application image cache budget exceeded");
}
}
GpuResourceBinding GpuResourceRuntime::import_application_image(const ApplicationImageSource& source){
    using application_images::require;using application_images::prune;
    require(source.source_known && source.resource_id && source.identity,
        "Application image requires proven original description and tracking identity");
    if(source.retained && source.retained->image && !source.retained->image->is_external() && commands_.owns(source.retained->image)){
        require(source.allocation.image==source.retained->image->image(),"Retained original image changed allocation identity");
        return *source.retained;
    }
    const auto& original=source.description;const auto& d=original.texture;const auto& native=source.allocation.description;
    const auto type=original.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE1D?VK_IMAGE_TYPE_1D:
        original.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE2D?VK_IMAGE_TYPE_2D:VK_IMAGE_TYPE_3D;
    require(original.dimension>=D3D11_RESOURCE_DIMENSION_TEXTURE1D && original.dimension<=D3D11_RESOURCE_DIMENSION_TEXTURE3D &&
        native.type==type && native.extent.width==d.Width && native.extent.height==d.Height && native.extent.depth==original.depth &&
        native.mips==d.MipLevels && native.layers==d.ArraySize && uint32_t(native.samples)==d.SampleDesc.Count && !d.SampleDesc.Quality,
        "Application image source shape does not match its actual allocation");
    require(d.Width && d.Width<=16384 && d.Height && d.Height<=16384 && original.depth && original.depth<=2048 &&
        d.ArraySize && d.ArraySize<=2048 && d.MipLevels && d.MipLevels<=D3D11_REQ_MIP_LEVELS &&
        (type!=VK_IMAGE_TYPE_3D || (d.Width<=2048 && d.Height<=2048)),"Application image exceeds original source dimensions");
    require(!(d.BindFlags&D3D11_BIND_UNORDERED_ACCESS) || d.SampleDesc.Count==1,"Original multisample images cannot be UAVs");
    const bool cube=bool(d.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE);
    require(!cube || ((native.flags&VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) && d.ArraySize%6==0),
        "Original cube image requires a complete compatible allocation");
    const bool typeless=original_default_format(d.Format)!=d.Format;
    require(native.format>VK_FORMAT_UNDEFINED && uint32_t(native.format)<185,"Application image format needs an explicit adapter");
    const auto& format=gpu::image_formats[uint32_t(native.format)];
    const bool depth=bool(format.aspects&VK_IMAGE_ASPECT_DEPTH_BIT);
    // A sampled-only depth image still has physical depth storage and an SRV of its typeless family.
    const auto expected=source_image_format(typeless?original_typed(d.Format,depth):d.Format);
    require(typeless && !depth?(bool(native.flags&VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) &&
        format.aspects==expected.aspects && format.compatibility==gpu::image_formats[uint32_t(expected.format)].compatibility):
        native.format==expected.format,"Application image source format does not match its allocation");
    require(!(d.BindFlags&D3D11_BIND_DEPTH_STENCIL) || depth,"Original depth image has no depth allocation");
    VkImageUsageFlags usage=0;
    if(d.BindFlags&D3D11_BIND_SHADER_RESOURCE)usage|=VK_IMAGE_USAGE_SAMPLED_BIT;
    if(d.BindFlags&D3D11_BIND_UNORDERED_ACCESS)usage|=VK_IMAGE_USAGE_STORAGE_BIT;
    if(d.BindFlags&D3D11_BIND_RENDER_TARGET)usage|=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if(d.BindFlags&D3D11_BIND_DEPTH_STENCIL)usage|=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    require((native.usage&usage)==usage,"Application allocation lacks its asserted original image bind usages");
    const auto buffer=imported_buffers_.find(source.resource_id);
    require(buffer==imported_buffers_.end() || buffer->second.buffer.expired(),"A retained source identity cannot change resource kind");
    auto previous=imported_images_.find(source.resource_id);gpu::Image retained;
    if(previous!=imported_images_.end())retained=previous->second.image.lock();
    if(retained){const auto& old=previous->second.description;
        require(retained->image()==source.allocation.image && old.dimension==original.dimension && old.depth==original.depth &&
            !memcmp(&old.texture,&d,sizeof(d)),"A retained application image identity changed its original description");
    }else{
        prune(imported_images_,[](const auto& value){return value.image.expired();});
        prune(imported_image_handles_,[](const auto& value){return value.second.expired();});
        const auto handle=imported_image_handles_.find(source.allocation.image);
        require(handle==imported_image_handles_.end() || handle->second.second.expired(),
            "A retained application image handle cannot acquire a different source identity");
    }
    // Reobserve even a live identity. Named references cannot inherit an old layout.
    GpuResourceBinding value;value.image=commands_.import_image(source.allocation);value.image_description=original;
    require(!retained || retained==value.image,"Application image allocation identity changed");
    imported_images_[source.resource_id]={value.image,original};
    imported_image_handles_[source.allocation.image]={source.resource_id,value.image};return value;
}
void GpuResourceRuntime::import_application_view(GpuResourceBinding& value,const ApplicationImageBinding& input){
    using application_images::require;using application_images::prune;using T=ResourceCopyTargetType;
    const auto& source=value.image_description;const auto& d=source.texture;
    require(input.view_known && !input.flags,"Application image view facts are unknown or require read-only depth hazard rules");
    require(source.dimension!=D3D11_RESOURCE_DIMENSION_TEXTURE3D || input.volume_full_depth,
        "A partial original volume view requires a slice adapter");
    UINT bind=0;VkImageUsageFlags usage=0;
    switch(input.type){
    case T::SHADER_RESOURCE:value.view=GpuResourceBinding::View::ShaderResource;bind=D3D11_BIND_SHADER_RESOURCE;usage=VK_IMAGE_USAGE_SAMPLED_BIT;break;
    case T::UNORDERED_ACCESS_VIEW:value.view=GpuResourceBinding::View::UnorderedAccess;bind=D3D11_BIND_UNORDERED_ACCESS;usage=VK_IMAGE_USAGE_STORAGE_BIT;break;
    case T::RENDER_TARGET:value.view=GpuResourceBinding::View::RenderTarget;bind=D3D11_BIND_RENDER_TARGET;usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;break;
    case T::DEPTH_STENCIL_TARGET:value.view=GpuResourceBinding::View::DepthStencil;bind=D3D11_BIND_DEPTH_STENCIL;usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;break;
    default:throw std::runtime_error("Application image has no original view slot");
    }
    require(bool(d.BindFlags&bind),"Application image view lacks its original bind flag");
    const bool typeless=original_default_format(d.Format)!=d.Format;
    require(input.format!=DXGI_FORMAT_UNKNOWN && (typeless?source_image_family(input.format)==d.Format:input.format==d.Format),
        "Application view format does not belong to its original allocation");
    auto translated=source_image_format(input.format);const auto native=value.image->description().format;
    if(gpu::image_formats[uint32_t(native)].aspects&VK_IMAGE_ASPECT_DEPTH_BIT){
        require(input.type==T::SHADER_RESOURCE || input.type==T::DEPTH_STENCIL_TARGET,"Depth allocation has no writable color view");
        if(translated.aspects==VK_IMAGE_ASPECT_COLOR_BIT){
            require(input.format==DXGI_FORMAT_R32_FLOAT || input.format==DXGI_FORMAT_R16_UNORM,"Depth input aspect needs an explicit format adapter");
            translated.aspects=VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        translated.format=native;
    }
    const auto& range=input.range;
    require(range.aspectMask==translated.aspects && range.levelCount && range.layerCount &&
        range.levelCount!=VK_REMAINING_MIP_LEVELS && range.layerCount!=VK_REMAINING_ARRAY_LAYERS,
        "Application image view needs explicit original subresource ranges");
    if(input.type!=T::SHADER_RESOURCE)require(range.levelCount==1 && input.view_type!=VK_IMAGE_VIEW_TYPE_CUBE &&
        input.view_type!=VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,"Original image output requires one mip and a non-cube view");
    require(input.type!=T::DEPTH_STENCIL_TARGET || source.dimension!=D3D11_RESOURCE_DIMENSION_TEXTURE3D,
        "Original depth views cannot address volume images");
    const std::array<uint64_t,9> key={input.source->resource_id,uint64_t(input.view_type),uint64_t(translated.format),
        range.aspectMask,range.baseMipLevel,range.levelCount,range.baseArrayLayer,range.layerCount,usage};
    const auto found=imported_image_views_.find(key);
    if(found!=imported_image_views_.end())value.image_view=found->second.lock();
    if(!value.image_view){
        prune(imported_image_views_,[](const auto& view){return view.expired();});
        value.image_view=commands_.view(value.image,input.view_type,translated.format,range,usage);
        imported_image_views_[key]=value.image_view;
    }
    value.view_format=input.format;
    value.native_view=input.native_view;
}
}
