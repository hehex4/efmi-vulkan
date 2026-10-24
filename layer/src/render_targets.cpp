#include "state.h"
#include "json.h"

namespace efvk {
namespace {
constexpr size_t kTargetContexts = 4096, kAttachments = 32;
TargetView view_locked(const Device* dev, VkImageView handle)
{
    TargetView out;
    const auto v = dev->image_views.find(handle);
    if (v == dev->image_views.end()) return out;
    out.view = v->second;
    const auto image = dev->images.find(v->second.image);
    if (image == dev->images.end() || image->second.id != v->second.image_id) return out;
    out.image = image->second; out.known = true; return out;
}
void append(CmdBuf* cb, RenderTargets state)
{
    cb->active_targets = 0;
    if (cb->target_contexts.size() >= kTargetContexts) return;
    cb->target_contexts.push_back(std::move(state));
    cb->active_targets = uint32_t(cb->target_contexts.size());
}
void write_view(JsonWriter& w, const TargetView* view, VkImageLayout layout)
{
    w.begin_object(); w.kb("bound", view != nullptr); w.kn("layout", layout);
    if (view) {
        w.kb("known", view->known); w.kn("view", view->view.id); w.kn("image", view->view.image_id);
        w.kn("format", view->view.format); w.kn("image_format", view->image.format);
        w.kn("samples", view->image.samples); w.kn("usage", view->image.usage); w.kn("type", view->view.type);
        w.key("extent"); w.begin_array(); w.num(view->image.extent.width); w.num(view->image.extent.height); w.num(view->image.extent.depth); w.end_array();
        const auto& r = view->view.range;
        w.key("range"); w.begin_array(); w.num(r.aspectMask); w.num(r.baseMipLevel); w.num(r.levelCount); w.num(r.baseArrayLayer); w.num(r.layerCount); w.end_array();
    }
    w.end_object();
}
}
void on_create_framebuffer(Device* dev, VkFramebuffer handle, const VkFramebufferCreateInfo* info)
{
    if (dev->passive || !g_cfg.track_resources) return;
    auto fb = std::make_shared<FramebufferInfo>();
    fb->width = info->width; fb->height = info->height; fb->layers = info->layers;
    fb->attachment_count = info->attachmentCount;
    fb->imageless = (info->flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT) != 0;
    fb->known = info->attachmentCount <= kAttachments && !(info->flags & ~VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT);
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    fb->id = dev->next_resource_id++;
    if (fb->known && !fb->imageless) {
        if (info->attachmentCount && !info->pAttachments) fb->known = false;
        else for (uint32_t i = 0; i < info->attachmentCount; ++i) fb->views.push_back(view_locked(dev, info->pAttachments[i]));
    }
    dev->framebuffers[handle] = std::move(fb);
}
void on_destroy_framebuffer(Device* dev, VkFramebuffer handle)
{
    if (dev->passive || !g_cfg.track_resources) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx); dev->framebuffers.erase(handle);
}
void on_begin_targets(Device* dev, CmdBuf* cb, const VkRenderPassBeginInfo* info)
{
    cb->active_targets = 0;
    if (!dev->capture_targets.load(std::memory_order_relaxed)) return;
    RenderTargets out; out.area = info->renderArea;
    std::shared_lock<std::shared_mutex> lock(dev->res_mx);
    const auto fb = dev->framebuffers.find(info->framebuffer);
    const auto rp = dev->render_passes.find(info->renderPass);
    if (fb != dev->framebuffers.end()) { out.framebuffer = fb->second; out.layers = fb->second->layers; }
    if (rp != dev->render_passes.end()) out.recipe = rp->second;
    out.known = out.framebuffer && out.framebuffer->known && out.recipe && out.recipe->complete && !out.recipe->subpasses.empty();
    if (!out.known) out.reason = "framebuffer_or_render_pass_unavailable";
    bool late = false; unsigned extensions = 0;
    for (auto* p = static_cast<const VkBaseInStructure*>(info->pNext); p; p = p->pNext) {
        if (++extensions > 32) { out.known = false; out.reason = "render_pass_begin_chain_limit"; break; }
        if (p->sType == VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO) {
            const auto* attachments = reinterpret_cast<const VkRenderPassAttachmentBeginInfo*>(p);
            if (late || !out.framebuffer || !out.framebuffer->imageless || attachments->attachmentCount > kAttachments ||
                attachments->attachmentCount != out.framebuffer->attachment_count || (attachments->attachmentCount && !attachments->pAttachments)) {
                out.known = false; out.reason = "invalid_imageless_attachments"; continue;
            }
            late = true;
            for (uint32_t i = 0; i < attachments->attachmentCount; ++i) out.views.push_back(view_locked(dev, attachments->pAttachments[i]));
        } else {
            out.known = false; out.reason = "render_pass_begin_extension";
        }
    }
    if (out.framebuffer && out.framebuffer->imageless && !late) { out.known = false; out.reason = "imageless_attachments_missing"; }
    append(cb, std::move(out));
}
void on_begin_targets(Device* dev, CmdBuf* cb, const VkRenderingInfo* info)
{
    cb->active_targets = 0;
    if (!dev->capture_targets.load(std::memory_order_relaxed)) return;
    RenderTargets out; out.dynamic = true; out.area = info->renderArea; out.layers = info->layerCount; out.view_mask = info->viewMask;
    out.flags = info->flags;
    out.known = !info->pNext && info->colorAttachmentCount <= 8 && (!info->colorAttachmentCount || info->pColorAttachments);
    if (!out.known) { out.reason = "dynamic_rendering_description"; append(cb, std::move(out)); return; }
    std::shared_lock<std::shared_mutex> lock(dev->res_mx);
    auto keep = [&](const VkRenderingAttachmentInfo& attachment) {
        if (attachment.pNext) { out.known = false; out.reason = "rendering_attachment_extension"; }
        out.views.push_back(view_locked(dev, attachment.imageView));
        out.views.push_back(view_locked(dev, attachment.resolveImageView));
        auto copy = attachment; copy.pNext = nullptr; return copy;
    };
    for (uint32_t i = 0; i < info->colorAttachmentCount; ++i) out.dynamic_colors.push_back(keep(info->pColorAttachments[i]));
    if (info->pDepthAttachment) { out.has_depth = true; out.depth = keep(*info->pDepthAttachment); }
    if (info->pStencilAttachment) { out.has_stencil = true; out.stencil = keep(*info->pStencilAttachment); }
    append(cb, std::move(out));
}
void on_next_targets(Device* dev, CmdBuf* cb)
{
    if (!cb->active_targets || cb->active_targets > cb->target_contexts.size()) return;
    auto out = cb->target_contexts[cb->active_targets - 1]; ++out.subpass;
    if (out.dynamic || !out.recipe || out.subpass >= out.recipe->subpasses.size()) {
        out.known = false; out.reason = "subpass_unavailable";
    }
    append(cb, std::move(out));
}
void write_render_targets(JsonWriter& w, const RenderTargets& out)
{
    w.begin_object(); w.kb("known", out.known); w.kb("dynamic", out.dynamic); w.kv("snapshot", "record");
    if (!out.reason.empty()) w.kv("reason", out.reason);
    w.kn("framebuffer", out.framebuffer ? out.framebuffer->id : 0); w.kn("render_pass", out.recipe ? out.recipe->id : 0);
    w.kn("subpass", out.subpass); w.kn("layers", out.layers);
    w.key("area"); w.begin_array(); w.inum(out.area.offset.x); w.inum(out.area.offset.y); w.num(out.area.extent.width); w.num(out.area.extent.height); w.end_array();
    if (out.dynamic) {
        w.kn("flags", out.flags);
        w.kn("view_mask", out.view_mask); size_t index = 0;
        auto dynamic = [&](const VkRenderingAttachmentInfo& attachment) {
            const auto* view = index < out.views.size() && attachment.imageView ? &out.views[index] : nullptr; ++index;
            const auto* resolve = index < out.views.size() && attachment.resolveMode != VK_RESOLVE_MODE_NONE && attachment.resolveImageView ? &out.views[index] : nullptr; ++index;
            w.begin_object(); w.key("target"); write_view(w, view, attachment.imageLayout);
            w.kn("resolve_mode", attachment.resolveMode); w.key("resolve"); write_view(w, resolve, attachment.resolveImageLayout); w.end_object();
        };
        w.key("colors"); w.begin_array(); for (const auto& a : out.dynamic_colors) dynamic(a); w.end_array();
        w.key("depth"); if (out.has_depth) dynamic(out.depth); else w.null();
        w.key("stencil"); if (out.has_stencil) dynamic(out.stencil); else w.null();
    } else if (out.recipe && out.subpass < out.recipe->subpasses.size()) {
        const auto& sub = out.recipe->subpasses[out.subpass]; w.kn("view_mask", sub.view_mask);
        const auto& views = out.framebuffer && !out.framebuffer->imageless ? out.framebuffer->views : out.views;
        auto attachment = [&](const VkAttachmentReference2& a) {
            if (a.attachment == VK_ATTACHMENT_UNUSED) { write_view(w, nullptr, a.layout); return; }
            TargetView unknown; write_view(w, a.attachment < views.size() ? &views[a.attachment] : &unknown, a.layout);
        };
        auto array = [&](const char* name, const std::vector<VkAttachmentReference2>& refs) {
            w.key(name); w.begin_array(); for (const auto& a : refs) attachment(a); w.end_array();
        };
        array("colors", sub.colors); array("inputs", sub.inputs); array("resolves", sub.resolves);
        w.key("depth"); if (sub.has_depth) attachment(sub.depth); else w.null();
    }
    w.end_object();
}
} // namespace efvk
