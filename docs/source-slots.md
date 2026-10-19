# DX11 registers from the game's SPIR-V (D-076)

Mods address the game's resources by DX11 register: `ps-t16 = ResourceDiffuse` in a matched draw,
`checktextureoverride = ps-t17`, `vs-cb1` windows. Under Vulkan the same draw binds those resources
at a set and binding. The correspondence is derived from the Vulkan shader alone, so it holds for
every character and Mod, including shaders nobody captured (D-075).

## The rule

The game compiles each shader twice from one HLSL source: FXC for DX11 and DXC for Vulkan
(SPIR-V generator 14). Neither keeps names (no RDEF chunk, no OpName), but both keep the source's
declaration order: DXC emits the module's resource variables in that order, FXC numbers registers
in that order. The DX11 build additionally places read-only buffers ahead of textures (FXC alone
does not; checked with an own shader), so:

- the t register of a resource is its rank among the module's shader resource views, read-only
  buffers first (StructuredBuffer and ByteAddressBuffer as NonWritable storage buffers, typed
  Buffer as texel buffers), then images, each group in declaration order; an array takes as many
  registers as it has elements;
- constant buffers: cb registers in the declaration order of uniform blocks. The Vulkan block is
  as large as the DX11 one or larger (layout padding) in nearly all cases, so this is checked by
  size only;
- samplers (s) and UAVs (u) are numbered in declaration order without ground truth yet.

`server/efmi_vk/source_slots.py` implements it (`declarations`, `registers`, `binding_of`).

## Evidence

`docs/source-slot-truth.json` holds, per game shader of paired scenes, the resource declarations
read from its SPIR-V (no byte code) and every slot the catalogue placed in one binding:
258 shaders from the 2026-10-19 catalogue (16 characters) and the 2026-10-12/13 arcane frames.
`scripts/check_source_slots.py` (suite step) applies the rule: all 1187 observed t registers of
pixel and vertex shaders, textures and buffers, come out as observed. 1121 constant buffer windows
agree; 5 differ, where the catalogue paired windows by size alone. `--refresh` rebuilds the truth
file from catalogues and their capture bundles.

Unit tests compile an own HLSL fixture with DXC and check kinds, order, array counts and register
numbers of the parser.

## Limits

- A shader whose DX11 build declares a resource the SPIR-V does not (or the other way round)
  would shift the following registers. None was seen in 258 shaders; the check would show it as a
  wrong register.
- An explicit `register()` in the game's source would override the order. None was seen.
- Constant buffer member layouts differ (the Vulkan blocks are padded), so byte offsets inside a
  window still need translation where a Mod reads game constants.
