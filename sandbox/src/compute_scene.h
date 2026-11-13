// Optional application compute work, with host readback after the submission fence.
// Included after the sandbox's buffer helpers; no layer-specific calls.
struct ComputeScene {
    std::wstring shader_path, output_path;
    bool indirect = false;
    bool binding_scene = false, late_update = false;
    unsigned repeat = 1;
    Buffer data, arguments;
    Buffer alternate, input;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorUpdateTemplate update_template = VK_NULL_HANDLE;
} compute_scene;

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
    if (!read || !make_buffer(c.data, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, true) ||
        !make_buffer(c.arguments, 12, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, true, true)) return false;
    memset(c.data.map, 0, 16);
    if(c.binding_scene){
        if(!make_buffer(c.alternate,16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true,true) ||
           !make_buffer(c.input,16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true,true))return false;
        memset(c.alternate.map,0,16);memset(c.input.map,0,16);
        *static_cast<uint32_t*>(c.input.map)=7;
    }
    const uint32_t groups[3] = {1, 1, 1}; memcpy(c.arguments.map, groups, 12);
    VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo si = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    VkDescriptorSetLayoutBinding bindings[3]={binding,binding,binding};bindings[1].binding=1;bindings[2].binding=2;
    si.bindingCount = c.binding_scene?3:1; si.pBindings = bindings;
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
    if(c.binding_scene && (g.descriptor_mode==1 || g.descriptor_mode==3)){
        VkDescriptorUpdateTemplateEntry entries[3]{};
        for(uint32_t i=0;i<3;++i)entries[i]={i,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,sizeof(VkDescriptorBufferInfo)*i,sizeof(VkDescriptorBufferInfo)};
        VkDescriptorUpdateTemplateCreateInfo t{VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO};
        t.descriptorUpdateEntryCount=3;t.pDescriptorUpdateEntries=entries;t.descriptorSetLayout=c.set_layout;
        t.templateType=g.descriptor_mode==3?VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR:VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
        t.pipelineBindPoint=VK_PIPELINE_BIND_POINT_COMPUTE;t.pipelineLayout=c.layout;
        CHECK(vkCreateDescriptorUpdateTemplate(g.device,&t,nullptr,&c.update_template));
    }
    if(c.binding_scene && g.descriptor_mode>=2)return true;
    VkDescriptorPoolSize capacity = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, c.binding_scene?3u:1u};
    VkDescriptorPoolCreateInfo pool = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 1; pool.poolSizeCount = 1; pool.pPoolSizes = &capacity;
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
        buffer.buffer=c.input.buf;write.dstBinding=2;vkUpdateDescriptorSets(g.device,1,&write,0,nullptr);
    }
    return true;
}

static void record_compute_scene(VkCommandBuffer cmd)
{
    auto& c = compute_scene; if (!c.pipeline) return;
    // The preceding submission has finished. Resetting here makes a skipped dispatch observable.
    memset(c.data.map, 0, 16);
    if(c.binding_scene){
        memset(c.alternate.map,0,16);*static_cast<uint32_t*>(c.input.map)=7;
        if(g.descriptor_mode==1){
            VkDescriptorBufferInfo buffers[3]={{c.data.buf,0,16},{c.alternate.buf,0,16},{c.input.buf,0,16}};
            vkUpdateDescriptorSetWithTemplate(g.device,c.set,c.update_template,buffers);
        }
    }
    VkMemoryBarrier host = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; host.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipeline);
    if(c.binding_scene && g.descriptor_mode>=2){
        VkDescriptorBufferInfo buffers[3]={{c.data.buf,0,16},{c.alternate.buf,0,16},{c.input.buf,0,16}};
        if(g.descriptor_mode==3)vkCmdPushDescriptorSetWithTemplateKHR(cmd,c.update_template,c.layout,0,buffers);
        else{
            VkWriteDescriptorSet writes[3]{};
            for(uint32_t i=0;i<3;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstBinding=i;
                writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&buffers[i];}
            vkCmdPushDescriptorSetKHR(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,c.layout,0,3,writes);
        }
    }else vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.layout, 0, 1, &c.set, 0, nullptr);
    for(unsigned i=0;i<c.repeat;++i){
        if(c.indirect)vkCmdDispatchIndirect(cmd,c.arguments.buf,0);else vkCmdDispatch(cmd,1,1,1);
        host.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&host,0,nullptr,0,nullptr);
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
    if (c.pipeline) vkDestroyPipeline(g.device, c.pipeline, nullptr);
    if (c.layout) vkDestroyPipelineLayout(g.device, c.layout, nullptr);
    if (c.pool) vkDestroyDescriptorPool(g.device, c.pool, nullptr);
    if (c.set_layout) vkDestroyDescriptorSetLayout(g.device, c.set_layout, nullptr);
    if(c.update_template)vkDestroyDescriptorUpdateTemplate(g.device,c.update_template,nullptr);
    destroy_buffer(c.data); destroy_buffer(c.arguments);
    destroy_buffer(c.alternate);destroy_buffer(c.input);
}
