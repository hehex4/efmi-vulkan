static bool create_dynamic_depth_one(VkImage& target,VkDeviceMemory& memory,VkImageView& target_view)
{
    if(!g.dynamic_depth)return true;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;image.format=g.native_output_stencil==2?VK_FORMAT_D32_SFLOAT_S8_UINT:g.native_output_stencil?VK_FORMAT_S8_UINT:VK_FORMAT_D32_SFLOAT;
    image.extent={g.extent.width,g.extent.height,1};image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;
    image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|((g.native_output_depth || g.native_output_stencil)?VK_IMAGE_USAGE_TRANSFER_SRC_BIT:0);
    CHECK(vkCreateImage(g.device,&image,nullptr,&target));
    VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(g.device,target,&requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;
    if(!find_memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0,&allocation.memoryTypeIndex))return false;
    CHECK(vkAllocateMemory(g.device,&allocation,nullptr,&memory));CHECK(vkBindImageMemory(g.device,target,memory,0));
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=target;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=image.format;
    view.subresourceRange={VkImageAspectFlags(g.native_output_stencil==2?VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT:g.native_output_stencil?VK_IMAGE_ASPECT_STENCIL_BIT:VK_IMAGE_ASPECT_DEPTH_BIT),0,1,0,1};CHECK(vkCreateImageView(g.device,&view,nullptr,&target_view));return true;
}
static bool create_dynamic_depth()
{
    if(!create_dynamic_depth_one(g.depth_image,g.depth_memory,g.depth_view))return false;
    return !g.native_depth_destination || create_dynamic_depth_one(g.native_depth_source,g.native_depth_source_memory,g.native_depth_source_view);
}
static void destroy_dynamic_depth_one(VkImage target,VkDeviceMemory memory,VkImageView view)
{
    if(view)vkDestroyImageView(g.device,view,nullptr);
    if(target)vkDestroyImage(g.device,target,nullptr);
    if(memory)vkFreeMemory(g.device,memory,nullptr);
}
static void destroy_dynamic_depth()
{
    destroy_dynamic_depth_one(g.depth_image,g.depth_memory,g.depth_view);
    destroy_dynamic_depth_one(g.native_depth_source,g.native_depth_source_memory,g.native_depth_source_view);
}
