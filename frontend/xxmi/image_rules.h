#pragma once
#include "runtime_host.h"

namespace efmi::frontend {
// A lossless common record for original 1D/2D/3D descriptions. Fields unused by
// a dimension are normalized only when converting to/from its native structure.
struct ImageSourceDescription {
    D3D11_RESOURCE_DIMENSION dimension=D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    D3D11_TEXTURE2D_DESC texture{};
    UINT depth=1;
};
struct OriginalImageView {
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
    D3D11_RENDER_TARGET_VIEW_DESC rtv{};
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
    bool valid=false;
};
// These call the pinned source's CPU rules. No original D3D device is invoked.
void original_image_override(CustomResource&,ImageSourceDescription&);
DXGI_FORMAT original_typeless(DXGI_FORMAT);
DXGI_FORMAT original_typed(DXGI_FORMAT,bool depth=false);
DXGI_FORMAT original_default_format(DXGI_FORMAT);
OriginalImageView original_image_view(const ImageSourceDescription&,ResourceCopyTargetType,DXGI_FORMAT);
bool original_float_clear(DXGI_FORMAT);
}
