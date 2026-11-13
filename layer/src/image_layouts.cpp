#include "state.h"

namespace efvk {
namespace {
template<class Barrier> void image_barrier(Device* device, CmdBuf* cb, const Barrier& barrier, bool unsupported, bool source)
{
    const auto found = device->images.find(barrier.image);
    if (found == device->images.end()) return;
    const auto& image = found->second;
    if (source && barrier.oldLayout == barrier.newLayout && barrier.srcQueueFamilyIndex == barrier.dstQueueFamilyIndex && !unsupported && !barrier.pNext)
        return;
    cb->image_layouts.barrier(image.id, image.levels, image.layers, barrier.subresourceRange,
        barrier.oldLayout, barrier.newLayout, barrier.srcQueueFamilyIndex, barrier.dstQueueFamilyIndex,
        device->synchronization2_enabled, unsupported || barrier.pNext || !image.capture_safe || source);
}
}

void on_image_barriers(Device* device, CmdBuf* cb, uint32_t count, const VkImageMemoryBarrier* barriers)
{
    if (!device->track_image_layouts.load(std::memory_order_relaxed)) return;
    std::shared_lock<std::shared_mutex> lock(device->res_mx);
    for (uint32_t i = 0; i < count; ++i) image_barrier(device, cb, barriers[i], false, false);
}

void on_image_barriers(Device* device, CmdBuf* cb, const VkDependencyInfo* dependency, bool event_source)
{
    if (!dependency || !device->track_image_layouts.load(std::memory_order_relaxed)) return;
    if (dependency->pNext) { cb->image_layouts.invalidate_all(); return; }
    std::shared_lock<std::shared_mutex> lock(device->res_mx);
    for (uint32_t i = 0; i < dependency->imageMemoryBarrierCount; ++i)
        image_barrier(device, cb, dependency->pImageMemoryBarriers[i], dependency->pNext != nullptr, event_source);
}

void on_image_pass(Device* device, CmdBuf* cb, bool ending)
{
    if (!device->track_image_layouts.load(std::memory_order_relaxed)) return;
    const auto& pass = cb->pass;
    // Dynamic rendering does not perform implicit layout transitions.
    if (!pass.render_pass) return;
    if (!pass.info || !pass.info->known || pass.info->imageless || !pass.recipe || !pass.recipe->complete ||
        pass.subpass >= pass.recipe->subpasses.size()) { cb->image_layouts.invalidate_all(); return; }
    const auto& recipe = *pass.recipe;
    uint32_t views = 0;
    for (const auto& subpass : recipe.subpasses) views |= subpass.view_mask;
    auto apply = [&](uint32_t attachment, VkImageLayout layout, uint32_t aspects = 0) {
        if (attachment == VK_ATTACHMENT_UNUSED) return;
        if (attachment >= pass.info->views.size() || attachment >= recipe.attachments.size()) {
            cb->image_layouts.invalidate_all(); return;
        }
        const auto& target = pass.info->views[attachment];
        if (!target.known) { cb->image_layouts.invalidate_all(); return; }
        const auto& image = target.image;
        auto range = target.view.range;
        if (aspects) range.aspectMask &= aspects;
        if (!range.aspectMask) return;
        if (!image.capture_safe || image.type == VK_IMAGE_TYPE_3D || recipe.attachments[attachment].flags)
            layout = VK_IMAGE_LAYOUT_MAX_ENUM;
        const uint32_t layers = range.layerCount == VK_REMAINING_ARRAY_LAYERS && range.baseArrayLayer < image.layers ?
            image.layers - range.baseArrayLayer : range.layerCount;
        if (views) {
            const auto base = range.baseArrayLayer; range.layerCount = 1;
            for (uint32_t bit = 0; bit < 32; ++bit) if (views & (1u << bit)) {
                if (bit >= layers) { cb->image_layouts.invalidate_all(); return; }
                range.baseArrayLayer = base + bit;
                cb->image_layouts.set(image.id, image.levels, image.layers, range, layout);
            }
        } else {
            if (!pass.info->layers || pass.info->layers > layers) { cb->image_layouts.invalidate_all(); return; }
            range.layerCount = pass.info->layers;
            cb->image_layouts.set(image.id, image.levels, image.layers, range, layout);
        }
    };
    auto references = [&](const RenderSubpass& subpass, const auto& visit) {
        for (const auto& r : subpass.colors) visit(r);
        for (const auto& r : subpass.resolves) visit(r);
        for (const auto& r : subpass.inputs) visit(r);
        if (subpass.has_depth) visit(subpass.depth);
    };
    std::vector<bool> used(recipe.attachments.size()), current(recipe.attachments.size());
    for (const auto& subpass : recipe.subpasses) references(subpass, [&](const VkAttachmentReference2& r) {
        if (r.attachment < used.size()) used[r.attachment] = true;
    });
    references(recipe.subpasses[pass.subpass], [&](const VkAttachmentReference2& r) {
        if (r.attachment < current.size()) current[r.attachment] = true;
    });
    for (uint32_t i = 0; i < used.size(); ++i) if (used[i]) {
        if (ending) apply(i, recipe.attachments[i].finalLayout);
        // Initial/final transitions may occur around other subpasses. Only the
        // current subpass's actual attachment uses establish an in-pass layout.
        else if (!current[i]) apply(i, VK_IMAGE_LAYOUT_MAX_ENUM);
    }
    if (!ending) references(recipe.subpasses[pass.subpass], [&](const VkAttachmentReference2& r) { apply(r.attachment, r.layout, r.aspectMask); });
}
}
