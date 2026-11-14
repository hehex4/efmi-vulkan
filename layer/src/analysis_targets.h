// Metadata-only requests for ordinary dynamic-rendering pass-end readback.
#pragma once
#include "state.h"
#include <set>

namespace efvk {
struct AnalysisAttachment {
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t slot = 0, resolve = 0, layers = 0, view_mask = 0;
};
inline bool analysis_dynamic_targets(const RenderTargets& targets, uint32_t options,
    std::vector<AnalysisAttachment>& outputs, std::set<VkImage>& attachments, std::string& why)
{
    outputs.clear(); attachments.clear();
    auto refuse = [&](const char* text) { why = text; return false; };
    if (!targets.known || !targets.dynamic || (targets.flags & ~VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT))
        return refuse("Dynamic attachment capture needs a known ordinary rendering instance");
    const auto expected = 2 * (targets.dynamic_colors.size() + targets.has_depth + targets.has_stencil);
    if (targets.views.size() != expected) return refuse("Dynamic attachment capture view snapshot is incomplete");
    std::vector<AnalysisAttachment> next;
    std::set<VkImage> used;
    size_t index = 0;
    auto append = [&](const VkRenderingAttachmentInfo& info, uint32_t slot, uint32_t category) {
        const auto& view = targets.views[index++]; const auto& resolve = targets.views[index++];
        switch (info.resolveMode) {
        case VK_RESOLVE_MODE_NONE: case VK_RESOLVE_MODE_SAMPLE_ZERO_BIT: case VK_RESOLVE_MODE_AVERAGE_BIT:
        case VK_RESOLVE_MODE_MIN_BIT: case VK_RESOLVE_MODE_MAX_BIT: break;
        default: return refuse("Dynamic attachment resolve mode needs a capture adapter");
        }
        if (!info.imageView) return true; // Standard null attachments ignore store and resolve handles.
        if (!view.known || view.handle != info.imageView) return refuse("Dynamic attachment view identity is unavailable");
        used.insert(view.view.image);
        const bool selected = (options & category) != 0;
        if (selected && info.storeOp == VK_ATTACHMENT_STORE_OP_STORE)
            next.push_back({info.imageView, info.imageLayout, slot, 0, targets.layers, targets.view_mask});
        if (info.resolveMode != VK_RESOLVE_MODE_NONE) {
            if (!info.resolveImageView || !resolve.known || resolve.handle != info.resolveImageView)
                return refuse("Dynamic resolve destination identity is unavailable");
            used.insert(resolve.view.image);
            // Resolve has its own implicit STORE, independent of the multisample source storeOp.
            if (selected) next.push_back({info.resolveImageView, info.resolveImageLayout, slot, 1, targets.layers, targets.view_mask});
        }
        return true;
    };
    for (uint32_t i = 0; i < targets.dynamic_colors.size(); ++i) if (!append(targets.dynamic_colors[i], i, 1)) return false;
    if (targets.has_depth && !append(targets.depth, 8, 2)) return false;
    if (targets.has_stencil && !append(targets.stencil, 9, 2)) return false;
    outputs = std::move(next); attachments = std::move(used); why.clear(); return true;
}
}
