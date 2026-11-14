static void dynamic_attachment_context_cases()
{
    Device d;CmdBuf cb;d.track_image_layouts=true;
    VkImageView handles[4]{};uint32_t ids[4]{};
    for(uint32_t i=0;i<4;++i){
        const auto image=h<VkImage>(9200+i);handles[i]=h<VkImageView>(9210+i);
        VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};create.imageType=VK_IMAGE_TYPE_2D;create.extent={4,4,1};
        create.format=VK_FORMAT_R8G8B8A8_UNORM;create.mipLevels=create.arrayLayers=1;create.samples=VK_SAMPLE_COUNT_1_BIT;
        create.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;on_create_image(&d,image,&create);ids[i]=d.images.at(image).id;
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;
        view.format=create.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};on_create_image_view(&d,handles[i],&view);
    }
    VkRenderingAttachmentInfo colors[2]={{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO},{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}};
    colors[0].imageView=handles[0];colors[0].resolveMode=VK_RESOLVE_MODE_AVERAGE_BIT;colors[0].resolveImageView=handles[1];
    colors[1].resolveImageView=h<VkImageView>(9999);colors[1].resolveMode=VK_RESOLVE_MODE_AVERAGE_BIT;
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};depth.imageView=handles[2];
    depth.resolveMode=VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;depth.resolveImageView=handles[3];
    auto stencil=depth;stencil.resolveMode=VK_RESOLVE_MODE_NONE;stencil.resolveImageView=h<VkImageView>(9999);
    VkRenderingInfo begin{VK_STRUCTURE_TYPE_RENDERING_INFO};begin.colorAttachmentCount=2;begin.pColorAttachments=colors;
    begin.pDepthAttachment=&depth;begin.pStencilAttachment=&stencil;begin.layerCount=1;
    auto capture=[&]{on_begin_targets(&d,&cb,&begin);on_begin_pass(&d,&cb,&begin);native_images_begin(&d,&cb,&begin);};
    on_begin_targets(&d,&cb,&begin);on_begin_pass(&d,&cb,&begin);colors[0].imageView=h<VkImageView>(9998);
    native_images_begin(&d,&cb,&begin);const auto first=cb.native_image_state;
    check(first && first->attachments_known && first->attachments==std::set<uint32_t>{ids[0],ids[1],ids[2],ids[3]},
          "dynamic conflicts retain actual color/depth/stencil and enabled resolve images despite caller mutation");
    VkImageLayout layout;on_image_pass(&d,&cb);
    check(!cb.image_layouts.get(ids[0],1,1,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},layout),
          "dynamic attachment context never invents an implicit layout transition");
    begin.pDepthAttachment=begin.pStencilAttachment=nullptr;begin.colorAttachmentCount=1;
    colors[0].imageView=handles[1];colors[0].resolveMode=VK_RESOLVE_MODE_NONE;colors[0].resolveImageView=h<VkImageView>(9999);capture();
    check(cb.native_image_state->attachments_known && cb.native_image_state->attachments==std::set<uint32_t>{ids[1]} && first->attachments.size()==4,
          "new dynamic passes replace current views without modifying earlier conflict sets or reading disabled resolve handles");
    colors[0].imageView=VK_NULL_HANDLE;colors[0].resolveMode=VK_RESOLVE_MODE_AVERAGE_BIT;capture();
    check(cb.native_image_state->attachments_known && cb.native_image_state->attachments.empty(),"a null attachment makes its resolve handle irrelevant");
    colors[0].imageView=handles[0];capture();
    check(!cb.native_image_state->attachments_known,"an enabled resolve with an unknown view cannot establish a complete conflict set");
    colors[0].resolveMode=VK_RESOLVE_MODE_NONE;colors[0].imageView=h<VkImageView>(9998);capture();
    check(!cb.native_image_state->attachments_known,"unknown current views are never treated as a pass without attachments");
    colors[0].imageView=handles[0];VkBaseInStructure extra{VK_STRUCTURE_TYPE_MAX_ENUM};begin.pNext=&extra;capture();
    check(!cb.native_image_state->attachments_known,"unknown rendering chains preserve attachment uncertainty");
    begin.pNext=nullptr;colors[0].pNext=&extra;capture();
    check(!cb.native_image_state->attachments_known,"unknown attachment chains preserve attachment uncertainty");
    colors[0].pNext=nullptr;on_begin_targets(&d,&cb,&begin);on_begin_pass(&d,&cb,&begin);
    cb.target_contexts.back().views.pop_back();native_images_begin(&d,&cb,&begin);
    check(!cb.native_image_state->attachments_known,"an incomplete host view snapshot cannot authorize image use");
    cb.active_targets=0;native_images_begin(&d,&cb,&begin);
    check(!cb.native_image_state->attachments_known,"missing target context cannot reuse an earlier pass");
    begin.flags=VK_RENDERING_SUSPENDING_BIT;capture();
    check(!cb.native_image_state->attachments_known,"segmented rendering retains its separate submission adapter boundary");
    begin.flags=VK_RENDERING_ENABLE_LEGACY_DITHERING_BIT_EXT;capture();
    check(!cb.native_image_state->attachments_known,"unhandled rendering flags do not establish conflict proof");
    begin.flags=VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;capture();
    check(cb.native_image_state->attachments_known && cb.native_image_state->attachments==std::set<uint32_t>{ids[0]},
          "a primary pass containing secondary commands still has its own exact attachment context");
    cb.reset_recording();
    check(!cb.native_image_state && cb.target_contexts.empty() && first->attachments.size()==4,
          "reset clears current context while previously retained immutable conflict facts survive");
}
