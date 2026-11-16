# Recovering shader input identity from installed assets

D-292, 2026-11-16. Offline evidence; source/runtime remains 3.1.7.

The stripped Vulkan modules surveyed in D-287 have no input names or semantic
decorations. This does not mean the installed assets lost all input identity.
The two explicitly examined initial asset files retain paired D3D11 and Vulkan
programs and a common vertex-channel table. No character-specific lookup was used.

## Original contract

3DMigoto `DirectX11/CommandList.cpp` 282 applies overrides to actual DX11 input
element semantics, formats and offsets. `UpdateInputLayout` retains the current
layout, obtains its original signature and creates the modified layout. DX11
supplies reference-counted lifetime with deferred destruction, hazard tracking
and one-context order. Recovering a semantic name solves only the input identity
part; it supplies none of those lifetime/order guarantees to Vulkan.

## Observed asset schema and bounds

The unmodified UnityPy 1.25.2 schema cannot parse any of these 39 Shader objects.
The engine adds descriptor-set parameter records, two instancing flags and a
second keyword-name array. After the parsed shader form, the observed root has
an aligned flag and inline per-LOD binary records: LOD, compressed bytes and
per-platform offset/compressed-length/decoded-length arrays. The remaining root
contains platform IDs, empty common binary arrays, stage counts and compression
type. These are observed byte boundaries, not a claim about all Unity releases.
The older AnimeStudio Shader reader helped identify parameter fields but expects
pointer records in this position; that older assumption does not parse this data.

`unity_shader_metadata.py` clones the type tree, consumes each entire object and
checks program version 202506210 and program types. Assets are read only, with
before/after SHA-256 equality. The survey checks matching LODs and dimensions,
bounded LZ4 regions, SMOL-V size, DXBC container ranges and actual SPIR-V inputs.
Missing/ambiguous platform pairs are reported separately; no semantic is guessed.

Pairing uses the same asset, subshader, pass, tier, keyword indices and shader
requirements. Equal blob indices alone do not establish a pair. The current
serialized compiler-platform IDs are 4 (D3D11) and 18 (Vulkan); these are not
Unity's public GraphicsDeviceType enum, whose Vulkan value is 21.

The trailing channel table contains a source mask and source/target pairs.
Source IDs agree with Unity's [VertexAttribute enum](https://github.com/Unity-Technologies/UnityCsReference/blob/master/Runtime/Export/Graphics/GraphicsEnums.cs).
The target-minus-13 relation to SPIR-V Location is an inference checked against
every examined module, including sparse locations. Actual DXBC signatures supply
semantic spelling/index through recognized source channels. Register order is
not used as identity. Unrecognized semantics, duplicate channels, mask mismatch,
incomplete locations and inconsistent paired metadata are refused.

## Evidence and limits

| Check | Result |
|---|---|
| Fully parsed Shader objects | 39: resources.assets 38, sharedassets1.assets 1 |
| Paired vertex references | 1,716 |
| Unique binary pairs | 1,682 |
| Pairs agreeing with channel/signature/location checks | 1,682 |
| Pairs where DXBC register set differs from Vulkan locations | 211 |
| Unique recovered vertex SPIR-V modules | 323 |
| Independent spirv-val, Vulkan 1.1 | 323 pass |
| Input names in those pair records | 0 of 6,678 located inputs |
| Conflicting signatures or semantic maps for identical SPIR-V | 0 |
| Exact historical capture matches | 16 pair records, 3 unique vertex modules |
| Unpaired vertex references | 4 HGRP/Lit Vulkan-only entries; no mapping emitted |
| Metadata/range rejection regression | 27 checks pass |

For example, ProBuilder/Standard Vertex Color has DXBC registers 0/1/2/3 for
POSITION/NORMAL/TEXCOORD/COLOR but Vulkan locations 0/2/3/7. Its shared channel
table explains the sparse locations. Directly copying the DXBC register numbers
would misidentify three inputs.

`build/d292-survey.json` and `.out` record the complete survey and independent
validation. The decoder built from the tracked bridge reproduces every module
hash and metadata record in `build/d292-rebuilt.json`; validation was not repeated
for those identical bytes. `build/d292-assets.json` records the original parser
failures. The two source hashes are:

- resources.assets: `0f5cf6be5346215cb7c1978429e5351e05196f579c69a28670e4dd593843fad6`
- sharedassets1.assets: `863dbfb1b3c4a494d5d3b801f2b3f98cbd1c243939382aacc1bb0c50c3527e09`

This examines only two initial unbundled assets. It does not cover the VFS shader
corpus, all 169 historical vertex shaders, future content versions or runtime
input-layout execution. A production solution would need generic asset discovery,
exact-content identity, ambiguity rejection and an independently checked compiler
mapping. No index is installed into the layer, no capture-derived character table
is generated, and no game is started or loaded.

## Reproduction

Dependencies are isolated under `F:/vulkan/unity-analysis`: UnityPy 1.25.2 and its
wheel dependencies in `packages`, and unmodified
[smol-v sources](https://github.com/aras-p/smol-v/tree/main/source) in `smol-v`.
The [UnityPy reader](https://github.com/K0lb3/UnityPy) is used only for asset
enumeration/type-tree decoding. Dependency sources, binaries and game assets are
not committed. `build_shader_survey_decoder.py` records source/DLL hashes.

With those dependencies installed, run from this checkout:

```powershell
D:/Python312/python.exe -B scripts/build_shader_survey_decoder.py
D:/Python312/python.exe -B scripts/survey_unity_shader_pairs.py `
  'D:/Hypergryph Launcher/games/EndField Game/Endfield_Data/sharedassets1.assets' `
  'D:/Hypergryph Launcher/games/EndField Game/Endfield_Data/resources.assets' `
  --captures 'F:/efmi vulkan/captures' --output build/shader-survey/report.json `
  --validation-dir build/shader-survey/modules `
  --validator 'F:/vulkan/Vulkan-ValidationLayers/external/Release/64/SPIRV-Tools/build/install/bin/spirv-val.exe'
D:/Python312/python.exe -B sandbox/run_all.py --no-build --only shader-asset-metadata
```

Reports contain metadata only. Optional decoded validation modules remain under
the checkout's ignored build directory. No runtime, GPU, launcher or system-wide
configuration changes are made by this survey.
