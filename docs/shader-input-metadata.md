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

## D-293: VFS initial shader corpus

The version-5 VFS reader checks all 17 index CRC32s and before/after source hashes.
They contain 418,339 entries. Container type 2 selects the complete initial bundle
group without character names: 1,075 ranges, 281,932,483 bytes. Every selected
payload matches its recorded MD5. Version 5 has eight-byte extension fields where
the older local reader assumed four; the final ten opaque extension bytes are
retained and CRC-covered, without inferring their meaning.

The separately installed AnimeStudio VFS library decodes those bundles in a
console child process. Warnings/errors invalidate the operation; file logging is
disabled and all output stays under the project build directory. Resource-data
streams are not written. The result is 1,076 serialized files containing 232
Shader objects. The tracked adapter reproduces all 1,076 original prototype file
hashes. No decoder or game data is linked into the Vulkan layer.

Those 232 assets have embedded type trees. They are used directly, and independently
confirm the complete decoded values of all 39 stripped initial Shader objects.
The authoritative names are `m_EnableShaderLODStreaming`, `subShaderBlobs`,
`m_ShaderLOD`, `m_CompressedBlob`, `m_Offsets`, `m_CompressedLengths` and
`m_DecompressedLengths`; the earlier inferred external-blob names are corrected.
The fallback descriptor collection is named `m_Bindings`. The original inferred
byte ranges and shader payloads remain identical.

| VFS survey check | Result |
|---|---|
| Paired references in the serialized vertex-program table | 20,653 |
| Unique actual vertex binary pairs | 18,957 |
| Pairs with checked channel/signature identity | 18,445 |
| Unique vertex modules independently validated for Vulkan 1.1 | 5,266 |
| Unique modules with unambiguous candidate semantics | 5,265 |
| Exact historical capture matches | 2,981 pair records, 101 unique vertex modules |
| Missing or ambiguous platform pairs | 1,276; not inferred |
| Compute programs found in the vertex-program container | 1,025 unique pairs; excluded by actual entry-point stage |
| Refused channel/signature pairs | 512, all for one empty-input Vulkan module |

The 512 refusals belong to HGRP/ProceduralSky. The same Vulkan module is paired
with either an empty DXBC signature or POSITION/TEXCOORD inputs. A successful
pair must not conceal a conflicting one: the entire module is excluded from
candidate semantic results. None of the 101 exact historical matches has this
ambiguity. The other mappings have no semantic conflict. The JSON counter formerly
named `vertex_pairs` is now `paired_program_references`, because container names
alone do not establish an executable shader stage.

Evidence is `build/d293-validated.json/.out`, `build/d293-compare.out`,
`build/d293-repro/index.json`, `build/d293-repro/unpacked/inventory.json` and
`build/d293-regression.out`. The focused regression passes 27 input metadata and
17 VFS framing/range checks. Reproduced assets and unchanged decoded modules are
compared by content rather than redundantly revalidated.

The initial VFS corpus still does not match 68 of the 169 unique historical vertex
modules. Other bundle groups, Vulkan-only variants and version changes remain
possible explanations, not established causes. General runtime input-layout
integration and full queue ordering remain open. Source and candidate remain
3.1.7; this offline work does not load the game.

Additional dependencies are PyCryptodome 4.5.0 under the same isolated `packages`
directory and the read-only VFS reader `AnimeStudio.dll` under
`F:/vulkan/unity-analysis/anime-reader`, with its license. The examined library's
SHA-256 is `4b1d4676772e71482996f0219625d3a7df596ff557d68284291795f310165eb4`.
The adapter uses the existing .NET 9 SDK. NuGet sources are cleared; SDK state,
temporary files and package caches are scoped to `F:/vulkan` in child processes.

```powershell
D:/Python312/python.exe -B scripts/read_shader_vfs.py `
  'D:/Hypergryph Launcher/games/EndField Game/Endfield_Data/StreamingAssets/VFS' `
  --output build/shader-vfs/index.json
D:/Python312/python.exe -B scripts/unpack_shader_assets.py `
  build/shader-vfs/index.json --output build/shader-vfs/unpacked
D:/Python312/python.exe -B scripts/survey_unity_shader_pairs.py `
  --asset-list build/shader-vfs/unpacked/inventory.json `
  --captures 'F:/efmi vulkan/captures' --output build/shader-vfs/survey.json `
  --validation-dir build/shader-vfs/modules `
  --validator 'F:/vulkan/Vulkan-ValidationLayers/external/Release/64/SPIRV-Tools/build/install/bin/spirv-val.exe'
```

## D-294: Content-addressed input cache and native reader

The survey now examines every Vulkan variant before platform pairing, including
variants without a DXBC counterpart. All 5,277 unique vertex modules have their
channel tables checked against any paired semantic evidence for that identity.
The eleven additional modules have no independently paired identity and are not
admitted. No new channel contradiction was found; 5,265 candidates remain. The
unpaired variants do not explain the 68 missing historical vertex modules.

`build_shader_input_index.py` rechecks source asset hashes, module bytes, actual
SPIR-V locations and the paired channel/signature relation. Every admitted module
must be present in the independent validation report. Existing D-293 validated
bytes are reused without rerunning unchanged modules through the validator.

The generated `build/d294-inputs/shader-inputs.bin` has 5,265 entries and 909,088
bytes; SHA-256 is
`ac322f7e4132d1670007e535360fc451d02f9d3704088546a858af279403bfe2`.
It contains no shader code, character names, captures, mesh layouts or source
paths. It is a whole-corpus derived cache, not a per-character profile. The format
is little-endian: `EFVIS001`, entry count and payload length (two uint32 values),
payload SHA-256 (32 bytes), then entries. Each entry has a full module SHA-256,
module byte length and semantic count, followed by location/index/name-length
uint32 values and unpadded ASCII name bytes. The reader caps the file at 8 MiB,
entries at 65,536, each module at 16 MiB and each semantic list at 32. Duplicate
identities, duplicate locations/names, malformed framing, checksum failures and
out-of-range fields refuse atomically, preserving the previous index.

The C++ `ShaderInputIndex` owns metadata and produces the existing `VertexProfile`
only on a full byte-length/SHA-256 match. A different generator/header word is a
different identity. The checker accepts exactly 5,265 of the 5,266 validated
modules and 101 of all 637 unique historical capture modules. Unknown stages and
the ambiguous sky module are absent. The 43 reader/layout checks include malformed
payloads with recomputed checksums, failure atomicity and an actual sparse-location
offset edit evaluated by the pinned original XXMI code.

Semantic correspondence does not prove the original DX11 element declaration
order. Cache-produced profiles therefore prove element independence before
applying format edits. If format resizing could realign another element, they
refuse pending declaration-order evidence. The pinned original
`RealignInputLayoutOffsets` uses the numeric original format plus its byte size
as its boundary; this adapter preserves that source behavior rather than changing
it to an offset-based calculation. Direct known-order callers keep their explicit
contract. DX11's reference-counted lifetime/deferred destruction, hazard tracking
and single-context order are still separate Vulkan responsibilities.

Evidence: `build/d294-unpaired.json`, `d294-survey.json`, `d294-reader.out`,
`d294-corpus.out`, `d294-captures.out` and `d294-regression.out`. The cache and native
reader are not yet consumed by the loaded layer in D-294. Runtime integration,
unsupported input conversions and source declaration order remain separate work.
Runtime and the offline candidate remain 3.1.7; no game load or deployment occurs.

```powershell
D:/Python312/python.exe -B scripts/build_shader_input_index.py build/d294-survey.json `
  --validation-report build/d293-validated.json --modules build/d293-validated-modules `
  --output build/d294-inputs/shader-inputs.bin
D:/Python312/python.exe -B scripts/check_shader_inputs.py
D:/Python312/python.exe -B scripts/check_shader_inputs.py `
  --index build/d294-inputs/shader-inputs.bin --modules build/d293-validated-modules --expected 5265
D:/Python312/python.exe -B sandbox/run_all.py --no-build --only shader-input-index
```
