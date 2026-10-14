# Original INI language compatibility

The compatibility target is the complete pinned XXMI language, not the syntax
seen in a few installed character Mods. The original C++ engine remains the
parser and evaluator. A successful parse does not establish that an executed
graphics command has a working Vulkan implementation.

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
directory are parsed, even dormant lists; a typo in an unused list rejects the
candidate. Original `user_config` cannot redirect writes away from the explicit
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
| General custom-resource copy/description/clear/store/readback and slot ranges | Parse support; general Vulkan execution remains open |
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
