# DX11 baseline of the sample mod, paired with the Vulkan frame

> 2026-10-12. Game data, one scene (character display), one mod (`arcane fes 1.2`), one frame per API.
> DX11: `captures/dx11-baseline/frame-a.*`, frame 30344, PID 9264, mods on, taken through the DX11 probe of
> the EFMI MCP project (0.1.6) with XXMI's `d3d11.dll` active. Vulkan: `captures/first-probe`, frame 30630,
> PID 8920, no mod loader. The two frames come from two game sessions of the same morning; the window size
> differed, so screen-sized textures do not compare. Not verified on a real character under Vulkan: nothing
> here draws anything.

How to repeat it: `scripts/dx11_baseline.py capture --label <name>` with the game running under DX11, then
`scripts/dx11_baseline.py summarize --label <name> --mod <ini> --vulkan captures/<bundle>`. The second step
writes `<name>.summary.json` next to the frame. Pairing uses the region hash of index data, which is the
same number under both APIs, and the look of what is bound (shape, size, mip count, format). The sibling
project is read as a library and is not written to.

## What the mod does under DX11

- XXMI issues 113 of the 346 calls of the frame: 56 indexed draws and 57 compute dispatches.
- The 12 original components are matched and skipped. Six of the mod's draw sections issue draws
  (components 0, 1, 2, 4, 6, 7); component 1 alone draws nine index ranges of one merged mesh. Sections
  3, 5 and 8 to 11 draw nothing in this state of the keys.
- A component is drawn in the passes the game draws it in, with the game's shaders. Component 0: 7 draws
  under DX11 (1 into the shadow map, 2 into the 5-target pass, 2 and 2 into the two 2-target passes) and
  7 draws under Vulkan (render passes 5, 8, 8, 21, 21, 24, 24). Of the 21 objects both frames contain
  (6 mod sections, 15 of the game's own index hashes), 18 are drawn the same number of times in the same
  order and were paired, 41 draws in all. The other three are drawn twice with the same shaders into the
  same targets, which the grouping folds into one use; they were left unpaired, not guessed.
- Mod geometry: 32-bit indices (the game: 16-bit, in 16 MiB pool buffers with nonzero bind offsets under
  both APIs); vertex streams of stride 16 / 12 / 16 with slot 3 bound to the position buffer again; the
  tangent comes from the game's 20-byte default buffer in slot 4 with stride 0.

## Merged skeleton: two compute steps per draw, both replaceable by CPU work

The EFMI core runs two of its own compute shaders (their reflection data is intact, so the names are the
core's own):

| Shader | Runs | Reads | Writes |
| --- | --- | --- | --- |
| `43f6d27b8549a1ad` | 12 times; the mod has 12 components | `ComponentBoneMatrices` = the game's bone buffer (8,413,184 bytes, structured, stride 16); `cb0` = 16 constants of the game's per-instance window | `MergedSkeletonRW`, 407,808 bytes |
| `798c78161fcb6665` | 45 times | `cb0` = the same 16 constants | `InstanceConfigRW`, 256 bytes |

45 is also the number of draws of the 12 original components in the Vulkan frame (7, 4, 4, 5, 4, 2, 3, 2,
2, 4, 4, 4): the second step runs once per original draw that is matched, the first once per component
and frame. The 56 draws of the mod follow in 23 uses.

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
| Per-instance window, 4096 constants: `vs-cb1`, `vs-cb2`, `vs-cb3` or `vs-cb6`, and `ps-cb0`, `ps-cb1` or `ps-cb2`, depending on the shader | set 2 binding 0, dynamic uniform buffer of 65,536 bytes, in all 38 paired draws that have one | fixed place; no per-shader search as under DX11 |
| Bone buffer: `vs-t0`, and `ps-t1` where the pixel shader reads it | one storage buffer of 8,413,184 bytes in set 0; binding 7, 9, 18 or 19 depending on the pipeline, once set 3 binding 2 | find by the buffer, not by the binding number |
| Other constant buffer windows | uniform buffers of the same size rounded up to 16 constants (1312 -> 1536, 11440 -> 11520, 32864 -> 33024) | by size |
| Material textures `ps-t10` to `ps-t18` | set 1, bindings 1 to 6 | per pixel shader, see below |
| Global textures (shadow maps, volumes, cube, ramps) | set 0 | per shader |

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

### Texture slots are per shader, on both sides

The same native part (index hash `a00ba2de`, 1056 indices, not replaced by the mod), main pass:

| DX11 `5872b5b6210fd060` | Texture | Vulkan pipeline 727 |
| --- | --- | --- |
| `ps-t10` | 256x1 RGBA8 | set 1 binding 4 (or set 0 binding 47: same look) |
| `ps-t11` | 256x256 RGBA8 | set 1 binding 1 |
| `ps-t15` | 2048x2048 BC7 sRGB, 12 mips (diffuse) | set 1 binding 6 |
| `ps-t16` | 2048x2048 BC7, 12 mips (light map) | set 1 binding 2 |
| `ps-t17` | 2048x2048 BC5, 12 mips (normal map) | set 1 binding 5 |
| `ps-t18` | 1024x1024 BC7 sRGB, 11 mips | set 1 binding 3 |

The order of the bindings is not the order of the registers, and other shaders have other tables (24
pixel shaders are in the summary; `ps-t13` is binding 1, 2, 3, 4 or 5 of set 1 depending on the shader).
The mod's INI already reflects this under DX11: its sections set `ps-t12` to `ps-t18` in different
combinations per component. Neither side carries names: the DX11 shaders are stripped of reflection data
like the SPIR-V is.

Checks on the table itself: within the frame no (pixel shader, slot) lands on two places, although
several shaders are used by more than one object. A second frame of the same session (`frame-b`, 150191)
gives the same 61 entries and none that differs; in it the native part above is drawn four times instead
of five, so the tool left it unpaired and its four material entries are missing there.

Consequence for D7: a mod's `ps-tN = ...` can only be carried out under Vulkan with a table from
(fragment shader, DX11 slot) to (set, binding). `dx11_baseline.py` produces such a table from a pair of
frames; shaders are the same for every player of a game version, so the table is data that can be shipped.

What this frame cannot give: the slots the mod replaces (`ps-t15`, `ps-t16`, `ps-t17` for component 0)
hold the mod's textures under DX11, so they pair with nothing. For component 0 the three unpaired Vulkan
bindings are set 1 binding 6 (BC7 sRGB, by format the diffuse map) and bindings 1 and 3 (both BC7, 2048,
12 mips: light map and normal map, not told apart). A frame with the mods switched off pairs the
originals directly, but two textures of the same look stay ambiguous even then. Telling them apart needs
the content: T-018.

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

## Open after this frame

- No frame with the mods switched off: the DX11 probe refused to press F11 because the game window was
  minimized, and nothing was sent. With such a frame every original component pairs like-for-like.
- Light map against normal map where both have the same look (T-018).
- How the fragment stage is kept on the game's data (T-019).
- One scene, one character, key states as they were; LOD1 not seen.
