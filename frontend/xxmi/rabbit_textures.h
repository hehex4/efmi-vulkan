// RabbitFX for character generations. rabbit_texture_declarations is the texture-role inspection
// fixture only (it binds t70 to t74 literally, as the patched DX11 shaders read them).
// rabbitfx_library_declarations is the production interface (D-082): the library's resources,
// variables and command lists as Mods use them, where SetTextures binds a Mod's diffuse, lightmap
// and normal map at the DX11 registers the draw's own fragment shader samples them from
// (CommandListResolveRoles, a native command, asks the host). Glow, FX and hue features need
// RabbitFX's shader rewrite and are not reproduced: those lists only reset their inputs.
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

// Registers a role can have: the catalogue's character shaders sample them at t0 to t25.
constexpr unsigned kRabbitRoleSlots = 32;
inline std::wstring rabbitfx_library_declarations()
{
    std::wstring text = LR"ini(namespace = RabbitFX
[Constants]
global $h = 0.0
global $s = 0.0
global $v = 0.0
global $brightness = 1.0
global $interpolate = 1.0
global $censor = 0
global $RabbitFXMain = 1718.1
global $RabbitFXShadow = 1718.2
global $RabbitFXShadowFace = 1718.3
global $diffuse_slot = -1
global $lightmap_slot = -1
global $normal_slot = -1
[ResourceDiffuse]
[ResourceLightmap]
[ResourceNormalmap]
[ResourceDiscardMap]
[ResourceRainmap]
[ResourceGlowMap]
[ResourceFXMap]
[CommandListResolveRoles]
[CommandListSetTextures]
if DRAW_TYPE == 4
run = CommandListResolveRoles
)ini";
    for (const auto& role : {std::pair<const wchar_t*, const wchar_t*>{L"diffuse_slot", L"ResourceDiffuse"},
                             {L"lightmap_slot", L"ResourceLightmap"}, {L"normal_slot", L"ResourceNormalmap"}}) {
        // Flat blocks, not an elif chain: each elif nests one level deeper in the original's
        // command graph, and both of its phases are walked.
        for (unsigned slot = 0; slot < kRabbitRoleSlots; ++slot) {
            text += std::wstring(L"if $") + role.first + L" == " + std::to_wstring(slot) + L"\n";
            text += L"ps-t" + std::to_wstring(slot) + L" = ref " + role.second + L"\nendif\n";
        }
    }
    text += LR"ini(endif
ResourceDiffuse = null
ResourceLightmap = null
ResourceNormalmap = null
ResourceDiscardMap = null
ResourceRainmap = null
[CommandListRun]
ResourceGlowMap = null
ResourceFXMap = null
$h = 0.0
$s = 0.0
$v = 0.0
$brightness = 1.0
$interpolate = 1.0
[CommandListClean]
[CommandListCleanTextures]
)ini";
    return text;
}
}
