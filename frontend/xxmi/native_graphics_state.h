#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <vector>

namespace efmi::frontend {
// Pointer-free current application facts. Bits: blend, depth, raster, topology.
struct NativeGraphicsState {
    unsigned known=0;
    VkPrimitiveTopology topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool restart=false;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    std::vector<VkPipelineColorBlendAttachmentState> blend;
    std::array<float,4> blend_constants{};
    uint32_t sample_mask=UINT32_MAX;
    bool alpha_to_coverage=false;
    VkViewport viewport{};
    VkRect2D scissor{};
    bool viewport_known=false,scissor_known=false;
};
}
