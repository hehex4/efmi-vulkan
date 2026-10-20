# RabbitFX texture roles under Vulkan (candidate 1.5.8, D-082)

RabbitFX is a ShaderRegex library for EFMI (`Mods/RabbitFX -ENDMI-/RabbitFX.ini`). Under DX11 it
rewrites the game's character pixel shaders so that three samples read a Mod's textures when they
are bound at t70 to t72: the diffuse map, the lightmap and the normal map. A Mod sets
`Resource\RabbitFX\Diffuse`, `LightMap` and `NormalMap` and runs `CommandList\RabbitFX\SetTextures`.
Nine installed character Mods use it (akekuri, avywenna, estella, mifu, perlica, rossiy, ardelia,
liino, typhoeus). Under Vulkan nothing rewrites the shaders; the layer instead finds the three samples
in the draw's own fragment shader and binds the Mod's textures at their DX11 registers (D-076), which
then go through the same material path as a Mod's fixed `ps-t16 = ...` bindings. The user chose this
classification on 2026-10-20 (docs/rabbitfx-assessment.md, option 2).

## The rule

From the fragment module (server/efmi_vk/texture_roles.py, frontend/gpu/texture_roles.cpp):

- its image samples in instruction order, each with the t register of its image, its instruction
  index, and whether its result is decoded as a tangent-space normal (scaled by 2 and, within 12
  steps of data flow, a square root of something derived from it);

and from the textures the draw binds there (format, size, mip levels):

- `S` an sRGB asset texture, `N` a two-channel one (BC5), `U` another linear block-compressed one,
  `o` anything else; an asset texture is square with a full mip chain, so render targets, lookup
  tables and placeholders are `o`.

Then: the diffuse map is the first `S`. A `U` right after it, of the same size, is the lightmap. The
normal map is the first `N` after those, else the first sample decoded as a normal within 100
instructions of the previous role's sample, else the next `U`. A role not found binds nothing, and
the game's texture stays. Registers t0 to t31 can take a role (the catalogue's are t0 to t25).

Every role sample of the catalogue's character shaders carries a Bias operand, so bias says nothing about the
normal map; the decode and the distance do. Format alone cannot tell a lightmap from a normal map
stored as BC7 UNORM.

## Checked against RabbitFX itself

The archived DX11 shaders of the catalogue are already patched, so `t70` names RabbitFX's diffuse
register and so on (scripts/rabbitfx_assessment.py). scripts/check_texture_roles.py (suite step,
truth in docs/texture-roles-truth.json, derived metadata only) pairs them with the Vulkan draws:

| | Agree with RabbitFX |
| --- | --- |
| diffuse | 462 of 480 draws |
| lightmap | 474 of 480 draws |
| normal map | 457 of 480 draws |
| all three | 117 of 128 patterns (module and bound textures) |

The 11 other patterns, all named in the check:

- RabbitFX takes the lightmap for the normal map (e68ce2ff, c3ec7e65, 85b304b4). In e68ce2ff an
  alpha test lies between the diffuse and the next sample, so RabbitFX's "only multiplications
  between" fails; the module reads that sample component by component and decodes the third one as
  a normal. The rule follows what the shader does, not RabbitFX's mistake.
- RabbitFX names the colour texture the normal map and a placeholder or a later small texture the
  diffuse (8210722e, 4d789b6d, 026c0d6f), or the fourth sample the diffuse (9bb6fe05).

The layer's C++ reading of the samples equals the Python one on all 83 modules.

## In the layer

The native Core's generations carry a RabbitFX namespace of their own (frontend/xxmi/rabbit_textures.h):
the library's resources and variables, SetTextures as flat `if $diffuse_slot == N` blocks that bind
`ps-tN = ref ResourceDiffuse` (and the lightmap and normal map alike), and CommandListResolveRoles, a
native command that asks the host (`RuntimeDraw::texture_roles`) and sets the three slots; a role
without a Mod texture gets no slot. The character backend answers per draw from the pipeline's cached
samples and the images its descriptors hold (status texture_role_queries, texture_role_draws).
Glow and FX maps (t60, t61) and the hue/saturation/brightness controls depend on RabbitFX's shader
rewrite and are not reproduced: Run and the clean lists only reset their inputs.

A Mod's slots are those of all its draws, RabbitFX roles included, and every fragment shader derives
rules for all of them. Two places refused a whole shader because one slot was odd there, and left
that shader's draws with the game's textures (BUG-030): a slot that holds a buffer or an array in that
shader, and an image that cannot take a file (an integer image, a depth comparison). Both now affect
only that slot; a draw that binds it is refused.

## Results (sandbox replay, 2026-10-20)

All 16 catalogue bundles replayed for 360 frames with layer 1.5.8 (scripts/replay_capture.py; its
textures now keep the recorded size and mip chain, so the classification sees what the game binds):
0 validation messages in 32 runs, the same 13 characters replaced in every pass with a component draw.
RabbitFX roles were asked in the draws of Liino (6102 queries, 3948 with a diffuse map), Typhoeus
(3580, 1790), mifu (3580, 2864), rossiy (4680, 3600) and perlica (2864, 1790); the other Mods use
RabbitFX only in sections that keep the game's mesh, or not at all. No draw kept the game's textures
for a refused profile (material_inherited 0 in all 13, it was 356 for mifu before BUG-030's fix) and
none was refused for its material. mifu's hair, which takes its textures only through RabbitFX, now
shows them in the forward pass (build/replay/characters.png).

Independent of RabbitFX: on arcane's main forward shader the rule names t16, t17 and t18 the diffuse
map, lightmap and normal map, the slots arcane's author bound by hand (`ps-t16`, `ps-t17`, `ps-t18`).

The forward targets of most characters look grey in the pictures: their shaders take colour from
light buffers and ramps the bundle does not hold (stand-ins), not from a wrong binding.

## Not covered

- RabbitFX in sections that keep the game's mesh and only change its textures (13 sections of six
  Mods in the dry run): the layer replaces only draws it draws itself. A texture-only replacement of
  game draws is the next step.
- The role answer in the game: the replay's textures have the recorded descriptions but no content.
