# EFMITTL transparency on Vulkan (D-113)

Since 1.7.8 the parts a Mod draws through EFMITTL are drawn opaque by the
library's own fallback (D-112). This document is the plan for drawing them
translucent, as the library does under DX11, and the record of what is built.

## Division of work

The library's INI keeps the policy; the layer supplies what the library gets
from the game under DX11 and does the GPU work of its capture and composite.

- **Policy, unchanged INI.** `CommandListDraw` decides per pass: opaque calls draw
  as before, the shadow pass draws through `CommandListShadow`, the G-buffer and
  outline leave the part out, the forward lit pass captures it, and `FrameReset`
  switches to the opaque fallback when its hooks did not show up in a frame.
- **Hook facts.** The library's four ShaderRegex groups tell it which pass a draw
  is in. On Vulkan their DXBC patterns cannot be evaluated (docs/
  efmittl-compatibility.md, offline facts), so the layer supplies their meaning
  from the replaced draw's own facts:
  - lit vertex shader (`$vs_kind = 1` before the draw, 0 after): a draw in a
    subpass with colour targets o0 and o1, no o2, and an EQUAL depth test, the
    forward lit pass of characters;
  - plain and fading shadow vertex shaders: never; their Vulkan outputs cannot
    feed the library's shadow pixel shaders, and the library's own rule for an
    unrecognised shadow pass draws the full shadow;
  - depth copy (`if $lit_seen` then `$copy_after_lit = 1`, then the composite):
    at the end of the forward lit render pass, the point the copy shader marks
    under DX11 (the next pass is that copy).
- **Capture, native.** `CommandListCapture` is a native boundary: the call's
  opacity, mask, channel, inversion and index range become a translucent item
  of the replaced draw's plan. The backend records it at the end of the forward
  pass, in the order of the calls, as the library's four custom shaders do:
  back faces then front faces, colour with the game's own shaders (its screen
  space mask `t4` replaced by a neutral one), opacity with the library's
  `alpha.hlsl`, each set into a layer's own colour (RGBA16F) and depth (D32,
  cleared to 0, GREATER_OR_EQUAL; EQUAL for the opacity). Four layers per frame;
  calls after the fourth share the last.
- **Composite, native.** After the captures, one full-screen draw with the
  library's `composite.hlsl` blends every layer surface in front of the scene
  depth, farthest first, into the forward pass's colour target (ONE,
  SRC_ALPHA).
- **One frame state.** The layers belong to the device, not to a character
  generation: every consumer's calls share them and one composite per forward
  pass blends them all, as the library's single frame state does.

The library's shaders are compiled from its own folder; nothing of the library
is copied into the project. A library whose capture or composite lists are not
the ones this bridge knows (a later version that renames them) is left to its
fallback, as before.

## Built in 1.7.9

- Layer (layer.cpp, render_targets.cpp, state.h): the first viewport and scissor
  of each recording, the classic render pass instance it is in (framebuffer,
  area, recipe, first draw) and the module's `pass_end` after the application's
  vkCmdEndRenderPass (ABI change: the injected DLL and the module go together).
- Hook facts (shader_hooks.cpp): the lit vertex shader is a vertex module whose
  located outputs are a subset of 1479b2b5's signature at locations 0-8 (float2,
  float3, float3, float4, four float3, one scalar) with the UV at 0 and the
  instance at 8, reading InstanceIndex, with a storage and a uniform buffer. The
  copy shader is a fragment module whose only work is one explicit-LOD sample of
  one float 2D texture at its only float2 input, stored to its only output.
  scripts/check_shader_hooks.py checks both on the paired catalogue: the seven
  Vulkan vertex shaders that pair with 1479b2b5 are lit and the 57 others are
  not; the copy rule names 4b4220a1 alone and no character shader.
- Frontend (efmittl_bridge.cpp): `$vs_kind` from the draw's lit fact before its
  lists and 0 after; the copy hook's pre list (`$copy_after_lit` when
  `$lit_seen`) on a copy draw; `CommandListCapture` a native boundary emitting a
  translucent plan item (opacity, mask, channel, inversion, index range) that
  reaches the renderer's draw list (FilePlanItem/CharacterDraw).
- Backend (translucency.cpp, character_backend.cpp): a translucent draw takes
  four capture variants of the game pipeline (capture_recipe: the layer's
  RGBA16F + D32 pass, GREATER_OR_EQUAL with writes for colour, EQUAL without for
  opacity, face culled, RGB or A written, the game's dynamic states kept), the
  colour one with the game's t4 re-pointed to a neutral mask, the opacity one
  with the library's alpha.hlsl compiled by DXC and its t100/t120 moved to the
  private set. At draw time only its private sets, the game's bound sets and
  dynamic offsets, viewport and scissor are kept; at the end of the pass the
  calls are recorded in order (four layers, later calls sharing the last; back
  faces then front faces, colour then opacity), then the composite with the
  library's composite.hlsl into the pass's colour target over its depth (depth
  made readable for the draw and back), then the game's bindings again. The
  layers are the device's: one composite per forward pass for every consumer.
- The opaque fallback stays where the bridge cannot work: a library without the
  known interface, a pass whose targets are not known, layers that cannot be
  made. A translucent call the renderer cannot capture is drawn as before.

Verification: Replay vk01 (Yvonne, 120 frames, --copy-passes): 456 captures, 114 composites, the library's $fallback staying 0, Vulkan validation 0 errors (4 performance notes); against 1.7.8's opaque fallback 30265 forward-target pixels differ, the sleeves (alpha 0.2) most, the dress (0.7) less (build/replay-1.7.9/). vk02 (TangTang, translucent variant on): 114 captures, 114 composites. Hook rules on the paired catalogue: the 7 Vulkan vertex shaders pairing with 1479b2b5 lit, 57 others not; copy rule names 4b4220a1 alone. Sandbox only.

Limits: shadows stay full (the Vulkan shadow vertex shaders output no UV the
library's shadow shaders could read, docs/efmittl-compatibility.md); the
composite runs at the end of the capturing pass, which for the forward pass is
the point the game's copy marks (the next pass is that copy); the layers assume
the game renders a frame on one queue at a time (BUG-032 saw it change queues
once); the bridge knows EFMITTL 0.5.x's interface by its list, variable and
resource names.
