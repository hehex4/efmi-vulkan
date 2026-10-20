# RabbitFX under Vulkan: assessment (2026-10-19)

The plan (M4 item 4) asks for an assessment before any RabbitFX work. Six of the twelve directory
packages that load in the sandbox reference it (akekuri, avywenna, estella, mifu, perlica, rossiy),
and three of the Mods planning refuses (ardelia, liino, typhoeus) need it inside their callback
lists. Without it those characters would show the Mod's mesh with the game's own textures.

## What RabbitFX does

`Mods/RabbitFX -ENDMI-/RabbitFX.ini` (version 2.8) is a ShaderRegex library. Its main pattern
selects the game's character pixel shaders by their constant buffer declarations (CB0[45], CB2[40xx]
and so on, two outputs) and rewrites the first texture sample after an optional dither/discard
prefix (diffuse), the sample right after it when only multiplications lie between them (lightmap),
a following biased sample (normal map) and the rain-map samples: each becomes `if t70 is bound,
sample t70, else the original`. The Mod binds its textures to ps-t70 to t74 through
`CommandList\RabbitFX\SetTextures` when `ps == $RabbitFXMain` (the pattern's filter_index). Glow
and FX maps (t60, t61) and hue shifts are further branches.

## Ground truth from the catalogue

The DX11 pixel shaders the probe archived in session A are the ones the game had bound, so they are
already patched; the register in each `else` branch is the role's original register.
`scripts/rabbitfx_assessment.py` reads them and pairs each shader with its Vulkan fragment module
through the catalogue:

- 163 DX11 pixel shaders, 91 patched by the main pattern. Role sets: diffuse, lightmap, normal and
  rain 36; diffuse only 26; diffuse, normal and rain 14; diffuse and normal 11; diffuse and rain 4.
  The diffuse register varies from t11 to t21: no fixed register.
- 83 pairs with a Vulkan fragment module (79 DX11 shaders). With registers by D-076, each role's
  position in the SPIR-V image sample sequence: diffuse first in 79 (second 2, third 1, fourth 1);
  lightmap always second (34); normal third 36, second 16, first 3; rain 14th to 19th.
- The SPIR-V constant buffer sizes are the full blocks (CB0 82 vec4) where DX11 declares the used
  range (CB0[45]), so the pattern's selection cannot be reproduced from block sizes directly.

A candidate rule (diffuse = first sample; lightmap = second when only an FMul lies between; normal
= the next one) gets diffuse right in 79 of 83, lightmap in 75, normal in 54, all three in 52. The
misses are structural: four shaders sample other textures before RabbitFX's dither/discard prefix
ends, and whether a normal-map role exists depends on DX11 instruction shapes (sample_b within 20
lines) that SPIR-V does not keep.

## Conclusion and options

No exact rule from SPIR-V was found; the diffuse role alone comes close (95 percent of the ground
truth). Options, for the user to decide:

1. Diffuse only, by the first-sample rule, applied to fragment shaders whose first sample reads a
   colour texture; lightmap and normal stay the game's. Known wrong on 4 of 83 captured pairs.
2. Classify the game textures bound at the first samples by format and size (the approach of
   WWMI-DX12's rabbitfx module), checked against the same ground truth before use.
3. Keep RabbitFX Mods on the game's textures (now), or refuse their packages until one of the
   above is verified, so no character appears with mismatched textures in the game.

A table of DX11 shader hashes per Vulkan module would give exact roles for captured shaders only
and is excluded by D-075.

## Decision (2026-10-20)

The user chose option 2, classification by the bound textures (D-082). The rule and its agreement with
RabbitFX are in docs/rabbitfx-roles.md.
