// Optional application compute work, with host readback after the submission fence.
// Included after the sandbox's buffer helpers; no layer-specific calls.
struct ComputeScene {
    std::wstring shader_path, output_path;
    bool indirect = false;
    Buffer data, arguments;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
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
    const uint32_t groups[3] = {1, 1, 1}; memcpy(c.arguments.map, groups, 12);
    VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo si = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    si.bindingCount = 1; si.pBindings = &binding;
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
    VkDescriptorPoolSize capacity = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
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
    return true;
}

static void record_compute_scene(VkCommandBuffer cmd)
{
    auto& c = compute_scene; if (!c.pipeline) return;
    // The preceding submission has finished. Resetting here makes a skipped dispatch observable.
    memset(c.data.map, 0, 16);
    VkMemoryBarrier host = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; host.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.layout, 0, 1, &c.set, 0, nullptr);
    if (c.indirect) vkCmdDispatchIndirect(cmd, c.arguments.buf, 0);
    else vkCmdDispatch(cmd, 1, 1, 1);
    VkMemoryBarrier read = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    read.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; read.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &read, 0, nullptr, 0, nullptr);
}

static void read_compute_scene()
{
    auto& c = compute_scene; if (!c.data.map || c.output_path.empty()) return;
    const auto pending = c.output_path + L".tmp";
    FILE* file = nullptr; _wfopen_s(&file, pending.c_str(), L"wb");
    if (file) {
        const bool wrote = fwrite(c.data.map, 1, 16, file) == 16; fclose(file);
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
    destroy_buffer(c.data); destroy_buffer(c.arguments);
}
