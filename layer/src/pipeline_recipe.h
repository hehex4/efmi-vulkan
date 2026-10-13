// Owned descriptions for rebuilding classic graphics pipelines after application objects die.
// Unsupported extensions are explicit: never silently drop state and call the result equivalent.
#pragma once

#include "resources.h"

namespace efvk {

class JsonWriter;

struct RenderSubpass {
    VkSubpassDescriptionFlags flags = 0;
    VkPipelineBindPoint point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    uint32_t view_mask = 0;
    std::vector<VkAttachmentReference2> inputs, colors, resolves;
    bool has_depth = false;
    VkAttachmentReference2 depth = {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
    std::vector<uint32_t> preserves;
    bool uses_color() const;
    bool uses_depth() const;
};

struct RenderPassRecipe {
    uint32_t id = 0;
    bool version2 = false;
    bool complete = false;
    std::string reason;
    VkRenderPassCreateFlags flags = 0;
    std::vector<VkAttachmentDescription2> attachments;
    std::vector<RenderSubpass> subpasses;
    std::vector<VkSubpassDependency2> dependencies;
    std::vector<uint32_t> correlated;

    static RenderPassRecipe capture(const VkRenderPassCreateInfo&);
    static RenderPassRecipe capture(const VkRenderPassCreateInfo2&);
    VkResult create(VkDevice, PFN_vkCreateRenderPass, PFN_vkCreateRenderPass2, VkRenderPass*) const;
    void write(JsonWriter&) const;
};

struct RecipeStage {
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
    std::string entry;
    std::shared_ptr<const ShaderInfo> shader;
    std::vector<VkSpecializationMapEntry> entries;
    std::vector<uint8_t> data;
};

struct VertexInputRecipe {
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
};

// Frontend-supplied stream contracts. Locations stay those of the application's shader;
// matching uses the original byte offset/format, never a component number or shader hash.
struct VertexElementPatch {
    uint32_t source_offset = 0;
    VkFormat source_format = VK_FORMAT_UNDEFINED;
    uint32_t offset = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
};
struct VertexStreamPatch {
    uint32_t binding = 0, source_stride = 0, stride = 0;
    std::vector<VertexElementPatch> elements;
};
// Bytes one element of a vertex format occupies; 0 for a format this code does not know.
uint32_t vertex_format_bytes(VkFormat);
// Every element in a replaced stream must be explicitly covered. Unreplaced streams,
// including stride-zero application defaults, survive unchanged. Failure is atomic.
bool patch_vertex_streams(const VertexInputRecipe&, const std::vector<VertexStreamPatch>&,
                          VertexInputRecipe& output, std::string& reason);

struct GraphicsRecipe {
    bool complete = false;
    std::string reason;
    uint32_t subpass = 0;
    VkPipelineCreateFlags flags = 0;
    std::shared_ptr<const RenderPassRecipe> render_pass;
    std::vector<RecipeStage> stages;
    VertexInputRecipe vertex;
    VkPipelineInputAssemblyStateCreateInfo assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    VkPipelineRasterizationStateCreateInfo raster = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    VkPipelineMultisampleStateCreateInfo multisample = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    VkPipelineDepthStencilStateCreateInfo depth = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bool has_depth = false, has_blend = false;
    uint32_t viewport_count = 0, scissor_count = 0;
    std::vector<VkViewport> viewports;
    std::vector<VkRect2D> scissors;
    std::vector<VkSampleMask> sample_mask;
    std::vector<VkPipelineColorBlendAttachmentState> blend_attachments;
    std::vector<VkDynamicState> dynamic;

    static GraphicsRecipe capture(const VkGraphicsPipelineCreateInfo&, const std::vector<ShaderStage>&,
                                  std::shared_ptr<const RenderPassRecipe>);
    // Pointers are baked only on the stack during this call; copies/moves of the recipe are safe.
    // All handles here must be owned by the caller, not borrowed application creation handles.
    VkResult create(VkDevice, PFN_vkCreateGraphicsPipelines, VkPipelineLayout, VkRenderPass,
                    const std::vector<VkShaderModule>&, VkPipeline*, const VertexInputRecipe* override_vertex = nullptr) const;
    void write(JsonWriter&) const;
};

void on_create_render_pass(Device*, VkRenderPass, const VkRenderPassCreateInfo*);
void on_create_render_pass2(Device*, VkRenderPass, const VkRenderPassCreateInfo2*);
void on_destroy_render_pass(Device*, VkRenderPass);

} // namespace efvk
