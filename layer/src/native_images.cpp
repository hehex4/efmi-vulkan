#include "native_images.h"
#include "native_descriptors.h"
#include "command_owner.h"
#include "../../frontend/gpu/native_shader.h"
#include "../../frontend/gpu/source_registers.h"
#include "../../frontend/gpu/image_formats.h"

namespace efvk {
using ImageResource = efmi::frontend::NativeResource;
namespace {
constexpr size_t kImageCells = 1024;
const char image_scope_owner = 0;
struct ImageScopeOwner : CommandOwner {
    std::vector<std::shared_ptr<const void>> resources;
    void reset_after_completion() override { resources.clear(); used = false; }
    bool seal() override { return false; }
};
// Caller holds res_mx. Pipeline identities are never reused within a device.
std::shared_ptr<const NativeImageCache::Access> access_locked(Device* device, VkPipeline pipeline)
{
    const auto found = device->pipes.find(pipeline);
    if (found == device->pipes.end()) return {};
    std::lock_guard<std::mutex> lock(device->native_images_mx);
    if (!device->native_image_cache) device->native_image_cache = std::make_shared<NativeImageCache>();
    auto& entries = device->native_image_cache->pipelines;
    const auto prior = entries.find(found->second.id);
    if (prior != entries.end()) return prior->second;
    if (entries.size() >= 4096) entries.clear();
    auto result = std::make_shared<NativeImageCache::Access>();
    for (const auto& stage : found->second.shaders) {
        std::vector<efmi::gpu::SourceRegister> registers;
        std::vector<std::pair<uint32_t, uint32_t>> used;
        if (!stage.shader || !efmi::gpu::source_registers(stage.shader->code, registers, result->reason) ||
            !efmi::gpu::native_shader_image_bindings(stage.shader->code, stage.entry, used, result->reason)) {
            result->reason = "Native application image consumer reflection is unavailable"; break;
        }
        const bool depth = efmi::gpu::image_depth_reference(stage.shader->code);
        std::vector<efmi::gpu::NativeStorageUse> storage;
        const bool formatless=std::any_of(registers.begin(),registers.end(),[](const auto& r){
            return r.kind==efmi::gpu::SourceRegister::ImageUav && !r.image_format;
        });
        if(formatless && !efmi::gpu::native_shader_storage_usage(stage.shader->code,storage,result->reason))break;
        for (const auto& resource : registers)
            if (std::find(used.begin(), used.end(), std::make_pair(resource.set, resource.binding)) != used.end()){
                NativeImageCache::Use consumer{{resource.set,resource.binding,resource.dim,resource.depth,
                    resource.kind==efmi::gpu::SourceRegister::ImageSrv,resource.arrayed,resource.multisampled,resource.sampled_float32,
                    resource.scalar_type,resource.image_format},depth};
                for(const auto& use:storage)if(use.set==resource.set && use.binding==resource.binding){
                    consumer.read|=use.read;consumer.write|=use.write;consumer.atomic|=use.atomic;
                }
                result->images.push_back(consumer);
            }
    }
    entries[found->second.id] = result;
    return result;
}
bool formatless_binding(Device* device,uint32_t format,const NativeImageCache::Use& use,std::string& why)
{
    auto refuse=[&](const char* text){why=text;return false;};
    if(use.atomic)return refuse("Native image atomics require a declared storage format");
    const VkFormatFeatureFlags2 required=(use.read?VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT:0)|
        (use.write?VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT:0);
    if(!required)return true;
    if((use.read && !device->enabled_features.shaderStorageImageReadWithoutFormat) ||
        (use.write && !device->enabled_features.shaderStorageImageWriteWithoutFormat))
        return refuse("Native formatless storage access feature is not enabled");
    const uint32_t api=std::min(device->app_api_version?device->app_api_version:VK_API_VERSION_1_0,device->api_version);
    if(!device->format_properties2 || (api<VK_API_VERSION_1_3 &&
        std::find(device->extensions.begin(),device->extensions.end(),VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME)==device->extensions.end()))
        return refuse("Native formatless storage requires enabled format feature queries");
    std::lock_guard<std::mutex> lock(device->native_images_mx);
    auto& formats=device->native_image_cache->storage_formats;
    auto found=formats.find(format);
    if(found==formats.end()){
        VkFormatProperties3 extra{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};VkFormatProperties2 properties{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
        properties.pNext=&extra;device->format_properties2(device->phys,VkFormat(format),&properties);
        found=formats.emplace(format,extra.optimalTilingFeatures).first;
    }
    return (found->second&required)==required || refuse("Native storage image format does not support the required unformatted access");
}
bool range_of(const ImageResource& resource, VkImageSubresourceRange& range)
{
    range = {resource.aspect, resource.base_mip, resource.mip_count, resource.base_layer, resource.layer_count};
    if (range.baseMipLevel >= resource.levels || range.baseArrayLayer >= resource.layers) return false;
    if (range.levelCount == VK_REMAINING_MIP_LEVELS) range.levelCount = resource.levels - range.baseMipLevel;
    if (range.layerCount == VK_REMAINING_ARRAY_LAYERS) range.layerCount = resource.layers - range.baseArrayLayer;
    return range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && range.levelCount && range.layerCount &&
        range.levelCount <= resource.levels - range.baseMipLevel && range.layerCount <= resource.layers - range.baseArrayLayer &&
        uint64_t(range.levelCount) * range.layerCount <= kImageCells;
}
std::shared_ptr<NativeImageState> scope(CmdBuf* cb, bool compute)
{
    if (!cb->native_image_state) {
        cb->native_image_state = std::make_shared<NativeImageState>();
        cb->native_image_state->compute = compute;
        if (!compute && cb->pass.info && cb->pass.info->known && !cb->pass.info->imageless) {
            cb->native_image_state->attachments_known = true;
            for (const auto& view : cb->pass.info->views)
                if (view.known) cb->native_image_state->attachments.insert(view.image.id);
                else cb->native_image_state->attachments_known = false;
        }
    }
    return cb->native_image_state;
}
bool read_layout(VkImageLayout layout) { return layout == VK_IMAGE_LAYOUT_GENERAL || layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }
void report(Device* device, CmdBuf* cb, const std::string& reason)
{
    if (reason.empty()) return;
    cb->native_image_reason = reason;
    const auto count = device->native_image_refused.fetch_add(1) + 1;
    if (count <= 8 || !(count & (count - 1))) logf("Native image reference suspended: %s", reason.c_str());
}
DrawRec current(CmdBuf* cb, bool compute)
{
    DrawRec draw; draw.kind = compute ? DK_DISPATCH : DK_DRAW;
    draw.pipeline = compute ? cb->comp_pipeline : cb->gfx_pipeline;
    const auto* sets = compute ? cb->comp_sets : cb->sets;
    std::copy(sets, sets + kMaxSets, std::begin(draw.sets));
    return draw;
}
}

void native_images_observe(Device* device, CmdBuf* cb, const DrawRec& draw)
{
    if (!device->track_image_layouts.load(std::memory_order_relaxed)) return;
    auto state = scope(cb, draw.kind == DK_DISPATCH || draw.kind == DK_DISPATCH_INDIRECT);
    state->required.clear();
    std::shared_lock<std::shared_mutex> lock(device->res_mx);
    const auto access = access_locked(device, draw.pipeline);
    if (!access || !access->reason.empty()) return;
    for (const auto& use : access->images) {
        const auto& r = use.resource;
        if (r.set >= kMaxSets) continue;
        const auto& bound = draw.sets[r.set];
        const DescriptorSetInfo* contents = bound.pushed.get();
        const auto found = device->descriptor_sets.find(bound.set);
        if (!contents && found != device->descriptor_sets.end() && found->second.id == bound.id) contents = &found->second;
        if (!contents || contents->unsupported || bound.unsupported || !contents->layout) continue;
        const auto declaration = contents->layout->bindings.find(r.binding);
        if (declaration == contents->layout->bindings.end() || declaration->second.count != 1 || declaration->second.flags) continue;
        const auto value = contents->values.find({r.binding, 0});
        if (value == contents->values.end()) continue;
        const auto& original = value->second.application ? *value->second.application : value->second;
        if (!original.known || !original.image_view) continue;
        uint32_t id = 0, levels = 0, layers = 0; VkImageSubresourceRange range{};
        if (original.native) {
            const auto& resource = *std::static_pointer_cast<const ImageResource>(original.native);
            if (resource.uploaded_readonly || resource.uploaded_mutable || !resource.image || !resource.image_safe) continue;
            id = uint32_t(resource.identity >> 2); levels = resource.levels; layers = resource.layers;
            range = {resource.aspect, resource.base_mip, resource.mip_count, resource.base_layer, resource.layer_count};
        } else {
            const auto view = device->image_views.find(original.image_view);
            if (view == device->image_views.end() || view->second.id != original.image_view_id) continue;
            const auto image = device->images.find(view->second.image);
            if (image == device->images.end() || image->second.id != view->second.image_id || !image->second.capture_safe) continue;
            id = image->second.id; levels = image->second.levels; layers = image->second.layers; range = view->second.range;
        }
        if (!cb->image_layouts.require(id, levels, layers, range, VkImageLayout(original.image_layout))) continue;
        if (range.baseMipLevel >= levels || range.baseArrayLayer >= layers) continue;
        if (range.levelCount == VK_REMAINING_MIP_LEVELS) range.levelCount = levels - range.baseMipLevel;
        if (range.layerCount == VK_REMAINING_ARRAY_LAYERS) range.layerCount = layers - range.baseArrayLayer;
        if (range.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT || uint64_t(range.levelCount) * range.layerCount > kImageCells) continue;
        for (uint32_t mip = 0; mip < range.levelCount; ++mip) for (uint32_t layer = 0; layer < range.layerCount; ++layer)
            state->required[{(uint64_t(id) << 2) | 2, range.baseMipLevel + mip, range.baseArrayLayer + layer}] = VkImageLayout(original.image_layout);
    }
}

// Caller holds res_mx; planning changes metadata only, never GPU state.
bool native_image_binding(Device* device, CmdBuf* cb, const DrawRec& draw, const efmi::frontend::NativeBinding& target,
                          const std::shared_ptr<const ImageResource>& resource, NativeImagePlan& plan, VkImageLayout& layout, std::string& why)
{
    auto refuse = [&](const char* text) { why = text; return false; };
    const auto& image = *resource;
    const bool compute = draw.kind == DK_DISPATCH || draw.kind == DK_DISPATCH_INDIRECT;
    const bool storage = target.kind == efmi::frontend::NativeBinding::Access;
    if (device->application_queue_count != 1 || device->material_queues.size() != 1)
        return refuse("Native application images require one application graphics queue");
    if (device->synchronization2_enabled) return refuse("Native application image accesses require a synchronization2 signal-scope adapter");
    if(storage && !compute)return refuse("Native graphics storage images require in-pass hazard tracking");
    const uint32_t usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (compute?VK_IMAGE_USAGE_STORAGE_BIT:0);
    VkImageSubresourceRange range;
    const bool cube=image.view_type==VK_IMAGE_VIEW_TYPE_CUBE || image.view_type==VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    const bool array=image.view_type==VK_IMAGE_VIEW_TYPE_2D_ARRAY || image.view_type==VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    const bool shape=image.view_type==VK_IMAGE_VIEW_TYPE_2D || image.view_type==VK_IMAGE_VIEW_TYPE_2D_ARRAY || (!storage && cube);
    if (!image.image || !image.image_view || !image.keep || !image.image_safe ||
        !(image.usage & (storage?VK_IMAGE_USAGE_STORAGE_BIT:VK_IMAGE_USAGE_SAMPLED_BIT)) ||
        (image.usage & ~usage) || image.samples != 1 || !shape ||
        image.view_format >= std::size(efmi::gpu::image_formats) || !efmi::gpu::image_formats[image.view_format].scalar_type ||
        !range_of(image, range) || (storage && range.levelCount!=1))
        return refuse("Native application image needs an owned optimal typed color view with compatible usage and range");
    const auto& format=efmi::gpu::image_formats[image.view_format];
    const auto access = access_locked(device, draw.pipeline);
    if (!access || !access->reason.empty()) return refuse("Native application image consumer reflection is unavailable");
    bool used = false;
    for (const auto& use : access->images) if (use.resource.set == target.set && use.resource.binding == target.binding) {
        const auto& r = use.resource; used = true;
        if (r.sampled == storage || r.dim != (cube?3u:1u) || r.depth == 1 || use.depth_reference ||
            r.arrayed != array || r.multisampled || r.scalar_type!=format.scalar_type)
            return refuse("Native application image shape differs from its non-depth typed consumer or numeric class");
        if(storage){
            if(r.image_format && (!format.storage_format || r.image_format!=format.storage_format))
                return refuse("Native storage image view differs from the declared shader format");
            if(!r.image_format && !formatless_binding(device,image.view_format,use,why))return false;
        }
    }
    // Private file allocations never enter application layout tracking. Every native alias
    // uses GENERAL, complete upload ownership is retained, and persistent compute bindings
    // receive call-boundary memory dependencies even without another override plan.
    if(image.uploaded_mutable){
        if(!compute || image.image_layout!=VK_IMAGE_LAYOUT_GENERAL)
            return refuse("Mutable native file images require compute scope and GENERAL publication");
        layout=VK_IMAGE_LAYOUT_GENERAL;return true;
    }
    if (!used) { layout = VK_IMAGE_LAYOUT_GENERAL; return true; }
    auto state = scope(cb, compute);
    if (!compute && (image.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
        (!state->attachments_known || state->attachments.count(uint32_t(image.identity >> 2))))
        return refuse("Native application image overlaps or may overlap this pass's attachments");
    std::vector<std::pair<NativeImagePlan::Key, VkImageLayout>> cells;
    for (uint32_t mip = 0; mip < range.levelCount; ++mip) for (uint32_t layer = 0; layer < range.layerCount; ++layer) {
        const NativeImagePlan::Key key{image.identity, range.baseMipLevel + mip, range.baseArrayLayer + layer};
        VkImageLayout current_layout;
        const auto active = state->active.cells.find(key), planned=plan.cells.find(key);
        if(active!=state->active.cells.end())current_layout=active->second.after;
        else if(planned!=plan.cells.end())current_layout=planned->second.after;
        else if (!cb->image_layouts.get(uint32_t(image.identity >> 2), image.levels, image.layers,
                    {VK_IMAGE_ASPECT_COLOR_BIT, range.baseMipLevel + mip, 1, range.baseArrayLayer + layer, 1}, current_layout))
            return refuse("Native application image subresource layout is unknown in this recording");
        cells.emplace_back(key, current_layout);
    }
    layout = cells.front().second;
    bool uniform = storage ? layout==VK_IMAGE_LAYOUT_GENERAL : read_layout(layout);
    for (const auto& cell : cells) uniform &= cell.second == layout;
    const auto wanted=uniform?layout:storage?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    NativeImagePlan next = plan;
    for(const auto& cell:cells){
        const auto prior=next.layouts.find(cell.first),active=state->active.layouts.find(cell.first);
        if((prior!=next.layouts.end() && prior->second!=wanted) ||
            (active!=state->active.layouts.end() && active->second!=wanted))
            return refuse("Native image descriptor layouts conflict within the active scope");
        if(next.layouts.size()+state->active.layouts.size()>=kImageCells &&
            prior==next.layouts.end() && active==state->active.layouts.end())
            return refuse("Native image descriptor cell budget exceeded");
        next.layouts[cell.first]=wanted;
    }
    if (uniform) { plan=std::move(next); return true; }
    if (!compute && (cb->in_rp || cb->inherited_rp)) return refuse("Native image layout transition must be prepared before the render pass");
    for (const auto& cell : cells) {
        if (cell.second == wanted) continue;
        if (cell.second != VK_IMAGE_LAYOUT_GENERAL && cell.second != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
            cell.second != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && cell.second != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
            cell.second != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            return refuse("Native application image layout cannot be temporarily accessed");
        const auto required = state->required.find(cell.first);
        if ((required != state->required.end() && required->second != wanted) ||
            (!storage && cell.second == VK_IMAGE_LAYOUT_GENERAL) || state->active.cells.count(cell.first) || plan.cells.count(cell.first))
            return refuse("Native image transition conflicts with application or native image use");
        if (next.cells.size() + state->active.cells.size() >= kImageCells && !next.cells.count(cell.first))
            return refuse("Native image transition cell budget exceeded");
        next.cells[cell.first] = {resource, std::get<1>(cell.first), std::get<2>(cell.first), cell.second,wanted,storage};
    }
    plan = std::move(next); layout = wanted; return true;
}

void record_native_images(Device* device, CmdBuf* cb, const NativeImagePlan& plan)
{
    if (plan.cells.empty() && plan.layouts.empty()) return;
    auto state = scope(cb, !cb->in_rp && !cb->inherited_rp);
    state->active.layouts.insert(plan.layouts.begin(),plan.layouts.end());
    if(plan.cells.empty())return;
    auto owner = std::static_pointer_cast<ImageScopeOwner>(owner_for(cb->owners, &image_scope_owner, [] {
        auto result = std::make_shared<ImageScopeOwner>(); result->backend = &image_scope_owner; return result;
    }));
    std::vector<VkImageMemoryBarrier> barriers;
    for (const auto& cell : plan.cells) {
        if (state->active.cells.count(cell.first)) continue;
        const auto& value = cell.second;
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | (value.writable?VK_ACCESS_SHADER_WRITE_BIT:0);
        barrier.oldLayout = value.before; barrier.newLayout = value.after;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = VkImage(value.resource->image); barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, value.mip, 1, value.layer, 1};
        barriers.push_back(barrier); state->active.cells.emplace(cell);
        owner->resources.push_back(value.resource->keep); owner->used = true;
    }
    if (!barriers.empty()) {
        device->d.CmdPipelineBarrier(cb->handle, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, uint32_t(barriers.size()), barriers.data());
        device->native_image_transitions.fetch_add(barriers.size());
    }
}

void native_images_end(Device* device, CmdBuf* cb)
{
    cb->native_graphics_ordered=false;
    auto state = std::move(cb->native_image_state);
    if (!state && !cb->native_images && !cb->native_mutable) return;
    std::vector<VkImageMemoryBarrier> barriers;
    if(state)for (const auto& entry : state->active.cells) {
        const auto& value = entry.second;
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | (value.writable?VK_ACCESS_SHADER_WRITE_BIT:0);
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.oldLayout = value.after; barrier.newLayout = value.before;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = VkImage(value.resource->image); barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, value.mip, 1, value.layer, 1};
        barriers.push_back(barrier);
    }
    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};memory.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;
    memory.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    device->d.CmdPipelineBarrier(cb->handle, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memory, 0, nullptr, uint32_t(barriers.size()), barriers.data());
}

void native_images_call(Device* device, CmdBuf* cb, bool compute)
{
    // Mutable descriptors persist even when the next application's dispatch has no native plan.
    // DX11 orders that context implicitly; these files need explicit dependencies at every call.
    if(compute && cb->native_mutable){
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        device->d.CmdPipelineBarrier(cb->handle,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    }
    if (!device->track_image_layouts.load(std::memory_order_relaxed) && !cb->native_images) return;
    cb->native_image_blocked = false;
    if (compute) cb->native_image_state.reset();
    scope(cb, compute);
    const auto draw = current(cb, compute);
    if(compute && cb->native_images && !cb->native_mutable){
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        device->d.CmdPipelineBarrier(cb->handle,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    }
    native_images_observe(device, cb, draw);
    if (cb->native_images) { std::string reason; if (!refresh_native_images(device, cb, draw, reason)) report(device, cb, reason); }
}

namespace {
void order_native_graphics(Device* device,CmdBuf* cb)
{
    cb->native_graphics_ordered=false;
    if(cb->inherited_rp || (!cb->native_images && !cb->native_uniforms && !device->native_binding_order.load(std::memory_order_relaxed)))return;
    std::call_once(device->native_barrier_load,[&]{if(device->owned_gdpa)device->native_barrier=
        reinterpret_cast<PFN_vkCmdPipelineBarrier>(device->owned_gdpa(device->handle,"vkCmdPipelineBarrier"));});
    if(!device->native_barrier)return;
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT;
    device->native_barrier(cb->handle,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    cb->native_graphics_ordered=true;
}
}

void native_images_begin(Device* device, CmdBuf* cb, const VkRenderPassBeginInfo* info)
{
    order_native_graphics(device,cb);
    if (!device->track_image_layouts.load(std::memory_order_relaxed) && !cb->native_images) return;
    cb->native_image_state = std::make_shared<NativeImageState>();
    auto& state = *cb->native_image_state;
    {
        std::shared_lock<std::shared_mutex> lock(device->res_mx);
        const auto framebuffer = device->framebuffers.find(info->framebuffer);
        if (framebuffer != device->framebuffers.end() && framebuffer->second->known && !framebuffer->second->imageless) {
            state.attachments_known = true;
            for (const auto& view : framebuffer->second->views)
                if (view.known) state.attachments.insert(view.image.id); else state.attachments_known = false;
        }
    }
    if (cb->native_images) {
        const auto draw = current(cb, false); std::string reason;
        if (!refresh_native_images(device, cb, draw, reason)) report(device, cb, reason);
    }
}

void native_images_begin(Device* device, CmdBuf* cb, const VkRenderingInfo*)
{
    order_native_graphics(device,cb);
    if (!device->track_image_layouts.load(std::memory_order_relaxed) && !cb->native_images) return;
    cb->native_image_state = std::make_shared<NativeImageState>();
    if (cb->native_images) { const auto draw = current(cb, false); std::string reason;
        if (!refresh_native_images(device, cb, draw, reason)) report(device, cb, reason); }
}
}
