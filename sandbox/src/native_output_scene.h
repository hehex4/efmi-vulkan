// Optional full-size owned target for actual output-destination tests.
struct NativeOutputScene {
    std::wstring output;
    bool query=false;VkQueryPool queries=VK_NULL_HANDLE;
    VkImage image=VK_NULL_HANDLE;
    VkImageView view=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    Buffer readback;
} native_output;

static bool create_native_output()
{
    auto& s=native_output;if(s.output.empty())return true;
    if(!g.dynamic_rendering || g.threads || g.rendering_split || g.dynamic_depth)return false;
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
    if(s.query){VkQueryPoolCreateInfo pool{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};pool.queryType=VK_QUERY_TYPE_OCCLUSION;pool.queryCount=uint32_t(g.draws);
        CHECK(vkCreateQueryPool(g.device,&pool,nullptr,&s.queries));}
    return make_buffer(s.readback,VkDeviceSize(g.extent.width)*g.extent.height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true,true);
}
static VkImageMemoryBarrier native_output_barrier()
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=native_output.image;
    barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;return barrier;
}
static void record_native_output_source(VkCommandBuffer command)
{
    const auto& s=native_output;if(!s.image)return;
    auto barrier=native_output_barrier();barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};color.imageView=s.view;
    color.imageLayout=barrier.newLayout;color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color={{.05f,.05f,.08f,1.0f}};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};rendering.renderArea.extent=g.extent;rendering.layerCount=1;
    rendering.colorAttachmentCount=1;rendering.pColorAttachments=&color;
    dynamic_begin_rendering(command,&rendering);record_setup(command);record_draw(command,0);dynamic_end_rendering(command);
    barrier.oldLayout=barrier.newLayout;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
}
static void record_native_output_readback(VkCommandBuffer command)
{
    const auto& s=native_output;if(!s.image)return;
    auto barrier=native_output_barrier();barrier.oldLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
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
}
static void destroy_native_output()
{
    auto& s=native_output;
    if(s.readback.map){
        FILE* file=nullptr;_wfopen_s(&file,s.output.c_str(),L"wb");
        if(file){fwrite(s.readback.map,1,size_t(s.readback.size),file);fclose(file);}
        const auto metadata=s.output+L".json";_wfopen_s(&file,metadata.c_str(),L"wb");
        if(file){fprintf(file,"{\"width\":%u,\"height\":%u,\"format\":%u}",g.extent.width,g.extent.height,unsigned(g.format));fclose(file);}
    }
    if(s.queries)vkDestroyQueryPool(g.device,s.queries,nullptr);
    destroy_buffer(s.readback);
    if(s.view)vkDestroyImageView(g.device,s.view,nullptr);
    if(s.image)vkDestroyImage(g.device,s.image,nullptr);
    if(s.memory)vkFreeMemory(g.device,s.memory,nullptr);
}
