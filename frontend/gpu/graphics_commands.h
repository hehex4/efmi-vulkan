#pragma once
#include "shader_commands.h"

namespace efmi::gpu {
// Owned graphics state. This API takes Vulkan descriptions after source-state
// conversion; it does not infer game bindings or alter an application render pass.
struct GraphicsDescription {
    std::vector<VkFormat> colors;
    VkFormat depth_format=VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
    VkPrimitiveTopology topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool primitive_restart=false,alpha_to_coverage=false;
    uint32_t sample_mask=UINT32_MAX;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    std::vector<VkPipelineColorBlendAttachmentState> blend;
    std::array<float,4> blend_constants{};
    std::vector<VkVertexInputBindingDescription> streams;
    std::vector<VkVertexInputBindingDivisorDescription> divisors;
    std::vector<VkVertexInputAttributeDescription> attributes;
    GraphicsDescription(){raster.polygonMode=VK_POLYGON_MODE_FILL;raster.frontFace=VK_FRONT_FACE_CLOCKWISE;raster.lineWidth=1;}
};
class GraphicsPipeline {
public:
    explicit GraphicsPipeline(std::shared_ptr<ShaderPipelineState>);
    ~GraphicsPipeline();
    const std::vector<HlslProgram>& programs() const;
private:
    friend class ShaderCommands;
    std::shared_ptr<ShaderPipelineState> impl_;
};
struct GraphicsVertexBuffer {uint32_t slot=0;Buffer buffer;VkDeviceSize offset=0;};
struct GraphicsDraw {
    std::vector<ImageView> colors;
    ImageView depth;
    VkExtent2D extent{};
    uint32_t layers=1;
    VkViewport viewport{};
    VkRect2D scissor{};
    std::vector<GraphicsVertexBuffer> vertices;
    // GPU-produced indices/counts must stay in valid ranges. Indexed fetches
    // are not inspected with a hidden CPU wait. Indirect first-instance support
    // must actually be enabled, even if a particular buffer happens to use zero.
    Buffer indices,arguments;
    VkDeviceSize index_offset=0,argument_offset=0;
    VkIndexType index_type=VK_INDEX_TYPE_UINT32;
    uint32_t count=0,instances=1,first=0,first_instance=0;
    int32_t vertex_offset=0;
    bool indexed=false;
};
}
