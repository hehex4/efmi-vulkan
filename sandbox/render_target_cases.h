// Attachment identity survives object destruction and secondary execution; no driver required.
static void render_target_cases()
{
    Device d; CmdBuf cb;
    const auto pass = h<VkRenderPass>(101);
    const auto fb = h<VkFramebuffer>(102);
    VkImageView views[] = {h<VkImageView>(103), h<VkImageView>(104)};
    for (unsigned i = 0; i < 2; ++i) {
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.format = i ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
        image.extent = {64, 32, 1}; image.mipLevels = 1; image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.usage = i ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const auto handle = h<VkImage>(201 + i);
        on_create_image(&d, handle, &image);
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = handle; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = image.format;
        view.subresourceRange = {VkImageAspectFlags(i ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1};
        on_create_image_view(&d, views[i], &view);
    }
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM; attachments[1].format = VK_FORMAT_D32_SFLOAT;
    for (auto& a : attachments) a.samples = VK_SAMPLE_COUNT_1_BIT;
    VkAttachmentReference colors[] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED}};
    VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference input{0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkSubpassDescription subs[2]{};
    subs[0].pipelineBindPoint = subs[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subs[0].colorAttachmentCount = 2; subs[0].pColorAttachments = colors; subs[0].pDepthStencilAttachment = &depth;
    subs[1].inputAttachmentCount = 1; subs[1].pInputAttachments = &input;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 2; rp.pAttachments = attachments; rp.subpassCount = 2; rp.pSubpasses = subs;
    on_create_render_pass(&d, pass, &rp);
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = pass; info.attachmentCount = 2; info.pAttachments = views;
    info.width = 64; info.height = 32; info.layers = 1;
    on_create_framebuffer(&d, fb, &info);
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = pass; begin.framebuffer = fb; begin.renderArea = {{3, 4}, {20, 10}};
    on_begin_targets(&d, &cb, &begin);
    check(cb.target_contexts.empty() && !cb.active_targets, "ordinary rendering keeps no target context snapshots");
    d.capture_targets = true;
    on_begin_targets(&d, &cb, &begin);
    check(cb.active_targets == 1 && cb.target_contexts[0].known, "capture records a complete framebuffer and render pass");
    const auto saved = cb.target_contexts[0];
    const auto& color = saved.framebuffer->views[0];
    check(color.known && color.view.image_id == d.images.at(h<VkImage>(201)).id && color.image.extent.width == 64,
          "target identity, view format and image dimensions are retained");
    JsonWriter json; write_render_targets(json, saved);
    check(json.out.find("\"snapshot\":\"record\"") != std::string::npos &&
          json.out.find("\"bound\":false") != std::string::npos && json.out.find("\"area\":[3,4,20,10]") != std::string::npos &&
          json.out.find("\"depth\":{\"bound\":true") != std::string::npos, "target JSON exposes timing, unused slots, depth and render area");
    on_next_targets(&d, &cb);
    check(cb.active_targets == 2 && cb.target_contexts[1].subpass == 1 && cb.target_contexts[1].known &&
          cb.target_contexts[0].subpass == 0, "next subpass retains earlier draw targets separately");
    json.out.clear(); write_render_targets(json, cb.target_contexts[1]);
    check(json.out.find("\"inputs\":[{\"bound\":true,\"layout\":5") != std::string::npos &&
          json.out.find("\"colors\":[]") != std::string::npos, "input attachment aliases keep their subpass layout");
    on_next_targets(&d, &cb);
    check(!cb.target_contexts.back().known, "an unavailable subpass is explicit, not inferred");
    on_destroy_framebuffer(&d, fb); on_destroy_render_pass(&d, pass);
    check(saved.framebuffer->views[0].view.id == color.view.id && saved.recipe->subpasses.size() == 2,
          "recorded attachment metadata survives framebuffer and pass destruction");
    on_create_render_pass(&d, pass, &rp); on_create_framebuffer(&d, fb, &info);
    check(saved.framebuffer->id != d.framebuffers.at(fb)->id && saved.recipe->id != d.render_passes.at(pass)->id,
          "reused framebuffer and render pass handles cannot change prior identities");
    info.flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT; info.pAttachments = nullptr;
    on_create_framebuffer(&d, fb, &info);
    VkRenderPassAttachmentBeginInfo late{VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO};
    late.attachmentCount = 2; late.pAttachments = views; begin.pNext = &late;
    on_begin_targets(&d, &cb, &begin);
    check(cb.target_contexts.back().known && cb.target_contexts.back().views[1].view.id == d.image_views.at(views[1]).id,
          "imageless framebuffer resolves the attachments supplied at Begin");
    late.attachmentCount = 1; on_begin_targets(&d, &cb, &begin);
    check(!cb.target_contexts.back().known, "imageless attachment count mismatch is unknown");
    begin.pNext = nullptr; on_begin_targets(&d, &cb, &begin);
    check(!cb.target_contexts.back().known, "missing imageless views are unknown");
    VkRenderingAttachmentInfo dynamic_color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    dynamic_color.imageView = views[0]; dynamic_color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    dynamic_color.resolveImageView = views[0]; dynamic_color.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    dynamic_color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    VkRenderingAttachmentInfo dynamic_depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    dynamic_depth.imageView = views[1]; dynamic_depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = begin.renderArea; rendering.layerCount = 1; rendering.viewMask = 3;
    rendering.flags = VK_RENDERING_SUSPENDING_BIT; rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &dynamic_color; rendering.pDepthAttachment = &dynamic_depth;
    on_begin_targets(&d, &cb, &rendering);
    const auto dynamic = cb.target_contexts.back();
    check(dynamic.known && dynamic.dynamic && dynamic.has_depth && !dynamic.has_stencil && dynamic.views.size() == 4,
          "dynamic rendering keeps color, resolve and depth facts");
    json.out.clear(); write_render_targets(json, dynamic);
    check(json.out.find("\"view_mask\":3") != std::string::npos && json.out.find("\"resolve_mode\":2") != std::string::npos &&
          json.out.find("\"flags\":2") != std::string::npos, "dynamic rendering exposes view mask, suspend flags and resolve mode");
    VkBaseInStructure unknown{VK_STRUCTURE_TYPE_MAX_ENUM}; rendering.pNext = &unknown;
    on_begin_targets(&d, &cb, &rendering);
    check(!cb.target_contexts.back().known && cb.target_contexts.back().reason == "dynamic_rendering_description",
          "unknown rendering extensions do not masquerade as complete target data");
    const auto child_handle = h<VkCommandBuffer>(301);
    auto child = std::make_unique<CmdBuf>(); child->target_contexts.push_back(dynamic);
    DrawRec draw; draw.targets = 1; child->draws.push_back(draw); d.cbs[child_handle] = std::move(child);
    cb.reset_recording(); cb.target_contexts.push_back(saved);
    on_execute_commands(&d, &cb, 1, &child_handle);
    check(cb.draws[0].targets == 2 && cb.target_contexts.size() == 2, "secondary target references rebase into the primary");
    cb.in_rp = true; cb.rp_count = 1; cb.active_targets = 1;
    on_execute_commands(&d, &cb, 1, &child_handle);
    check(cb.draws[1].targets == 1 && cb.target_contexts.size() == 2, "secondary draws inside a primary pass inherit its targets");
    cb.reset_recording();
    check(!cb.active_targets && cb.target_contexts.empty(), "command reset retires target metadata");
    cb.target_contexts.resize(4096); rendering.pNext = nullptr;
    on_begin_targets(&d, &cb, &rendering);
    check(cb.target_contexts.size() == 4096 && !cb.active_targets, "capture target storage is bounded per command buffer");
    d.passive = true; on_create_framebuffer(&d, h<VkFramebuffer>(501), &info);
    check(!d.framebuffers.count(h<VkFramebuffer>(501)), "passive devices do not track framebuffers");
}
