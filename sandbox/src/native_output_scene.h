// Optional full-size owned target for actual output-destination tests.
struct NativeOutputScene {
    std::wstring output;
    bool query=false;VkQueryPool queries=VK_NULL_HANDLE;
    VkRenderPass source_pass=VK_NULL_HANDLE;VkFramebuffer source_framebuffer=VK_NULL_HANDLE;
    VkImage image=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    Buffer readback,depth_readback,depth_source_readback;
} native_output,native_output_extra;

static bool create_native_output_one(NativeOutputScene& s)
{
    if(s.output.empty())return true;
    if(g.native_output_owned && (!g.native_output_classic || g.native_output_mrt || g.dynamic_depth))return false;
    if((g.native_output_no_color && (!g.dynamic_depth || g.native_output_mrt)) || (!g.dynamic_rendering && !g.native_output_classic) || (g.native_output_classic && (g.native_output_mrt || g.dynamic_depth)) || g.threads || g.rendering_split || (g.dynamic_depth && !g.native_output_depth && !g.native_output_stencil))return false;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType=VK_IMAGE_TYPE_2D;image.format=g.format;image.extent={g.extent.width,g.extent.height,1};
    image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
    image.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    CHECK(vkCreateImage(g.device,&image,nullptr,&s.image));
    VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(g.device,s.image,&requirements);
    VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=requirements.size;
    if(!find_memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0,&memory.memoryTypeIndex))return false;
    CHECK(vkAllocateMemory(g.device,&memory,nullptr,&s.memory));CHECK(vkBindImageMemory(g.device,s.image,s.memory,0));
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=s.image;
    view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=g.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    CHECK(vkCreateImageView(g.device,&view,nullptr,&s.view));
    if(g.native_output_classic){
        VkAttachmentDescription attachment{};attachment.format=g.format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_GENERAL;
        VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};
        sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
        VkSubpassDependency dependency{};dependency.srcSubpass=VK_SUBPASS_EXTERNAL;dependency.dstSubpass=0;
        dependency.srcStageMask=dependency.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo create{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};create.attachmentCount=1;create.pAttachments=&attachment;
        create.subpassCount=1;create.pSubpasses=&sub;create.dependencyCount=1;create.pDependencies=&dependency;
        CHECK(vkCreateRenderPass(g.device,&create,nullptr,&s.source_pass));
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};framebuffer.renderPass=s.source_pass;
        framebuffer.attachmentCount=1;framebuffer.pAttachments=&s.view;framebuffer.width=g.extent.width;framebuffer.height=g.extent.height;framebuffer.layers=1;
        CHECK(vkCreateFramebuffer(g.device,&framebuffer,nullptr,&s.source_framebuffer));
    }
    if(s.query){VkQueryPoolCreateInfo pool{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};pool.queryType=VK_QUERY_TYPE_OCCLUSION;pool.queryCount=uint32_t(g.draws);
        CHECK(vkCreateQueryPool(g.device,&pool,nullptr,&s.queries));}
    if(!make_buffer(s.readback,VkDeviceSize(g.extent.width)*g.extent.height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true,true))return false;
    if((g.native_output_depth || g.native_output_stencil) && !make_buffer(s.depth_readback,VkDeviceSize(g.extent.width)*g.extent.height*(g.native_output_stencil?1:4),VK_BUFFER_USAGE_TRANSFER_DST_BIT,true,true))return false;
    return !g.native_depth_destination || make_buffer(s.depth_source_readback,VkDeviceSize(g.extent.width)*g.extent.height*(g.native_output_stencil?1:4),VK_BUFFER_USAGE_TRANSFER_DST_BIT,true,true);
}
static bool create_native_output()
{
    if(!create_native_output_one(native_output))return false;
    if(g.native_output_mrt || g.native_output_owned){
        if(native_output.output.empty())return false;
        native_output_extra.output=native_output.output+(g.native_output_owned?L".original.raw":L".mrt.raw");
        return create_native_output_one(native_output_extra);
    }
    return true;
}
static VkImageMemoryBarrier native_output_barrier(VkImage image)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image;
    barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;return barrier;
}
static void record_native_output_source(VkCommandBuffer command)
{
    const auto& s=native_output;if(!s.image)return;
    if(g.native_output_classic){
        VkClearValue clear{};clear.color={{.05f,.05f,.08f,1.0f}};
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=s.source_pass;begin.framebuffer=s.source_framebuffer;
        begin.renderArea.extent=g.extent;begin.clearValueCount=1;begin.pClearValues=&clear;
        if(g.native_output_classic==2){VkSubpassBeginInfo sub{VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO};sub.contents=VK_SUBPASS_CONTENTS_INLINE;vkCmdBeginRenderPass2(command,&begin,&sub);}
        else vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
        record_setup(command);record_draw(command,0);
        if(g.native_output_classic==2){VkSubpassEndInfo sub{VK_STRUCTURE_TYPE_SUBPASS_END_INFO};vkCmdEndRenderPass2(command,&sub);}else vkCmdEndRenderPass(command);
        return;
    }
    auto barrier=native_output_barrier(s.image);barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};color.imageView=s.view;
    color.imageLayout=barrier.newLayout;color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color={{.05f,.05f,.08f,1.0f}};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};rendering.renderArea.extent=g.extent;rendering.layerCount=1;
    VkRenderingAttachmentInfo colors[2]{color,color};colors[1].imageView=VK_NULL_HANDLE;
    rendering.colorAttachmentCount=g.native_output_mrt?2:1;rendering.pColorAttachments=colors;
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};VkImageMemoryBarrier depth_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    if(g.native_depth_destination){
        depth.imageView=g.native_depth_source_view;depth.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;depth.storeOp=VK_ATTACHMENT_STORE_OP_STORE;depth.clearValue.depthStencil={1.0f,0};
        rendering.pDepthAttachment=g.native_output_stencil==1?nullptr:&depth;rendering.pStencilAttachment=g.native_output_stencil?&depth:nullptr;
        depth_barrier.image=g.native_depth_source;depth_barrier.srcQueueFamilyIndex=depth_barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        depth_barrier.subresourceRange={VkImageAspectFlags(g.native_output_stencil==2?VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT:g.native_output_stencil?VK_IMAGE_ASPECT_STENCIL_BIT:VK_IMAGE_ASPECT_DEPTH_BIT),0,1,0,1};
        depth_barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;depth_barrier.newLayout=depth.imageLayout;
        depth_barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;depth_barrier.dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,0,0,nullptr,0,nullptr,1,&depth_barrier);
    }
    dynamic_begin_rendering(command,&rendering);record_setup(command);record_draw(command,0);dynamic_end_rendering(command);
    if(g.native_depth_destination){
        depth_barrier.oldLayout=depth_barrier.newLayout;depth_barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        depth_barrier.srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;depth_barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&depth_barrier);
    }
    barrier.oldLayout=barrier.newLayout;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
}
static void record_native_output_readback_one(VkCommandBuffer command,const NativeOutputScene& s)
{
    if(!s.image)return;
    auto barrier=native_output_barrier(s.image);barrier.oldLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    VkBufferMemoryBarrier buffer{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};buffer.buffer=s.readback.buf;buffer.size=VK_WHOLE_SIZE;
    buffer.srcQueueFamilyIndex=buffer.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    buffer.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;buffer.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&buffer,1,&barrier);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={g.extent.width,g.extent.height,1};
    vkCmdCopyImageToBuffer(command,s.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,s.readback.buf,1,&copy);
    barrier.oldLayout=barrier.newLayout;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    buffer.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&buffer,1,&barrier);
    if(s.depth_readback.buf){
        barrier.image=g.depth_image;barrier.subresourceRange.aspectMask=g.native_output_stencil==2?VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT:g.native_output_stencil?VK_IMAGE_ASPECT_STENCIL_BIT:VK_IMAGE_ASPECT_DEPTH_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        buffer.buffer=s.depth_readback.buf;buffer.srcAccessMask=buffer.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&buffer,1,&barrier);
        copy.imageSubresource.aspectMask=g.native_output_stencil?VK_IMAGE_ASPECT_STENCIL_BIT:VK_IMAGE_ASPECT_DEPTH_BIT;
        vkCmdCopyImageToBuffer(command,g.depth_image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,s.depth_readback.buf,1,&copy);
        barrier.oldLayout=barrier.newLayout;barrier.newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        buffer.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&buffer,1,&barrier);
    }
    if(s.depth_source_readback.buf){
        barrier.image=g.native_depth_source;barrier.subresourceRange.aspectMask=g.native_output_stencil==2?VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT:g.native_output_stencil?VK_IMAGE_ASPECT_STENCIL_BIT:VK_IMAGE_ASPECT_DEPTH_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        buffer.buffer=s.depth_source_readback.buf;buffer.srcAccessMask=buffer.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&buffer,1,&barrier);
        copy.imageSubresource.aspectMask=g.native_output_stencil?VK_IMAGE_ASPECT_STENCIL_BIT:VK_IMAGE_ASPECT_DEPTH_BIT;
        vkCmdCopyImageToBuffer(command,g.native_depth_source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,s.depth_source_readback.buf,1,&copy);
        barrier.oldLayout=barrier.newLayout;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        buffer.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&buffer,1,&barrier);
    }
}
static void record_native_output_readback(VkCommandBuffer command)
{
    if(native_output_extra.image && g.native_output_mrt){auto barrier=native_output_barrier(native_output_extra.image);
        barrier.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    }
    record_native_output_readback_one(command,native_output);
    record_native_output_readback_one(command,native_output_extra);
}
static void destroy_native_output_one(NativeOutputScene& s)
{
    if(s.readback.map){
        FILE* file=nullptr;_wfopen_s(&file,s.output.c_str(),L"wb");
        if(file){fwrite(s.readback.map,1,size_t(s.readback.size),file);fclose(file);}
        const auto metadata=s.output+L".json";_wfopen_s(&file,metadata.c_str(),L"wb");
        if(file){fprintf(file,"{\"width\":%u,\"height\":%u,\"format\":%u}",g.extent.width,g.extent.height,unsigned(g.format));fclose(file);}
    }
    if(s.depth_readback.map){
        FILE* file=nullptr;const auto path=s.output+(g.native_output_stencil?L".stencil.raw":L".depth.raw");_wfopen_s(&file,path.c_str(),L"wb");
        if(file){fwrite(s.depth_readback.map,1,size_t(s.depth_readback.size),file);fclose(file);}
    }
    destroy_buffer(s.depth_readback);
    if(s.depth_source_readback.map){
        FILE* file=nullptr;const auto path=s.output+(g.native_output_stencil?L".source-stencil.raw":L".source-depth.raw");_wfopen_s(&file,path.c_str(),L"wb");
        if(file){fwrite(s.depth_source_readback.map,1,size_t(s.depth_source_readback.size),file);fclose(file);}
    }
    destroy_buffer(s.depth_source_readback);
    if(s.queries)vkDestroyQueryPool(g.device,s.queries,nullptr);
    destroy_buffer(s.readback);
    if(s.source_framebuffer)vkDestroyFramebuffer(g.device,s.source_framebuffer,nullptr);
    if(s.source_pass)vkDestroyRenderPass(g.device,s.source_pass,nullptr);
    if(s.view)vkDestroyImageView(g.device,s.view,nullptr);
    if(s.image)vkDestroyImage(g.device,s.image,nullptr);
    if(s.memory)vkFreeMemory(g.device,s.memory,nullptr);
}

static void destroy_native_output()
{
    destroy_native_output_one(native_output_extra);destroy_native_output_one(native_output);
}
