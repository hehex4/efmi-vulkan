// Presence is independent of test/write enable, view ownership and resolve destinations.
static void dynamic_depth_cases()
{
    Device d; CmdBuf cb; d.shader_draws = true;
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}, depth = color, stencil = color;
    VkRenderingInfo begin{VK_STRUCTURE_TYPE_RENDERING_INFO};
    begin.colorAttachmentCount = 1; begin.pColorAttachments = &color; begin.pDepthAttachment = &depth;
    uint32_t mask = 999;
    auto snapshot = [&] { on_begin_targets(&d, &cb, &begin); return native_dynamic_outputs(&cb, cb.active_targets, mask); };
    check(snapshot() && !mask, "dynamic null attachments have known absent output state");
    color.imageView = h<VkImageView>(9201);
    check(snapshot() && mask == 1, "nonnull color presence does not acquire or require allocation owners");
    depth.imageView = h<VkImageView>(9202);
    check(snapshot() && mask == 257, "actual depth view is present even without depth testing");
    const auto bound = cb.active_targets;
    depth.imageView = VK_NULL_HANDLE; depth.resolveImageView = h<VkImageView>(9203); depth.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
    check(snapshot() && mask == 1, "resolve handles do not establish depth view presence");
    check(native_dynamic_outputs(&cb, bound, mask) && mask == 257, "earlier draw snapshots retain their own output presence");
    begin.pStencilAttachment = &stencil; stencil.imageView = h<VkImageView>(9204);
    check(snapshot() && mask == 257, "stencil-only view establishes DSV presence");
    depth.imageView = h<VkImageView>(9202);
    check(snapshot() && mask == 257, "separate actual depth and stencil views establish presence without merging resources");
    cb.inherited_rp = true;
    check(!native_dynamic_outputs(&cb, cb.active_targets, mask) && !mask, "secondary inheritance cannot infer parent view presence");
    cb.inherited_rp = false;
    check(!native_dynamic_outputs(&cb, 0, mask), "before Begin has no output context");
    check(!native_dynamic_outputs(&cb, 999, mask), "out-of-range context refuses");
    cb.target_contexts.back().known = false;
    check(!native_dynamic_outputs(&cb, cb.active_targets, mask), "unknown rendering descriptions refuse presence");
    cb.target_contexts.back().known = true; cb.target_contexts.back().dynamic = false;
    check(!native_dynamic_outputs(&cb, cb.active_targets, mask), "classic context uses its existing subpass adapter");
    begin.flags = VK_RENDERING_SUSPENDING_BIT | VK_RENDERING_RESUMING_BIT;
    check(snapshot() && mask == 257, "segmented rendering presence does not grant image synchronization permission");
    cb.reset_recording();
    check(!native_dynamic_outputs(&cb, bound, mask), "recording reset drops old output presence");
}
