// Optional application compute work, with host readback after the submission fence.
// Included after the sandbox's buffer helpers; no layer-specific calls.
struct ComputeScene {
    std::wstring shader_path, output_path, reference_shader_path;
    struct ReferenceImage { VkImage image=VK_NULL_HANDLE; VkImageView view=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; } reference_images[2];
    VkPipeline reference_pipeline=VK_NULL_HANDLE;
    bool indirect = false, uniform_alias = false;
    bool binding_scene = false, late_update = false, constant_input = false;
    bool secondary = false, image_input = false, capture_usage = false, storage_image = false;
    bool reusable = false, buffer_barriers = false;
    VkImage writable_image = VK_NULL_HANDLE;
    VkImageView writable_view = VK_NULL_HANDLE;
    VkDeviceMemory writable_memory = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    unsigned repeat = 1;
    uint32_t input_word = 7;
    Buffer data, arguments;
    Buffer alternate, input;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorUpdateTemplate update_template = VK_NULL_HANDLE;
} compute_scene;

#include "compute_image_reference.h"

static bool create_compute_scene()
{
    auto& c = compute_scene;
    if (c.shader_path.empty()) return true;
    FILE* file = nullptr; _wfopen_s(&file, c.shader_path.c_str(), L"rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END); const long size = ftell(file); rewind(file);
    if (size < 20 || size > (16 << 20) || size % 4) { fclose(file); return false; }
    std::vector<uint32_t> code(size / 4);
    const bool read = fread(code.data(), 1, size, file) == size; fclose(file);
    if (!read || !make_buffer(c.data, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | (c.uniform_alias ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : 0) | (c.capture_usage ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT : 0), true, true) ||
        !make_buffer(c.arguments, 12, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | (c.capture_usage ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT : 0), true, true)) return false;
    memset(c.data.map, 0, 16);
    if(c.binding_scene){
        if(!make_buffer(c.alternate,16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|(c.capture_usage?VK_BUFFER_USAGE_TRANSFER_SRC_BIT:0),true,true) ||
           !make_buffer(c.input,16,(c.constant_input?VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT:VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)|(c.capture_usage?VK_BUFFER_USAGE_TRANSFER_SRC_BIT:0),true,true))return false;
        memset(c.alternate.map,0,16);memset(c.input.map,0,16);
        *static_cast<uint32_t*>(c.input.map)=c.input_word;
    }
    if(c.storage_image){
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;image.format=VK_FORMAT_R32_UINT;image.extent={4,4,1};
        image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
        image.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(c.capture_usage?VK_IMAGE_USAGE_TRANSFER_SRC_BIT:0);
        CHECK(vkCreateImage(g.device,&image,nullptr,&c.writable_image));
        VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(g.device,c.writable_image,&requirements);uint32_t index=0;
        if(!find_memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0,&index))return false;
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};memory.allocationSize=requirements.size;memory.memoryTypeIndex=index;
        CHECK(vkAllocateMemory(g.device,&memory,nullptr,&c.writable_memory));CHECK(vkBindImageMemory(g.device,c.writable_image,c.writable_memory,0));
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=c.writable_image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=VK_FORMAT_R32_UINT;
        view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};CHECK(vkCreateImageView(g.device,&view,nullptr,&c.writable_view));
    }
    const uint32_t groups[3] = {1, 1, 1}; memcpy(c.arguments.map, groups, 12);
    VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo si = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    VkDescriptorSetLayoutBinding bindings[5]={binding,binding,binding,binding,binding};bindings[1].binding=1;bindings[2].binding=2;bindings[3].binding=3;bindings[3].descriptorType=g.reference_storage?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    if(c.constant_input)bindings[2].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    if(c.image_input)bindings[2].descriptorType=(c.storage_image || (g.reference_storage && !g.reference_sampled_source))?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    const uint32_t sampler_slot=c.reference_shader_path.empty()?3:4;
    if(g.texture_shape)bindings[sampler_slot]={sampler_slot,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,&g.sampler};
    si.bindingCount = g.texture_shape?sampler_slot+1:!c.reference_shader_path.empty()?4:c.binding_scene?3:1; si.pBindings = bindings;
    if(c.binding_scene && g.descriptor_mode>=2)si.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    CHECK(vkCreateDescriptorSetLayout(g.device, &si, nullptr, &c.set_layout));
    VkPipelineLayoutCreateInfo li = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1; li.pSetLayouts = &c.set_layout;
    CHECK(vkCreatePipelineLayout(g.device, &li, nullptr, &c.layout));
    VkShaderModuleCreateInfo mi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mi.codeSize = size; mi.pCode = code.data(); VkShaderModule module = VK_NULL_HANDLE;
    CHECK(vkCreateShaderModule(g.device, &mi, nullptr, &module));
    VkComputePipelineCreateInfo pi = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pi.layout = c.layout; pi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; pi.stage.module = module; pi.stage.pName = "main";
    const auto made = vkCreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &pi, nullptr, &c.pipeline);
    vkDestroyShaderModule(g.device, module, nullptr); CHECK(made);
    if(!c.reference_shader_path.empty() && !create_reference_images())return false;
    if(c.image_input && (g.descriptor_mode==1 || g.descriptor_mode==3))return false;
    if(c.binding_scene && (g.descriptor_mode==1 || g.descriptor_mode==3)){
        VkDescriptorUpdateTemplateEntry entries[3]{};
        for(uint32_t i=0;i<3;++i)entries[i]={i,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,sizeof(VkDescriptorBufferInfo)*i,sizeof(VkDescriptorBufferInfo)};
        if(c.constant_input)entries[2].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        VkDescriptorUpdateTemplateCreateInfo t{VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO};
        t.descriptorUpdateEntryCount=3;t.pDescriptorUpdateEntries=entries;t.descriptorSetLayout=c.set_layout;
        t.templateType=g.descriptor_mode==3?VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR:VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
        t.pipelineBindPoint=VK_PIPELINE_BIND_POINT_COMPUTE;t.pipelineLayout=c.layout;
        CHECK(vkCreateDescriptorUpdateTemplate(g.device,&t,nullptr,&c.update_template));
    }
    if(c.binding_scene && g.descriptor_mode>=2)return true;
    VkDescriptorPoolSize capacities[5]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, c.binding_scene?3u:1u},{(c.storage_image || (g.reference_storage && !g.reference_sampled_source))?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,c.reference_shader_path.empty()?1u:2u},{VK_DESCRIPTOR_TYPE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
    VkDescriptorPoolCreateInfo pool = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 1; pool.poolSizeCount = g.reference_sampled_source?5:c.constant_input?4:g.texture_shape?3:c.image_input?2:1; pool.pPoolSizes = capacities;
    CHECK(vkCreateDescriptorPool(g.device, &pool, nullptr, &c.pool));
    VkDescriptorSetAllocateInfo allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = c.pool; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &c.set_layout;
    CHECK(vkAllocateDescriptorSets(g.device, &allocate, &c.set));
    VkDescriptorBufferInfo buffer = {c.data.buf, 0, 16};
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = c.set; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &buffer; vkUpdateDescriptorSets(g.device, 1, &write, 0, nullptr);
    if(c.binding_scene){
        buffer.buffer=c.alternate.buf;write.dstBinding=1;vkUpdateDescriptorSets(g.device,1,&write,0,nullptr);
        buffer.buffer=c.uniform_alias?c.data.buf:c.input.buf;write.dstBinding=2;
        if(c.constant_input)write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        VkDescriptorImageInfo image{VK_NULL_HANDLE,c.storage_image?c.writable_view:c.reference_images[0].view?c.reference_images[0].view:g.shaped_texture_view?g.shaped_texture_view:g.texture_view,(c.storage_image || g.reference_storage)?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        if(c.image_input){write.descriptorType=(c.storage_image || (g.reference_storage && !g.reference_sampled_source))?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.pImageInfo=&image;write.pBufferInfo=nullptr;}
        vkUpdateDescriptorSets(g.device,1,&write,0,nullptr);
        if(c.reference_images[1].view){write.descriptorType=g.reference_storage?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.dstBinding=3;image.imageView=c.reference_images[1].view;vkUpdateDescriptorSets(g.device,1,&write,0,nullptr);}
    }
    return true;
}

static void bind_compute_descriptors(VkCommandBuffer cmd)
{
    auto& c=compute_scene;
    if(c.binding_scene && g.descriptor_mode>=2){
        VkDescriptorBufferInfo buffers[3]={{c.data.buf,0,16},{c.alternate.buf,0,16},{c.uniform_alias?c.data.buf:c.input.buf,0,16}};
        if(g.descriptor_mode==3)vkCmdPushDescriptorSetWithTemplateKHR(cmd,c.update_template,c.layout,0,buffers);
        else{
            VkWriteDescriptorSet writes[5]{};
            for(uint32_t i=0;i<3;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstBinding=i;
                writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&buffers[i];}
            if(c.constant_input)writes[2].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            VkDescriptorImageInfo image{VK_NULL_HANDLE,c.storage_image?c.writable_view:c.reference_images[0].view?c.reference_images[0].view:g.shaped_texture_view?g.shaped_texture_view:g.texture_view,(c.storage_image || g.reference_storage)?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            if(c.image_input){writes[2].descriptorType=(c.storage_image || (g.reference_storage && !g.reference_sampled_source))?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[2].pImageInfo=&image;writes[2].pBufferInfo=nullptr;}
            VkDescriptorImageInfo destination{VK_NULL_HANDLE,c.reference_images[1].view,g.reference_storage?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            if(destination.imageView){writes[3]=writes[2];writes[3].dstBinding=3;writes[3].descriptorType=g.reference_storage?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[3].pImageInfo=&destination;}
            VkDescriptorImageInfo sampler_info{g.sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
            uint32_t count=destination.imageView?4:3;
            if(g.texture_shape){auto& sampler=writes[count];sampler.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;sampler.dstBinding=count;
                sampler.descriptorCount=1;sampler.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;sampler.pImageInfo=&sampler_info;++count;}
            vkCmdPushDescriptorSetKHR(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,c.layout,0,count,writes);
        }
    }else vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.layout, 0, 1, &c.set, 0, nullptr);
}

static void record_compute_scene(VkCommandBuffer cmd)
{
    auto& c = compute_scene; if (!c.pipeline) return;
    // The preceding submission has finished. Resetting here makes a skipped dispatch observable.
    memset(c.data.map, 0, 16);
    if(c.binding_scene){
        memset(c.alternate.map,0,16);*static_cast<uint32_t*>(c.input.map)=c.input_word;
        if(g.descriptor_mode==1){
            VkDescriptorBufferInfo buffers[3]={{c.data.buf,0,16},{c.alternate.buf,0,16},{c.uniform_alias?c.data.buf:c.input.buf,0,16}};
            vkUpdateDescriptorSetWithTemplate(g.device,c.set,c.update_template,buffers);
        }
    }
    VkMemoryBarrier host = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; host.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | (c.uniform_alias ? VK_ACCESS_UNIFORM_READ_BIT : 0);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
    if(c.storage_image){
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=c.writable_image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkClearColorValue zero{};vkCmdClearColorImage(cmd,c.writable_image,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&barrier.subresourceRange);
        barrier.oldLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    }
    if(c.reference_pipeline)initialize_reference_images(cmd);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipeline);
    bind_compute_descriptors(cmd);
    if(c.reference_pipeline){record_reference_images(cmd);return;}
    for(unsigned i=0;i<c.repeat;++i){
        if(c.indirect)vkCmdDispatchIndirect(cmd,c.arguments.buf,0);else vkCmdDispatch(cmd,1,1,1);
        host.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|(c.uniform_alias?VK_ACCESS_UNIFORM_READ_BIT:0);
        if(c.buffer_barriers){
            VkBufferMemoryBarrier buffers[3]{};const Buffer* originals[]={&c.data,&c.input,&c.alternate};
            for(unsigned j=0;j<3;++j){auto& b=buffers[j];b.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.buffer=originals[j]->buf;b.size=VK_WHOLE_SIZE;}
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,3,buffers,0,nullptr);
        }else vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&host,0,nullptr,0,nullptr);
    }
    VkMemoryBarrier read = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    read.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; read.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &read, 0, nullptr, 0, nullptr);
}

static void late_compute_data()
{
    auto& c=compute_scene;if(!c.binding_scene || !c.late_update)return;
    *static_cast<uint32_t*>(c.input.map)=11;
}

static void read_compute_scene()
{
    auto& c = compute_scene; if (!c.data.map || c.output_path.empty()) return;
    const auto pending = c.output_path + L".tmp";
    FILE* file = nullptr; _wfopen_s(&file, pending.c_str(), L"wb");
    if (file) {
        uint32_t result[4];memcpy(result,c.data.map,16);
        if(c.binding_scene)result[1]=*static_cast<uint32_t*>(c.alternate.map);
        const bool wrote = fwrite(result, 1, 16, file) == 16; fclose(file);
        if (wrote) MoveFileExW(pending.c_str(), c.output_path.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
}

static void destroy_compute_scene()
{
    auto& c = compute_scene;
    destroy_reference_images();
    if (c.pipeline) vkDestroyPipeline(g.device, c.pipeline, nullptr);
    if (c.layout) vkDestroyPipelineLayout(g.device, c.layout, nullptr);
    if (c.pool) vkDestroyDescriptorPool(g.device, c.pool, nullptr);
    if (c.set_layout) vkDestroyDescriptorSetLayout(g.device, c.set_layout, nullptr);
    if(c.update_template)vkDestroyDescriptorUpdateTemplate(g.device,c.update_template,nullptr);
    destroy_buffer(c.data); destroy_buffer(c.arguments);
    destroy_buffer(c.alternate);destroy_buffer(c.input);
    if(c.writable_view)vkDestroyImageView(g.device,c.writable_view,nullptr);
    if(c.writable_image)vkDestroyImage(g.device,c.writable_image,nullptr);
    if(c.writable_memory)vkFreeMemory(g.device,c.writable_memory,nullptr);
}
