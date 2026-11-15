#include "native_resources.h"
#include "texture_identity.h"

namespace efvk {
bool native_dynamic_outputs(const CmdBuf* cb, uint32_t context, uint32_t& mask)
{
    mask = 0;
    if (!cb || cb->inherited_rp || !context || context > cb->target_contexts.size()) return false;
    const auto& targets = cb->target_contexts[context - 1];
    if (!targets.known || !targets.dynamic) return false;
    for (size_t i = 0; i < targets.dynamic_colors.size() && i < 8; ++i)
        if (targets.dynamic_colors[i].imageView) mask |= 1u << i;
    if ((targets.has_depth && targets.depth.imageView) || (targets.has_stencil && targets.stencil.imageView)) mask |= 1u << 8;
    return true;
}

namespace {
bool image_resource(VkImageView handle, const ImageViewInfo& view, const ImageInfo& resource,
                    efmi::frontend::NativeResource& output)
{
    output.image_view=uint64_t(handle);output.usage=resource.usage;
    output.image=uint64_t(view.image);output.image_view_id=view.id;
    output.view_type=view.type;output.view_format=view.format;
    const auto& range=view.range;
    output.aspect=range.aspectMask;output.base_mip=range.baseMipLevel;output.mip_count=range.levelCount;
    output.base_layer=range.baseArrayLayer;output.layer_count=range.layerCount;
    output.image_safe=resource.capture_safe && resource.tiling==VK_IMAGE_TILING_OPTIMAL && view.native_safe;
    if(resource.lifetime && !resource.lifetime->dependencies.empty() && view.lifetime && !view.lifetime->dependencies.empty())
        output.keep=view.lifetime;
    output.bound = true; output.dimension = uint32_t(resource.type) + 2;
    output.identity = (uint64_t(resource.id) << 2) | 2;
    output.width = resource.extent.width; output.height = resource.extent.height; output.depth = resource.extent.depth;
    output.levels = resource.levels; output.layers = resource.layers; output.samples = resource.samples;
    output.format = dxgi_view_format(view.format,view.range.aspectMask); output.format_known = output.format != 0;
    output.resource_format = dxgi_format(resource.format);
    output.bind_flags = ((resource.usage & VK_IMAGE_USAGE_SAMPLED_BIT) ? 8u : 0u) |
        ((resource.usage & VK_IMAGE_USAGE_STORAGE_BIT) ? 128u : 0u) |
        ((resource.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ? 32u : 0u) |
        ((resource.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ? 64u : 0u);
    const bool writable = (resource.usage & (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0;
    output.hash_known = !writable && resource.content == IMG_HASHED;
    output.hash = resource.content_crc;
    return true;
}
// Output locations are subpass/rendering slots, not framebuffer attachment indices or descriptors.
bool target_resource(Device* device, CmdBuf* cb, const DrawRec& draw,
                     const efmi::frontend::NativeBinding& alias, efmi::frontend::NativeResource& output, std::string& why)
{
    auto refuse=[&](const char* reason){why=reason;return false;};
    const bool depth=alias.kind==efmi::frontend::NativeBinding::DepthTarget;
    if((depth && alias.slot) || (!depth && alias.slot>=8))return refuse("Native output slot is outside tracked slots");
    if(cb->inherited_rp)return refuse("Native output resources in inherited secondary recordings need parent attachment context");
    if(!draw.targets){
        if(cb->in_rp)return refuse("Native output attachment context was not recorded");
        return true;
    }
    if(draw.targets>cb->target_contexts.size())return refuse("Native output attachment context is unavailable");
    const auto& targets=cb->target_contexts[draw.targets-1];
    if(!targets.known)return refuse("Native output attachment description is unsupported");
    const TargetView* target=nullptr;VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
    if(targets.dynamic){
        size_t index=0;const VkRenderingAttachmentInfo* attachment=nullptr;
        if(depth){
            const auto* d=targets.has_depth && targets.depth.imageView?&targets.depth:nullptr;
            const auto* s=targets.has_stencil && targets.stencil.imageView?&targets.stencil:nullptr;
            if(d && s && (d->imageView!=s->imageView || d->imageLayout!=s->imageLayout))
                return refuse("Native depth target cannot combine separate depth and stencil views or layouts");
            attachment=d?d:s;
            index=2*targets.dynamic_colors.size()+(d?0:targets.has_depth?2:0);
        }else if(alias.slot<targets.dynamic_colors.size()){
            attachment=&targets.dynamic_colors[alias.slot];index=2*alias.slot;
        }
        if(!attachment || !attachment->imageView)return true;
        if(index>=targets.views.size())return refuse("Native output view metadata is unavailable");
        target=&targets.views[index];layout=attachment->imageLayout;
    }else{
        if(!targets.recipe || targets.subpass>=targets.recipe->subpasses.size())return refuse("Native output subpass is unavailable");
        const auto& sub=targets.recipe->subpasses[targets.subpass];
        const VkAttachmentReference2* ref=depth?(sub.has_depth?&sub.depth:nullptr):
            (alias.slot<sub.colors.size()?&sub.colors[alias.slot]:nullptr);
        if(!ref || ref->attachment==VK_ATTACHMENT_UNUSED)return true;
        const auto& views=targets.framebuffer && !targets.framebuffer->imageless?targets.framebuffer->views:targets.views;
        if(ref->attachment>=views.size())return refuse("Native output attachment index has no view");
        target=&views[ref->attachment];layout=ref->layout;
    }
    if(!target->known)return refuse("Native output view identity is unavailable");
    const auto view=device->image_views.find(target->handle);
    if(view==device->image_views.end() || view->second.id!=target->view.id || view->second.image_id!=target->image.id)
        return refuse("Native output view was destroyed or replaced");
    const auto image=device->images.find(view->second.image);
    if(image==device->images.end() || image->second.id!=target->image.id)
        return refuse("Native output image was destroyed or replaced");
    output.descriptor_type=VK_DESCRIPTOR_TYPE_MAX_ENUM;output.image_layout=layout;
    device->track_image_layouts.store(true,std::memory_order_relaxed);
    if(!image_resource(target->handle,view->second,image->second,output))return false;
    if(depth){
        auto depth_format=[](VkFormat format)->uint32_t{
            switch(format){
            case VK_FORMAT_D16_UNORM:return 55;
            case VK_FORMAT_D32_SFLOAT:return 40;
            case VK_FORMAT_D24_UNORM_S8_UINT:return 45;
            case VK_FORMAT_D32_SFLOAT_S8_UINT:return 20;
            default:return 0;
            }
        };
        // OMGetRenderTargets returns a DSV, whose format differs from a sampled depth view.
        output.format=depth_format(view->second.format);output.format_known=output.format!=0;
        output.resource_format=depth_format(image->second.format);
    }
    return true;
}
}
bool native_output_resource_locked(Device* device,CmdBuf* cb,const DrawRec& draw,const efmi::frontend::NativeBinding& alias,
                                   efmi::frontend::NativeResource& output,std::string& why)
{
    using B=efmi::frontend::NativeBinding;output={};
    if(!device || !cb || (alias.kind!=B::ColorTarget && alias.kind!=B::DepthTarget)){
        why="Native output resource context is unavailable";return false;
    }
    return target_resource(device,cb,draw,alias,output,why);
}
bool native_resource(Device* device, CmdBuf* cb, const DrawRec& draw, const efmi::frontend::NativeBinding& alias,
                     efmi::frontend::NativeResource& output, std::string& why)
{
    using B = efmi::frontend::NativeBinding;
    auto refuse = [&](const char* reason) { why = reason; return false; };
    output = {};
    if (!device || !cb) return refuse("Native resource recording is unavailable");
    std::shared_lock<std::shared_mutex> lock(device->res_mx);
    if(alias.kind==B::ColorTarget || alias.kind==B::DepthTarget)return target_resource(device,cb,draw,alias,output,why);
    if(alias.set>=kMaxSets)return refuse("Native descriptor set is outside tracked slots");
    const auto pipeline = device->pipes.find(draw.pipeline);
    if (pipeline == device->pipes.end() || !pipeline->second.layout) return refuse("Native pipeline layout is unknown");
    const auto& expected = *pipeline->second.layout;
    const auto& bound = draw.sets[alias.set];
    if (!bound.layout || alias.set >= bound.layout->prefix.size() || alias.set >= expected.prefix.size() ||
        !expected.prefix[alias.set] || expected.prefix[alias.set] != bound.layout->prefix[alias.set] || bound.unsupported)
        return refuse("Native descriptor binding is absent, incompatible or unsupported");
    const DescriptorSetInfo* contents = bound.pushed.get();
    if (!contents) {
        const auto set = device->descriptor_sets.find(bound.set);
        if (set != device->descriptor_sets.end() && set->second.id == bound.id) contents = &set->second;
    }
    if (!contents || !contents->layout || contents->unsupported) return refuse("Native descriptor contents are unavailable");
    const auto layout = contents->layout->bindings.find(alias.binding);
    const auto count = contents->counts.find(alias.binding);
    if (layout == contents->layout->bindings.end() || count == contents->counts.end() || alias.element >= count->second)
        return refuse("Native descriptor address is not in the bound layout");
    const uint32_t stages[] = {1, 2, 4, 8, 16, 32};
    if (alias.stage >= 6 || !(layout->second.stages & stages[alias.stage])) return refuse("Native descriptor is not visible to the alias stage");
    const auto found = contents->values.find({alias.binding, alias.element});
    if (found == contents->values.end() || !found->second.known) return refuse("Native descriptor element has no known value");
    const auto& value = found->second;
    output.descriptor_type=value.type;output.image_layout=value.image_layout;
    const bool uniform = value.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || value.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    const bool storage = value.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || value.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    const bool sampled = value.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || value.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    const bool image = sampled || value.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    const bool texel = value.type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || value.type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    if ((alias.kind == B::Constant && !uniform) || (alias.kind == B::Resource && !(sampled || storage || value.type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER)) ||
        (alias.kind == B::Access && !(storage || value.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || value.type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)))
        return refuse("Native alias kind does not match the descriptor type");
    if(value.native){
        output=*std::static_pointer_cast<const efmi::frontend::NativeResource>(value.native);
        output.descriptor_type=value.type;
        output.image_layout=value.image_layout;
        if(uniform || storage){
            output.offset=value.offset;output.range=value.range;output.stride=uniform?16u:output.buffer_stride;
            output.format=0;output.format_known=true;output.buffer_view=0;
        }
        return true;
    }
    if (uniform || storage || texel) {
        VkBuffer handle = value.buffer; uint32_t id = value.buffer_id; uint64_t offset = 0, range = 0;
        if (texel) {
            if (!value.buffer_view) return true;
            const auto view = device->buffer_views.find(value.buffer_view);
            if (view == device->buffer_views.end() || view->second.id != value.buffer_view_id) return refuse("Native texel view was destroyed");
            handle = view->second.buffer; id = view->second.buffer_id; offset = view->second.offset; range = view->second.range;
            output.format = dxgi_format(view->second.format); output.format_known = output.format != 0;
        } else {
            if (!handle) return true;
            if (bound.dyn_first > cb->dyn.size() || bound.dyn_count > cb->dyn.size() - bound.dyn_first)
                return refuse("Native descriptor dynamic offsets are unavailable");
            std::vector<BoundRange> ranges;
            collect_buffer_ranges_locked(device,bound,bound.dyn_count ? cb->dyn.data() + bound.dyn_first : nullptr,bound.dyn_count,alias.set,ranges);
            const auto range_info = std::find_if(ranges.begin(),ranges.end(),[&](const BoundRange& r) { return r.binding == alias.binding && r.element == alias.element; });
            if (range_info == ranges.end()) return refuse("Native buffer descriptor range is invalid or unavailable");
            offset = range_info->off; range = range_info->size; output.format_known = true;
            output.stride = uniform ? 16 : 0;
        }
        const auto buffer = device->bufs.find(handle);
        if (buffer == device->bufs.end() || buffer->second.id != id) return refuse("Native descriptor buffer was destroyed");
        if (range == VK_WHOLE_SIZE && offset <= buffer->second.size) range = buffer->second.size - offset;
        if (offset > buffer->second.size || !range || range > buffer->second.size - offset) return refuse("Native descriptor buffer range exceeds its allocation");
        output.bound = true; output.dimension = 1; output.identity = uint64_t(id) << 2;
        output.size = buffer->second.size; output.offset = offset; output.range = range;
        output.buffer=uint64_t(handle);output.buffer_view=uint64_t(value.buffer_view);
        if(buffer->second.lifetime && !buffer->second.lifetime->dependencies.empty()){
            if(texel){
                const auto& owner=device->buffer_views.at(value.buffer_view).lifetime;
                if(owner && !owner->dependencies.empty())output.keep=owner;
            }else output.keep=buffer->second.lifetime;
        }
        const auto usage = buffer->second.usage;
        output.usage=usage;
        output.bind_flags = ((usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ? 1u : 0u) |
            ((usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) ? 2u : 0u) | ((usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) ? 4u : 0u) |
            ((usage & (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)) ? 8u : 0u) |
            ((usage & (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)) ? 128u : 0u);
        return true;
    }
    if (image) {
        device->track_image_layouts.store(true, std::memory_order_relaxed);
        if (!value.image_view) return true;
        const auto view = device->image_views.find(value.image_view);
        if (view == device->image_views.end() || view->second.id != value.image_view_id) return refuse("Native image view was destroyed");
        const auto found_image = device->images.find(view->second.image);
        if (found_image == device->images.end() || found_image->second.id != view->second.image_id) return refuse("Native descriptor image was destroyed");
        return image_resource(value.image_view,view->second,found_image->second,output);
    }
    return refuse("Native descriptor type is not represented by a resource alias");
}
}
