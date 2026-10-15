#pragma once
#include "CommandList.h"
#include <functional>
#include <variant>

namespace efmi::frontend {
struct SourceResourceIdentity {
    enum class Tracking {Unknown,Untracked,Tracked};
    Tracking tracking=Tracking::Unknown;
    bool hash_known=false;
    uint32_t hash=0;
    // The provider proves the original policy's result for this exact requested
    // region. A successful zero is native fallback; false means unknown data.
    using RegionHash=std::function<bool(UINT offset,UINT size,uint32_t&,std::wstring&)>;
    RegionHash region_hash;
};
using SourceResourceDescription=std::variant<std::monostate,D3D11_BUFFER_DESC,
    D3D11_TEXTURE1D_DESC,D3D11_TEXTURE2D_DESC,D3D11_TEXTURE3D_DESC>;
struct SourceResourceMatch {
    bool bound=false;
    SourceResourceIdentity identity;
    SourceResourceDescription description;
    // Out-of-band values returned by the source GetResource for this target.
    UINT stride=0,offset=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
};
// Uses the unchanged original descriptor/draw predicates and region helpers.
// Requires the owning generation's globals; never dereferences a D3D object.
void source_resource_matches(const SourceResourceMatch&,ResourceCopyTargetType,
                             CommandListState&,TextureOverrideMatches&);
}
