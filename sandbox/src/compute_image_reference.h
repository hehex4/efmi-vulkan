// Distinct application images, persistent descriptor use and layout restoration on the GPU.
static void bind_compute_descriptors(VkCommandBuffer);
static uint32_t reference_layers() { return g.reference_layer_count?g.reference_layer_count:g.texture_shape==3?12:g.texture_shape==2?6:g.texture_shape==1?3:1; }
static uint32_t reference_mips() { return g.reference_storage?1:g.texture_shape?3:1; }
static bool create_reference_images()
{
    auto& c=compute_scene;
    for(auto& result:c.reference_images){
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;
        image.format=g.reference_storage?(g.reference_float_source && &result==&c.reference_images[0]?VK_FORMAT_R32_SFLOAT:VK_FORMAT_R32_UINT):VK_FORMAT_R8G8B8A8_UNORM;image.extent={g.texture_shape?4u:1u,g.texture_shape?4u:1u,1};
        image.mipLevels=reference_mips();image.arrayLayers=reference_layers();
        if(g.texture_shape>=2)image.flags=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
        image.usage=(g.reference_storage?VK_IMAGE_USAGE_STORAGE_BIT:0)|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(c.capture_usage?VK_IMAGE_USAGE_TRANSFER_SRC_BIT:0);
        CHECK(vkCreateImage(g.device,&image,nullptr,&result.image));
        VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(g.device,result.image,&requirements);uint32_t index=0;
        if(!find_memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0,&index))return false;
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=requirements.size;memory.memoryTypeIndex=index;
        CHECK(vkAllocateMemory(g.device,&memory,nullptr,&result.memory));CHECK(vkBindImageMemory(g.device,result.image,result.memory,0));
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=result.image;
        view.viewType=g.texture_shape==3?VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:g.texture_shape==2?VK_IMAGE_VIEW_TYPE_CUBE:g.texture_shape==1?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D;
        view.format=image.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,reference_mips(),0,reference_layers()};
        if(g.reference_partial)view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,1,2,1,2};
        if(g.reference_remaining){view.subresourceRange.levelCount=VK_REMAINING_MIP_LEVELS;view.subresourceRange.layerCount=VK_REMAINING_ARRAY_LAYERS;}
        if(g.reference_narrow && &result==&c.reference_images[1]){view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.subresourceRange.layerCount=1;}
        CHECK(vkCreateImageView(g.device,&view,nullptr,&result.view));
    }
    FILE* file=nullptr;_wfopen_s(&file,c.reference_shader_path.c_str(),L"rb");if(!file)return false;
    fseek(file,0,SEEK_END);const long size=ftell(file);rewind(file);
    if(size<20 || size>(16<<20) || size%4){fclose(file);return false;}
    std::vector<uint32_t> code(size/4);const bool read=fread(code.data(),1,size,file)==size;fclose(file);if(!read)return false;
    VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};shader.codeSize=size;shader.pCode=code.data();VkShaderModule module{};
    CHECK(vkCreateShaderModule(g.device,&shader,nullptr,&module));
    VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pipeline.layout=c.layout;
    pipeline.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline.stage.module=module;pipeline.stage.pName="main";
    const auto made=vkCreateComputePipelines(g.device,VK_NULL_HANDLE,1,&pipeline,nullptr,&c.reference_pipeline);
    vkDestroyShaderModule(g.device,module,nullptr);CHECK(made);return true;
}
static void reference_transition(VkCommandBuffer cmd,VkImage image,VkImageLayout before,VkImageLayout after)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.oldLayout=before;barrier.newLayout=after;barrier.image=image;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,reference_mips(),0,reference_layers()};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
}
static void reference_clear(VkCommandBuffer cmd,VkImage image,float value)
{
    for(uint32_t layer=0;layer<reference_layers();++layer)for(uint32_t mip=0;mip<reference_mips();++mip){
        VkClearColorValue color{};if(g.reference_storage){if(g.reference_float_source && image==compute_scene.reference_images[0].image)color.float32[0]=value+layer*3+mip;else color.uint32[0]=uint32_t(value)+layer*3+mip;}else{color.float32[0]=(value+layer*3+mip)/255.0f;color.float32[3]=1;}
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,mip,1,layer,1};
        vkCmdClearColorImage(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
    }
}
static void initialize_reference_images(VkCommandBuffer cmd)
{
    for(unsigned i=0;i<2;++i){const auto image=compute_scene.reference_images[i].image;
        reference_transition(cmd,image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        reference_clear(cmd,image,i?37.0f:1.0f);
        reference_transition(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,g.reference_storage?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}
static void reference_dispatch(VkCommandBuffer cmd)
{
    vkCmdDispatch(cmd,1,1,1);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
}
static void record_reference_images(VkCommandBuffer cmd)
{
    auto& c=compute_scene;const auto source=c.reference_images[0].image;
    reference_dispatch(cmd);
    reference_transition(cmd,source,g.reference_storage?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    reference_clear(cmd,source,9);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,c.reference_pipeline);
    reference_dispatch(cmd);
    // The layer's temporary sampling transition must already have restored TRANSFER_DST.
    reference_transition(cmd,source,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
    reference_dispatch(cmd);
    reference_transition(cmd,source,VK_IMAGE_LAYOUT_GENERAL,g.reference_storage?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // Application binds supersede native persistent descriptors, including pushed descriptors.
    bind_compute_descriptors(cmd);
    reference_dispatch(cmd);
}
static void destroy_reference_images()
{
    auto& c=compute_scene;if(c.reference_pipeline)vkDestroyPipeline(g.device,c.reference_pipeline,nullptr);
    for(const auto& image:c.reference_images){
        if(image.view)vkDestroyImageView(g.device,image.view,nullptr);
        if(image.image)vkDestroyImage(g.device,image.image,nullptr);
        if(image.memory)vkFreeMemory(g.device,image.memory,nullptr);
    }
}
