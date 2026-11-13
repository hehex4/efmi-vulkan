#include "native_resources.h"
#include "texture_identity.h"

namespace efvk {
bool native_resource(Device* device, CmdBuf* cb, const DrawRec& draw, const efmi::frontend::NativeBinding& alias,
                     efmi::frontend::NativeResource& output, std::string& why)
{
    using B = efmi::frontend::NativeBinding;
    auto refuse = [&](const char* reason) { why = reason; return false; };
    output = {};
    if (!device || !cb || alias.set >= kMaxSets) return refuse("Native descriptor set is outside tracked slots");
    std::shared_lock<std::shared_mutex> lock(device->res_mx);
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
        const auto& resource = found_image->second;
        output.image_view=uint64_t(value.image_view);output.usage=resource.usage;
        output.image=uint64_t(view->second.image);output.image_view_id=view->second.id;
        output.view_type=view->second.type;output.view_format=view->second.format;
        const auto& range=view->second.range;
        output.aspect=range.aspectMask;output.base_mip=range.baseMipLevel;output.mip_count=range.levelCount;
        output.base_layer=range.baseArrayLayer;output.layer_count=range.layerCount;
        output.image_safe=resource.capture_safe && resource.tiling==VK_IMAGE_TILING_OPTIMAL && view->second.native_safe;
        if(resource.lifetime && !resource.lifetime->dependencies.empty() && view->second.lifetime && !view->second.lifetime->dependencies.empty())
            output.keep=view->second.lifetime;
        output.bound = true; output.dimension = uint32_t(resource.type) + 2;
        output.identity = (uint64_t(resource.id) << 2) | 2;
        output.width = resource.extent.width; output.height = resource.extent.height; output.depth = resource.extent.depth;
        output.levels = resource.levels; output.layers = resource.layers; output.samples = resource.samples;
        output.format = dxgi_view_format(view->second.format,view->second.range.aspectMask); output.format_known = output.format != 0;
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
    return refuse("Native descriptor type is not represented by a resource alias");
}
}
