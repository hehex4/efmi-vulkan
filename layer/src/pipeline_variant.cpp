#include "pipeline_variant.h"

namespace efvk {

PipelineVariant::~PipelineVariant() { clear(); }

void PipelineVariant::clear()
{
    if (pipeline) d_.DestroyPipeline(dev_, pipeline, nullptr);
    if (layout) d_.DestroyPipelineLayout(dev_, layout, nullptr);
    for (auto s : sets_) if (s) d_.DestroyDescriptorSetLayout(dev_, s, nullptr);
    pipeline = VK_NULL_HANDLE; layout = VK_NULL_HANDLE; own_layout = VK_NULL_HANDLE; sets_.clear();
}

bool prepare_vertex_variant(const GraphicsRecipe& recipe, const PipelineLayoutInfo& source,
                            uint32_t max_sets, VertexRepoint& rewritten, std::string& reason)
{
    reason.clear(); rewritten = VertexRepoint();
    if (!recipe.complete) { reason = "recipe:" + recipe.reason; return false; }
    if (!source.complete || source.flags || source.sets.size() != 3 || source.sets.size() >= max_sets) {
        reason = "layout_requires_three_complete_sets_and_one_free_slot"; return false;
    }
    for (const auto& s : source.sets) {
        if (!s || !s->complete || s->flags) { reason = "set_layout_unsupported"; return false; }
        for (const auto& pair : s->bindings) {
            if (!pair.second.immutable_samplers.empty() || pair.second.flags) {
                reason = "immutable_sampler_or_binding_flags"; return false;
            }
        }
    }
    size_t vs_index = recipe.stages.size();
    for (size_t i = 0; i < recipe.stages.size(); i++)
        if (recipe.stages[i].stage == VK_SHADER_STAGE_VERTEX_BIT) vs_index = i;
    if (vs_index == recipe.stages.size() || !repoint_vertex(recipe.stages[vs_index].shader->code, 3, rewritten)) {
        reason = "vertex_rewrite:" + rewritten.reason; return false;
    }
    auto buffer_binding = [&](uint32_t set, uint32_t binding, bool storage) {
        if (set >= source.sets.size()) return false;
        const auto& bindings = source.sets[set]->bindings;
        auto it = bindings.find(binding);
        if (it == bindings.end() || it->second.count != 1 || !(it->second.stages & VK_SHADER_STAGE_VERTEX_BIT)) return false;
        uint32_t type = it->second.type;
        return storage ? type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC :
                         type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    };
    if (!buffer_binding(rewritten.bone_set, rewritten.bone_binding, true) || !buffer_binding(2, 0, false)) {
        reason = "vertex_buffer_layout_mismatch"; return false;
    }
    return true;
}

bool PipelineVariant::build(const GraphicsRecipe& recipe, const PipelineLayoutInfo& source,
                            const VertexInputRecipe& vertex, uint32_t max_sets)
{
    clear(); reason.clear(); result = VK_SUCCESS;
    VertexRepoint rewritten;
    if (!prepare_vertex_variant(recipe, source, max_sets, rewritten, reason)) return false;
    size_t vs_index = 0;
    while (recipe.stages[vs_index].stage != VK_SHADER_STAGE_VERTEX_BIT) ++vs_index;
    own_set = 3;
    auto good = [&](VkResult r, const char* what) {
        result = r;
        if (r != VK_SUCCESS) { reason = what; return false; }
        return true;
    };
    for (const auto& s : source.sets) {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (const auto& pair : s->bindings) {
            const auto& b = pair.second;
            bindings.push_back({pair.first, static_cast<VkDescriptorType>(b.type), b.count, b.stages, nullptr});
        }
        VkDescriptorSetLayoutCreateInfo ci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = static_cast<uint32_t>(bindings.size()); ci.pBindings = bindings.data();
        VkDescriptorSetLayout set = VK_NULL_HANDLE;
        if (!good(d_.CreateDescriptorSetLayout(dev_, &ci, nullptr, &set), "create_set_layout")) { clear(); return false; }
        sets_.push_back(set);
    }
    VkDescriptorSetLayoutBinding own_bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo set_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set_info.bindingCount = 2; set_info.pBindings = own_bindings;
    if (!good(d_.CreateDescriptorSetLayout(dev_, &set_info, nullptr, &own_layout), "create_own_layout")) { clear(); return false; }
    sets_.push_back(own_layout);
    VkPipelineLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = static_cast<uint32_t>(sets_.size()); layout_info.pSetLayouts = sets_.data();
    layout_info.pushConstantRangeCount = static_cast<uint32_t>(source.pushes.size()); layout_info.pPushConstantRanges = source.pushes.data();
    if (!good(d_.CreatePipelineLayout(dev_, &layout_info, nullptr, &layout), "create_pipeline_layout")) { clear(); return false; }
    VkRenderPass pass = VK_NULL_HANDLE;
    if (!good(recipe.render_pass->create(dev_, d_.CreateRenderPass, d_.CreateRenderPass2, &pass), "create_render_pass")) {
        clear(); return false;
    }
    std::vector<VkShaderModule> modules;
    for (size_t i = 0; i < recipe.stages.size(); i++) {
        const auto& code = i == vs_index ? rewritten.words : recipe.stages[i].shader->code;
        VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = code.size() * sizeof(uint32_t); ci.pCode = code.data();
        VkShaderModule module = VK_NULL_HANDLE;
        if (!good(d_.CreateShaderModule(dev_, &ci, nullptr, &module), "create_shader_module")) break;
        modules.push_back(module);
    }
    if (modules.size() == recipe.stages.size())
        good(recipe.create(dev_, d_.CreateGraphicsPipelines, layout, pass, modules, &pipeline, &vertex), "create_variant_pipeline");
    for (auto m : modules) d_.DestroyShaderModule(dev_, m, nullptr);
    d_.DestroyRenderPass(dev_, pass, nullptr);
    if (result != VK_SUCCESS) { clear(); return false; }
    return true;
}

static bool geometry_ready(const DrawGeometry& g)
{
    if (!g.index || g.buffers.empty() || g.buffers.size() != g.offsets.size()) return false;
    for (auto b : g.buffers) if (!b) return false;
    return true;
}

bool emit_variant_draw(const VariantDispatch& d, VkCommandBuffer cmd, const PipelineVariant& variant,
                       VkDescriptorSet own, uint32_t instance_offset, const DrawGeometry& replacement,
                       const IndexedDraw& draw, VkPipeline original_pipeline, const DrawGeometry& original)
{
    if (!variant.pipeline || !variant.layout || !own || !original_pipeline || !draw.count || !draw.instances ||
        !geometry_ready(replacement) || !geometry_ready(original) || replacement.first_binding != original.first_binding ||
        replacement.buffers.size() != original.buffers.size()) return false;
    d.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, variant.pipeline);
    d.CmdBindVertexBuffers(cmd, replacement.first_binding, static_cast<uint32_t>(replacement.buffers.size()),
                          replacement.buffers.data(), replacement.offsets.data());
    d.CmdBindIndexBuffer(cmd, replacement.index, replacement.index_offset, replacement.index_type);
    d.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, variant.layout, variant.own_set, 1, &own, 1, &instance_offset);
    d.CmdDrawIndexed(cmd, draw.count, draw.instances, draw.first, draw.vertex_offset, draw.first_instance);
    d.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, original_pipeline);
    d.CmdBindVertexBuffers(cmd, original.first_binding, static_cast<uint32_t>(original.buffers.size()),
                          original.buffers.data(), original.offsets.data());
    d.CmdBindIndexBuffer(cmd, original.index, original.index_offset, original.index_type);
    return true;
}

} // namespace efvk
