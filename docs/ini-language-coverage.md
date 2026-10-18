# Original INI language compatibility

## Warnings follow the original loader (D-067, 2026-10-18)

The original loader reports configuration problems through its overlay and keeps
going: a duplicate section keeps the first copy, an unrecognized line or a broken
expression is skipped, a duplicate ShaderOverride hash is merged with a possible
conflict warning, a regex group with a numbered Pattern section is disabled.
Until 2026-10-18 the hosted runtime and the folder inspection rejected the whole
configuration for any such warning, which kept an installed Mod with two
identical duplicate resource sections (INPUT-001) and any configuration with a
harmless warning from loading at all.

Now a generation loads like the original. The warnings of its parse are counted
and the latest 64 texts kept (`RuntimeStatus.diagnostics`,
`diagnostic_messages`); the layer writes them to its log at load ("INI warning:")
and reports them as `status.replacement.ini.diagnostics` and
`diagnostic_messages`. The folder inspection completes, lists every warning with
its level and fails only on dire messages (the original's internal BUG reports and
failed operations). Language fixtures still require that an invalid construct
leaves a reported warning, so nothing is accepted silently.

Unchanged: a section or operation this host has no adapter for still refuses the
candidate (for example ShaderOverride in a generation without shader hooks), and
F10 keeps the active generation for such refusals and for failed resource
preparation. The offline package builders keep refusing on warnings, because they
produce verified fixed artefacts.

Installed folder on 2026-10-18 (58 INI inputs, about 22 enabled Mods): the parse
completes with 11 original warnings and no dire message, among them INPUT-001's
duplicates in `zhuangfangyi lilith vice 2.0` and a duplicate section, five
entries outside a section and an unresolved RabbitFX resource reference in
`gilberta eva 2.3` (`build/ini-folder/report.json`).

Latest owned checkpoint1.3.2: original shader hooks, general source resource
matching and patternless ShaderRegex/Core IB replacement execute in the bounded
Vulkan host. Full rebuilt regression passes (runtime407, resources9301,
compiler172/58, installed programs37/37, clean validation). The complete source
parser corpus remains726/726. Assembly-dependent shader patterns and application
import/placement remain separate; see shader-matching-audit.md. Historical
checkpoints below do not expand the deployed character runtime's scope.

Candidate1.1.8 adds original indirect compute, owned sampler defaults and bounded
shared-storage/sample-use checks. Full regression passes in
build/next/run_all-1.1.8.out, resources3720 with clean validation. Native audit found
BUG-022 binding conflicts/batch/OM effects; fix before graphics integration.

Candidate1.1.7 executes opt-in original CustomShader direct compute against owned
Vulkan resources. Original lists/expressions/counters/state restoration are
preserved; unsupported graphics/device cases refuse. This is separate from
complete Mod rendering. Full rebuilt regression passes in
build/next/run_all-1.1.7.out. See custom-shaders.md for scope and remaining work.

The compatibility target is the complete pinned XXMI language, not the syntax
seen in a few installed character Mods. The original C++ engine remains the
parser and evaluator. A successful parse does not establish that an executed
graphics command has a working Vulkan implementation.

Latest compiler checkpoint: 1.1.6 preserves original HLSL source/includes and
maps native reflected slots into SPIR-V for all six stages. Compiler checks 116,
independently valid modules 33; actual Vulkan compute output agrees with native
DX11 WARP. Full regression passes in build/next/run_all-1.1.6.out, including
3544 resource checks with clean validation. This does not execute general INI
CustomShader yet. See docs/hlsl-compiler.md for remaining flags/device gaps.

Owned-execution checkpoint 1.1.5 publishes original GPU IniParams at
zero/Constants, root, draw/dispatch and key-transition boundaries. It corrects
Constants post and root abort behavior. The 3509-check resource fixture includes
native HLSL Texture1D consumption; the complete rebuilt regression passes in
`build/next/run_all-1.1.5.out`. This extends the existing buffer/image commands.
Remaining formats/conversions, CustomShader, general shader matching, application
integration and complete directory graphics remain open. Dist stays 1.0.2;
owned resource/shader-consumer tests are not game evidence.

## What the installed Mods use (census, 2026-10-18)

`scripts/ini_operation_census.py` (suite step "installed INI operation census")
counts every command the original parser built for the enabled Mods and Core, by
class and by what decides its adapter: target kinds and stages, copy options,
draw form. It walks registered lists, override and shader lists and the branch
lists of if blocks, and attributes sections to their Mod through file paths and
namespaces. It executes nothing; `build/ini-operations/report.json`.

On 2026-10-18: 58 INI inputs, 16,423 commands in 57 variants, 21 owners (19
character Mods, RabbitFX and Core). Only 16 of the 32 original command classes occur. The support
columns are a reading of `DrawPlanAdapter` and the owned GPU adapters, not a
measurement; "production" is the character renderer that runs in the game,
"owned" the sandbox domain with owned resources.

| Use | Commands (owners) | Production | Owned |
| --- | ---: | --- | --- |
| Branches, variables, calls, callback copies, IniParams, pools, skip, layout edits | about 13,600 (21) | CPU, original code | CPU, original code |
| `vb`/`ib`/`ps-t` = resource or null | 1,247 (20) | plan bindings | yes |
| resource = resource or null | 658 (20) | declaration references | yes |
| `drawindexed`, `drawindexedinstanced` | 476 (19) | yes | yes |
| `this = null` with `handling = skip` (index-buffer hides) | 73 (14) | hide | not needed |
| `CheckTextureOverride = ps-t` (Core OverrideTextures) | 74 (3) | native Core boundary, no image identities | explicit identities only |
| image panel `k/1.ini`: `o0 = bb`, clears, `draw`, CustomShader graphics | about 20 each (20) | refused | graphics yes; backbuffer placement and PNG no |
| CustomShader compute: `cs-t`/`cs-u`/`cs-cb`, `dispatch` | about 70 (3) | refused | yes |
| constant buffer windows (`vs-cb`, `ps-cb` copies) | about 25 (2) | refused | yes |
| `store`, `draw = from_caller` | 6 (3) | refused | store yes; caller state partial |

Findings: no installed Mod replaces a texture by its hash (`this = Resource...`
does not occur; every `this` is an index-buffer hide), so textures come through
fixed `ps-t` slots and RabbitFX roles. The construct present in almost every Mod
that cannot run in the game today is the image panel shipped as `k/1.ini`.

## Source authority and coverage checks

The source authority is XXMI-Libs-Package commit
`a7847eae82aaac32175c9a7d199673d5c9323bcd`, in
`F:/vulkan/XXMI-Libs-Package/DirectX11/`. The host uses the pinned WWMI-DX12
compatibility headers at commit `113f42cc2dc5702dbba624ee16abac96d68ed3b5`.
Neither dependency is edited. Production includes original `IniHandler.cpp`,
`CommandList.cpp`, `Input.cpp` and `Override.cpp`; adapters intercept their API
boundaries, not their expression grammar.

`scripts/ini_language.py` inventories source definitions, excluding commented-out
constructs. `scripts/check_ini_language.py` exercises the original compiled
parser against that inventory. The fixture corpus is independent of installed
Mods. It covers:

- Every ordinary command dispatch keyword and all 48 arithmetic, comparison,
  logical, bitwise and function operators.
- Every named expression operand, IniParams components, indexed scissor queries,
  numeric literals, local/global/persist/locked variables, branches and phases.
- Every declared shader/resource member, its argument types and missing-argument
  rejection; fixed, dynamic and range pipeline slots for all declared stages.
- Resource and pool metadata, original copy options, layout directives, all
  source-supported initial-data formats, null/reference and scalar pool forms.
- Every CustomShader state key, compiler flag, blend/depth/stencil/raster/topology
  enum table, key/preset option, ShaderOverride and TextureOverride key, fuzzy
  numeric matching and ShaderRegex pattern/replacement/declaration structure.
- Invalid commands, operands, member arity, enum values, flow, locked writes,
  patterns, unsupported original initial-data formats and malformed matches.

Negative tests require a structured parser diagnostic. A process crash, missing
host or stale executable cannot count as a successful rejection. Source and
executable fingerprints are checked before running the corpus. Upstream NOTICE
messages remain distinct from syntax warnings, including recognized deprecated
`depth_filter` options. Both warnings and notices are reported by the host.

The source inventory, detailed case results and notices are in
`build/ini-language/`. Coverage is deliberately not labelled exhaustive: global
loader/driver/system settings and some argument combinations remain outside the
semantic fixtures. The inventory records the original global setting reads;
the inspector never invokes the original loader's installation or OS mutations.

`scripts/check_ini_evaluation.py` separately executes every operator through the
production `IniRuntime`, with independent expected values. It also checks
precedence, exponent associativity, signed zero, negative rounding, time,
parameter reads and draw-context counters. Expected output is compared from
the runtime's actual float bits. These are CPU results, not rendered pixels.

## Runtime changes in 1.0.4

`RuntimeConfig.mods_directory` selects an enabled directory generation. The
original Include implementation handles recursion, order, namespaces, static
conditions, dependencies and exclusions. All explicit command lists in that
directory are parsed, even dormant lists; until D-067 a typo in an unused list
rejected the candidate, now it is reported and skipped as by the original loader. Original `user_config` cannot redirect writes away from the explicit
project-owned settings path. Settings reload through original Constants parsing.

Legacy explicit-input character profiles still import a scoped reachable Core
graph. They are not complete directory configurations: their selected Core
files omit dependencies such as the text renderer. Keeping this mode working
does not establish full-directory rendering. The deployed profile remains in
that legacy mode.

Key and preset `run` targets are now included in dependency compilation, including
cycle callbacks whose temporary activation lists are copied by the original
engine. A target compiled from another root is not compiled twice. This fixes
key-only callbacks that previously remained empty. Presets are enumerated before
Constants, matching original forward-reference ordering.

The adapter allocates isolated CPU IniParams storage, executes parameter
assignments and reads, uses original hold restoration and preset transitions,
and handles `handling = abort` across nested, root and post lists. Time and
dispatch argument counters use supplied original-engine context. It does not
claim a GPU IniParams descriptor or a rendered panel.

Native Core callbacks also honor abort inside their pre/post lists and the
parent transaction. The fixture first fails against the earlier adapter, then
checks that assignments before abort survive while later commands and draws
do not run. Original cycle callbacks keep the pinned engine's smart-start and
accumulation behavior; tests do not impose a replacement cycle interpretation.

The original optimizer temporarily owns branch command lists. It releases those
owners after optimization, so removed branches cannot be revisited through the
old raw registration pointers. Adapt stable roots and recursively visit only
their surviving children. Directory fixtures include eliminated nested branches;
the original Core graph also exposed the dangling-pointer defect before the fix.

Failed generation preparation preserves the existing generation. Error messages
include the load phase and bounded original parser diagnostics.

## Pool execution changes in 1.0.5

Pool-to-pool `copy_desc`, whole-pool `null`, `$Pool[*] = null` and
`Pool[*] = null` now run the original metadata, proxy/index, variable and resource
reset methods. A scalar write no longer invalidates every resource reference
in the pool. The adapter mirrors original resource null state and retains
unaffected slots and mixed elements. `reset_per_frame_limits` executes the
original selected resource/shader counters; it does not itself supply the
missing general copy or CustomShader execution engine.

The pool fixture checks metadata copies preserving existing values, copied
defaults, proxy detach, retained values after index reset, independent slots,
same-slot type switches, both halves of mixed elements, alias reset and scoped
counter reset. Resource references remain limited to nonexpiring direct-index
pools, pending a broader GPU lifetime adapter.

## Execution remains separate from parsing

| Original operation family | Current execution boundary |
| --- | --- |
| Expressions, variables, branches, named calls, callback references | Original evaluator/command graph, bounded CPU execution |
| IniParams, time, call counters, presets, keys, persist, F10 | CPU runtime; shader upload and full rendering context still needed |
| Indexed draws, VB/IB and pixel-resource references, layout edits | Existing scoped character Vulkan backend; not every draw/resource form |
| Scalar pools, metadata/reset, native merged-skeleton/identity contracts | Original CPU logic plus explicit native Core boundaries; GPU resource pools are partial |
| Custom-resource buffer references, copy/copy_desc, buffer regions, clear/store/readback | Opt-in original-runtime GPU adapter in an explicit private outside-render-pass domain; raw/no_view_cache and owned per-description copies; logical slots, no application descriptor emission |
| Buffer slot ranges and broadcasts | Owned GPU adapter, plain reference and compatible copy paths; original null/view/counter behavior; fresh typed/raw views remain open |
| Buffer identity/size/stride/source-stride/format/flags/offset and extent metadata | Owned GPU adapter with original sentinels, override precedence and actual draw-context offsets; declaration-only Constants queries allocate nothing |
| FIFO/spatial/expiring buffer pools and whole-pool assignments | Original pool rules and owned-buffer GPU execution; frame duration supplied by the host |
| Image declarations, DDS, references/copies/pools, clears, properties and color MSAA resolve | Opt-in original-runtime owned-image adapter; original helpers and native view/clear/copy reference; integer RTV and single-sample R16/R32 color/depth conversions included, remaining formats/options and shader use stay open |
| Remaining conversions/options and region/spatial hashes | Parse support; general Vulkan execution remains open |
| Non-indexed/automatic/indirect draws and compute dispatch | Parse support; general Vulkan execution remains open |
| CustomShader render state and HLSL | Original metadata parsing; original panel HLSL compiles separately, general pipeline execution remains open |
| ShaderOverride/ShaderRegex | Original metadata/pattern parsing; source-shader identity and DX11 assembly-to-SPIR-V adaptation remain open |
| Clear hooks, frame analysis, overlay, upscaling and system/driver options | Inventoried; host/settings and graphics adapters remain open |

The production character runtime still refuses non-native CustomShader,
ShaderOverride and ShaderRegex sections. The full-directory inspector accepts
their syntax, but that inspector is not a replacement renderer. Future adapters
must preserve source semantics or report a precise refusal; missing rendering
features must not become silent no-ops.

## Verification checkpoint

Candidate 1.0.4 passes the rebuilt full suite in
`build/next/run_all-1.0.4-final.out`: 726 source-defined parser checks, 65 independent
expression results, 65 production CPU runtime checks, 15 native-callback checks,
the second-character main-INI inspection, existing character/live-INI GPU tests
and all 76 Vulkan validation checks. Shared ABI remains `16b90a85431f86b4`.
Dist remains 1.0.2. These numbers are test counts, not a percentage of compatibility.

The following 1.0.5 full rebuild/regression passes in
`build/next/run_all-1.0.5.out`: pool adapter 23 checks, production runtime 68,
source parser 726, expression results 65 and the unchanged graphics/validation
suites. Eleven new pool cases first fail against 1.0.4, retained in
`build/ini-language/pool-before-1.0.5.json`. Production fixtures separately verify
that a reset emits null without changing an earlier owned file plan or another
generation. No deployment; ABI is unchanged.

No game load, deployment, module swap, new capture or new character acceptance is
implied by these source, CPU or sandbox checks.

The next 1.0.6 checkpoint adds real GPU buffer execution through the production
original engine. Targeted checks pass (120, clean core/sync validation), as does
a full suite. The final frozen-source rebuilt suite passes in
`build/next/run_all-1.0.6-verified.out`. See docs/gpu-resource-commands.md for
scope, source-view semantics, synchronous store feedback and lifetime boundaries.
This opt-in extension does not enable complete directory graphics in the existing
character renderer or remove the remaining shader/image execution gaps.

Candidate 1.0.7 adds original buffer slot ranges and broadcasts, with 155 GPU
resource checks and clean validation. The full rebuilt suite passes in
`build/next/run_all-1.0.7.out`. Plain ranges preserve their original view creation,
null handling, constant-buffer regions and counter bypass. This remains owned
buffer execution rather than application descriptor emission or shader drawing.

Candidate 1.0.8 adds source-defined buffer regions, CB windows/rounding,
vertex-to-structured conversion, raw/no_view_cache and separate owned copy pools.
The alias-overwrite fixture fails against 1.0.7 and passes after the correction.
All 243 GPU resource checks and the complete rebuilt suite pass in
`build/next/run_all-1.0.8.out`. Unwritten new-allocation tails remain undefined;
tests compare only specified bytes or retained contents of known cached buffers.

Candidate 1.0.9 adds original buffer property queries and fixes target-specific
view metadata, cached-null behavior and source_stride retention. Its new view
fixture fails against 1.0.8 before the correction. The 303 GPU resource checks
and full rebuilt suite pass in `build/next/run_all-1.0.9.out`. Pure declaration
queries allocate nothing in Constants; VB/IB offsets require an actual draw
context. Hash properties, image execution and shader/directory drawing remain
explicit development work.

Candidate 1.1.0 executes original whole-pool buffer assignments and every named
copy modifier under its buffer semantics. Source allocations/views survive
destination changes; original per-element limits, metadata and cache ownership
are preserved. In particular buffer resolve_msaa does not copy bytes. The 402
GPU resource checks and full rebuilt suite pass in `build/next/run_all-1.1.0.out`.
Keyed/expiring pools, remaining properties, image execution and shader/directory
graphics remain separate offline work; deployment stays 1.0.2.

Candidate 1.1.1 enables original keyed/spatial/expiring pools for owned buffers
and supplies original frame_time in Present/draw contexts. It also fixes a
reproduced partial-submission bug: a CPU failure now notifies the GPU extension
and invalidates its pending scope. The 485 GPU resource checks and full rebuilt
suite pass in `build/next/run_all-1.1.1.out`. Pool resource ownership and expiry
are separate from a shader descriptor bind or a rendered character. General
mutable images, remaining resource queries/conversions and shader/directory
execution stay open; no game acceptance or deployment is implied.
