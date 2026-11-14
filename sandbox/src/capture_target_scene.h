// Owned attachment fixture: two clear-only passes need no graphics pipeline.
struct CaptureTargetScene {
    bool draw = false, mip_view = false;
    bool enabled = false, after_graphics = false, transfer = true, store = true, layered = false, unused = false;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
} capture_target;

static bool create_capture_target()
{
    auto& s = capture_target;
    if (!s.enabled) return true;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D; image.format = s.draw?g.format:VK_FORMAT_R8G8B8A8_UNORM;
    image.extent = {s.layered?8u:4u, s.layered?8u:4u, 1}; image.mipLevels = s.layered?2:1; image.arrayLayers = s.layered?4:1;
    image.samples = VK_SAMPLE_COUNT_1_BIT; image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (s.transfer ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0) | (s.draw ? VK_IMAGE_USAGE_SAMPLED_BIT : 0);
    CHECK(vkCreateImage(g.device, &image, nullptr, &s.image));
    VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(g.device, s.image, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize = requirements.size;
    if (!find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &allocation.memoryTypeIndex)) return false;
    CHECK(vkAllocateMemory(g.device, &allocation, nullptr, &s.memory));
    CHECK(vkBindImageMemory(g.device, s.image, s.memory, 0));
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = s.image; view.viewType = s.layered && !s.mip_view?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D; view.format = image.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, s.layered?1u:0u, 1, s.layered?1u:0u, s.layered && !s.mip_view?3u:1u};
    CHECK(vkCreateImageView(g.device, &view, nullptr, &s.view));
    VkAttachmentDescription attachment{}; attachment.format = image.format; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = s.store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout = s.draw?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = s.unused?0:1; subpass.pColorAttachments = &color;
    VkSubpassDependency dependency{}; dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0;
    dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = s.draw?0:VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount = 1; pass.pAttachments = &attachment; pass.subpassCount = 1; pass.pSubpasses = &subpass;
    pass.dependencyCount = 1; pass.pDependencies = &dependency;
    const uint32_t mask=5;VkRenderPassMultiviewCreateInfo multiview{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};
    if(g.capture_multiview){multiview.subpassCount=1;multiview.pViewMasks=&mask;pass.pNext=&multiview;}
    CHECK(vkCreateRenderPass(g.device, &pass, nullptr, &s.pass));
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass = s.pass; framebuffer.attachmentCount = 1; framebuffer.pAttachments = &s.view;
    framebuffer.width = framebuffer.height = 4; framebuffer.layers = s.layered && !s.mip_view && !g.capture_multiview?2:1;
    VkFramebufferAttachmentImageInfo image_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO};
    image_info.usage=image.usage;image_info.width=image_info.height=4;image_info.layerCount=view.subresourceRange.layerCount;
    image_info.viewFormatCount=1;image_info.pViewFormats=&image.format;
    VkFramebufferAttachmentsCreateInfo attachment_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO};
    attachment_info.attachmentImageInfoCount=1;attachment_info.pAttachmentImageInfos=&image_info;
    if(g.imageless){framebuffer.flags=VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;framebuffer.pAttachments=nullptr;framebuffer.pNext=&attachment_info;}
    CHECK(vkCreateFramebuffer(g.device, &framebuffer, nullptr, &s.framebuffer));
    return true;
}

static void record_capture_target(VkCommandBuffer command, bool after_graphics)
{
    const auto& s = capture_target;
    if (!s.enabled || s.after_graphics != after_graphics) return;
    for (uint32_t i = 0; i < 2; ++i) {
        VkClearValue clear{}; clear.color.float32[0] = clear.color.float32[2] = float(i);
        clear.color.float32[1] = float(1 - i); clear.color.float32[3] = 1;
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = s.pass; begin.framebuffer = s.framebuffer; begin.renderArea.extent = {4, 4};
        begin.clearValueCount = 1; begin.pClearValues = &clear;
        VkRenderPassAttachmentBeginInfo late_views{VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO};
        late_views.attachmentCount=1;late_views.pAttachments=&s.view;if(g.imageless)begin.pNext=&late_views;
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=s.image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,s.layered?1u:0u,1,s.layered?1u:0u,s.layered && !s.mip_view?(g.capture_multiview?3u:2u):1u};
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};color.imageView=s.view;
        color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp=s.store?VK_ATTACHMENT_STORE_OP_STORE:VK_ATTACHMENT_STORE_OP_DONT_CARE;color.clearValue=clear;
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};rendering.renderArea.extent={4,4};
        rendering.layerCount=s.layered && !s.mip_view && !g.capture_multiview?2:1;rendering.viewMask=g.capture_multiview?5:0;
        rendering.colorAttachmentCount=s.unused?0:1;rendering.pColorAttachments=&color;
        if(g.dynamic_rendering){
            barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            dynamic_begin_rendering(command,&rendering);
        }else vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
        if(s.draw){record_setup(command);record_draw(command,0); }
        if(g.dynamic_rendering){
            dynamic_end_rendering(command);
            barrier.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barrier.newLayout=s.draw?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        }else vkCmdEndRenderPass(command);
    }
}

static void destroy_capture_target()
{
    auto& s = capture_target;
    if (s.framebuffer) vkDestroyFramebuffer(g.device, s.framebuffer, nullptr);
    if (s.pass) vkDestroyRenderPass(g.device, s.pass, nullptr);
    if (s.view) vkDestroyImageView(g.device, s.view, nullptr);
    if (s.image) vkDestroyImage(g.device, s.image, nullptr);
    if (s.memory) vkFreeMemory(g.device, s.memory, nullptr);
}
