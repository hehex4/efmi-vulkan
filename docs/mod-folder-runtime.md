# Directory-wide Mod compatibility

## Required architecture

The user clarified the target on 2026-10-13/14: a common original-INI runtime,
model loader, skeleton path and texture backend for the enabled Mods directory.
Existing characters are regression inputs, not a whitelist of supported syntax.
A new character must not require handwritten component logic or mandatory paired
captures. Game shader contracts belong to a shared shader catalogue. A genuinely
unknown shader or operation must produce an explicit limitation rather than an
invented mapping. LOD0 and LOD1 remain the requested scope.

Parsing and resource declaration are independent of the graphics API. The pinned
XXMI engine owns namespaces, includes, expressions, conditionals, pools, command
lists, keys, priorities and resource metadata. Vulkan adapters implement the
resulting buffer/image binding, draws, dispatches, copies and shader operations.
Scanning the directory must not eagerly upload every discovered asset to VRAM.

## Original-language inspection

The source-defined syntax and execution matrix is maintained in
`docs/ini-language-coverage.md`. It supersedes any interpretation of the installed
corpus count as proof of support for every original construct.

`scripts/check_ini_folder.py` uses `host_probe.exe --parse-folder` with the Core
and Mods directories. The original Include implementation handles recursive
ordering, implicit namespaces, conditions and DISABLED*/desktop.ini exclusions.
The Python inventory fingerprints candidate files; it does not choose which
commands execute. The original parser builds all command lists, resource/pool
declarations, keys, overrides, shader patterns and CustomShader render states.
CustomShader compilation/execution is not performed by this inspection.

Current read-only installation results, `build/ini-folder/report.json`:

| Item | Count |
| --- | ---: |
| INI candidates, including Core and libraries | 36 |
| Parsed sections, including engine built-ins | 1,330 |
| Global / persistent variables | 1,050 / 210 |
| Resource / pool declarations | 655 / 77 |
| Explicit command lists / parsed commands | 189 / 8,937 |
| Texture overrides / key bindings | 224 / 175 |
| CustomShaders, including two built-ins | 32 |
| ShaderRegex groups / patterns | 13 / 12 |
| Parser diagnostics | 0 |

`sandbox/ini_folder_test.py` supplies positive and negative directory fixtures:
nested includes, cross-file names, duplicate basenames in separate directories,
key expressions after constant registration, disabled files, static include
conditions, invalid expressions/commands/types/patterns and missing dependencies.
These checks and the installed corpus are part of `sandbox/run_all.py`.

## File and model inspection

`scripts/check_folder_assets.py` consumes resource declarations from the original
parser, including resources in branches that are not selected by default. It
does not reimplement Resource section parsing. Current evidence in
`build/folder-assets/report.json`:

- 445 file declarations resolve to 443 unique paths. All source fingerprints
  remain unchanged. Existing file bytes total 4,362,783,804; this is disk data,
  not an allocation or VRAM estimate.
- All 108 DDS files pass the production metadata decoder, including dimensions,
  exact mip byte ranges, compression and linear/sRGB distinction.
- All 310 available buffer declarations satisfy their declared element/stride
  alignment. This alone does not establish draw bounds or vertex semantics.
- Sixteen PNG resources are inventoried but the Vulkan immutable-file adapter
  does not yet load them. Header dimensions are not decoded-pixel evidence.
- Eleven declared files are absent (ten DDS and Core's MyText.txt). This is a
  declaration finding; reachability must be established before calling it a
  visible defect. Do not edit the installed INIs to hide these results.

`scripts/check_mod_corpus.py` additionally selects every merged-skeleton export
under Mods by its declared feature, then runs the original bone initializer and
sampled draw callbacks. This is an isolated callback fixture, not full directory
execution. The tested scenarios are defaults, all persistent values 1/2, and
each persistent value set to 1, at LOD0/LOD1. They are not exhaustive combinations.

All nine exported character skeleton initializers succeed. Six isolated callback
fixtures succeed. Ardelia, Liino and Typhoeus require additional companion command
lists, RabbitFX effects and/or custom rendering. Their full-directory syntax
passes; isolated-fixture failures must not be relabelled as invalid source INI.
The texture-only RabbitFX fixture records logical roles and null assignments;
it is not equivalent to executing every original RabbitFX shader branch. It is
restricted to offline inspection. CharacterProgram rejects that fixture mode:
production needs original shader-filter gating and fallback sampling semantics.

The Yvonne package passes the production decoder with nine components, 462
bones, fourteen IB sections (five are independent hides), 24 streams and 72
default-scenario LOD draw records. The original main INI runtime fixture executes
all fourteen entries with synthetic native identity/config/bone-source inputs:
36 emitted draws, six empty skips and 23 listeners including F10. This is neither
a measured shader mapping nor a rendered character. Original source slot facts
are optional inputs; an invalid slot refuses, absent facts stay unknown, and a
subsequent valid request recovers. No Vulkan binding is relabelled as a DX11 slot.

## Runtime boundary and remaining work

The deployed 1.0.2 backend still runs one prepared main-INI profile. The new
directory inspector is not a directory renderer. Source 1.0.3 adds generic
component-number mapping from evaluated bone ranges and exact IB-only hides,
and supports an explicit native bone-source query boundary without
character-specific branches. Texture-role planning remains an offline fixture.

Remaining offline work includes a complete graphics directory generation, shared
registration/input/persistence/F10 in that renderer, routing multiple matched
objects, demand-driven model/image ownership, source-image identity, shared
shader binding contracts, general resource transformations/copies, PNG/WIC,
CustomShader draw/dispatch and ShaderRegex effects. Panel HLSL compilation remains
separate from panel rendering. Report unsupported operations instead of ignoring
them, and preserve the last working generation when a reload cannot be prepared.

1.0.4 adds a production CPU directory generation for the currently executable
section families, original Include loading, independent namespace routing and
directory settings/reload tests. It also repairs key/preset-only command
dependencies, IniParams CPU execution and optimizer-owned branch lifetimes.
This does not remove the non-native shader-section refusal or turn the deployed
single-character profile into a complete-directory GPU renderer.

No deployment, running-game module exchange, new game capture or new-character
visual acceptance is implied by any result in this document.
