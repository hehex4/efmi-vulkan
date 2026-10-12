# DX11 baseline of the sample mod, paired with the Vulkan frame

> 2026-10-12. Game data, one scene (character display), one mod (`arcane fes 1.2`).
> DX11: four frames of one session (PID 9264, XXMI's `d3d11.dll` active), taken through the DX11 probe of
> the EFMI MCP project (0.1.6) into `captures/dx11-baseline`: `frame-a` (30344) and `frame-b` (150191) with
> the mods on, `frame-off` (170064) after EFMI's F11 switched all mods off, `frame-on` (170102) after the
> second F11. Vulkan: `captures/first-probe`, frame 30630, PID 8920, no mod loader. The Vulkan frame comes
> from another session of the same morning with another window size, so screen-sized textures do not
> compare. Not verified on a real character under Vulkan: nothing here draws anything.

How to repeat it, with the game running under DX11:

- `scripts/dx11_baseline.py capture --label <name>` saves a full frame with the constant buffer windows
  and, for every shader the frame uses, the slots it declares (`<name>.shaders.json`). `shaders --label`
  adds the declarations to a frame saved without them while the same game still runs.
- `scripts/dx11_baseline.py summarize --label <name> --mod <ini> --vulkan captures/<bundle>` writes
  `<name>.summary.json`.

Objects are paired by the region hash of their index data, which is the same number under both APIs
(mod parts by the index ranges of the mod's draw sections), draws by their order, and what is bound by
its look: shape, size, mip count, format. A DX11 slot takes part only if the shader of that stage
declares it; DX11 leaves earlier bindings in place and they would pair with things the draw never reads.
Equal looks are reported as a group and not resolved. The sibling project is read as a library and is
not written to.

## What the mod does under DX11

- With the mods on, XXMI issues 113 of the 346 calls of `frame-a`: 56 indexed draws and 57 compute
  dispatches. With the mods off it issues 2 of 279 (the notification).
- The 12 original components are matched and skipped. Six of the mod's draw sections issue draws
  (components 0, 1, 2, 4, 6, 7); component 1 alone draws nine index ranges of one merged mesh. Sections
  3, 5 and 8 to 11 draw nothing in this state of the keys. The mod's toggle keys only select index
  ranges, so no frame was taken for them.
- A component is drawn in the passes the game draws it in, with the game's shaders. Component 0: 7 draws
  under DX11 (1 into the shadow map, 2 into the 5-target pass, 2 and 2 into the two 2-target passes) and
  7 draws under Vulkan (render passes 5, 8, 8, 21, 21, 24, 24).
- With the mods off the game draws the 12 components 7, 4, 4, 5, 4, 2, 3, 2, 2, 4, 4, 4 times, 45 draws.
  The Vulkan frame has the same 12 numbers. All 45 pair draw for draw. Over everything both frames
  contain, 24 of 27 objects pair in `frame-off` (59 draws) and 18 of 21 in `frame-a` (41 draws). The rest
  are drawn a different number of times in the two frames, or (one mod part) twice with the same shaders
  into the same targets; they were left unpaired, not guessed.
- Mod geometry: 32-bit indices (the game: 16-bit, in 16 MiB pool buffers with nonzero bind offsets under
  both APIs); vertex streams of stride 16 / 12 / 16 with slot 3 bound to the position buffer again; the
  tangent comes from the game's 20-byte default buffer in slot 4 with stride 0.

## Merged skeleton: two compute steps, both replaceable by CPU work

The EFMI core runs two of its own compute shaders (their reflection data is intact, so the names are the
core's own):

| Shader | Runs | Reads | Writes |
| --- | --- | --- | --- |
| `43f6d27b8549a1ad` | 12 times; the mod has 12 components | `ComponentBoneMatrices` = the game's bone buffer (8,413,184 bytes, structured, stride 16); `cb0` = 16 constants of the game's per-instance window | `MergedSkeletonRW`, 407,808 bytes |
| `798c78161fcb6665` | 45 times | `cb0` = the same 16 constants | `InstanceConfigRW`, 256 bytes |

45 is the number of draws of the 12 original components: the second step runs once per original draw
that is matched, the first once per component and frame. The 56 draws of the mod follow in 23 uses. All
three mods-on frames have the same counts.

The 16 constants are the instance entry of the draw being replaced. Constant 3 is the translation
(-300, 300, -300, 1), constant 5 is (bone offset, bone offset of the previous frame, 258, 1.0f) with the
first three as integers: (504633, 502851, 258) in the game's entry, (1533, 3030, 258) in the copy the mod
draw uses. Offsets count 16-byte elements. This is the structure read under Vulkan on the same morning:
constant 5 of the 65,536-byte dynamic uniform buffer at set 2 binding 0, bone rows at `x * 16` and
`y * 16` in the 8,413,184-byte storage buffer.

So under DX11 the core needs exactly three inputs per replaced draw: the instance entry (256 bytes), the
bone rows it points at, and `IniParams`. Under Vulkan all three are in host-visible mapped memory
(first probe session). Whether they are already written when the draw is recorded is what T-017 measures.

## Where a DX11 slot is under Vulkan

| DX11 | Vulkan | Rule |
| --- | --- | --- |
| Per-instance window, 4096 constants: `vs-cb1`, `vs-cb2`, `vs-cb3` or `vs-cb6`, and `ps-cb0`, `ps-cb1` or `ps-cb2`, depending on the shader | set 2 binding 0, dynamic uniform buffer of 65,536 bytes: 56 of 56 paired draws that have one in `frame-off`, 38 of 38 in `frame-a` | fixed place; no per-shader search as under DX11 |
| Bone buffer: `vs-t0`, and `ps-t1` where the pixel shader reads it | one storage buffer of 8,413,184 bytes in set 0; binding 7, 9, 18 or 19 depending on the pipeline, once set 3 binding 2 | find by the buffer, not by the binding number |
| Other constant buffer windows | uniform buffers of the same size rounded up to 16 constants (1312 -> 1536, 11440 -> 11520, 32864 -> 33024) | by size |
| Material textures | set 1 | per Vulkan fragment shader, see below |
| Global textures (shadow maps, volumes, cube, ramps, 1x1 defaults) | set 0 | per Vulkan fragment shader |

One Vulkan binding serves the vertex and the fragment stage. Under DX11 the stages have separate slots,
and the core replaces only the vertex shader's: the mod draw has XXMI's 256-byte buffer at `vs-cb2` and
XXMI's merged skeleton at `vs-t0`, while `ps-cb2` and `ps-t1` stay the game's. The pixel shader does read
them: `5872b5b6210fd060` picks the entry by the instance index it gets from the vertex shader and, when
bit 16 of `constant4.w` is set, loads the fourth component of elements `constant5.x` and
`constant5.x + 2` of `t1` (two translation terms of the component's first bone); otherwise it takes them
from constant 3. A Vulkan replacement that swaps the shared binding changes what the fragment shader
reads as well. Two ways out, neither chosen yet (T-019): give the vertex stage bindings of its own in the
pipeline variant (the stage's SPIR-V module is separate, and re-pointing a variable is a change of two
decorations), or accept that the fragment shader reads the merged buffer at the patched offset and check
what that changes on screen.

### Texture slots: a table per Vulkan fragment shader

Component 1 with the mods off, main pass (DX11 `5872b5b6210fd060`, Vulkan fragment shader `c90341d7`):

| DX11 | Texture | Vulkan |
| --- | --- | --- |
| `ps-t11` | 256x256 RGBA8 | set 1 binding 1 |
| `ps-t15` | 2048x2048 BC7 sRGB, 12 mips (diffuse) | set 1 binding 6 |
| `ps-t16` | 2048x2048 BC7, 12 mips (light map) | set 1 binding 2 |
| `ps-t17` | 2048x2048 BC5, 12 mips (normal map) | set 1 binding 5 |
| `ps-t18` | 1024x1024 BC7 sRGB, 11 mips | set 1 binding 3 |
| `ps-t12`, `ps-t14`, `ps-t19`, `ps-t21` | global textures | set 0 bindings 44, 39, 45, 36 |

- The order of the bindings is not the order of the registers, and every shader has its own table:
  `ps-t13` is binding 1, 2, 3, 4 or 5 of set 1, or a binding of set 0, depending on the shader.
- Neither side carries names: the DX11 shaders are stripped of reflection data like the SPIR-V is.
- The table belongs to the Vulkan fragment shader, not to the DX11 pixel shader. DX11
  `a6bf994da789358e` is drawn with two Vulkan fragment shaders, `bc9d2d41` and `d2ebb272`; its `ps-t12`
  is set 1 binding 2 in the first and binding 3 in the second, where the material set has one more
  texture in front (the vertex stage's `vs-t1` is the candidate). Bindings are numbered per Vulkan
  program, across both stages.
- Over the four DX11 frames the table has 111 entries for 36 Vulkan fragment shaders, and no entry of
  one frame contradicts another frame. A fragment shader whose pipeline is not in the Vulkan bundle is
  listed under its pipeline number.

### What the mod's own slot assignments become

The mod's draw sections set `ps-t12` to `ps-t18` in different combinations per component. With the mods
off, every such slot of every pass was looked up (`frame-off`; 19 lookups have a declared slot, 15 are
placed, 4 are one of two):

| Component | Pass (Vulkan fragment shader) | Result |
| --- | --- | --- |
| 0 | main, `09e0be38` | `ps-t15` diffuse -> set 1 binding 6; `ps-t16` and `ps-t17` -> bindings 1 and 3, order open (both 2048x2048 BC7) |
| 0 | second draw of render pass 21, `4e9748d0` | `ps-t15` -> set 0 binding 36, a 1x1 volume texture; the game's diffuse is at `ps-t13` here |
| 0 | render pass 24, `2e2750b8` | `ps-t15` -> set 1 binding 2 (256x256 RGBA8); `ps-t17` -> set 1 binding 6, which is the diffuse map in this pass; `ps-t16` -> one of two 512x512 BC7 |
| 1 | main, `c90341d7` | `ps-t15`, `ps-t16`, `ps-t17`, `ps-t18` -> set 1 bindings 6, 2, 5, 3 |
| 2 | main, `493c1a7e` | `ps-t13` -> set 1 binding 4; `ps-t14` -> binding 3 |
| 2 | second draw, `50c1e4bb` | `ps-t13` -> set 1 binding 3; `ps-t14` -> set 0 binding 36 (1x1) |
| 4 | main, `3983573e` | `ps-t18` -> set 1 binding 7; `ps-t17` -> binding 5 or 8 |
| 6 | main, `2c9bccf9` | `ps-t12` -> set 1 binding 3 |

In 37 further combinations of slot and pass (shadow pass, 5-target pass and others) the shader does not
declare the slot, and the mod's assignment has no effect under DX11.

The DX11 resource hashes show which textures these are for component 0: the main pass has `fb865d37`,
`9a6c6973`, `733efa0d` at `ps-t15`, `ps-t16`, `ps-t17`; render pass 24 has the same three at `ps-t17`,
`ps-t18`, `ps-t19`; the `4e9748d0` pass has `fb865d37` at `ps-t13`.

The mod assigns its slots in every pass, whatever the shader of that pass has there. The DX11 frames show
the consequence for component 0: in the `4e9748d0` pass the mod's diffuse map replaces a 1x1 volume
texture and the game's own diffuse map stays at `ps-t13`; in render pass 24 the mod's normal map sits in
the slot that pass reads its diffuse map from, and the game's light and normal maps stay at `ps-t18` and
`ps-t19`. That is how the mod looks under DX11. A port that is meant to show the same picture has to
translate by slot and pass, which is what the table does; replacing "the diffuse map" by role would give
another picture.

Consequence for D7: a mod's `ps-tN = ...` is carried out under Vulkan through a table from
(Vulkan fragment shader, DX11 slot) to (set, binding), which can be a binding of the global set.
`dx11_baseline.py` produces the table from paired frames; shaders are the same for every player of a
game version, so the table is data that can be shipped. Where two textures of a draw look alike the
entry stays open; telling them apart needs the content (T-018).

## Vertex input

Component 0, main pass. Left: the mod's draw under DX11. Right: the game's own pipeline 857 under Vulkan.

| DX11 element | DX11 mod draw | Vulkan location | Vulkan, game |
| --- | --- | --- | --- |
| POSITION | slot 0, float3, offset 0 | 0 | binding 0, float3, offset 0 |
| NORMAL | slot 0, R32, offset 12 | 2 | binding 0, R32, offset 12 |
| TANGENT | slot 4, RGBA8 unorm, offset 12 | 3 | binding 4, RGBA8 unorm, offset 12 |
| TEXCOORD | slot 1, float2, offset 0 | 1 | binding 1, float2, offset 0 |
| TEXCOORD4 | slot 1, RGBA8 snorm, offset 8 | 4 | binding 1, RGBA8 snorm, offset 8 |
| TEXCOORD5 / 6 / 7 | slot 3, float3 / R32 / R32 | 5 / 6 / 7 | binding 3, the same |
| BLENDWEIGHTS | slot 2, RGBA16 unorm, offset 0 | 8 | binding 2, RGBA16 unorm, offset 0 |
| BLENDINDICES | slot 2, **RGBA16 uint**, offset 8 | 9 | binding 2, **RGBA8 uint**, offset 8 |
| Strides | 16, 12, **16**, 16, 0 | | 16, 12, **12**, 16, 0 |

One difference: the blend indices. The pipeline variant of D8 changes the stride of that binding and the
format of the one unsigned-integer attribute on it; it does not need the semantic names. In the shadow
pass the same attributes sit at locations 5 and 6, so the attribute is found by its binding and format.

## Open

- Four table entries where two textures of a draw look alike (T-018).
- How the fragment stage is kept on the game's data (T-019).
- The table covers the fragment shaders of this scene. Three objects common to both frames are drawn a
  different number of times and are not paired.
- One scene, one character; LOD1 not seen; the Vulkan frame is from another session.
