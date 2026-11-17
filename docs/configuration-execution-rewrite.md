# Configuration execution rewrite

User direction, 2026-11-16: the DX11 compatibility route must follow the original
configuration and resource execution model. Remove character/Mod package loading
completely, then continue Vulkan-native functionality. This supersedes the package
discovery, activation and isolation decisions in D-078/D-131/D-174/D-177.

## Original behavior

`DirectX11/IniHandler.cpp::ParseIncludedIniFiles` parses all included files into one
registry. Namespace reuse is not a reason to remove a file: the original handles
duplicate sections and retains distinct sections. `HackerContext.cpp::BeforeDraw`
executes matching shader command lists, which reach the original resource matcher.
`CommandList.cpp::CustomResource::Substantiate` creates a declared resource on its
first actual use. Neither file names nor skeleton initializer discovery select a
loading unit. The original Core's lists and resources are configuration content.

DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking
and one-context order. A Vulkan implementation must retain resource and command
owners, introduce proven dependencies and restore caller state explicitly. Merely
combining or renaming character packages does not implement this contract.

## Required implementation

1. Load the root INI and its original includes into one interpreter generation.
   Run Constants, Present, keys and matching lists through that generation. Do not
   scan for character INIs, pre-execute skeleton initializers, filter by namespace
   ownership or rebuild configurations after excluding a character package.
2. Use declaration identities for lazy resource creation and references. Mesh
   streams and skeleton buffers are resources; they are not a package activation
   unit. Execute the installed Core's commands through the ordinary GPU adapter.
3. Translate evaluated draw/dispatch/resource operations through shared Vulkan
   execution. Preserve the original matching and command order, including lists
   contributed by different included files. Supply verified application bindings.
4. Remove per-character controllers, discovery, package load/exchange/status paths
   and obsolete configuration options. Keep historical evidence clearly historical;
   update executable checks and release acceptance to configuration behavior.
5. Finish the native route's resource/queue scheduler, access phases, compute
   readiness and remaining capture boundaries. Real-game acceptance remains separate
   and requires the user's on-the-spot authorization.

## Evidence and status

D-329 connects production source projection, original registry transactions,
per-call/final binding application and whole application-image layout handoff.
First-use CPU allocations retain independent producers across discarded recordings
and configuration generations. Dedicated upload publication supports readonly use
on multiple application queues of the supported family. Explicit INI virtual slots
and IniParams survive application pipelines that have no corresponding descriptor.

The production chain now copies application input, dispatches an original CustomShader,
binds its result for the caller and restores the original source for the post caller:
7 -> 12 -> 7 produces 19 on the actual GPU. A separate original CustomShader generates
replacement vertices that reach actual pixels. These are sandbox integration results,
not real-game Core acceptance. Prepared graphics CustomShaders, current-value Store,
broader application source/queue contracts and capture migration remain open. Do not
describe the rewrite as complete or restore the removed package renderer.

D-303: the root-configuration fixture passes 10 checks for root includes,
cross-directory references, namespace sharing, inactive initializers, unused missing
resources and independent generation lifetime. The ordinary GPU adapter passes 40
headless checks for lazy instantiation, reference/copy semantics and complete resource
retirement, with zero core/synchronization validation diagnostics. The original runtime
regression passes 553 checks. Both new entry boundaries failed before their changes.

A read-only load of the installed root configuration also succeeds through this GPU
adapter: 2,172 variables, 848 referenced file resources, 44 original diagnostics and
zero adapter gaps during loading. No character/Core extension is supplied. This proves
configuration loading, not live application binding import or Core rendering.

The production entry is now being replaced in D-304. It loads `configuration`
(`EFMI_VK_CONFIGURATION`) as the root containing d3dx.ini. `auto` searches the
injected DLL's ancestors for that file. The original Include rules select inputs;
the backend neither enumerates character INIs nor accepts a package path.

Removed source paths: CharacterProgram discovery/catalogues, CharacterController,
CharacterGeneration, the package hash index/activation worker, native CharacterCore
substitution, per-Mod persistence migration and offline package-planning commands.
F10 exchanges the complete configuration. Global matching evaluates every matching
section, including distinct sections sharing a namespace across directories. The
public module ABI retains its historical names; no character loading is behind them.

Production checks pass 13 loading/matching/F10/module-exchange assertions, 60 native
GPU call assertions and 42 native file-resource assertions. Final affected-area checks
pass root 10 / owned GPU 40 / production 13; the original interpreter passes 553,
tracker 813 and retained offline format decoder 148. Source and four matching
development DLLs are 3.2.2 / ABI 964248fc551584a0. The separately verified candidate
remains 3.2.1. No game acceptance is claimed.

The removal deliberately does not retain the former package renderer as a fallback.
The ordinary GPU frontend and native resource executor are present, but generic mesh
bindings, application resource import for original Core execution, exact shader
semantics integration and ordered in-draw GPU work remain required. Existing capture
replay and package-rendering acceptance fixtures need migration; their historical
results do not validate this replacement. Finish these adapters before claiming
compatibility completion or returning to the remaining native queue work.

D-305 supplies generic immutable file VB/IB assignments and persistent IA state.
Declarations are uploaded on use through the shared resource cache. Every recorded
use retains its complete allocation/publication chain; application rebinds clear only
named slots. Actual pixels, 16/32-bit indices, bounds, post restoration, F10, two queues
and module exchange pass 52 checks, with 825 tracker and 553 original runtime checks.
Native call/file regressions pass 60/42. Layout and stride variants, retained application
IA inputs and owned GPU producer ordering remain incomplete. Source/development DLLs
are 3.2.3; this is sandbox evidence only.

D-306 connects the exact shader input index to the generic executor and resolves
supported original semantic edits and file strides without packages. Explicit, caller
and implicit calls retain independent asynchronous pipeline variants; selected shaders
are preserved, and temporary pipelines restore after each call. Classic, RenderPass2,
dynamic rendering and private target scopes pass actual-pixel checks (122 geometry
assertions). Fresh interpreter checks pass 553; tracker 825 and current pipeline tests
also pass after updating the former dynamic-layout refusal assertion. Source/development
DLLs are 3.2.4. Application IA facts, original Core resource imports and ordered in-draw
GPU commands remain active work; no real-game acceptance is claimed.

D-307 supplies actual application IA properties and retained references. Original size,
stride and offset rules and index-region matching pass 34 focused checks. Restoring a
reference requires the exact current original binding; foreign/recycled resources still
need ordered import. Tracker 838, migrated native aliases 89, original interpreter 553
and ABI 9 pass, alongside geometry/call/file checks. The empty-config file test's early
normal exit before probe connection is recorded; its isolated rerun passes. Source/four
development DLLs are 3.2.5. These results still do not establish live Core execution.

D-308 supplies the retained prepared buffer-command primitive needed for in-place
execution. Headless producer/consumer, ordinary execution and lifetime checks pass 59;
root/owned/registry/runtime checks also pass. Source/development DLLs are 3.2.6. It is
not wired into production draw interception yet: access footprints, original registry
imports/rollback, ordered packets, pass splitting and compute-state restoration remain.
See prepared-resource-commands.md for the contract and verification scope.

D-309 adds ordered physical buffer access metadata and bounded admission, passing 65
headless checks with clean validation. Source/development DLLs are 3.2.7. Original
registry rollback/imports and host insertion remain open; this does not enable live
Core execution or close the native queue scheduler.

D-310 adds reversible named-buffer registry preparation. Cancelled initialization,
copy caches, aliases and per-frame allowances restore; partial allocation failures
can retry the same command. Root 10 / GPU 88 and fresh interpreter 553 pass alongside
production/IA/native-resource checks, with clean Vulkan validation. Source/development
DLLs are 3.2.8. Pool and image transactions, application imports and ordered production
insertion remain open; ordinary original pool execution is retained and verified.

D-311 restores native shader filter queries through the shared registry. Bound state,
original module hashes and explicit/default filters follow the original semantics,
including during shader replacement or hunting. The five migrated native acceptance
scripts use root Include configuration and explicit F10 keys. Configuration 27,
interpreter 570, native compute 91 and empty-input 45 checks pass on 3.2.9. Original
register declarations still need verified source facts; Core GPU integration remains
unfinished and no game acceptance is claimed.

D-312 adds retained application-buffer snapshots to the original prepared registry.
It requires complete verified source binding facts, rejects identity/handle reuse and
preserves admitted references through cancellation. Original INI copy/CustomShader
packets consume two different application producers at their actual insertion points
and retain owners beyond interpreter destruction. Configuration GPU 126, root 27,
interpreter 570 and prepared 65 pass on 3.3.0 with clean validation. Production host
mapping, pool/image/parameter transactions and ordered pass insertion remain open.

D-313 journals original pool lookup, FIFO/expiry, proxy/reset/metadata and lazy
registration changes. Cancelled work restores admitted values and resources, while
ordinary scalar globals persist. GPU 169, root 27, interpreter 570 and prepared 65
pass on 3.3.1 with clean validation; production registry/tracker/ABI checks also pass.
Pool algorithms remain the original source. Owned image/parameter transactions and
production mapping/insertion remain open; this does not close native queue scheduling.

D-314 adds owned image transfer/clear/resolve/compute packets with initial/final
layout and subresource access metadata. Explicit admission publishes logical state;
cancellation restores named image caches and retries IniParams publication at its
original boundary. Source/development DLLs are 3.3.2. GPU configuration 208, prepared
85, owned images/parameters 3,010, root 27 and interpreter 570 pass with clean Vulkan
validation. Application image facts/ownership and ordered production insertion are
still required; this does not enable real-game Core execution.

D-315 removes character-package and installed-Core dependencies from swap-chain
Present acceptance. Minimal root Includes and full F10 exchange pass 82/82 in both
entry modes, including independent pixels, allocation/descriptor reuse, cache restart,
conditional chains and notice bursts. Runtime stays 3.3.2. The earlier failed legacy
entry and migrated status-path assertions are retained as separate evidence.

D-316 supplies retained application image commands with fresh whole-image layout
proofs and immutable restoration, passing prepared 112 checks on 3.3.3. D-317 adds
complete original buffer/image source snapshots and independent observations for
saved references. GPU 267 checks pass on 3.3.4, including array views, full resource
copies, current producer data, source hazards and rollback. Review reproduces and
fixes omitted image allocation-pool rollback (BUG-164). Root 27/interpreter 570 and
affected regressions pass. Ordered packet segmentation, production source mapping,
pass/compute restoration and recording lifetime remain open before Core execution.

D-318 adds ordered prepared segments under one registry transaction and final
admission. Image initialization stays private until acceptance; aggregate budgets and
physical alias checks span cuts. Application writes between segments produce 18/30,
and original copy segments expose 7 then 13 at the respective host positions. Prepared
135 and GPU 282 pass on 3.3.5 with clean validation, plus affected regressions. This
provides segmentation, not production draw-plan insertion or pass/queue restoration.

D-319 preserves original per-frame limits across cancelled GPU preparation. The
same-frame retry failure is reproduced and fixed; CPU-only shader/resource resets
also checkpoint before mutation. GPU 303, root 27, interpreter 570, production 13,
ordinary image/parameter 3,010, tracker 838 and ABI 9 pass at 3.3.6 with clean
validation. Ordered draw-plan insertion and the production host contract remain open.

D-320 adds the explicit original prepared draw-plan boundary. Each root call retains
current resource bindings between GPU segments, and final admission covers calls as
well as commands. Post matching rechecks the current IB in shader-stage order.
BUG-166 stale application-snapshot queries are reproduced and fixed. Final GPU 353,
root 27, interpreter 570, production 13, ordinary image/parameter 3,010, tracker 838
and ABI 9 pass with clean validation on four development DLLs 3.3.7. This is frontend
sequence evidence; complete production admission/recording is still required.

D-321 removes the obsolete prequeued draw API, delayed Store feedback, bare owned
vertex payload and remaining character-selection RuntimeDraw fields. Four DLLs are
3.3.8; GPU 353, root 27/interpreter 570 and affected production/geometry/IA/resource/
tracker/ABI/file checks pass. Complete production insertion remains pending.

D-322 adds native compute-state restoration for prepared insertion: actual selected
pipeline, allocated sets, dynamic offsets, compatible layout recreation and complete
state refusal. An independent application dispatch after private work produces 132,
including a destroyed original pipeline layout, with clean validation. Prepared 139,
tracker 867, native compute 91, first-use 12, uniform aliases 15, production 13, ABI 9
and text pass on four development DLLs 3.3.9. Full production insertion remains open.

D-323 moves 33 more independent native acceptance scripts, the matched-descriptor
wrapper and standalone vertex-semantic entry to the root configuration. Installed
Core is no longer inserted by environment arguments or missing custom-root Includes.
Relative-root module exchange passes. Retired package counters and folder status
lookups are replaced with the current shared registry API while retaining independent
GPU pixel/data assertions. The first own-mesh and target-size runs fail only those
stale assertions. GS startup exposes an independent duplicate sandbox CLI option;
--geometry-shader-scene now differs from --geometry-shader PATH (BUG-167).

The rebuilt sandbox passes all 33 selected entry checks (190/190), plus eight affected
follow-up invocations (90/90): GS skip, HS/DS replacement, dynamic input/raster, F10,
ordered descriptor colors, exact/unknown/numeric semantics and classic frame analysis.
Text 735 passes. Layer/four DLLs remain 3.3.9; no game or deployment. Legacy character/
capture fixtures and full host resource/pass/recording integration remain open.

D-324 removes historical package/material decoding from every production target
and isolates it in sandbox/offline. Deleted the unused native BoneMerge batches,
specialized shader and generator. The old live package test driver is retired;
offline inspection remains explicitly separate. Four DLLs 3.4.0 pass the affected
configuration/tracker/recipe/cache/snapshot/variant/ABI checks, offline decoder 148,
fresh root 27 and interpreter 570. Production Core insertion remains incomplete.

D-325 exposes one cancellable original draw transaction to host orchestration.
Matching-registry ownership, complete application import, original ordered planning,
positioned sealing and explicit admission remain under the caller's serialization.
GPU 376 passes with clean validation and independent 7/12/7 captures after the host
and registry retire. Four DLLs 3.4.1, fresh root 27/interpreter 570 and affected
production/Present/F10/tracker/ABI checks pass. Native pass preservation, complete
source mapping, per-call admission and recording ownership still gate production
Core insertion; the host API alone is not an enabled rendering path.

D-326 supplies the native pass boundary for ordered buffer packets. Preflight proves
sequence/device/family, attachment allocation disjointness and compute restoration,
then allocates every compatible classic LOAD continuation. Recording emits the
packet strictly between End and Begin, preserving outputs even when there is no
color attachment. Unadmitted work emits nothing; recording owners retain accepted
packets through legal completion/reset. Eight classic/dynamic GPU modes pass 31
checks with zero validation errors/warnings, and tracker checks pass 1011. Image
layout handoff, complete host source mapping and per-call admission still gate
production Core insertion. This remains sandbox evidence, not game acceptance.

D-327 extends the pass insertion path to private images with continuous GENERAL
boundaries and complete allocation disjointness. Actual GPU clear/copy ordering,
attachment preservation and completion lifetime pass 61 checks across eight pass
modes, with zero validation diagnostics; tracker checks remain 1011. Application
image handoff and production host source mapping/per-call admission remain open.

D-328 retains final original bindings after post/tail execution, including skipped
draws with only binding assignments. The final snapshot shares admission with the
calls/work and survives registry retirement through retained allocations. Ordinary
result reuse clears it; legacy file/native executors refuse it until final binding
emission is integrated. GPU 384 passes with clean validation after reproducing and
fixing the new-field cache reuse defect. Production source mapping and complete
per-call/final binding admission still remain required.

D-329 connects production source projection, call/final snapshots and application
image layout handoff to ordered native admission/recording. D-330 restores imported
CPU-cache query provenance; D-331 supplies known current shader declarations.
D-332 executes installed Core initialization and constant/rolling bone import
through original command lists, including cached shaders and parameter restoration.
Vulkan device creation negotiates their supported execution capabilities. Private
upload producers and immutable resource packets now retire by producer and consumer
fence proofs, including F10 and module exchange without Present waits.

D-333 connects patternless original ShaderRegex entry with exact paired source-model
evidence, including installed Core enable/disable gates. D-334 prepares immutable
graphics draws, records all descriptor/attachment/IA accesses and retains resources
after the preparing engine retires (234 GPU checks). D-335 restores proven core
graphics state, descriptor offsets/layouts and effective retained IA bindings around
host insertion. Classic/dynamic graphics packets and subsequent application pixels
pass 95 GPU checks, validation clean. Unknown extended state/windows still refuse;
D-336 admits original graphics with explicit proven source stages and full state
overrides. A production INI graphics -> texture -> compute -> application chain
produces GPU 23; unknown stage/state/sampler variants preserve original 7. Five
modes pass 20 checks. General inherited source facts still need integration.

These are scoped sandbox/GPU results. Compatibility is not complete: patterned
ShaderRegex patches, original-only entry without native IB conveniences,
production graphics CustomShaders, current GPU Store boundaries, broader queue/source
contracts and obsolete capture fixture migration remain. No game acceptance is
implied and native-route work follows completion of this compatibility rewrite.
