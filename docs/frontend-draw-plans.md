# Frontend draw plans

frontend/xxmi/draw_plan.cpp mediates backend operations from command trees
compiled by the pinned original XXMI frontend. It contains no component number,
character name, shader hash or asset filename. Expressions, branches and named
calls continue to execute through XXMI. The adapter collects ordered VB/IB/PS
resource bindings, indexed draw arguments and copied layout operations, instead
of calling the reference's D3D context.

This is a CPU intermediate representation, not a ready GPU transaction. It is
not yet linked into the layer. Source/build/next stay 0.9.3, dist stays 0.9.1,
and the existing live hook still selects the explicit component-7 package.

## Contract

Register native command-list boundaries before the original optimisation pass.
They are marked runtime-populated so an empty declaration is not eliminated.
A registered native handler receives pre/post phase bits and must explicitly
succeed. Boundaries without a handler remain External operations, which must
resolve before a GPU transaction can execute.
No missing Core function is silently replaced by an empty command list.

After upstream optimisation, adapt the selected roots and their reachable named
calls/branches. Repeated references are shared. Execution is serial and uses a
fresh CommandListState. It checks the whole result before publishing it to the
caller; failures retain the previous output. No live draw's skip flag is changed.
An empty handling=skip plan deliberately hides geometry. A zero-count draw emits
no draw item. Vertex offsets remain signed; instance counts and ranges retain
their separate meanings. Every draw owns a copy of its layout requests.

Supported bindings are custom-resource references or explicit null to vertex,
index and pixel-resource slots, including the original optimiser's adjacent
texture batches. Unsupported copies, queries, commands, destinations and dynamic
targets fail when reached. Expressions are inspected once for supported CPU
operands before original evaluation; unadapted resource/window/device queries
cannot enter the reference graphics context. Graph/recursion, steps, operations,
draws and layout requests are bounded. Aliases may only enter adapted lists.

Resource pointers borrow from the frontend generation; the future layer caller
must retain it. This adapter does not create GPU resources, ensure their ranges,
resolve material descriptors or submit commands. Variable side effects are not
rolled back on failure. The installed sample draw callbacks have no such writes;
full Core transactions must handle state publication explicitly. Persist/reload,
pool resource copies and native skeleton operations remain separate work.

## Native verification

scripts/probe_xxmi_host.py now builds the planner as well as the CPU prototype.
The original 23 host checks and 28 planner checks pass. Planner checks include
source-order bindings/nulls, arbitrary vertex slot 5, native-boundary retention,
both conditional branches, indexed/instanced arguments, signed base vertices,
per-draw layout copies, deliberate hidden geometry and root aliases. A later
dispatch, invalid count, non-finite expression, recursive call, GPU query, slot
query, excess draw count or unadapted alias rejects the complete output without
touching the original draw. GPU queries are refused before any device access.

The default tests are synthetic and headless. No layer change means the earlier
full 0.9.2 layer regression remains applicable; this checkpoint runs its separate
native frontend build/tests and the project text/diff checks.

## Read-only sample callback execution

```powershell
& 'D:/Python312/python.exe' -B scripts/check_mod_draw_plans.py
```

The tool requires a successful host build and verifies all recorded source and
executable hashes before running it. It reads the actual installed arcane.ini,
declares its file resources through the original parser, compiles its 12 draw
callbacks and preserves OverrideTextures as an unresolved native boundary.
Other Core/entry-point lists, pools, the image panel and present/key handlers
are not compiled or executed by this fixture. The loader preserves the original
Windows text-mode CRLF behavior and refuses parser diagnostics.

Verified source SHA-256:
697d0c98378b83d3aef592a54536470bff4410993f475c1514ed701c4b6f71bd.
The generic fixture runs declared defaults, all persistent scalars at 1, all at
2, then each scalar at 1 with the other defaults, each with LOD 0 and 1. This
is 16 selected scenarios, not every possible key combination or real key input.

Results: 384 plans, 404 indexed draw items, 192 intentionally mesh-empty callback
runs, all 18 unique indexed ranges and 38 distinct bound resource declarations.
Every selected index range and its adjusted min/max vertex index fit the actual
index/vertex files in all four required streams. The original INI hash remains
unchanged. Outputs and resource fingerprints: build/xxmi-host/mod-plans.json and
mod-plan-report.json. No source mod, Core or launcher file is modified, and no
asset payload is committed.

This proves callback selection and file bounds for the observed input. It does
not prove full mod initialization, complete shared skeletons, shader declaration
compatibility, texture interpretation/upload, animation or rendered appearance.
Next integration work must resolve those data-dependent backend requirements
without introducing per-component implementations.

## Owned resource plans and cache handoff

frontend/xxmi/resource_plan snapshots original CustomResource declarations once per
frontend generation, then replaces borrowed pointers with owned paths, byte strides,
index types and resource usages. No file is opened by this conversion. Unsupported
pooled/generated/transformed resources remain dormant until reached, then refuse the
complete output rather than being mistaken for immutable files. A missing declaration
from another generation also refuses. Unloaded file resources have is_null set in XXMI;
that flag must not be confused with an explicit null binding in the command stream.

The owned representation preserves binding order, indexed/instanced arguments, all
layout match/replacement fields, skip flags and native pre/post boundaries. The file
contract currently accepts plain Buffer vertex streams with explicit byte stride,
R16_UINT/R32_UINT index buffers and unmodified DDS image resources. Format conversion,
generated Core buffers and other image loaders require their own adapters. It contains
no character names, hashes or component branches.

frontend/assets/file_plan resolves these declarations into the common ResourceCache.
Setup declares without demand or IO; later requests collect typed immutable Ready
owners. Repeated file/usage bindings share one upload. Pending, invalid, failed or
foreign-generation bindings preserve the caller's previous complete owner collection.
The prepared plan freezes metadata so later caller edits cannot alter it. Resource
readiness does not resolve External operations or authorize a draw transaction.

Verification: 23 hosted CPU, 28 command planner, 19 resource planner and 15 DDS checks
pass. All 384 actual callback plans compile to owned resources; independent Python
checks compare each path/usage/stride/index type with the original parsed declaration.
The original 404 draws, 18 index ranges and 38 files still fit and retain their hashes.
The headless GPU suite passes 161 checks with clean core/sync validation, including
real file-plan uploads, repeated bindings, explicit null, pending-output preservation,
metadata edits, foreign generations, a nonintegral vertex file and retained owners.
Historical fragment rewrites/nulls still pass. Logs: build/frontend-resource-host.txt,
build/frontend-resource-mod.txt and build/frontend-resource-gpu.txt.

Remaining: interpret binding state at each draw, resolve native Core and material
boundaries, connect shared character identity/bone completeness and link the frontend
into the layer. Index/vertex range proofs here remain offline file checks, not a new
runtime draw-range adapter. No deployment or real-character acceptance is claimed.

## Per-draw binding resolution (2026-10-13, standalone)

`frontend/assets/file_draws.*` preflights a Ready file plan into one immutable
binding snapshot per draw. Each snapshot retains its file owners, current vertex
strides, index format, pixel-resource selections, layout edits and all indexed
arguments. Inherited application slots and explicit nulls remain different states.
Later material or vertex changes do not alter earlier draws.

Unresolved native calls fail before demand loading. Pending files, a missing file
IB, an excessive index range or an unproved signed base/vertex range preserve the
entire previous output. BufferUpload stores typed whole-file index minima/maxima
from its immutable source during preparation, exposing them only when Ready; it
retains no CPU payload and performs no GPU readback for this purpose. Whole-file
extrema are conservative: a valid slice can still be refused because of indices
outside the slice. All 404 selected sample draws pass this rule. Application
pipeline attributes, per-instance rates, untouched resources and native Core
semantics still require the runtime adapter. This stage emits no commands.

Verification: 200 headless asset checks, 28 actual BUF roundtrips/extrema, clean
core/sync validation. Material pixels use resolved stride/index/argument data and
retain owners after cache retirement. Late invalid draws and unresolved native
operations reject complete transactions. Host 23/28/19/15 and 384 sample plans
pass; historical fragment relocation/null checks pass. Logs:
`build/file-draw-bindings-{gpu,host,mod,fragments}.txt`. No DLL/deployment change.

## Native callbacks and retained draw palettes

Native handlers may invoke adapted original callback lists through the same
transaction, sharing recursion/step limits. A refused nested call makes the
outer plan fail even if a handler mistakenly ignores false. Pre and post lists
resolve at their execution time, preserving original alias changes during pre.
No handler means an explicit unresolved operation, never implicit success.

A native palette source supplies an immutable owner separately for each indexed
draw. FilePlanCompiler, prepared file plans and resolved FileDraws retain it;
an absent palette on the next draw does not inherit the prior one. Later GPU
emission still must validate original pipeline/profile and descriptor contracts.
This plumbing does not itself implement the EFMI native Core builtins.

Validation: 12 new native bridge cases, 201 headless GPU asset checks with zero
validation errors/warnings, original spatial 21, shared palette 24 plus 283 saved
records, and all 384 sample callback scenarios. Reports build/xxmi-host/report.json,
build/assets-gpu/report.json and build/shared-palettes/report.json. No layer
source/build/version change, new game data or deployment.


The original-list observer and execute_pair APIs now retain actual Core iteration
and pre/post lifecycle. Root pre/post execute as one plan transaction; late post
failure preserves the prior output and caller phase. A separate 64-level active
command-stack budget guards original flow-control nesting as well as named calls.
The full historical component fixture and limits are in docs/core-skeleton.md.


## Original input-layout evaluation and shared vertex contracts

frontend/xxmi/command_module.cpp includes the pinned original CommandList.cpp
without editing it and exposes only its CPU layout evaluator. VertexProfile
uses exact retained VS words and caller-proven semantic names/indices by Vulkan
location. It reconstructs supported original element formats, runs the original
ordered matching/format/offset rules and feeds the shared stream patcher with
file strides. It does not reimplement the original matching or realignment code.
Semantic relinking, unsupported formats/rates, changed inherited streams and
conflicting edits to aliased bytes refuse. The original stride-zero tangent
binding survives; no component number or hash drives the implementation.

The historical profile reader accepts only complete equal-order DX/Vulkan
layouts with matching format/stream/offset fields and consistent semantics across
all paired uses. Profiles are observed data, not a general semantic inference
algorithm. All 17 vertex modules of actual custom draws have this earlier paired
evidence; current code must match its full fingerprint, not only CRC. Only
LOD0/LOD1 are replayed.

Native vertex tests pass 18. The full original entry/Core fixture now emits the
resolved vertex declarations after each of its 370 custom draws; every stride,
format, location and offset equals an independent known-sample reference. Both
12-to-16 and 4-to-8 byte blend streams work through the same evaluator. Host
23/28/19/15/11/15/native 14, spatial 21, palette 24/283 and callback 384 checks
remain passing. Reports build/core-components/report.json and
build/xxmi-host/report.json. No GPU character draw or DLL integration is implied.

## Combined retained draw transaction

frontend/gpu/draw_transaction composes FileDraws, resolved vertex declarations,
MaterialPlan and complete compact palette snapshots. It checks file bounds,
direct single-instance arguments, replacement strides and the restorable
application binding range before publishing a complete DrawTransaction. An
inherited stream with advancing stride refuses without a replacement-index bounds
proof; the observed stride-zero default attributes remain inherited. The source
profile still must prove the declaration and shader correspondence.

request_draw_variants asks the existing asynchronous cache for every contract;
pending or failure preserves the caller's complete result. Equal declarations
and material contracts share a variant. append_draw_snapshots accepts arenas
allocated off the draw path and publishes a RecordedTransaction only after every
append succeeds. Late exhaustion may consume unused slots but never emits a
partial GPU draw. Seal arenas before submit, retain the recording through command
completion and reuse/resubmission, and release it before device destruction.
Application pipeline/geometry lifetime and queue synchronization remain external.

The combined headless fixture uploads actual synthetic mesh/image files through
ResourceCache, resolves MaterialProfile and retires all producer caches before
emission. Pixel readback proves red/green materials on separate entry offsets,
the 257 bone index through a 16-bit declaration, distinct first/base indices,
explicit-null output, unchanged original FS buffers and restoration of original
VB/IB offset/type, pipeline, descriptors and dynamic offsets. The sealed command
is submitted twice and all replacement owners expire only after its recording is
released. Failure controls cover late palette/material/bounds errors, pending
variants and missing/exhausted/sealed arenas. Asset suite: 253 passes, zero
validation errors/warnings; report build/assets-gpu/report.json. Historical
material outcomes remain 353 accepted, six unknown LOD1 mappings and eleven
dimension refusals. These are standalone helper tests, not whole-character game
rendering or DLL integration. Native texture-override dispatch remains open.

## Native texture queries and original Core callbacks

The dispatch mechanism is now implemented separately from runtime source
correspondence. DrawPlanAdapter.texture_queries accepts a provider of proven
source-API resource hashes for logical pixel slots or previously bound custom
declarations. This is not a Vulkan handle or an arbitrary content checksum.
An unknown lookup must return false. Explicit unbinds need no provider lookup.
Bound identities use original find_texture_override_for_hash and its draw-context
filters, with original callback lists already adapted in the same generation.
Descriptor fuzzy matches are explicitly unsupported, never silently omitted.

CheckTextureOverride mediates dynamic and inclusive slot ranges with the original
resolver. The original command executor runs callbacks, with all pre callbacks
before post callbacks for an explicit combined request. Phase-local requests keep
their phase. this targets the checked slot and restores through nested callbacks
and failure. Per-plan binding state means a post query can see a custom file or
null installed during pre. Scalar filters preserve the original negative-zero
unbound value and highest-priority explicit filter_index behavior. Unknown data,
unsupported targets, invalid ranges and recursion refuse the whole plan output.

Install the texture provider before CoreComponentBridge.install. Its boundaries()
then lets the original OverrideTextures list compile; without a provider that
list remains an explicit unresolved event. This avoids separate configuration
switches that could accidentally compile an empty texture command list.

Native checks pass 21. scripts/check_texture_queries.py also reads the unchanged
installed API.ini: 22 checks, including all 23 slots in both phases and a post
lookup observing the pre replacement. Identities in that test are synthetic.
Reports build/xxmi-host/report.json and texture-core-report.json; spatial 21,
palette 24/283 and full original entry replay remain passing. The historical
replay still contains 283 unresolved texture calls because its source identities
are unavailable. This mechanism does not establish live source correspondence,
whole-character material rendering or DLL integration.
