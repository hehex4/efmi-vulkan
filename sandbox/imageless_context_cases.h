// Imageless runtime views belong to one pass instance, including its implicit transitions.
static void imageless_context_cases()
{
    Device d;CmdBuf cb;d.track_image_layouts=true;
    const auto pass=h<VkRenderPass>(8901);const auto fb=h<VkFramebuffer>(8902);
    VkImageView handles[]={h<VkImageView>(8903),h<VkImageView>(8904)};uint32_t ids[2]{};
    for(uint32_t i=0;i<2;++i){
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;image.extent={8,8,1};
        image.format=VK_FORMAT_R8G8B8A8_UNORM;image.mipLevels=2;image.arrayLayers=2;image.samples=VK_SAMPLE_COUNT_1_BIT;
        image.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        const auto handle=h<VkImage>(8910+i);on_create_image(&d,handle,&image);ids[i]=d.images.at(handle).id;
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=handle;view.viewType=VK_IMAGE_VIEW_TYPE_2D;
        view.format=image.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,1,1,1,1};on_create_image_view(&d,handles[i],&view);
    }
    VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R8G8B8A8_UNORM;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};
    sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&color;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=1;rp.pAttachments=&attachment;rp.subpassCount=1;rp.pSubpasses=&sub;
    on_create_render_pass(&d,pass,&rp);
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fi.flags=VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
    fi.renderPass=pass;fi.attachmentCount=1;fi.width=fi.height=4;fi.layers=1;on_create_framebuffer(&d,fb,&fi);
    const auto template_info=d.framebuffers.at(fb);
    VkImageView selected=handles[0];VkRenderPassAttachmentBeginInfo late{VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO};late.attachmentCount=1;late.pAttachments=&selected;
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass;begin.framebuffer=fb;begin.pNext=&late;
    on_begin_targets(&d,&cb,&begin);on_begin_pass(&d,&cb,&begin);
    check(cb.active_targets && !d.shader_draws && !d.capture_targets,"layout tracking resolves late attachments without hunting or active shader hooks");
    check(cb.pass.info && cb.pass.info->imageless && cb.pass.info->views_resolved && cb.pass.info->views.size()==1 &&
          cb.pass.info->views[0].image.id==ids[0],"pass instance retains exact runtime view identities");
    check(cb.pass.info!=template_info && !template_info->views_resolved && template_info->views.empty(),"resolving a pass never mutates its shared framebuffer template");
    const auto first=cb.pass.info;selected=handles[1];
    check(first->views[0].image.id==ids[0],"caller array mutation cannot change retained pass views");
    native_images_begin(&d,&cb,&begin);
    check(cb.native_image_state && cb.native_image_state->attachments_known && cb.native_image_state->attachments==std::set<uint32_t>{ids[0]},
          "native pre-pass image conflict proof uses the recorded instance rather than caller memory");
    VkImageLayout layout;const VkImageSubresourceRange used{VK_IMAGE_ASPECT_COLOR_BIT,1,1,1,1};
    on_image_pass(&d,&cb);check(cb.image_layouts.get(ids[0],2,2,used,layout) && layout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          "resolved imageless pass establishes the actual attachment layout");
    check(!cb.image_layouts.get(ids[0],2,2,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},layout),"imageless transitions do not invent facts for unused mips or layers");
    on_image_pass(&d,&cb,true);check(cb.image_layouts.get(ids[0],2,2,used,layout) && layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          "imageless pass end records its exact readable final layout");
    on_begin_targets(&d,&cb,&begin);on_begin_pass(&d,&cb,&begin);native_images_begin(&d,&cb,&begin);
    check(cb.pass.info->views[0].image.id==ids[1] && first->views[0].image.id==ids[0] &&
          cb.native_image_state->attachments==std::set<uint32_t>{ids[1]},"reused framebuffer selects fresh pass views without altering the previous pass");
    late.attachmentCount=0;on_begin_targets(&d,&cb,&begin);on_begin_pass(&d,&cb,&begin);native_images_begin(&d,&cb,&begin);on_image_pass(&d,&cb);
    check(!cb.pass.info->views_resolved && !cb.native_image_state->attachments_known && cb.image_layouts.exhausted(),
          "mismatched late views discard previous context and invalidate stale layouts");
    cb.image_layouts.reset();begin.pNext=nullptr;on_begin_targets(&d,&cb,&begin);on_begin_pass(&d,&cb,&begin);
    check(!cb.pass.info->views_resolved,"missing late attachment list cannot reuse an earlier resolution");
    late.attachmentCount=1;VkBaseInStructure extension{VK_STRUCTURE_TYPE_MAX_ENUM};late.pNext=&extension;begin.pNext=&late;
    on_begin_targets(&d,&cb,&begin);on_begin_pass(&d,&cb,&begin);check(!cb.pass.info->views_resolved,"unknown begin extensions do not supply complete attachment context");
    cb.reset_recording();check(!cb.pass.info && cb.target_contexts.empty() && !cb.native_image_state && first->views[0].image.id==ids[0],
          "recording reset clears current context while retained immutable facts survive");
}
