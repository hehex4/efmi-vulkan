#pragma once
#include "state.h"
#include "../../frontend/xxmi/native_bindings.h"

namespace efvk {
struct NativeMemoryRange {
    uint64_t memory=0,offset=0,size=0;bool linear=false;
};
inline NativeMemoryRange native_memory(const ImageInfo& image){return {uint64_t(image.mem),image.mem_off,image.mem_size,image.tiling==VK_IMAGE_TILING_LINEAR};}
inline NativeMemoryRange native_memory(const BufInfo& buffer){return {uint64_t(buffer.mem),buffer.mem_off,buffer.mem_size,true};}
inline NativeMemoryRange native_memory(const efmi::frontend::NativeResource& resource){return {resource.memory,resource.memory_offset,resource.memory_size,resource.memory_linear};}
inline bool native_memory_overlap(const NativeMemoryRange& a,const NativeMemoryRange& b,uint64_t granularity){
    if(!a.memory || !b.memory || a.memory!=b.memory)return false;
    if(!a.size || !b.size || a.size-1>UINT64_MAX-a.offset || b.size-1>UINT64_MAX-b.offset)return true;
    uint64_t first_a=a.offset,last_a=a.offset+a.size-1,first_b=b.offset,last_b=b.offset+b.size-1;
    if(a.linear!=b.linear){
        if(!granularity || (granularity&(granularity-1)))return true;
        first_a/=granularity;last_a/=granularity;first_b/=granularity;last_b/=granularity;
    }
    return first_a<=last_b && first_b<=last_a;
}
// Caller holds res_mx. Inspect retained attachment facts, including implicit WSI storage.
// Full resource spans intentionally reject aliases even when a shader uses only part.
inline bool native_attachment_memory(Device* device,const CmdBuf* cb,const efmi::frontend::NativeResource& resource,std::string& why){
    if(!cb->in_rp && !cb->inherited_rp)return true;
    if(resource.keep && (resource.uploaded_readonly || resource.uploaded_mutable))return true;
    if(resource.keep && resource.initialization && resource.initial_contents)return true;
    auto refuse=[&](const char* reason){why=reason;return false;};
    if(!resource.memory || !resource.memory_size)return refuse("Native input attachment memory proof needs a retained allocation range");
    auto view_safe=[&](const TargetView& view){
        if(!view.known || !view.handle || !view.view.image)return refuse("Native input attachment memory identity is unavailable");
        if(resource.image==uint64_t(view.view.image) || native_memory_overlap(native_memory(resource),native_memory(view.image),device->physical_properties.limits.bufferImageGranularity))
            return refuse("Native input overlaps attachment memory");
        if(view.image.mem)return true;
        for(const auto& swapchain:device->swapchains)for(auto image:swapchain.second.images)
            if(image==view.view.image)return true;
        return refuse("Native input attachment memory range is unavailable");
    };
    const RenderTargets* targets=cb->active_targets && cb->active_targets<=cb->target_contexts.size()?&cb->target_contexts[cb->active_targets-1]:nullptr;
    if(targets && !targets->known)return refuse("Native input attachment memory context is unsupported");
    if(targets && targets->known && targets->dynamic){
        const size_t count=targets->dynamic_colors.size()+size_t(targets->has_depth)+size_t(targets->has_stencil);
        if(targets->views.size()!=2*count)return refuse("Native input attachment memory context is incomplete");
        size_t index=0;
        auto attachment_safe=[&](const VkRenderingAttachmentInfo& attachment){
            const auto& view=targets->views[index++];const auto& resolve=targets->views[index++];
            if(!attachment.imageView)return true;
            if(view.handle!=attachment.imageView)return refuse("Native input attachment memory view identity changed");
            if(!view_safe(view))return false;
            if(attachment.resolveMode!=VK_RESOLVE_MODE_NONE){
                if(resolve.handle!=attachment.resolveImageView)return refuse("Native input resolve attachment memory identity changed");
                if(!view_safe(resolve))return false;
            }
            return true;
        };
        for(const auto& color:targets->dynamic_colors)if(!attachment_safe(color))return false;
        return (!targets->has_depth || attachment_safe(targets->depth)) && (!targets->has_stencil || attachment_safe(targets->stencil));
    }
    const auto framebuffer=targets && targets->known?targets->framebuffer:cb->pass.info;
    if(!framebuffer || !framebuffer->known)return refuse("Native input attachment memory context is unavailable");
    const auto& views=framebuffer->imageless && targets?targets->views:framebuffer->views;
    if(views.size()!=framebuffer->attachment_count || (framebuffer->imageless && !targets && !framebuffer->views_resolved))
        return refuse("Native input attachment memory views are unresolved");
    for(const auto& view:views)if(!view_safe(view))return false;
    return true;
}
}
