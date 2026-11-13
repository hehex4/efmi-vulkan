static void image_layout_tracker_cases()
{
    Device device; CmdBuf command;
    const auto image = h<VkImage>(401);
    ImageInfo info; info.id = 11; info.capture_safe = true; info.levels = 3; info.layers = 4;
    info.type = VK_IMAGE_TYPE_2D; info.format = VK_FORMAT_R8G8B8A8_UNORM;
    device.images[image] = info;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.image = image; barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 3, 0, 4};
    VkImageLayout layout;
    on_image_barriers(&device, &command, 1, &barrier);
    check(!command.image_layouts.get(11, 3, 4, barrier.subresourceRange, layout), "image layout recording is disabled until requested");
    device.track_image_layouts = true;
    on_image_barriers(&device, &command, 1, &barrier);
    check(command.image_layouts.get(11, 3, 4, barrier.subresourceRange, layout) && layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          "legacy callback tracks the application's image identity and whole range");
    device.synchronization2_enabled = true;
    barrier.oldLayout = barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    on_image_barriers(&device, &command, 1, &barrier);
    check(command.image_layouts.get(11, 3, 4, barrier.subresourceRange, layout) && layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          "enabled synchronization2 changes legacy equal-layout barrier semantics too");
    VkImageMemoryBarrier2 second{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    second.image = image; second.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 1, 2};
    second.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; second.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    second.srcQueueFamilyIndex = second.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1; dependency.pImageMemoryBarriers = &second;
    on_image_barriers(&device, &command, &dependency, true);
    check(!command.image_layouts.get(11, 3, 4, second.subresourceRange, layout), "event source half does not publish a completed transition");
    on_image_barriers(&device, &command, &dependency);
    check(command.image_layouts.get(11, 3, 4, second.subresourceRange, layout) && layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          "event wait or barrier2 publishes the destination layout");
    check(command.image_layouts.get(11, 3, 4, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 4}, layout) && layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          "callback partial transitions preserve independent mip state");

    const auto secondary_handle = h<VkCommandBuffer>(402);
    auto secondary = std::make_unique<CmdBuf>(); secondary->handle = secondary_handle;
    secondary->image_layouts.set(11, 3, 4, second.subresourceRange, VK_IMAGE_LAYOUT_GENERAL);
    device.cbs[secondary_handle] = std::move(secondary);
    on_execute_commands(&device, &command, 1, &secondary_handle);
    check(command.image_layouts.get(11, 3, 4, second.subresourceRange, layout) && layout == VK_IMAGE_LAYOUT_GENERAL,
          "ExecuteCommands composes secondary layout effects at the execution point");
    device.cbs[secondary_handle]->reset_recording();
    check(command.image_layouts.get(11, 3, 4, second.subresourceRange, layout), "secondary reset leaves primary image facts intact");
    command.reset_recording();
    check(!command.image_layouts.get(11, 3, 4, second.subresourceRange, layout), "command reset removes prior recording image facts");

    auto framebuffer = std::make_shared<FramebufferInfo>(); framebuffer->known = true; framebuffer->layers = 2;
    TargetView target; target.known = true; target.image = info; target.view.image_id = info.id;
    target.view.range = {VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 1, 3};
    framebuffer->views.push_back(target);
    target.image.id = 12; target.view.image_id = 12; framebuffer->views.push_back(target);
    auto recipe = std::make_shared<RenderPassRecipe>(); recipe->complete = true;
    recipe->attachments.resize(2);
    recipe->attachments[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    recipe->attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    recipe->subpasses.resize(2);
    VkAttachmentReference2 reference{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2}; reference.attachment = 0;
    reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    recipe->subpasses[0].colors.push_back(reference);
    reference.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; reference.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    recipe->subpasses[1].inputs.push_back(reference);
    command.pass.render_pass = h<VkRenderPass>(403); command.pass.info = framebuffer; command.pass.recipe = recipe;
    command.image_layouts.set(11, 3, 4, barrier.subresourceRange, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    command.image_layouts.set(12, 3, 4, barrier.subresourceRange, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    on_image_pass(&device, &command);
    check(command.image_layouts.get(11, 3, 4, second.subresourceRange, layout) && layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          "classic pass begin establishes the first subpass attachment layout");
    check(command.image_layouts.get(11, 3, 4, {VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 3, 1}, layout) && layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          "automatic transitions use framebuffer layer count rather than the entire view");
    on_next_targets(&device, &command); on_image_pass(&device, &command);
    check(command.image_layouts.get(11, 3, 4, second.subresourceRange, layout) && layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          "next subpass transitions work without capture-target snapshots");
    recipe->subpasses.emplace_back(); command.pass.subpass = 2; on_image_pass(&device, &command);
    check(!command.image_layouts.get(11, 3, 4, second.subresourceRange, layout),
          "attachments outside the current subpass do not retain a guessed early-final layout");
    on_image_pass(&device, &command, true);
    check(command.image_layouts.get(11, 3, 4, second.subresourceRange, layout) && layout == VK_IMAGE_LAYOUT_GENERAL,
          "pass end establishes the known final layout");
    check(command.image_layouts.get(12, 3, 4, barrier.subresourceRange, layout) && layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          "unused attachments do not invent final-layout transitions");

    command.image_layouts.reset(); command.pass.subpass = 0; recipe->subpasses.resize(2);
    framebuffer->views[0].view.range.baseArrayLayer = 0; framebuffer->views[0].view.range.layerCount = 4;
    framebuffer->layers = 1; recipe->subpasses[0].view_mask = 1; recipe->subpasses[1].view_mask = 4;
    command.image_layouts.set(11, 3, 4, barrier.subresourceRange, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    on_image_pass(&device, &command);
    check(command.image_layouts.get(11, 3, 4, {VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 2, 1}, layout) && layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          "multiview transitions cover the union of views used by all subpasses");
    check(command.image_layouts.get(11, 3, 4, {VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 1, 1}, layout) && layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          "multiview does not transition unused layers");
    framebuffer->imageless = true; on_image_pass(&device, &command);
    check(command.image_layouts.exhausted(), "unresolved attachment state cannot leave stale image facts usable");
}
