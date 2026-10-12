// A sandbox-tested M2 building block. Probe mode never requests or emits these variants.
#pragma once

#include "pipeline_recipe.h"
#include "spirv_repoint.h"

namespace efvk {

#define EFVK_VARIANT_FUNCS(X) \
    X(CreateShaderModule) X(DestroyShaderModule) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
    X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateRenderPass) X(CreateRenderPass2) X(DestroyRenderPass) \
    X(CreateGraphicsPipelines) X(DestroyPipeline) \
    X(CmdBindPipeline) X(CmdBindVertexBuffers) X(CmdBindIndexBuffer) X(CmdBindDescriptorSets) X(CmdDrawIndexed)

struct VariantDispatch {
#define X(n) PFN_vk##n n = nullptr;
    EFVK_VARIANT_FUNCS(X)
#undef X
};

// CPU-only eligibility check. A true result is not proof that the driver can create the variant.
bool prepare_vertex_variant(const GraphicsRecipe&, const PipelineLayoutInfo&, uint32_t max_sets,
                            VertexRepoint&, std::string& reason);

class PipelineVariant {
public:
    PipelineVariant(VkDevice dev, const VariantDispatch& dispatch) : dev_(dev), d_(dispatch) {}
    ~PipelineVariant();
    PipelineVariant(const PipelineVariant&) = delete;
    PipelineVariant& operator=(const PipelineVariant&) = delete;
    // Do this on a worker with owned metadata; the caller keeps the original draw until ready.
    bool build(const GraphicsRecipe&, const PipelineLayoutInfo&, const VertexInputRecipe&, uint32_t max_sets);
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout own_layout = VK_NULL_HANDLE;
    uint32_t own_set = 0;
    std::string reason;
    VkResult result = VK_SUCCESS;
private:
    void clear();
    VkDevice dev_;
    VariantDispatch d_;
    std::vector<VkDescriptorSetLayout> sets_;
};

struct DrawGeometry {
    uint32_t first_binding = 0;
    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceSize> offsets;
    VkBuffer index = VK_NULL_HANDLE;
    VkDeviceSize index_offset = 0;
    VkIndexType index_type = VK_INDEX_TYPE_UINT32;
};

struct IndexedDraw {
    uint32_t count = 0, instances = 1, first = 0, first_instance = 0;
    int32_t vertex_offset = 0;
};

// No commands on failure. Leaves sets 0..own_set-1 and dynamic state untouched; restores every
// pipeline/vertex/index binding it changes. Texture patches and indirect draws are not supported.
bool emit_variant_draw(const VariantDispatch&, VkCommandBuffer, const PipelineVariant&, VkDescriptorSet own,
                       uint32_t instance_offset, const DrawGeometry& replacement, const IndexedDraw&,
                       VkPipeline original_pipeline, const DrawGeometry& original);

} // namespace efvk
