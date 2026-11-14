// Owned attachment fixture: two clear-only passes need no graphics pipeline.
struct CaptureTargetScene {
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
    image.imageType = VK_IMAGE_TYPE_2D; image.format = VK_FORMAT_R8G8B8A8_UNORM;
    image.extent = {s.layered?8u:4u, s.layered?8u:4u, 1}; image.mipLevels = s.layered?2:1; image.arrayLayers = s.layered?4:1;
    image.samples = VK_SAMPLE_COUNT_1_BIT; image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (s.transfer ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    CHECK(vkCreateImage(g.device, &image, nullptr, &s.image));
    VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(g.device, s.image, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize = requirements.size;
    if (!find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &allocation.memoryTypeIndex)) return false;
    CHECK(vkAllocateMemory(g.device, &allocation, nullptr, &s.memory));
    CHECK(vkBindImageMemory(g.device, s.image, s.memory, 0));
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = s.image; view.viewType = s.layered?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D; view.format = image.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, s.layered?1u:0u, 1, s.layered?1u:0u, s.layered?3u:1u};
    CHECK(vkCreateImageView(g.device, &view, nullptr, &s.view));
    VkAttachmentDescription attachment{}; attachment.format = image.format; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = s.store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = s.unused?0:1; subpass.pColorAttachments = &color;
    VkSubpassDependency dependency{}; dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0;
    dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount = 1; pass.pAttachments = &attachment; pass.subpassCount = 1; pass.pSubpasses = &subpass;
    pass.dependencyCount = 1; pass.pDependencies = &dependency;
    const uint32_t mask=5;VkRenderPassMultiviewCreateInfo multiview{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};
    if(g.capture_multiview){multiview.subpassCount=1;multiview.pViewMasks=&mask;pass.pNext=&multiview;}
    CHECK(vkCreateRenderPass(g.device, &pass, nullptr, &s.pass));
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass = s.pass; framebuffer.attachmentCount = 1; framebuffer.pAttachments = &s.view;
    framebuffer.width = framebuffer.height = 4; framebuffer.layers = s.layered && !g.capture_multiview?2:1;
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
        vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(command);
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
