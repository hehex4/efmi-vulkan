// Texture-role inspection fixture only, not the production RabbitFX library.
// It deliberately omits shader-filter gating and cannot establish rendering
// equivalence. Production must execute the original library and shader contract.
// Slots retain source meaning, never assumed to be Vulkan binding numbers.
#pragma once
#include <string>

namespace efmi::frontend {
inline std::wstring rabbit_texture_declarations()
{
    return LR"ini(namespace = RabbitFX
[ResourceDiffuse]
[ResourceLightmap]
[ResourceNormalmap]
[ResourceDiscardMap]
[ResourceRainmap]
[CommandListSetTextures]
ps-t70 = ref ResourceDiffuse
ps-t71 = ref ResourceLightmap
ps-t72 = ref ResourceNormalmap
ps-t73 = ref ResourceDiscardMap
ps-t74 = ref ResourceRainmap
ResourceDiffuse = null
ResourceLightmap = null
ResourceNormalmap = null
ResourceDiscardMap = null
ResourceRainmap = null
)ini";
}
inline bool rabbit_texture_slot(unsigned slot) { return slot >= 70 && slot <= 74; }
}
