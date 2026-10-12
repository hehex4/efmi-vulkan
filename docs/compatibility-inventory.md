# EFMI 1.4.8 compatibility inventory

Static inspection on 2026-10-11. No INI commands, mod shaders, launcher changes or game code were
executed. This is an implementation contract, not a claim that Vulkan already renders the mod.
The machine-readable companion, `compatibility-inputs.json`, records every scanned INI section
and directive with source line numbers, all direct EFMIv1 references, methods and file hashes.

## Pinned inputs

| Input | Identity |
| --- | --- |
| Game engine | Endfield.exe and UnityPlayer.dll file version 2021.3.34.0; product version 2021.3.34f5 (0) |
| Game content | SHA-256 of GameAssembly.dll, config.ini, game_files, package_files, boot.config and initial/main content indices in the JSON manifest |
| Core | EFMI 1.4.8 (`main.ini`, `$version = 1.48`); 46 files, 12 INIs |
| XXMI DLL | 1.2.2.0, SHA-256 `ea97846af4610b9af42beddccd7f22a5e73e85b25669cd4391ea8000962fdd7d` |
| XXMI source | v1.2.2, commit `a7847eae82aaac32175c9a7d199673d5c9323bcd` |
| Acceptance folder | `Mods/arcane fes 1.2`, 86 files, **two** INIs |
| Main INI | SHA-256 `697d0c98378b83d3aef592a54536470bff4410993f475c1514ed701c4b6f71bd` |
| Panel INI | `k/1.ini`, SHA-256 `5339c435ef80aa2c295b916b86019046e65ea47335df6143153e92af1e69f9ff` |

The installed DLL matches the DLL in the [official XXMI v1.2.2 release](https://github.com/SpectrumQT/XXMI-Libs-Package/releases/tag/v1.2.2)
byte for byte. The downloaded archive SHA-256 is
`13034531d4855e05273d9fbaf18539ec390432950ebd5986a115e5d3e5d8e275`, agreeing with the release asset digest.
The release tag resolves to the commit above. Reference repositories and archive are under `F:/vulkan`.

The game marketing/content version string was not recoverable from the inspected plain-text files;
Unity's engine version must not be reported as that version. Exact selected binary/content-index
fingerprints identify this local build without inventing a release label. These are not a hash of
every installed game file. Core/mod tree digests use the script's sorted canonical metadata list:
Core `80e8c39f0ba3370358f8bc9d20dfe0c94a6f62fa15fd56ae6dd8a8607e97e150`,
mod `b4e97eb522b43548efcc84a72c1a42f1b259a9f50771d6127f02afe1708b6f28`.

## Scope correction

`arcane.ini` has 114 section occurrences and 706 active statements: 13 Key sections, two Constants,
two Present, 21 TextureOverride, 16 CommandList, six Pool and 54 Resource sections. It configures
13 component slots, 499 bones and eight instances; entry points actually name **12 component ids
(0-11)**, with 12 main and nine LOD1 hash rules. Do not invent a component-12 draw to fit the declared count.
Eighteen drawindexed statements select replacement geometry. There is no RabbitFX reference or
ShaderOverride in this main INI.

The directory also contains `k/1.ini` (11 sections, 592 statements), an interactive image panel
using `Draw_2D.hlsl`, two PNG resources, cursor input and the main mod's 13 toggle variables.
The installed d3dx.ini recursively includes Mods and only excludes DISABLED* and desktop.ini,
so the panel is active input. It contributes another TextureOverride for an already used IB hash.
The earlier plan's single-INI description was wrong. Character geometry/animation acceptance can
be checked separately, but complete unmodified-folder compatibility also requires this panel.
Its missing shader/input support must be reported explicitly, never silently counted as working.

Core's 12 scanned INIs include both `help.ini` and `Libraries/Help.ini`. The main include chain
reaches the library copy; the extra root file is fingerprinted/inventoried without claiming it executes.
Core Debugger is not included by the installed d3dx.ini and is outside EFMI's include tree here.

## Main INI contract

| Construct or command | Required behavior |
| --- | --- |
| namespace; repeated Constants/Present | Preserve root duplicate rejection and included global-section append order; resolve relative and `\\EFMIv1\\` names without collapsing entries |
| global, global persist; assignment | Float/expression semantics of XXMI; preserve persistence and read/write through cross-namespace references |
| if, elif, else, endif | Nested control flow, arithmetic, comparisons and logical short circuit; source order matters |
| Present post assignments | Queue post-phase resets after the correct present phase, including object_detected and active flags |
| Key: condition, key, type=cycle | 13 toggle sections, comma lists with trailing commas, modifiers; repeated key chords must keep all bindings |
| hash, match_first_index, match_index_count | Match draw index regions; absent first-index is not an invented zero constraint; 21 main-file rules |
| run; CommandList = ref | Named calls and mutable callbacks, namespace resolution, recursion limit |
| handling = skip | Transactional replacement: skip only when replacement is ready; preserve the original on backend failure |
| drawindexed | Count, first index, base vertex in order; per-toggle geometry selection |
| ib; vb0/vb1/vb2/vb3 | Replacement buffers, offsets and strides; vb0/vb3 can share the position resource |
| vb2->ElementFormat(BLENDINDICES, 0) | Override blend indices to R16G16B16A16_UINT; update the Vulkan vertex-input pipeline variant |
| ps-t12 through ps-t18; null | Replace or clear exact pixel-stage texture registers after verified Vulkan binding mapping |
| Resource assignment; ref/copy/null | References and copies have different lifetime/data behavior; merged-skeleton initialization uses a copy |
| type, filename, stride, format, array, data, bind_flags | Buffers/RWBuffer, typed views and DDS; relative paths, expression-sized arrays; DXGI_FORMAT_R32_UINT and R32G32B32A32_FLOAT; shader_resource/unordered_access flags |
| Pool assignment and indexed variables | Alias pool objects, retain resources and scalars by index; expressions can choose an index |
| pool_size, pool_index_type, pool_spatial_radius | Array sizes and spatial indexing using configured radius |
| pool_expiration_timeout_frames, pool_expiration_reset_elements, pool_expiration_refresh_on_read, pool_variable_default_value | Frame-based eviction/default/reset/read-refresh semantics, not a plain dictionary substitute |

Known duplicate chords include Alt+6, Alt+7 and Alt+8. Persisted names include swapkey0-9,
swapkey11, swapkey12 and stocking (there is no swapkey10 declaration). Preserve them exactly.

## Complete direct EFMIv1 interface used by arcane.ini

All names below are in namespace EFMIv1. The JSON companion gives every call/read/write line.

| Kind | Names |
| --- | --- |
| Variables: registration/object | required_version, object_guid, mod_id, bones_count, component_count, instance_count, custom_mesh_scale |
| Variables: component | component_id, gpu_posed, lod_level |
| Variables: skeleton sizing | cfg_ms_bone_entry_size, cfg_ms_implicit_bones_count, cfg_ms_max_lod_level_count, cfg_ms_skeletons_count |
| Variables: spatial configuration | cfg_spatial_base_radius, cfg_spatial_detault_value, cfg_spatial_expiration_frames, cfg_spatial_expiration_read_refresh, cfg_spatial_expiration_reset, cfg_spatial_instance_load_ratio |
| Command lists | RegisterMod, Object_ReadConfig, Component_ReadConfig, SpatialIdentity_IdentifyComponentInstances, Component_DrawInstances, OverrideTextures, InitializeMergedSkeleton, MergedSkeleton_AttachComponent |
| Mutable callbacks | Callback_Component_DrawCustom, Callback_MergedSkeleton_ConnectComponent |
| Pools | Input_ObjectSpatialIdentity, Input_MergedSkeleton_Component_VertexGroupOffsets, Input_MergedSkeleton_Component_VertexGroupCounts, Input_MergedSkeleton_Component_LodRemaps, Input_MergedSkeleton_Instance_UpdateFrame, Input_MergedSkeleton_Instance_LodLevel |
| Resources | ModName, ModAuthor, ModDesc, ModLink, ModLogo, OutputMergedSkeleton_Template, Output_MergedSkeleton |

The misspelling `cfg_spatial_detault_value` is the actual interface spelling. Registration sets
the required version and object GUID, transfers metadata references and consumes the returned mod_id.
The instance path sets object/component config, supplies the spatial pool, identifies instances,
then draws through callbacks. Skeleton initialization copies the configured output template and
sets the component bone ranges/LOD remaps. Later attachment passes six pool/resource references
to Core. Retaining descriptor handles at submit does not establish the required bone bytes at record.

## Additional Core constructs

Core has 26 Constants, 57 CommandList, 74 Resource, four Present, 15 CustomShader, five Key,
10 Include, one ShaderRegex and 23 Pool section occurrences. The following is the additional
surface beyond the main INI table; exact per-file directives and line numbers are in the JSON.

| Surface | Observed forms and planned owner |
| --- | --- |
| Includes and configuration | include, namespace; global locked, global/local, persist; binary constants; ref/copy/null; `pre` and `post` phases |
| Expressions | else if as well as elif; bitwise operations and shifts, countbits; DRAW_TYPE, DRAW_NUMBER, FRAME_NUMBER, INSTANCE_COUNT, INDEX_COUNT, FIRST_INDEX, FIRST_VERTEX, FIRST_INSTANCE; time/hunting/input expressions in presentation logic |
| Resource queries | ->Size, ->Offset, ->SourceStride, ->Index, ->Region, ->SpatialHash; shader ->cb_size and ->srv_stride; native Core-VK must supply correct typed/ranged data |
| Pool property | pool_allocate_slot_on_missing in addition to the main INI's seven property names; index modes include fifo as well as spatial |
| Geometry and texture triggers | CheckTextureOverride on ib and ps-t0..22; drawindexedinstanced; native draw hook replaces the Core ShaderRegex trigger |
| Skeleton/shapekey work | cs, cs-cb0, cs-t0/1/2, cs-u0/2/3, dispatch, clear, store; vs-cb1/2/3/4, ps-cb1/2, vs-t0. Core-VK implements the required import/config operations rather than issuing compute inside a render pass |
| Parameter vectors | x/y/w and x0/y0/z0/w0, x1/y1/z1/w1; x100..103/y100..103/z100..103/w100..103 |
| Display resources/pipelines | vs/ps/gs/hs/ds, draw, blend, cull, depth_clip_enable, topology, o0; vs-t100/101, ps-t100/101/114, gs-t100/112/113/114, cs-t100/113/114; Resource array/data/type/filename/format/stride |
| ShaderRegex | shader_model and CheckTextureOverride in main.ini; the native backend provides draw matching without importing DX11 shader assembly patches |

Shapekeys, Core text/help/cursor rendering and hunting are inventoried but remain outside initial
character replacement. Their absence is explicit. The actual mod panel adds one CustomShader with
vs/ps HLSL, disabled cs/hs/ds/gs, triangle topology, blend/cull, o0=backbuffer, draw/clear, ps-t100/101,
parameter components in slots 87-104, and cursor/keyboard state. It does not become supported merely
because ordinary texture descriptors are tracked.

## D4: host the XXMI engine behind an explicit adapter

Decision: retain the plan's XXMI reuse through a host adapter for M3; do not describe the parser as
a standalone portable library. Update on 2026-10-12: the bounded standalone CPU host compiles,
links and passes 23 checks (docs/xxmi-host-prototype.md). No XXMI code is yet linked into the
layer DLLs. A reduced interpreter remains a fallback, not an implemented alternative.

Evidence at the pinned [XXMI commit](https://github.com/SpectrumQT/XXMI-Libs-Package/tree/a7847eae82aaac32175c9a7d199673d5c9323bcd/DirectX11):
CommandList.cpp has 13,545 lines, IniHandler.cpp 5,263, CommandList.h 1,980. CommandListState exposes
HackerDevice/HackerContext and ID3D11Device1/ID3D11DeviceContext1; expression evaluation takes that
state and a device. IniHandler includes Globals/Override/Hunting/ShaderRegex/cursor and calls the
command/resource parsers directly. Linking these translation units alone therefore does not
isolate parsing from D3D state, resources, logging, input, persistence and globals.

The [WWMI-DX12 reference](https://github.com/lekarsenten/wwmi-dx12/tree/113f42cc2dc5702dbba624ee16abac96d68ed3b5)
at commit `113f42cc2dc5702dbba624ee16abac96d68ed3b5` demonstrates a concrete hosting arrangement:
its project compiles pristine CommandList/ResourceHash/input/util code, while port_ini.cpp includes
IniHandler.cpp with selected host hooks, and a D3D-shaped device/context adapter backs resource work.
Its migoto adapter directory has 49 C++/header files and 17,849 lines. This is a substantial backend,
not a tiny parser shim. Its notices identify an older XXMI baseline (e8ea2075); it cannot be copied
as evidence that the newer Pool/Region/ElementFormat behavior is already hosted correctly.

Required boundaries for the M3 prototype: an explicit frontend state; names/variables/callbacks,
keys/persist and pool semantics; file/resource loaders; draw-state queries; draw/dispatch/copy
requests into the Vulkan/Core-VK backend. Unknown reachable commands fail with diagnostics.
The first frontend checks must include duplicate sections, callback mutation, pool expiration,
Region offsets, ElementFormat and duplicate key listeners. Keep upstream sources pinned and
attributed under GPL-3.0; no original DLL injection or D3D11 hook is part of this host.

The original static assessment completed within M0's three-day limit. The later prototype follows
G2/G3 and the first component replacement; its bounded CPU checks do not establish whole-engine
runtime equivalence or the remaining graphics/resource adapter effort.

## Reproduce and remaining gate

```powershell
& 'D:/Python312/python.exe' -B scripts/inventory_compat.py --check-snapshot docs/compatibility-inputs.json
```

The script only reads external files; it writes lexical details under build/inventory and compares
the compact tracked manifest. `--snapshot` updates the manifest only when deliberately requested.
The detailed JSON contains no resource payloads and is not an interpreter or reachability proof.

Next evidence requires the game: actual Vulkan API usage, threading/submits, a sample-character
frame with matching IB regions, shader/binding identities and record-time byte availability.
The modded DX11 baseline is still missing. Static fingerprints and sandbox validation cannot fill
those blanks, and cannot select GPU read-back versus CPU import. T-008/T-010 remain separate tests.
