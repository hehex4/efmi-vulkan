#pragma once
#include "draw_plan.h"
#include <optional>

namespace efmi::frontend {
// Source descriptions before device-state creation. A null sampler retains its
// native default identity. Actual Vulkan conversion and device validation are
// separate from the original parser's replace/merge rules.
struct SourceGraphicsState {
    D3D11_BLEND_DESC blend{};
    std::array<float,4> blend_factor{{1,1,1,1}};
    UINT sample_mask=UINT_MAX;
    D3D11_DEPTH_STENCIL_DESC depth{};
    UINT stencil_ref=0;
    D3D11_RASTERIZER_DESC raster{};
    D3D11_PRIMITIVE_TOPOLOGY topology=D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    std::array<std::optional<D3D11_SAMPLER_DESC>,16> pixel_samplers;
};
SourceGraphicsState source_graphics_defaults();
SourceGraphicsState source_graphics_override(const CustomShader&,const SourceGraphicsState&);
}
