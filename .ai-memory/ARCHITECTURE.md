# EFMI Vulkan - Architecture

Current migration, D-304 (2026-11-16): production loads one original root INI and
Include registry through ConfigurationProgram. CharacterProgram, CharacterCore,
CharacterGeneration and per-package controllers/activation have been deleted.
Below, package rendering describes the superseded implementation. Generic geometry,
original Core application resource execution and ordered GPU work are still being
ported; see docs/configuration-execution-rewrite.md and CURRENT_STATE.md. Do not use
the former package model as an implementation requirement.

D-305 adds a generic file IA executor. NativeGeometryState is immutable recording
state retained independently of backend generations. Application binds clear only
their named slots; geometry command owners retain distinct uploads and their completed
publication dependencies. Draws validate effective ranges before recording any plan.
The current subset requires matching static strides; layouts and live Core resources
remain migration work.

D-306 supersedes that stride/layout limit for supported static declarations. The root
configuration owns exact module semantics and bounded lookup identities. Each draw's
ordered edits produce its effective Vulkan input declaration and an asynchronous
variant of the exact selected shaders. Recording owners keep these pipelines; temporary
pipeline binds restore immediately while resource IA state persists. Dynamic rendering
uses the same path. Live application/Core resource ordering is still migration work.

D-307 exposes application IA through retained resource snapshots. Original IA getters
use actual static/dynamic metadata, whole-buffer size and the original region helpers.
The current index-region hash remains distinct from whole-resource content identity.
Restoring the same current original slot needs no new producer; foreign references
remain refused until an ordered application-resource import exists. No package boundary
or per-character activation participates in these queries or restores.

D-308 adds PreparedResourceCommands to the private GPU engine. Preparation holds an
immutable bounded transfer/compute sequence and its allocations, pipelines and
descriptors; it records and submits nothing. Imported buffers are valid only in this
mode. Host insertion must supply pass/queue/access proof, restore touched compute state
and retain the packet with the executable recording. The production draw path has not
enabled this primitive yet; original registry imports/rollback and ordered insertion
remain required. See docs/prepared-resource-commands.md.

> Rewritten 2026-11-10 from the code (docs/architecture-review-2.1.md, P4-1). What was here before, the
> history of the candidates 0.5 to 2.0, is `.ai-memory/archive/ARCHITECTURE-until-2.1.md`. Verified state,
> versions and what is deployed are in CURRENT_STATE.md, never here.

D-295 shares an immutable exact-shader input cache across a folder generation's
packages. The cache is read from stable host settings at generation load, not from
the staged backend DLL's directory. Matched draw plans use original semantic edits
and retain both source and resolved Vulkan declarations in their cache identity.
Merged bone interpretation follows the resolved layout; conflicting mask reuse is
refused. This private metadata path changes no host ABI or GPU ordering contract.

D-282 retires the old single-component executor and its Device member. m2_backend.cpp
now contains only replacement-module host dispatch, submission and exchange. The
historical `m2` hook/probe names remain for compatibility; EFMIM201 files cannot
activate a runtime backend. Character packages and Mods folders keep their shared
backend, while legacy mesh decoding survives only as an offline fixture utility.

D-283 replaces output/descriptor type-pair whitelists with the full transaction's
allocation-range proof. NativeTargetPlan events contain their new attachment scope;
recording transfers current image requests and restoration cells at the actual
split. Current/future descriptors, saved fallbacks, suspended requests and pending
restoration accesses participate even when no longer visibly bound. Same-storage
phase transitions and cross-queue accesses still require separate proof.

D-289 puts host signal history behind an opaque shared owner. Submission attempts
retain identity-specific contributions, commit on success and remove only their
own provisional records on failure. Lifetime hooks replace wholesale capacity
purges; late returns never resurrect forgotten objects. The shared Device change
requires ABI 347c72b0cc7d0e40 at source 3.1.5. The concrete host bookkeeping header
is not part of the exchange protocol. This is still a handle-classification
heuristic, not a payload/value or resource-access scheduler.

D-288 intercepts application semaphore creation/destruction and expires queue-signal
history at those boundaries. Failed creation preserves the current history, and no
ledger lock crosses a driver call. The shared dispatch table changes ABI to
d551ddcbfa805dbb, requiring a matching host and module. Raw signal history still
does not prove payload identity, timeline values or cross-queue resource order.

D-286's headless queue experiment proves that a global submission-order completion
chain can deadlock a valid future timeline wait. Readiness-selected per-queue gates
avoid that cycle, but binary consumers require every dependent gate signal to be
submitted first. The experiment is documented in docs/queue-ordering-proof.md;
production access guards remain until full batch/payload/idle/lifetime integration.

D-285 requests game-shader graphics twins and unmatched graphics file publications
without waiting on the recording thread. Pending attempts retain the application
draw until a later ready bind; they do not enter permanent-failure caches/notices.
Creation/reload still prewarms variants, and command owners retain published GPU
objects through completion. Compute first-use waits retain one-shot semantics.

D-284 shares package content validation between framed file decoding and runtime
generations. Runtime validation borrows the existing payload and derives masks/remaps
in a metadata candidate; rejection leaves previous state intact. File bounds and
CRC framing are still checked for actual binary packages. The runtime no longer
serializes/copies a complete mesh merely to validate it, and non-file fields remain
owned by their generation.

D-279 routes native pipeline/image/target scope callbacks through the installed module.
The host retains Vulkan pipeline handles and opaque implementation owners; native_state.h
defines the shared recording/cache protocol. Private planner and pipeline layouts do not
enter the ABI digest, while genuine shared state does.

D-280 gives matched and unmatched draws one NativeDrawTransaction for file publication,
target/descriptor preflight, ordered bindings and capture binding snapshots. Geometry and
translucency still keep specialized helpers. D-281 resolves cross-slot application pixel
references through the current fragment register map into retained private material views.
Those views use the shared native image checks and preserve their verified layout; source
slots, rather than per-frame Vulkan handles, participate in the logical plan cache key.
The cross-queue guard and broader in-draw CustomShader input gap remain open.

D-223 geometry analysis snapshots core/EXT dynamic strides, bound ranges and topology per draw.
D-224 retains full dynamic input declarations per draw, including divisor and independently updated
stride; caller arrays and later setters cannot mutate earlier snapshots. Recipes distinguish capture completeness from replacement completeness; capture support alone
cannot authorize pipeline cloning. Static pipeline geometry and existing capture ownership,
submission limits and completion publication remain authoritative.

D-225 native shader twins explicitly retain original geometry and exact dynamic declarations.
This construction mode rejects vertex-stream overrides; mesh replacement still requires the
stricter complete recipe. Full dynamic vertex input recreates without a static input description.

D-226 native twins also retain supported core1.3 raster/depth/stencil dynamics and viewport/scissor
counts. Dynamic discard cannot suppress fixed structures that rasterization may later consume;
unknown extension chains and incomplete required state remain refused.

D-227 records core/EXT primitive restart per draw, distinguishing unset from disabled. Captures
select dynamic or static pipeline state and retain cut indices unchanged; native-only twins retain
the declaration. Secondary execution preserves snapshots and reset clears current validity.

D-228 native output queries/references map o0-o7/oD through the actual classic/imageless/dynamic
attachment context, independently of descriptor aliases. History owns metadata only; a new source
reference validates live IDs and retains the complete view/image/allocation chain. Writable output
hashes are not guessed. Native descriptor consumers enforce layout/hazard/ownership limits;
the later D-238 output executor is separate from those consumers. Inherited secondary attachment
context still needs a parent-aware adapter.

D-229 native call state obtains rt_width/rt_height from known output0 resource dimensions for
2D/MS views. Unknown source metadata stays unknown; absent/non-2D views preserve original -1
semantics. View mip size and framebuffer/render-area size are never substituted.

D-230 resolves imageless views into immutable per-pass framebuffer metadata before native
pre-pass dependencies. The shared framebuffer template stays unresolved; exact runtime views drive
implicit layout changes and native attachment conflict proof. Missing/unknown Begin metadata
cannot reuse earlier facts. Target history retains metadata, never allocation owners.

D-231 native-only shader twins retain dynamic-rendering view masks and attachment format arrays.
They recreate a null-renderPass pipeline without a classic pass allocation and preserve original
geometry/raster state. Mesh replacement remains separately gated; unknown chains still refuse.

D-232 native scopes retain rendering flags. Suspended/resumed boundaries insert no synchronization;
same-recording continuations preserve the initial proof, while fresh recordings cannot invent it.
Final end closes the scope. Temporary image layouts across segments require a submission adapter.

D-233 ordinary dynamic native image scopes use immutable actual color/depth/stencil and enabled
resolve image IDs for conflict proof. Null/ignored handles do not become attachments. Application
barriers remain the only layout source; unknown or segmented contexts remain unresolved.

D-234 filters dynamic native ShaderFixes by actual depth/stencil presence, independent of pipeline
formats or depth-test/write enable. Native cache keys distinguish unknown, absent and present;
Begin refreshes pipelines that were bound before the attachment context became known.

D-235 direct compute with synchronization2 uses compute-to-compute dependencies, including
UNIFORM_READ for unchanged original aliases. Private mutable resources retain completed upload
publication and one-queue ownership. Application references, indirect replay and graphics ordering
remain separately gated; merely enabling synchronization2 does not establish a signal-stage adapter.

D-236/D-237 frame analysis resolves actual imageless views and ordinary dynamic stored outputs.
Captures run after driver End, with exact layouts and independent resolve-store semantics. Complete
context is validated before any copy; suspended/resumed contexts emit no capture work.

D-238/D-239 native_targets.cpp preflights output plans separately from descriptors, then splits
supported stored ordinary single-color dynamic passes into explicit dependency/LOAD continuations.
The host restores introduced layouts at application End; complete target owners survive command
completion and module exchange. Shared restoration state is ABI-stamped. Actual null attachments
require the application's enabled unused-attachments feature and retain the original format contract.
Query/feedback, unknown attachment mappings, unsupported passes, aliases and queue scopes refuse.

D-240 proves already absent output assignments from immutable current context and emits no pass,
owner, layout or dependency work for them. Plan-wide nonnull writes prevent dropping later null
events based on initial absence. These no-ops can coexist with separately validated descriptors;
real mixed output/descriptor changes still need sequential alias planning.

D-241 output continuation preserves a known stored single-sample depth attachment without resolve
while moving a color target. Exact view/layout and LOAD retain earlier writes; every actual attachment
stays in conflict proof. Active depth replacement and stencil still need their own adapters.

D-242 color outputs have independent original/active views and exact pipeline contracts. Full
continuations LOAD every stored attachment; host End restores all introduced views. Cross-slot active
image overlaps and unmodeled view aliases refuse atomically. Even an initially all-null context
requires exact single-sample pipeline state before introducing an actual image.

D-243 extends unchanged attachment preservation to stored single-sample stencil-only and shared
depth/stencil views. Exact per-aspect format/LOAD contracts and actual conflict identities survive
color changes; real depth/stencil rebinding remains a separate adapter.

D-244 actual DSV null removes both depth/stencil views while preserving stored colors and their
introduced-image retention/restoration. Shader depth filters preserve the current pre-list choice;
the following application draw refreshes from current actual attachments before running its lists.

D-245 zero-color dynamic passes carry their actual empty color set through null-DSV continuation
and host End. New actual color bindings establish no-resolve LOAD/STORE state explicitly, clearing
ordinary resolve fields and metadata that a previously null attachment made irrelevant.

D-246 retains independent original/active DSV state beside colors. Introduced depth/stencil views
use exact per-role formats, common all-format-aspect layout proof and completion owners; null,
original restoration and host End restore their preceding layouts. Missing roles arise only from
defined pipeline formats. Explicit initialization keeps DSV state across zero-color plans.

## Ordered image phases (D-290, 3.1.6)

Native target preflight classifies future sampled bindings by actual Vulkan
set/binding/element and output intervals. Only disjoint intervals on the exact
same image can pass the former whole-plan conflict; every other storage and
lifetime check remains. Descriptor preflight still proves an already readable
layout. Target pass splits restore it with explicit dependencies before sampling.
This does not add arbitrary phase-local transitions, DX11 automatic input nulling
or cross-queue ordering. The planner is private; ABI remains 347c72b0cc7d0e40.

## What it is

A DLL the launcher injects into the game's Vulkan process. It sits in front of the game's Vulkan calls,
finds the draws of characters that an installed EFMI Mod replaces, runs the Mod's INI through the
original 3DMigoto (XXMI) parser and interpreter, and draws the Mod's meshes and textures in the game's
own render passes. Nothing of the game, the launcher or the Mods is changed on disk.

The native route (D-187 onward) adds SPIR-V-hash ShaderOverride lists, hunting/dumps and native
ShaderFixes. `shader_fixes.cpp` loads an immutable shader revision with the folder generation; the
variant cache applies it to game twins and character variants. Recorded command owners retain the
actual pipelines, and resource metadata retains original descriptor-layout/immutable-sampler handles
through application destruction. Native HLSL can be compiled and validated offline by
`scripts/compile_game_shader.py`. Depth filters are part of the shader revision; hunting original/pink
is per-draw variant state, restored by the host after forwarding and included in both cache keys.
Owned recipes cover all five classic graphics stages (including tessellation patch state) and
compute. Compute dispatch lists and hunting use the same generations/owners, preserving fixed
workgroup limits and the application's synchronization contract. Empty Mods still loads the
installation's configured-Core generation for root commands and keys. Explicit `vk_binding` aliases
scope logical slots to a shader's Vulkan descriptor address. `native_resources.cpp` validates and
answers current metadata; the original getters/matcher handle queries and fuzzy matches, preserving
resource/view formats and dynamic ranges. GPU-writable content is not read through a CPU shadow.
`native_execution.cpp` records direct call plans and complete caller replays around the original
call, with compute dependencies and cached graphics access checks. `native_descriptors.cpp` clones
retained descriptor sets for persistent buffer/file/image bindings and upload publication. Writable
files use private declaration identity and pinned residency. Writable file images retain all mips
and layers, use distinct sampled/storage views and stay in GENERAL without application layout
tracking. Recording dependency state extends explicit compute ordering across later application calls and secondary execution. Graphics
application uniforms require proof of a dependency recorded before the current pass; inherited
secondaries need a parent-aware adapter, while immutable uploads keep publication semantics. Accepted
uniform bindings retain ordering requirements across later passes and configuration changes. Application
image requests preserve original fallback elements; `native_images.cpp` refreshes them per call,
uses recording-local `ImageLayouts` and restores temporary layouts outside render passes. Scope and
reflection-cache structures shared by the host and exchangeable module participate in the ABI stamp.
`gpu_analysis.cpp` owns bounded staging copies and completed timeline publication independently of
module lifetime. Its scope is post-dispatch/end-of-pass data on supported application queue graphs;
unknown synchronization, usage and image ownership/layouts are explicit refusals.
The precise supported interfaces and remaining route-2 work are in
`docs/vulkan-native-mods.md`; sandbox verification and game acceptance stay separate.

## Binaries

| Binary | Built from | Role |
| --- | --- | --- |
| `efmi_vk_inject.dll` / `efmi_vk_layer.dll` (one binary, two names) | `layer/src` + the frontend | The host: hooks, tracker, probe pipe, and a built-in copy of the replacement code. The file name selects the inject entry (export redirection of the loader, `bootstrap.cpp`) or the layer entry. |
| `efmi_vk_backend.dll` | `layer/module` + the replacement sources | The replacement code alone, exchangeable while the game runs (`character_module.h`, `EFVK_ABI`, docs/hot-swap.md). |
| `dxcompiler.dll`, `efmi-vkd3d-shader.dll`, `SPIRV-Tools-shared.dll` | deployed beside the layer | The owned GPU runtime's tools: HLSL to SPIR-V, DXBC to SPIR-V, validation. Without them the owned runtime is not made and CustomShader sections do nothing. |
| `server/efmi_vk` | Python | Probe client, command line and MCP server (development only). |

## Source map

| Folder | What |
| --- | --- |
| `layer/src/layer.cpp`, `bootstrap.cpp`, `state.*`, `resources.*`, `resource_hooks.h`, `ipc.cpp` | Hooks, device and command-buffer tracking, descriptor and image metadata, the probe's requests. |
| `layer/src/character_backend.h`, `character_module.h` | The replacement's public interface, and the boundary to a module (`CharacterModule`, `HostApi`). |
| `layer/src/character_backend*.cpp`, `character_backend_internal.h` | The replacement, in parts by concern (the table below); the header holds the types they share. |
| `layer/src/character_program.cpp` | The layer's side of the INI runtime: which INIs are inputs, the folder's load and its isolation (`discover_folder`), the conversion of a plan into the package's draws (`plan`). |
| `layer/src/bone_merge.*`, `merged_skeleton.*`, `draw_snapshots.*`, `pipeline_variant.*`, `variant_cache.*`, `spirv_repoint.*`, `texture_residency.*`, `translucency.*`, `character_present.*`, `notices.*` | Skeleton import on the GPU, per-draw snapshots, private pipeline variants, texture loading and residency, EFMITTL's translucency, the owned Present, the overlay's notices. |
| `frontend/xxmi/ini_module.cpp`, `command_module.cpp` | The original's `IniHandler.cpp` and `CommandList.cpp` (pinned in `F:\vulkan\XXMI-Libs-Package\DirectX11`), included unchanged. |
| `frontend/xxmi/runtime.*`, `runtime_parser.cpp`, `runtime_shim.cpp`, `engine_globals.h` | `IniRuntime`: one generation of the original engine with its own globals (D-129), its load, `draw`, `present`, `save`. |
| `frontend/xxmi/draw_plan.*` | `DrawPlanAdapter`: wraps the original's command objects; decides per command which backend takes it; runs a replaced draw's lists against the draw state. |
| `frontend/xxmi/draw_state.h` | The draw state (D-161): `DrawState` (D3D11's context as values per slot), `SlotValue`, the table of what a draw makes of a slot (`honour_table`), the table of what an operand reads where nothing answers (`operand_table`, D-163). |
| `frontend/xxmi/texture_properties.*` | The original's texture property getters (`->Format`, `->Width`, ...) over a description: the host's of the game's texture, a Mod's file header (D-170). |
| `frontend/xxmi/character_core.cpp`, `efmittl_bridge.cpp`, `rabbit_textures.h` | Native replacements of the EFMI Core's lists and of RabbitFX; EFMITTL's shader groups run as their own lists, chosen by facts the host reads off SPIR-V (`layer/src/shader_hooks.cpp`, D-171). |
| `frontend/xxmi/gpu_*.cpp`, `owned_runtime.*`, `frontend/gpu/*` | The owned GPU runtime: D3D11's resource and state semantics on Vulkan resources the layer owns. |
| `frontend/assets/*` | DDS and WIC decoding, uploads, the file cache. |
| `sandbox/`, `scripts/` | Test programs, the suite (`sandbox/run_all.py`), builds and checks. |

The replacement's parts (`layer/src`, split 2026-11-10, D-160; one file of 4326 lines before):

| File | Concern |
| --- | --- |
| `character_backend_internal.h` | The types the parts share: `CharacterGeneration` (a package), `FolderGeneration`, `CharacterController`, `CharacterBackend`, `CharacterCommand` (the backend's part of a command buffer), `GpuMesh`, `Identity`, the counters and stages. Shared helpers are in the namespace `character_internal`; a class declares its member functions here and each is defined in the part of its concern. |
| `character_backend.cpp` | The backend of a device: the configured folders and packages, device setup, creation and stop, the pipeline cache file, a pipeline retired, a pass ended, the submission (`character_submit`), the presentation (`character_frame`), the module's table. |
| `character_backend_index.cpp` | The device-wide index of sections, the hash of a draw's index region, `character_record` (the draw handed to the package it matches). |
| `character_backend_generation.cpp` | A package's life: load (`init`, `init_adopted`), activation, the worker with the mesh and the snapshot batches, notes, stop, the Present of a generation of its own (`ini_present`, `generation_frame`). |
| `character_backend_mesh.cpp` | A package's mesh on the device (`GpuMesh::create`, `copy`). |
| `character_backend_identity.cpp` | Identity and bones: the instance entry's range, `identify`, the skeleton's ring and import jobs on the device, a pipeline's bone site and Core facts. |
| `character_backend_materials.cpp` | Materials: the frame gate, the texture queries (`pixel_texture`, `texture_roles`), a fragment shader's rules and derived profiles, `material_draws`, what a retired pipeline leaves (`generation_retire`). |
| `character_backend_record.cpp` | Recording: `record()`, `record_textures()`, the translucent captures, the plan with its owned scope (`plan_draw`) and that work's submission. |
| `character_backend_folder.cpp` | The folder generation: `folder_create`, `folder_frame`, the F10 exchange (`folder_frame_and_exchange`), the settings adopted from the Mods' own files. |
| `character_backend_controller.cpp` | One package's generations (`CharacterController`): its frame, its own F10 exchange outside a folder generation, stop and return; the F10 rescan of configured packages. |
| `character_backend_twins.cpp` | The application's pipelines without the character dither (D-133): `character_bind`, `character_created`, `prepare_twins`. |
| `character_backend_status.cpp` | The status: `character_status`, `generation_status`. |

## The INI runtime

- **The original is the parser and the evaluator.** Syntax, expressions, keys, presets, pools,
  namespaces and section order are its code. A generation (`IniRuntime`) owns one set of the original's
  globals; a thread enters it through `Scope` (its mutex), loads run one after another (`load_mutex`).
- **One generation for the Mods folder (D-131).** Inputs: the installation's d3dx.ini without its
  `[Include]` and `[Loader]`, four Core files (`API.ini`, `MergedSkeleton.ini`,
  `SpatialIdentification.ini`, `Helpers.ini`), then every enabled INI of the Mods folder in the
  original's include order. The native libraries' files join by their key and ShaderRegex sections.
- **Three backends behind the adapter.**

| Backend | Used by | Model |
| --- | --- | --- |
| Owned GPU runtime (`GpuResourceRuntime`, `GpuShaderRuntime`) | Present, Constants, key lists, CustomShader, and owned work inside a replaced draw | D3D11's state machine on owned resources; judged against the original (`check_dx11_judge.py`). |
| Plan path (`DrawPlanAdapter::Runtime::run`, then `CharacterProgram::plan`) | every replaced draw | The lists run against a draw state (`draw_state.h`, D-161): every resource assignment sets a slot or a declaration and refuses nothing; a draw is recorded with the state it sees, and one table decides per slot what the layer gives it (honoured, ignored, degraded with a notice, that draw left out, the draw left to the game). The plan's six kinds of items (bind vertex, index, pixel resource; draw; external; translucent) are what each recorded draw needs bound. |
| Native Core (`CharacterCore`) | the EFMI Core's lists a Mod calls | C++ in place of `RegisterMod`, `InitializeMergedSkeleton`, `Component_ReadConfig`, `MergedSkeleton_DetectBoneDataSource`, `SpatialIdentity_IdentifyComponentInstances`, `MergedSkeleton_AttachComponent`, `Component_DrawInstances`. |

- **Failure units (Phases 0 and 1 of the review).**
  - A command, in a Present, Constants or key list and in a replaced draw's lists, at any depth of called
    lists: it does nothing and is said once (`BridgeCommand::run`, `Runtime::skipped`, D-155, D-162).
  - An operand nothing answers: the value of its row in `operand_table()`, said once (D-163).
  - An owned GPU command: one that fails before recording leaves its scope as it was; one that fails after
    recording drops that scope's work, is not run again, and the next scope starts clean
    (`GpuResourceRuntime::run`, `failed()`).
  - A Mod at load: `CharacterCore::prepare` names every Mod it cannot prepare
    (`CharacterDiscovery::refused`); `discover_folder` leaves those out and loads again, leaves out a second
    INI under one namespace, and searches by halves for an input the folder does not load with.
  - The Core: every name the native handlers use is checked at load (`check_interface`); a missing one
    refuses the folder with a statement that names it.
  - A replaced draw as a whole, only for what the host itself cannot do (`refuse_draw`: a native Core
    command's contract, the draw's bone data, a buffer the Mod's mesh needs and the layer cannot bind, the
    planner's budgets; then the file compiler and the conversion): the lists still run to their end, so
    the Mod's variables are the original's, and the draw stays the game's (D-162).
  - An intercepted shader call of the owned domain (`execute_intercepted`) is one transaction, as before.
- **What is said.** The original's warnings are the generation's diagnostics (logged at load). What the
  layer does not do as the original does goes through `LayerGap` (`layer_gap.h`) to `IniRuntime::gap_count`
  and `gaps_since`, which the host shows; per-Mod gaps found at load are `CharacterDiscovery::gaps`.

## What makes a Mod a Mod (the gates)

1. Every enabled INI is an input: its lists and keys run. A package (what carries sections, mesh files
   and the texture catalogue on the device) is made per INI whose lists run the Core's
   InitializeMergedSkeleton (`CharacterProgram::mods_characters`, D-177), with a skeleton, and per
   folder outside those INIs' folders whose INIs have mesh sections, without one (D-174). A Mods folder
   without a character Mod loads as any other; the folder's generation is a program of its own.
2. The folder loads (above).
3. Discovery (`CharacterCore::discover_sections`, `discover_contract`): the host's sections are the
   registry's TextureOverrides with a constant `match_index_count`, as the original's draw maps hold them
   (D-173). One of the Mod's namespace whose lists hand the Core a constant component and LOD (directly or
   through a variable of the Mod's, in the section's list or one it runs: D-177) is a part of the
   character; one that assigns none, and one of another INI of the Mod's folder, is a section without a
   component (its lists run at the draws it matches, and the plan says whether the draw is hidden, drawn
   with the Mod's textures, replaced by a mesh of the Mod's under the game's vertex stage and bones
   (D-176), or left). The skeleton comes from the lists that run the Core's initializer, the counts the
   Mod's lists hand to the Core and the pools they reference into its inputs (D-177): a
   component is an offset and a count of bones with a remap per LOD, and two may share bones (D-175; the
   package's `spans` are what a draw's needed bones are counted in). A LOD no line assigned has no remap. The mesh sections of an INI outside every character Mod's folder are
   the sections of that folder's package without a skeleton (D-174).
4. Registration: `RegisterMod` compares the Mod's required version with the installed Core's `$version`
   (read from the Core's main.ini), as the original's.
5. Each draw: `record()`.

## The draw path (`CharacterGeneration::record`)

`vkCmdDrawIndexed` hook, then the device-wide index of sections (prefiltered by index count, the index
region hashed with CRC-32C; the sections of the hash in two rounds, those with a component first, and a
draw's lists run once: `character_record`, D-173), then per matched draw: activation of the package on its first match,
identification of the object from the instance entry (set 2 binding 0), skeleton import for the
component (GPU dispatches in front of the recording's batch, D-127), `IniRuntime::draw` (the matched
sections' lists into a plan, with an owned scope for owned work), `CharacterProgram::plan` (the plan
into the package's draws, cached by signature), materials (which fragment images are re-pointed, from the
fragment shader's SPIR-V), pipeline variants (the game's pipeline with the vertex buffer bindings moved
and images added, built off the thread), snapshot arena entries, emission into the game's command
buffer, and the game's bindings put back. A refusal anywhere keeps the game's draw and is said once per
Mod and reason.

## The Present path

`vkQueuePresentKHR` hook, `character_frame`: keys, window and cursor sampled once; the folder generation's
Present under its plan lock (`folder_frame`): an owned scope on the presenting queue, the key lists,
transitions and Present lists, submission ordered before the presentation; settings saved off the
thread; then the F10 exchange (`folder_frame_and_exchange`): a successor loads in the background, its
packages are brought up beside the live ones, and every package is exchanged at one presentation.

## Threads and locks

Recording threads (several), the submitting thread, the presenting thread (possibly a frame-generation
library's), and the layer's own: the generation worker per package, variant builders, texture loaders,
retirers, the watchdog, the pipe.

Order where nested: `character_mx` (shared; exclusive at a module exchange), the controller's mutex,
`mod_mx` (per package), `res_mx` (the tracker's, shared in `record()`), `plan_mx` (the folder generation),
the generation's `Scope`, then component mutexes (texture residency, variant cache, bone merge, pool).
Presenting: `present_mx`, `character_mx` shared, `plan_mx`, `Scope`. No inversion is known.
D-278 replaces character first-use variant/texture waits with requests and a frame readiness
gate. D-280 also requests matched native files without waiting and prepares native objects
outside `res_mx`; lookup pointers are invalidated after that unlock, and the retained
transaction outlives the recording locks. Unmatched native file acquisition, synchronous
preparation costs and broader latency work remain separate audits. An idle image staging
arena yields its budget to buffer uploads; live slices remain retained through completion.

## Lifetime

No single mechanism: command owners released at the game's reset or free of a command buffer (arenas,
variants, meshes, twins), per-queue timeline semaphores for bone work, the texture timeline, presenter
tickets, fences for notices and meshes. A package stops once (`stop_once`); a folder generation retires
off the presenting thread. The variant cache lets go of the finished variants least recently asked for when it is full (D-181, review L-08).

## What the layer assumes of this game

The instance entry at set 2 binding 0 with the position at byte 48 and the bone offsets at byte 80; 512
implicit bones; a vertex shader with one storage buffer; a pipeline layout with at least three sets and
one free; indexed draws in a primary command buffer and a classic render pass; host-mapped index pools;
one graphics queue family. A game update that moves one of them is told once, when no character draw
of the process meets them (D-183, review L-07; the status's `game_shape`).

## Verification map

`sandbox/run_all.py --only <group>`: `quick` (text, units, evaluation, the execution matrix, the judge),
`ini`, `owned`, `bones`, `replay`, `textures`, `submission`. The original is the judge of the owned
runtime only (`scripts/check_dx11_judge.py`, `sandbox/judge_cases`); replaced draws are checked by plans
over the installed Mods (`check_mod_states.py`, `check_mod_degradation.py`, `check_mod_containment.py`),
by `docs/ini-execution-matrix.json`, by captured-frame replays and by the live scenes
(`sandbox/runtime_test.py`). The matched-draw judge is the review's P3-1.

## Where to read more

docs/architecture-review-2.1.md (findings and plan), docs/ini-semantics.md (how each INI construct is
handled), docs/folder-generation.md, docs/engine-generations.md, docs/f10-operations.md,
docs/draw-gpu-work.md, docs/texture-residency.md, docs/hot-swap.md, docs/plan.md.

## Classic output destinations (D-247)

An ordinary primary classic pass with one stored single-sample color attachment can redirect that output to a retained same-format application view. RenderPass2 and resolved imageless attachments use the same exact contract. Preparation validates the whole ordered list before commands, then creates compatible LOAD passes and framebuffers. Published metadata remains classic, including the actual framebuffer and pass recipe used by source queries and pass-end analysis.

The first split accounts for the application's implicit final transition; private continuations use explicit dependencies and color-optimal initial/final layouts. Switching away restores introduced views to their saved layouts, while the original returns to its declared final layout. Returning to the original transitions it back before LOAD, and host End restores its final layout again. A bounded per-backend cache revalidates live identities; command owners retain cached or evicted objects and their view resources until completion, including across module exchange. Null/depth/MRT/resolve/segmented passes and broader queue or persistent-image contexts retain guards pending their own adapters.

## Stored classic attachment combinations (D-248)

Ordinary one-subpass classic continuations retain all stored single-sample color and depth/stencil attachments. Color slots map through unique subpass attachment references; combined depth/stencil moves both aspects together. The first private End performs all original final transitions, so preserved attachments also return from their original final layouts before LOAD. Later changes restore outgoing views and transition incoming views without changing other active layouts. Host End restores every current attachment, including preserved originals.

Cached framebuffers revalidate and retain every view, not only the changed destination. Preserving the application's original image stays within its existing acquired/render-pass scope; it does not make a swapchain reference transferable or remove the allocation-owner requirement for new resource bindings. Null, resolve, segmented and broader queue scopes keep separate guards.

Original classic layout restoration follows image identity across all original attachments. Moving an original into another vacated compatible slot does not replace its own implicit final-layout contract with the new slot's contract (BUG-111).

## Acquisition signal scope (D-291, 3.1.7)

The existing AcquireNextImageKHR/AcquireNextImage2KHR counted entries clear the
acquired binary semaphore's queue-signal history only on success/suboptimal.
Failures preserve it. Driver calls run outside the history lock. This corrects
signal-source classification; it does not establish payload/value hazards or
cross-queue order. Shared ABI remains 347c72b0cc7d0e40, but old hosts need replacement
while closed because a backend exchange cannot install host acquisition hooks.

## Offline input metadata reader (D-292)

The layer still does not infer semantic names from Vulkan locations. The separate
offline asset reader pairs DXBC and SPIR-V through shader pass/tier/keyword and
requirement metadata, verifies common source-channel tables and reports exact
binary matches. Initial assets demonstrate that stripped SPIR-V names do not
imply loss of all installed input identity. No runtime index is installed. See
docs/shader-input-metadata.md for the 39-object/323-module scope and remaining
VFS discovery, compiler-encoding and runtime integration requirements.

## Offline VFS discovery (D-293)

Version-5 index CRCs and payload ranges/MD5s establish read-only content identity.
Container file types select the corpus, not characters. A separate console reader
unpacks serialized assets under build; embedded Shader schemas take precedence
over the stripped-asset fallback. All 232 initial VFS shaders produce 5,266
independently validated vertex modules. Conflicting/refused pairs exclude an
entire SPIR-V identity from candidate semantics, including the empty-input sky
stub. 101 historical vertex modules match exactly; 68 do not yet. This pipeline
is offline evidence, not a runtime index. See docs/shader-input-metadata.md.
