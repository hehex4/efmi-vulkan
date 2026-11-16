# EFMI Vulkan - Known issues

## BUG-156: Legacy blend width and offset edits used a mutable or stale interpretation (fixed 3.2.0)

The no-cache path fixed a stream's index width after its first draw, rejected successive different plain width overrides, and read original pipeline offsets when proving bone usage. d299-before.out on 3.1.9 passes 16/20 with the width and ordered-edit diagnostics. D-299 uses the effective declaration and per-layout mask cache and applies supported plain edits in order. The focused 41 checks and full affected-area regression pass (GPU semantics 143). Missing identity still does not support arbitrary semantic mapping, numeric conversion or unknown declaration-order realignment; offline plan adoption retains its prior width rule.

## BUG-155: A shared blend buffer refused a second semantic layout (fixed 3.1.9)

D-295 attached a single adopted bone interpretation to each stream. A legal second declaration therefore refused with One blend stream under different semantic bone layouts (d298-before.out, 9/10). D-298 validates immutable bytes independently and retains per-layout masks without changing stream defaults. Package checks pass 148, tracker 829 and GPU semantics 102, including moving bones and actual exchange. The no-cache width restriction is still open; this does not implement arbitrary semantic inference or cross-queue ordering.

## Cleanup exception: automatic approval review (D-297, 2026-11-16)

The main project cleanup completed and retained-file hashes pass. Ten obsolete
root dist test executables/mismatch DLL files (about 45 MiB) remain because both
the additional cleanup and a narrower file-only deletion were rejected by
automatic approval review. Only blocked by policy was returned; no specific
reason was supplied. This is a tooling exception, not a runtime regression.
Do not bypass the review or count these files as deleted.
D-298 also received a blocked-by-policy rejection for six completed test/build
fixture directories; they remain. The returned reason contained no further detail.

## BUG-154: Matched InputLayout edits could not use exact shader semantics (supported subset fixed 3.1.8)

- The 3.1.7 baseline d295-before.out passes 6/9: a declared POSITION offset in a replacement file is rejected by the legacy blend/offset-only adapter, leaving the original mesh. This is a sandbox compatibility failure, not a reported real-character result.
- D-295 supplies whole-corpus exact shader identities to the pinned original layout evaluator. Source/resolved declarations remain distinct in plan/variant caching, and merged bone offsets/widths follow the result. Metadata defaults to stable generation settings so staged module exchange keeps working.
- New GPU checks pass 81/81, including independent triangles for successive offsets, unknown/numeric refusals, default cache, injection, exchange and changing merged bones. Reader, frontend/runtime, package, capture/readiness and validation checks pass. All four artifacts are 3.1.8 with unchanged ABI.
- Remaining I-06 limits are explicit: application-stream edits, semantic relinking, numeric input lowering, unknown declaration order and different bone layouts for one adopted stream. Only 101/169 historical vertex modules currently have exact installed-asset identities. No real-game verification is claimed.


## BUG-153: Acquired semaphores inherited a stale queue producer (fixed 3.1.7)

- I-01 / BUG-153: a binary semaphore reused for swapchain acquisition retained its old queue-producer classification. The extracted prior forward-only adapter passes 825/829 checks in d291-before.out; the four failures are successful/suboptimal acquisition through the two existing API entries. This is a mocked driver/history baseline, not an observed GPU acquisition failure.
- Original HackerDXGI.cpp 584-627 forwards Present/GetBuffer to DXGI, with post-present commands after the real Present. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan acquisition has its own signal scope, which does not inherit an earlier queue signal on a reused semaphore.
- Source 3.1.7 / ABI 347c72b0cc7d0e40 expires history only after VK_SUCCESS or VK_SUBOPTIMAL_KHR from AcquireNextImageKHR/AcquireNextImage2KHR. Timeout/not-ready/error results preserve history; fence-only calls preserve other semaphores. Arguments/results are forwarded exactly, and no history lock crosses the driver. The existing counted dispatch slots are reused; this host hook change requires a new host even though shared ABI is unchanged.
- d291-pair.out builds four matching artifacts and passes tracker 829/829. Sixteen new checks cover result classes, both APIs, fence-only calls, later queue reclassification and unlocked forwarding. d291-regression.out acquisition-history passes bone-region reuse, two-queue bone replay, graphics file/exchange 18, actual overlap 16 per entry plus 18 wrapper checks, 17 probe configurations, Vulkan validation 76 and text 700. The intentional binary 03238 canary remains distinct from unexpected diagnostics.
- d291-release-build.out and d291-release-check.out build and check the unpublished no-probe candidate build/candidate-d291/EFMI-Vulkan-3.1.7.zip, SHA-256 20ed0a51ff1bfe7aa8659aa713f859f710441289af9a83afb43366d7ef854401. Archive/digest, all 22 installed character packages, both owned unpack layouts and core/synchronization validation pass. It supersedes candidate-d288 (3.1.4), including D-289 failed-submit history and D-290 ordered image phases. Sandbox package loading is not real-character rendering.
- Full queue payload/value scheduling, additional phase-local image transitions/automatic input nulling and in-draw CustomShader/private-bone/arbitrary semantic inputs remain open. No real-game load/deployment, launcher/system change or remote operation occurred; root dist remains untouched.


## BUG-152: Whole-plan input union refused disjoint write/read phases (fixed within the readable-layout contract in 3.1.6)

- I-04 / BUG-152: source 3.1.5's whole-plan input union refuses a legal write, detach, then sample of the same retained image. The final d290-before.out baseline passes 9/12: the independent control writes/reads correctly, but the ordered same-image plan is refused. The fixture uses retained application-owned targets, not a swapchain allocation disguised as owned storage.
- Original CommandList.cpp 10094/10112 retains the other OM views, changes one and releases the temporary references. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order; OMSetRenderTargets also nulls conflicting input bindings. Vulkan still refuses simultaneous sampled/output bindings here rather than claiming that implicit DX11 nulling is implemented.
- Source 3.1.6 / ABI 347c72b0cc7d0e40 classifies explicit sampled-image binding intervals against output intervals in command-list order. Replacing the same set/binding/element ends a read interval across stage aliases; binding an output invalidates every still-active read of that image. Each invalid interval is visited once. Only the exact image identity gets the exception; different objects sharing storage, current descriptors, suspended requests, saved fallbacks and restoration cells retain their existing checks. Descriptor ownership/layout validation still runs before commands; existing pass-split barriers restore the readable layout and carry the dependency.
- d290-pair.out builds all four matching artifacts and passes tracker 813/813; d290-final-pair.out recompiles the explicit header include and corrected stage-alias test selector, reuses only unchanged 3.1.6 objects, and repeats tracker/stamp checks. Twelve CPU cases cover interval closure, repeated reads, another output slot, array elements, stage aliases and unknown contexts. The private planner does not change the shared ABI.
- d290-regression.out passes matched descriptors 72, output memory ranges 52, application-image/output combinations 35, previous resource transactions 64, persistent uniforms 52, tracker 813, Vulkan validation 76 and text 698. Its new phase step passes 51/53: read-before-write actually yields 902 old white pixels then 5,516 new green pixels across successive draws, and the post fixture initially re-evaluated a mutated branch condition. The fixture now states that mixed old/new oracle and freezes the source/main branch per draw. d290-phase-corrected.out passes both corrected cases, 12/12, on unchanged production artifacts.
- The older application-image step initially passes 74/76: push/secondary graphics captured the original warmup frame, despite later ShaderFix binds and successful retained references with clean validation (the push status records 290 binds and 730 executions). Those ordinary graphics fixtures now wait for ready replacement frames and explicitly trigger their one-shot readback, as required by D-285's asynchronous behavior. d290-application-corrected.out passes graphics/push/secondary 15/15 without changing the pixel oracle or production code. Other successful steps are not repeated.
- Across the nine phase scenes, write-then-read and repeated/post phases produce 6,418 green pixels in the target and consumer. The control consumer stays white. Both simultaneous-overlap cases retain original output with explicit refusal; injection and actual module exchange pass with clean core/synchronization validation. Additional phase-local layout transitions, automatic conflicting-input nulling, full multi-queue ordering and broader owned inputs remain open. Development source is 3.1.6; the last no-probe candidate is still 3.1.4. No real-game load or deployment, launcher/system change or remote operation.


## BUG-151: Failed queue submissions retained producer classifications (fixed 3.1.5)

- I-01 / BUG-151: QueueSubmit and both sync2 entries published signal history before calling the driver, without removing it on failure. The original HackerContext.cpp 1691/1698/1705 forwards asynchronous operations to its context; AfterDraw 1079 restores/releases references. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan must handle its own failed submissions explicitly.
- An extracted history seam preserving the previous no-rollback behavior passes 793/801 checks in d289-before.out; eight new failure cases fail. This is a mocked driver/host-state reproduction after extraction, not a GPU allocation failure or an unchanged 3.1.4 binary result. Vulkan requires out-of-memory submission failures to leave referenced synchronization state unaffected.
- Source 3.1.5 / ABI 347c72b0cc7d0e40 uses one contribution per submitted signal and one completion result per call. Failure removes only that attempt; committed and other pending contributions survive. Detached object identities cannot be reinserted or erased by late returns. Lifetime hooks now bound the map by live objects, replacing the old wholesale 65,536-entry purge. The concrete history implementation is host-owned behind an opaque shared pointer, outside the module ABI; the shared Device layout change itself requires matching artifacts.
- d289-pair.out failed at the previously recorded transient state.obj LNK1163 linker error; there was no concurrent build. After review, d289-pair-retry.out builds the matching host/inject/backend/tracker and passes 801/801. Eighteen added checks cover legacy/sync2 errors, duplicate signals, overlapping outcomes in both orders, handle reuse, abandoned attempts, the former capacity threshold, and two controlled host threads. Existing C4018 warnings remain. The linker failure is not passing evidence and its underlying cause remains unknown.
- d289-regression.out signal-submission passes all selected steps: tracker 801, actual overlapping queues through all three entries, both bone replays, cold graphics file/module exchange 18, 17 probe configurations, Vulkan validation 76 and text 695. No queue guard is removed. Provisional handle classification still does not prove binary payloads, timeline values or introduced cross-queue resource access.
- Continue I-04 ordered image read/write phases: target switches already restore introduced images, but preflight treats all future descriptors as simultaneous. Validate disjoint binding/output intervals and actual pixels before accepting a phase. Full I-01 scheduling and I-06 owned inputs/arbitrary semantics remain open. Development artifacts are 3.1.5; the last separately checked no-probe candidate remains 3.1.4. No game load, deployment, launcher/system change or remote operation.


## BUG-150: Recycled semaphore handles inherited queue-signal history (fixed 3.1.4)

- I-01 / BUG-150: the signal-history set previously retained destroyed semaphore handles until its 65,536-entry cap cleared everything. A recycled handle could inherit the previous object's queue-producer classification. d288-before.out on source 3.1.3 passes 773/775: both creation/destruction lifetime hooks are absent. This baseline proves the missing boundaries; no real-game handle-reuse failure is claimed.
- Original HackerContext.cpp 1691/1698/1705 forwards Begin, End and GetData to the DX11 context; AfterDraw 1079 restores/releases retained shaders. DX11 provides reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan object identity cannot be replaced by an unbounded history of raw handle values. Application destruction still requires its submitted uses to have completed; metadata removal does not add a GPU idle or take ownership away from the application.
- Source 3.1.4 / ABI d551ddcbfa805dbb tracks application CreateSemaphore/DestroySemaphore. Successful creation discards any recycled history, destruction removes history before forwarding, and failed creation leaves prior records intact. Unrelated entries survive; null destruction and the exact create chain/allocator/result are forwarded. No bookkeeping lock crosses the driver call. The bounded signal heuristic is not upgraded into a payload/value or resource-hazard proof, and multi-queue guards remain.
- d288-pair.out builds host/inject/backend/tracker with matching stamps and passes 783/783 checks. The ten lifetime checks cover registration plus deterministic mocked handle reuse, failed creation, unrelated history, null destruction, arguments and lock scope. Adding function pointers to the shared DeviceDispatch changes the ABI deliberately: this backend requires a matching 3.1.4 host, not a module-only exchange into a 3.1.3 host. Existing C4018 warnings are unchanged.
- d288-regression.out semaphore-lifetime passes bone-region reuse replay, two-queue GPU/CPU bone replay, tracker 783, three actual-overlap runs with 16 checks each plus 18 wrapper checks, 17 probe configurations, Vulkan validation 76 and text 692. The intentional 03238 binary canary remains separate from unexpected diagnostics. d288-exchange.out adds 18/18 cold file/output and completed module-exchange checks on the new host/module pair; unaffected regression steps are not repeated.
- d288-release-build.out produces the unpublished 3.1.4 no-probe candidate in build/candidate-d288/EFMI-Vulkan-3.1.4.zip, SHA-256 341e7ae42754786dc5ba5215dbd16e24e6f7617c31607f29d5318a40f0705a93. d288-release-check.out passes archive/digest/no-probe checks, all 22 installed character package loads, both owned unpack layouts and core/synchronization validation. This supersedes the earlier 3.1.3 audit candidate; sandbox loading is not real-character rendering. Root dist and actual game deployment remain untouched.
- I-01's full submission/payload/value contracts, I-04's sequential same-storage phases and I-06's owned application inputs/arbitrary semantics remain open. I-09's real-game proof still requires on-the-spot authorization. The lifecycle correction does not justify weakening these boundaries. No real-game load, launcher/system change or remote operation occurred.


## BUG-149: Release checker rejected the current package summary (fixed checker; source 3.1.3)

- Source remains 3.1.3 / ABI 134dd78b13c38aaa. The original CommandList.cpp 282 applies InputLayout overrides using actual DX11 semantics/formats/offsets in order. DX11 provides reference-counted lifetime with deferred destruction, hazard tracking and one-context order; importing a descriptor alone supplies none of the missing Vulkan draw-insertion or access dependencies.
- scripts/survey_vertex_semantics.py reads and SHA-256 deduplicates all 4,724 retained SPIR-V files: 637 unique modules, 169 vertex modules, 1,044 located input variables, no input names and no variable/member HLSL semantic decorations. build/d287-semantics.json records the inventory and example hashes. This is historical capture evidence. Arbitrary source semantic names cannot be recovered merely from Location numbers; no character table is introduced. In-draw owned application resources also require correctly ordered insertion, not only a resource-query callback.
- d287-release-build.out builds an unpublished no-probe candidate in build/candidate-d287/EFMI-Vulkan-3.1.3.zip, SHA-256 420d4d446519174a3e7a958ef30b3688bf71142dbb2e3f02a213ef1ec260c2e8. The pre-existing C4018 warnings remain. Root dist and all real-game files remain unchanged. D-135's release deliberately has no probe/module-control pipe; development artifacts retain the module exchange path.
- BUG-149 is in the release checker: d287-release-check.out falsely rejects three successful folder loads because it still expects the old character-Mod log spelling. Current character_backend_folder.cpp reports packages, including their bone-free count. The checker now parses both spellings while retaining zero-left-out and exact one-character layout assertions; its docstring is raw to avoid a Python escape warning. No runtime behavior changes for this fix.
- d287-release-after.out passes archive contents, SHA-256, no-probe binary checks, loading all 22 installed character packages (23 enabled folders including EFMITTL), both project-owned EFMI/EFMI-Mods unpack layouts, handled-fault classification and core/synchronization validation. These prove sandbox loading, not rendering every installed Mod on its actual character. Cleanup validates resolved paths remain inside the owned test directory. Text check passes 690 files. No game load, deployment, launcher, system or remote action.
- docs/architecture-audit-3.1.3.md consolidates implemented fixes and open boundaries. I-01 production scheduling, I-04 sequential same-storage phases and I-06 owned inputs/arbitrary semantics remain open; I-09 real-character verification is still gated. Continue inspecting the existing submission provenance/lifetime handling without removing queue guards.


## I-01: Global completion serialization creates a measured cycle; gating needs binary forwarding

- I-01 now has actual pending GPU work rather than alternating drained frames. The new headless queue_overlap fixture submits a timeline-waiting consumer before its producer, verifies the consumer fence is pending, then checks all 64 copied words. No QueueWaitIdle/DeviceWaitIdle is used. A third private queue is used only for the scheduler experiment.
- Original HackerContext.cpp 1079 runs post lists and restores/releases shaders on one DX11 context. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan owners and per-queue completion counters do not supply the missing cross-queue access order.
- A global completion chain demonstrably stalls this otherwise valid graph. Both counters stay zero and a bounded fence wait times out. Host signal 1, below pending device signal 2, legally releases the experiment; the consumer reads initial data and all work exits normally. Do not implement this rejected shortcut in the loader.
- Readiness markers plus per-queue gates select the later ready producer before the earlier waiting consumer. A private GPU release queue waits on readiness and previous completion, then signals the selected gate. This yields correct data. CPU counter observations select work; GPU edges carry the memory dependency. An additional 03238 validation canary rejects a binary consumer whose producer's gate signal has not been submitted; forwarding succeeds once the complete dependency chain is submitted.
- d286-build-binary.out builds the standalone fixture without warnings; production source and all four DLL stamps remain 3.1.3 / 134dd78b13c38aaa. d286-binary.out passes 16/16 native checks per entry (none/layer/inject), 18/18 wrapper checks and text 688. Each entry produces exactly the intentional binary validation canary and no unexpected core/synchronization diagnostics. Earlier 14/14 results predate that canary. No production guard is removed.
- docs/queue-ordering-proof.md records the proof and integration requirements: original batch boundaries, binary/present forwarding, imported payloads and host signals, keyed mutexes, sparse work, submission metadata, idle/error/destruction semantics and upload-queue progress. I-01 remains open until these contracts and introduced graphics accesses are implemented together. Continue I-06's actual application-input and vertex-layout adapters without substituting sandbox evidence for real-game verification. No game, deployment, launcher, system or remote action.


## BUG-148: Graphics shader twins and unmatched files blocked recording (fixed 3.1.3)

- I-05 / BUG-148: source 3.1.2 waits on cold unmatched graphics file publication and game-shader twins. Baselines d285-files-before.out (6/7) and d285-shaders-before.out (27/31) reproduce recording waits while rendering correctly with clean validation.
- Original CommandList.cpp 6407 substantiates custom resources on first device use; LoadBufferFromFile at 6471 frees source bytes after SubstantiateBuffer at 6505. HackerContext.cpp 1079 runs post lists and restores/releases shaders. The original may synchronously prepare resources once. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order; the Vulkan implementation must retain uploads and variant owners explicitly.
- Source 3.1.3 / ABI 134dd78b13c38aaa requests graphics twins asynchronously and leaves the application pipeline active while pending. Unmatched graphics native file requests also return pending without recording waits or permanent refusal notices; a separate status counter records these attempts. Creation/reload prewarming and completion-retained owners remain. Compute first-use waits stay because a dispatch can be a one-shot producer; this change does not claim all preparation is asynchronous.
- d285-pair.out builds all four matching artifacts and passes tracker 773/773. The initial d285-regression.out passes compute 12, matched readiness 20, native files 42, tracker 773, validation 76 and text 685. Its two image failures captured the original warmup frame before asynchronous publication, despite later successful binds/execution. Test-client capture now waits for readiness and several recorded frames; no pixel oracle was relaxed.
- d285-shaders-after.out passes 36/36, d285-files-after.out 18/18 and d285-output-after.out 64/64. These cover vertex/pixel replacement, an empty Mods folder, F10, cold file publication, sandbox module exchange, descriptor/output combinations, original restoration, zero recording waits and clean core/synchronization validation. Additional invalid/interface/deleted/unchanged/depth-filter/empty-folder shader boundaries: native game shaders: 91/91 checks passed.
- I-01 remains the next architectural priority: establish actual overlapping queue submissions and future timeline waits before introducing access ordering. The existing two-queue frame fixture drains the prior queue and cannot establish that proof. I-06's general CustomShader/private bone/InputLayout adapters and I-09's real-character verification remain open. No real-game load, deployment, launcher, system or remote action.


## I-08: Runtime binary validation round trip removed in D-284 (3.1.2)

- I-08's runtime validation serialized the entire resident mesh into an EFMICH01 byte vector, calculated a CRC and decoded another full payload merely to reuse checks. This was an in-memory round trip, not a temporary disk file. The unchanged decoder baseline passes 115 checks; d284-folder-before.out loads all 22 installed character Mods independently and in the folder generation.
- Original CommandList.cpp 6471 LoadBufferFromFile reads the source, calls SubstantiateBuffer and frees the source bytes at 6505. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order; it does not require re-encoding resources into a package format. Vulkan retains its resource/upload owners while validation borrows the generation's existing mesh bytes.
- Source 3.1.2 / ABI 134dd78b13c38aaa shares bounded content validation between binary decoding and CharacterPackage::validate. File framing/size/checksum checks remain before table parsing; ranges, index/vertex bounds, skeleton spans, remaps and weights remain before publication. Runtime validation derives into a metadata candidate and updates masks/remaps only on success, preserving the payload allocation and runtime-only material flags, sources, draw fields and section predicates. Bone-free packages use the same entry. Known runtime bone widths are checked as represented instead of silently reverting to file defaults.
- d284-pair.out builds host/inject/backend/tracker and the character helper: tracker 773/773, package 129/129, four matching stamps. Fourteen new checks cover unchanged payload ownership, metadata preservation, idempotence, malformed in-memory inputs, failure atomicity, derived-state refresh, runtime bone widths and bone-free resources. The prior C4018 warnings remain unchanged.
- d284-regression.out package-validation passes all steps: cold file/material replacement across both entries 51, cross-slot application images through merged/texture/own routes 98, installed folder comparison 22 Mods, tracker 773, validation 76 and text 684. d284-plan-parity.txt compares all 44 before/after offline records, sorting only the unordered image_files inventory; all other reported fields match. The first raw JSON equality check exposed that existing unordered inventory, not a semantic regression. The offline adapter's existing EFMITTL/game-resource limitations remain; no real-character result is claimed.
- I-08's obsolete backend and binary validation round trip are both resolved. File length alone remains no reason for arbitrary rewrites. Continue I-05: unmatched native file first-use still waits (D-283 classic file-image status records one 69,545 us wait), and game-shader twins still call the bounded synchronous variant await. I-01's cross-queue proof, I-06's broader application-input adapters and I-09's real-game verification remain open. No real-game, deployment, launcher, system or remote actions.


## BUG-147: Output events lost late image state and ignored retained fallback storage (fixed 3.1.1)

- I-04 / BUG-147: d283-cpu-before.out on 3.1.0 passes 766/771, exposing lost late image state and unchecked suspended requests/application fallbacks. d283-gpu-before.out passes 2/5: a retained application image plus classic output switch is refused, preserving original screen/owned geometry 6418/902 with clean validation.
- Original CommandList.cpp 10094/10112 keeps the other OM views while replacing one, then releases retained references. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan must preserve its current descriptor layout requests and eventual restoration accesses across every private pass split.
- Source 3.1.1 / ABI 134dd78b13c38aaa removes independent_descriptor and buffer_destination pair whitelists. One allocation-range check covers current descriptors, future assignments, suspended native requests, saved application fallbacks, pending image restoration, vertex/index/indirect inputs, introduced outputs and preserved output storage. Descriptor preflight still proves actual types, layouts and ownership before commands. Output events prepare only attachment metadata, then inherit live image cells/layouts/requirements at emission, including data added after output preflight. Both classic and dynamic splits use this handoff.
- d283-pair.out builds all four artifacts, tracker 773/773 and matching stamps. d283-regression.out native-output-transaction passes application-image/output scenes 29, earlier buffer/file/null transaction cases 53, real memory aliases 44, matched descriptors 72, application images 76, persistent uniforms 52, tracker 773, Vulkan validation 76 and text 684. The new classic scenes cover before/after/post order and completed sandbox module exchange; CPU checks also verify exact final restoration barriers. No real-game load or deployment, launcher, system or remote action.
- The original I-02 descriptor-template premise was false: captures/catalogue-20261019/vulkan/vk16/api-after.json records 30,812,846 vkUpdateDescriptorSetWithTemplate calls and 409 template creations. Its enabled extensions include timeline and external Win32 semaphores; this does not establish their actual types/use, but disallows assuming a binary-only device. issues.md now distinguishes these historical observations from this round's sandbox evidence.
- I-04's pair whitelist and state-loss bug are fixed; same-storage read/write phase changes that require new transitions remain refused until a sequential access planner proves them. I-01's cross-queue access proof remains open, not bypassed. A possible readiness-before-execution scheduler must prove future timeline waits, external dependencies, binary/present submission validity and GPU memory transitivity before changing guards. Continue I-08's in-memory package validation cleanup while retaining this queue boundary; I-06's in-draw CustomShader/private-bone/general-layout work remains separate.


## I-08: Selectable single-component executor retired in D-282; binary validation round trip remains

- I-08's single-component prototype was still selectable, not dead code: d282-before.out on 3.0.9 passes 6/12. Both layer and inject entries allocate/configure it for an EFMIM201 package, even with replacement disabled; the original application and validation remain correct.
- Source 3.1.0 / ABI 134dd78b13c38aaa removes M2Backend, M2GpuMesh, M2Command and Device::m2. Character packages and Mods folders still use the shared replacement module. Unknown/legacy package files remain inactive with an explicit retirement diagnostic; the probe cannot enable an absent backend. Submission-owner sealing, native scope callbacks, module staging/retirement and character shutdown remain in the host glue. Historical hook/configuration names remain compatible for character packages. The offline legacy mesh decoder remains a fixture utility.
- This removes a separate per-component execution architecture; it does not alter original INI semantics. The original BeforeDraw/AfterDraw lists operate on one retained context, with DX11 reference-counted lifetime and deferred destruction, hazard tracking and context order. Modern Vulkan replacements continue through their explicit command owners and shared module dispatch.
- d282-final-pair.out builds the four artifacts, tracker 764/764 and matching stamps. The first build exposed CommandOwner declarations formerly supplied through the removed private includes; state.cpp, host glue and the lifetime fixture now include that public ownership interface explicitly. The failed d282-pair.out is not passing evidence. Removing an actual Device member changes the shared ABI deliberately; this version requires a matching host, rather than pretending it is a private implementation change.
- d282-regression.out backend-retirement passes all steps: legacy refusal 12, character package 115 and replacement 377, matched descriptors 72, application texture references 98, module boundary 7, classic scope 17, image scope 13, uniform scope 13, own-mesh scope 24, merged scope 11, tracker 764, Vulkan validation 76 and text 683. Both loader entries exit normally. Modern module exchange is sandbox-only. No real-game, deployment, launcher, system or remote action.
- I-08's binary round-trip validation and further file separation remain open; file length alone does not justify arbitrary rewrites. Continue I-04's shared pass/image state, preserving physical-storage checks and persistent requests. I-01 still needs a cross-queue dependency solution: a global completion chain can deadlock valid wait-before-signal submissions.


## BUG-146 / I-06: Cross-slot application pixel references degraded or refused (D-281; sampled-image cases fixed)

- I-06 / BUG-146: d281-before.out on 3.0.8 passes 38/44. Cross-slot pixel references degrade to the original texture or are refused by the matched material planner; four independent scenes miss the requested green/zero pixels, with clean core/sync validation.
- Original CommandList.cpp 9700+ obtains an AddRef-held PS SRV and its resource; 10051 binds that view at the destination slot. DX11 provides reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan must retain the view/memory and validate its actual layout, consumer shape, attachments and queue scope.
- Source 3.0.9 / ABI 0aba80ce34aa46ca enables moved pixel references only for hosts with the adapter. Logical plans retain source slots, resolve empty sources to explicit null, and include the source slot in the cache key. Material snapshots resolve the current source view through the shared native resource/image checks and retain its owner and verified layout. File images and application images share the existing private material bindings; source/target application descriptors stay intact. The register map is cached, and no file wait is added.
- d281-pair.out builds all four artifacts, tracker 764/764 and matching stamps. d281-fixtures.out rebuilds character, snapshot and GPU helpers. The added second source is statically accessed by the fixture shader, giving layout observation an independent application requirement. d281-after.out passes 88/88: texture-only and merged paths, absent sources, saved references after rewriting the source slot, and GENERAL layouts. d281-own.out adds 10/10 for own meshes replacing or accompanying the application, changing bones, disable/enable and restoration; its fixture-only rebuild is d281-own-fixture.out.
- d281-regression.out texture-rebinding passes all steps: cross-slot scenes 88, matched descriptors 72, readiness 20, own shadows 48, own forward captures 63, merged opacity 48, texture outputs 45, native application images 76, recipe 404, cache 81, snapshots 52, pipeline variants 63, tracker 764, validation 76, text 682, original INI evaluation 65 and the execution matrix. The additional own-mesh cases run afterward against unchanged production DLLs; unaffected checks are not repeated.
- I-06 is partly fixed, not closed: this is retained sampled-image rebinding with known layout and existing queue/storage proof, not general in-draw CustomShader imports, arbitrary InputLayout changes or native overrides of private merged-bone inputs. I-01, I-04 and legacy cleanup remain open. No real-game, deployment, launcher, system or remote changes.


## BUG-144 / BUG-145: Disconnected matched descriptors and mixed-upload starvation (D-280; reproduced cases fixed)

- I-03 / BUG-144: d280-before.out on 3.0.7 passes 12/13. A matched own mesh ignores its explicit native constant: green 0, original pink 1152, while geometry/depth and core/sync checks pass.
- Original CommandList.cpp 10030+ binds stage resources in list order; 10094/10112 retain the other outputs. HackerContext.cpp 3145 brackets the caller with BeforeDraw/AfterDraw. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan must retain each planned descriptor and restore the private variant before emitting subsequent application events.
- Source 3.0.8 / ABI 0aba80ce34aa46ca gives matched merged, own-mesh, texture-only and application calls one NativeDrawTransaction: file publication, target preflight, descriptor preflight, ordered emission and retained descriptor snapshots for captures. Explicit vk_binding aliases resolve against the current shader hash; ordinary Mod bindings retain their existing path. Matched native file readiness is nonblocking. Preparation releases res_mx and invalidates tracker lookup pointers before reacquiring it; the transaction outlives resource locks so rejected private descriptor owners cannot deadlock during destruction.
- BUG-145: d280-descriptors.out initially passes 61/70. An idle 256 MiB image arena consumes the shared staging budget, starving constant buffers through hundreds of repeated file reads. Buffer preparation now releases an idle arena immediately; live slices retain it until GPU completion. The old first-use deadlock run d280-after.out was stopped after diagnosis and is not passing evidence. The ordered-color fixture also now accounts for the framebuffer's BGRA format.
- d280-buffer-pair.out builds all four artifacts, tracker 764/764, and verifies matching version/ABI stamps. d280-regression.out matched-descriptors passes all steps: new descriptor scenes 72, nonblocking readiness 20, original INI evaluation 65, GPU resources 11288, own shadows 48, own forward captures 63, merged opacity 48, beside-original outputs 131, texture outputs 45, native calls 60, descriptor bindings 48, uniforms 16, files 42, tracker 764, validation 76, text 681 and the INI execution matrix. New pixel checks cover ordered green/blue/green constants, merged and texture-only inputs, bounded mixed uploads, post restoration and sandbox module exchange.
- This consolidates resource execution, not every geometry-specific path. Explicit native aliases for original bindings do not bridge inputs relocated into the merged skeleton/private material interface; that remaining I-06 boundary needs separate proof. Automatic cross-slot application textures, in-draw CustomShader inputs and general layouts remain open. I-01's cross-queue hazards, I-04's application-image/output lifetime state and I-08 legacy retirement remain open. No real-game, deployment, launcher, system or remote actions.


## BUG-143 / I-07: Private pipeline representation coupled the module ABI (D-279; fixed within shared-state boundary)

- I-07 / BUG-143 baseline d279-before.out on 3.0.6 passes 3/6: private pipeline/target/image header edits unnecessarily change the shared module ABI. The expanded check also protects native_state.h and now passes 7/7.
- Original HackerContext.cpp 1079 runs post lists and restores/releases retained shaders; CommandList.cpp 2860 retains the original shader before substitution. DX11 provides reference-counted lifetime with deferred destruction, hazard tracking and one-context order. The Vulkan host must retain implementation owners without interpreting an old module's private objects.
- Source 3.0.7 / ABI 0aba80ce34aa46ca dispatches native pipeline selection and image/target scope callbacks through the installed CharacterModule under character_mx. native_state.h explicitly defines surviving recording/cache protocol; NativeBoundPipeline exposes only VkPipeline and an opaque retained implementation owner. PipelineVariant and private target/image planners are outside ABI_ROOTS. Changes to genuinely shared state still require a new host; this is not a claim of universal ABI stability.
- d279-final-pair.out fully builds host/inject/backend/tracker, passes 764 CPU checks and verifies four stamps. The first d279-pair.out failed on a missing native_images.h declaration include after the split; it is not passing evidence. The private probe adds 64 bytes before PipelineVariant's original members, naturally retains the same ABI, and builds a distinct module while the normal host hash stays 04cdedbe3f0bcabe25500238ed6ccf67d5ed5baa021f96d88922890122bacb7c. The header is restored byte-for-byte afterward.
- d279-regression.out module-boundary passes native classic scope 17, application images 13, uniform persistence 13, own mesh 24, merged geometry 11, ABI 7, tracker 764 and Vulkan validation 76. Its character step encountered a probe metadata JSON mistaken for a layer manifest; metadata now uses .txt. d279-character-final.out passes 375/377, exposing two older assertions that required D-278's removed cold waits. They now require zero recording waits while retaining final pixel/upload checks; d279-cold-final.out passes both affected scenes against the same private-layout module and unchanged host. Unaffected scenes are not rerun. Probe objects are isolated from future normal incremental builds. No real-game, deployment, launcher, system or remote actions.
- Continue I-03's common ordered transaction and explicit matched descriptor resolution, while preserving the unresolved I-01 cross-queue resource proof. No new unobserved extension matrices. I-04's persistent image/target restoration, I-06's in-draw application inputs, I-08 legacy retirement and I-09 real-character verification remain distinct work.


## BUG-142 / I-05: Character readiness waited under resource and generation locks (D-278; reproduced waits fixed)

- Verified issues.md against the current implementation, corrected overbroad queue/deployment claims and incomplete ABI remedies in its implementation-audit section. Freeze expansion of unobserved Vulkan extension matrices; preserve the game's observed classic-pass, ordinary-descriptor and two-graphics-queue priority. I-01 remains open: completion tracking alone does not solve introduced cross-queue resource accesses.
- I-05 / BUG-142 baseline d278-before.out on 3.0.5 passes 17/20. Cold merged/texture draws incur three pipeline waits and two texture waits; own mesh incurs one pipeline wait. Correct pixels and clean core/sync validation isolate the readiness issue.
- Original CommandList.cpp 6407 substantiates custom resources once, 6553 loads files through DirectXTK, and HackerContext BeforeDraw/AfterDraw owns draw ordering. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan retains completed upload owners/publication and frame readiness explicitly; it need not hold engine resource/generation locks waiting for workers.
- Source 3.0.6 / ABI f1f9f5cf861f3caa queues every cold character variant without await and requests published texture bindings without waiting. Pending replacement draws are held until a complete frame; beside-original lists retain their caller, and explicit failed-resource fallbacks remain. The shared record entry applies this policy to merged, own-mesh and texture-only paths. Native unmatched file acquisition, game-shader twin waits outside res_mx, synchronous shader preparation and broader transaction consolidation remain separate work; this is not a claim that all recording costs vanished.
- d278-pair.out builds all four artifacts and passes tracker 764/764. d278-after.out is 20/20. A final gate placement review rebuilds the two affected binaries in d278-final-pair.out with four matching stamps. d278-regression.out draw-readiness passes cold readiness 20, own shadows 48, own forward captures 63, merged opacity 48, beside-original outputs 131, texture-only outputs 45, tracker 764, Vulkan validation 76 and text 677. No real-game, deployment, launcher, system or remote action.
- Next: move native scope dispatch behind the installed CharacterModule, separate stable recording protocol from private planners, and replace shared PipelineVariant representation with a Vulkan handle plus opaque retained owner. Test a module whose private PipelineVariant layout actually differs, not only an identical rebuilt DLL. Then continue the cross-queue proof and shared matched/unmatched draw transaction work.


## BUG-141: Retained uniform requests missed later attachment conflicts (D-277; fixed)

- Finished the already pending D-277 change while auditing issues.md; source 3.0.5 / ABI f1f9f5cf861f3caa. Retained application graphics uniforms now preserve their normalized application fallback, recheck attachment storage at later passes, suspend conflicting requests and restore them at a later independent pass without another native list. The same retained descriptor request mechanism serves images and buffers; private file publications remain separate.
- Original CommandList.cpp 9692 retains resource bindings and 10000+ uses 16-byte constant-buffer units. DX11 provides reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan must retain the original descriptor and fold dynamic offsets exactly once when cloning it.
- Baselines on 3.0.4: d277-cpu-before.out 762/764 and d277-gpu-before.out 13/15. Real GPU descriptors are deliberately shader-unused: no undefined alias reads; CPU driver stubs verify actual handles/ranges, GPU counters verify suspension and restoration with real allocations. The initial new CPU fixture incorrectly combined a dynamic descriptor with a push layout and an older mock lacked the newly needed end barrier. Corrected to ordinary allocated dynamic descriptor sets and complete mock dispatch. The failed d277-pair.out / d277-cpu-final.out executions are not validation evidence.
- d277-cpu-final2.out passes 764/764; d277-final-pair.out builds host/inject/backend and checks four matching version/ABI stamps. d277-regression.out passes the native-uniform-persistence area, including 52 persistence checks, existing application images, graphics uniforms, tracker, core/synchronization validation and English text. Only sandbox module exchange; no game load, deployment, launcher, system or remote changes.
- Priority now follows the verified Endfield architecture review. Freeze expansion of unobserved Vulkan feature matrices. Audit actual cross-queue resource dependencies separately from per-queue completion; fix blocking draw readiness and the shared execution/ABI boundary. Game validation remains a separately authorized gate, not an offline result.


## BUG-140: Input-only bindings bypassed allocation overlap proof (D-276; fixed for new inputs and image refresh)

- D-275 committed and main fast-forwarded as 339c3e7. BUG-140: d276-cpu-before.out on 3.0.3 passes 743/744, accepting a sampled-only image whose distinct handle hides overlapping current attachment storage. d276-gpu-before.out passes 13/14: an input-only constant reference to the output allocation is accepted, with unchanged screen/owned pixels 6418/902. The extra descriptor is unused by the shader, so this is a valid conservative-policy baseline with normal exits and clean core/sync validation.
- Original CommandList.cpp 9692 retains the current SRV, 10051 binds it, and 10000+ sets constant-buffer ranges in 16-byte units. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. A Vulkan input-only assignment needs its own attachment-storage proof even if no output command appears in the plan. Source 3.0.4 / ABI be521eec46555682 shares D-275's ordinary allocation-range arithmetic between output plans, new graphics buffers and application image binding/refresh. Active colour, depth/stencil and enabled resolve views participate; classic and resolved imageless snapshots are retained facts. Only tracked swapchain images qualify for implicit WSI storage; unknown views/ranges refuse. Private published file ownership remains separate.
- d276-pair.out fully builds runtime/tracker/layer/backend and passes 757 checks. Its stricter secondary check regresses the existing exact-image-reference case: d276-secondary-before.out is 3/4 with correct pixels, normal exit and clean validation but a refused native list. A secondary may retain its exact original application view identity and readable descriptor state without introducing a different image access; other views still require parent attachment proof. The follow-up also prevents an unsupported active context from borrowing older framebuffer facts. d276-final-pair.out rebuilds the affected image/descriptor/output sources and tracker, relinks and passes 760/760 and four matching 3.0.4/be52 stamps. The shared utility header conservatively invalidates the existing original-runtime header cache; that dependency rebuild ran. Historical C4018 warnings are unchanged.
- d276-regression.out native-input-memory passes input allocation cases 40, tracker 760, existing application images, graphics uniforms, Vulkan validation 76 and text 673. Eight new GPU scenes cover overlap/disjoint ranges, dynamic/classic passes, pushed descriptors and completed sandbox module exchange. Existing actual application-texel sampling, reload, secondaries and module exchange also pass. Normal exits and clean validation; no game, deployment, launcher, system or remote action occurred.
- Next: retained application uniform bindings can cross into a later render pass without another native list. Current native_uniforms supplies ordering but does not re-evaluate attachment storage as persistent images do. Reproduce that boundary, retain the application's original descriptor fallback, suspend unsafe requests and restore them in a later independent pass. Broader application-image/output lists still require live layout-state preservation. Imported/external allocation identity, allocator subrange reuse after logical resource destruction, capture dimension changes and broader pass/resolve/queue adapters remain separate audits.


## BUG-139: Distinct resources hid output storage conflicts (D-275; fixed for output plans)

- D-274 committed and main fast-forwarded as 9f2a522. BUG-139: d275-cpu-before.out on 3.0.2 passes 720/721, accepting an output image and a different uniform buffer bound over the same allocation bytes. An initial fixture compile error was corrected before that baseline ran. d275-gpu-before.out uses the retained 3.0.2 production DLLs and a newly built sandbox: 13/15, overlapping and disjoint bindings both redirect screen/owned geometry from 6418/902 to 0/6418. Production hashes are in build/d275-initial-dist/hashes.json; its sandbox was subsequently rebuilt. The extra descriptor is deliberately unused by the shader, so the old baseline is valid and core/sync clean; it tests conservative bound-descriptor rejection without issuing an undefined simultaneous access.
- Original CommandList.cpp 10094/10112 retains other outputs when replacing one, and 10051 binds PS SRVs in context order. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan resource types/handles alone do not prove independence: requirements from the driver, bound memory offsets and bufferImageGranularity determine ordinary allocation overlap. Reference: https://docs.vulkan.org/spec/latest/chapters/resources.html#resources-memory-aliasing . This corrects D-271/D-272's earlier assumption that a buffer cannot alias an image.
- Source 3.0.3 / ABI be521eec46555682 queries ordinary image/buffer memory requirements, carries allocation spans in retained NativeResource snapshots and preserves original output storage metadata. Whole-list output preflight checks live/pushed descriptors, texel views, future bindings, vertex/index/indirect inputs, active attachments, original outputs and restoration snapshots. Unknown same-allocation spans and overflow cannot prove independence; linear/nonlinear ranges use granularity pages. Independent suballocations remain accepted. Published private file resources keep their separate ownership proof.
- d275-pair.out fully builds runtime/tracker/layer/backend, passing the first 739 checks and four matching stamps. Review then added preserved-output and null-only event negatives plus retained-span assertions: d275-preserved-before.out passes 741/743, proving two further omissions. d275-final-pair.out recompiles the changed target implementation and tracker against the unchanged matching objects, relinks and passes 743/743 and all four stamps. Historical character_package.cpp C4018 warnings remain unchanged.
- d275-regression.out native-output-memory passes real allocation cases 44, target resources 30, tracker 743, Vulkan validation 76 and text 671. Eight scenes cover overlap/disjoint ranges, dynamic/classic rendering, pushed descriptors and completed sandbox module exchange; every process exits normally with clean core/sync validation. Targeted compatibility checks also pass buffer SRVs (core/black/null), published constants, exact-black file/null images and depth destination changes. No game, deployment, launcher, system or remote action occurred.
- Next: apply backing-storage proof to ordinary native input changes, which can occur without an output event. Audit current image attachment-identity checks and constant-buffer inputs against real retained attachment storage before allowing broader application-image/output combinations. Imported/external allocation identity, persistent image layout preservation, capture dimension changes and broader pass/resolve/queue adapters still need separate proof. The staged build/d275-before.py application-image baseline has not run.


## BUG-138: Null image SRVs were rejected beside output changes (D-274; fixed offline)

- D-273 committed and main fast-forwarded as 9049bb3. BUG-138: d274-valid-before.out on 3.0.1/f910 passes 9/21. Null image bindings work alone but before/after/post combinations with output switches remain refused, preserving screen/owned geometry 6418/902. Every valid-baseline process exits normally with clean core/sync validation. The earlier d274-before.out 2/21 encountered D-201's already-known pinned VVL bug: cc_descriptor.cpp 2060 checks any immutable sampler for VUID 09506 without testing YCbCr conversion, although the sandbox sampler has no conversion. It is not a clean planner baseline. The corrected fixture uses mutable samplers without disabling validation or modifying the SDK.
- Original CommandList.cpp 9692/9720 returns a null resource for an absent SRV, and 10051 passes the null view to PSSetShaderResources; 10094 changes outputs in context order. DX11 supplies null-SRV zero reads, reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan now admits an absent SRV into the combined plan because it introduces no image alias. Whole-list descriptor preflight still proves actual kind and the enabled nullDescriptor feature before any commands. Source reference: https://docs.vulkan.org/refpages/latest/refpages/source/VkWriteDescriptorSet.html.
- Source 3.0.2 / ABI f9104b7b1d2fa5de: d274-pair.out fully builds runtime/tracker/layer/backend and the changed sandbox, passing tracker 720; four matching version/ABI stamps pass. Compilation completed during an intentional chat interruption and was checked on resumption. d274-regression.out native-output-null-images passes null combinations 71, existing native images 46, tracker 720, validation 76 and text 670.
- Thirteen new GPU scenes cover before/after/post order, core/KHR rendering, injection, classic RenderPass/RenderPass2, pushed descriptors, wrong kind, application-image refusal and completed sandbox module exchange. Exact RGB-zero pixels prove null sampling in the redirected destination. The sandbox can explicitly request robustness2 with all features false; the no-feature scene proves the device log says null descriptors off, preserves both original pixel results and reports the feature refusal. All normal exits and clean core/sync validation; immutable-sampler null validation remains limited by pinned VVL. No game, deployment, launcher, system or remote action occurred.
- Next priority is backing-memory overlap proof before expanding application-image/output combinations. Inspection found that output conflict checks compare image handles, while ImageInfo/BufInfo already record memory handles and offsets but no image allocation span. Vulkan permits distinct resources to alias a VkDeviceMemory range, including buffer/image granularity padding: https://docs.vulkan.org/spec/latest/chapters/resources.html#resources-memory-aliasing. Reproduce with independent CPU/GPU fixtures, retain the original DX11 runtime distinction, and add explicit allocation-range checks without rejecting disjoint suballocations. build/d275-before.py stages the separate application-image combination baseline but has not run. Capture dimension changes and broader pass/resolve/queue adapters remain offline work.


## BUG-137: Published file textures were rejected beside output changes (D-273; fixed offline)

- D-272 committed and main fast-forwarded as bef42fa. BUG-137: d273-before.out on 3.0.0/f910 passes 11/22 with the corrected exact-black oracle. Published file images bind alone, but combining them with before/after/post output changes triggers the blanket image-mixture refusal. Screen/owned geometry stays 6418/902 instead of redirecting; the black texture never controls the full destination. All normal exits and clean core/sync validation.
- Original CommandList.cpp 11995 creates a compatible SRV for the retained resource, 10051 binds it and 10094 preserves other outputs while changing one. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan admits only retained, published read-only sampled file images whose allocations have no storage or attachment usage. These cannot introduce application attachment aliases. Whole-list descriptor preflight, shader shape/type checks, upload publication waits and exact current-descriptor conflict checks remain in place. Application-image references and mutable images retain their refusal.
- Source 3.0.1 / ABI f9104b7b1d2fa5de: d273-pair.out fully builds runtime/tracker/layer/backend and passes tracker 720; four matching version/ABI stamps pass. d273-regression.out passes existing native images 46, tracker 720, validation 76 and text 669. Combined file images report 73/74: the mutable-image negative already preserves the original but reports an earlier compatible-usage refusal, not the expected mixture refusal. Correcting that specific reason assertion gives 4/4 in d273-mutable.out on unchanged production binaries. Passing unaffected cases are not repeated.
- Sixteen new GPU scenes cover command order, core/KHR rendering, injection, classic RenderPass/RenderPass2, pushed descriptors, exact black texture data, wrong array shape and descriptor kind, application/mutable image refusals, file reload and completed sandbox module exchange. Reload changes the DDS from white to black and verifies the new generation and exact black pixels in the redirected destination. Normal exits and clean core/sync validation. The module exchange is sandbox-only; no game, deployment, launcher, system or remote action occurred.
- Continue null image SRVs beside output changes. An absent image introduces no alias, while existing descriptor preflight must still enforce actual kind and the device's nullDescriptor feature. build/d274-before.py prepares a baseline. Application-image alias state, capture dimension changes and broader pass/resolve/queue adapters remain offline work.


## BUG-136: Buffer SRVs were rejected beside output changes (D-272; fixed offline)

- D-271 committed and main fast-forwarded as 838ecab. BUG-136: d272-before.out on 2.9.9/f910 reports 11/22. A published buffer SRV works alone but combining it with output changes rejects the plan with the blanket non-constant descriptor reason; original screen/owned pixels remain 6418/902. Before, after and post combinations all fail. Its black-pixel assertion was subsequently corrected: band_pixels counts differences from the nonblack clear color, not nonblack pixels. d272-baseline-audit.out rechecks the retained complete bytes of all five baseline scenes, clean validation and actual refusal reasons; the old black case has zero exact black pixels. Processes exit normally.
- Original CommandList.cpp 2032 recognizes Buffer/BufferEx SRV ranges, 9692/9720 retains the current SRV and resource, and 10051/10094 sets PS resources and outputs in context order. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan now classifies SRV destinations using the retained bound-set layout: buffer slots cannot introduce image attachment aliases, including when null. The later whole-list descriptor preflight still proves actual pipeline compatibility, kind, resource retention, range, stride, usage and access ordering before commands are emitted.
- Source 3.0.0 / ABI f9104b7b1d2fa5de follows the required single-digit counter after 2.9.9. d272-pair.out fully builds runtime/tracker/layer/backend and passes tracker 720; four matching version/ABI stamps pass. d272-regression.out passes constant combinations 64, tracker 720, validation 76 and text 668, but buffer combinations report 69/73. d272-pixels.out proves black/null data already creates 6418 exact black pixels: that failure was the band-count oracle. The fixed-struct sandbox shader has no runtime array stride, so stride 8 is not a valid stride-mismatch negative; stride 3 instead proves rejection of an incomplete file element. d272-corrected.out passes all 12 checks for the three affected scenes on unchanged production binaries. Passing unaffected scenes are not repeated.
- Sixteen buffer scenes cover before/after/post output order, core/KHR rendering, injection, classic RenderPass/RenderPass2, pushed descriptors, white/black published data, actual null buffers and a completed sandbox module exchange. Wrong kind, incomplete file stride, application writable-buffer hazards and mixed image assignments preserve original pixels with precise refusal reasons. All normal exits and clean core/sync validation. These are sandbox results; no game, deployment, launcher, system or remote action occurred.
- Continue published read-only file images beside output changes. Their independently owned images cannot be application attachments, and their existing publication/descriptor path already retains ownership and waits. build/d273-before.py prepares the baseline. Application-image aliases, persistent image transitions, capture dimension changes and broader pass/resolve/queue adapters remain offline work.


## BUG-135: Constant-buffer assignments blocked output destination changes (D-271; fixed offline)

- D-270 committed and main fast-forwarded as 56e43ce. BUG-135: the corrected d271-valid-before.out on retained, hash-verified 2.9.7/f910 passes 7/13, refusing combined output/constant-buffer lists. Its native_targets.cpp is identical to 2.9.8. The initial d271-before.out and d271-regression.out mixed fixture used a VS alias under a PS override, so their failures are not valid target-planner evidence. The corrected fixture uses a PS alias. d271-partial.out on the first 2.9.9 build also passes 7/13, exposing the second loop that incorrectly treats constants as output slots.
- Original CommandList.cpp 10000+ sets constant-buffer ranges in 16-byte units; 10094/10112 sets one output while retaining the others. These operations coexist in one context. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan permits typed constant assignments beside outputs, excludes them from output-event preparation and retains the existing whole-list descriptor preflight for layout kind, range, usage, ownership and graphics dependencies. Image descriptor mixtures remain refused before commands are emitted.
- Source 2.9.9 / ABI f9104b7b1d2fa5de: d271-pair.out fully builds runtime/tracker/layer/backend. d271-final-pair.out recompiles native_targets.cpp against the unchanged matching objects, relinks and passes tracker 720; four matching version/ABI stamps pass. The first area run already passes unchanged ordinary outputs 52, graphics uniforms 16, tracker 720 and validation 76. After the loop fix, d271-final-regression.out passes mixed bindings 64, validation 76 and text 667. Passing unaffected steps are not repeated.
- Thirteen mixed cases cover original order, before/after output assignments, core/KHR rendering, injection, classic RenderPass/RenderPass2, published uniform bytes, wrong descriptor kind, image refusal and a completed sandbox backend exchange. The file shader uses a color component impossible for the original uniform payload, independently proving that the buffer controls pixels in the switched destination. All normal exits and clean core/sync validation. The swap is sandbox-only; no game, deployment, launcher, system or remote action occurred.
- Continue mixed outputs with retained buffer SRVs: an SRV backed by a proven buffer cannot alias a framebuffer image, but the current blanket non-constant refusal still rejects it. build/d272-before.py prepares a fresh baseline. Unknown/null/image SRVs and writable-resource hazards retain their guards. Capture dimension changes and broader pass/resolve/persistent-image/queue adapters remain offline work.


## BUG-134: Warm transparent layers required nullDescriptor after depth was unbound (D-270; fixed offline)

- D-269 committed and main fast-forwarded as a486511. BUG-134: d270-before.out on 2.9.7/f910 passes 23/27 with actual device null descriptors off. A warm null-depth scene stops at 58 captures/composites instead of 98 and reports a feature requirement; restoring depth only reaches 68. Every process exits normally with clean core/sync errors and complete target bytes. The replay explicitly requests robustness2 with all features false; the layer respects that application request.
- Original CommandList.cpp 10051 binds the current PS resource, including null. EFMITTL Capture retains the current scene references, and shaders/composite.hlsl 35 loads SceneDepth. DX11 supplies null-SRV zero reads, reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan now compiles a separately keyed depthless composite using the existing typed-zero SPIR-V rewrite, binds a retained valid spare depth view and transitions that view even when all four layers exist. No null descriptor is needed for this scene-depth slot; incomplete cold-layer history is unchanged. This is not a claim that every backend feature works without robustness2.
- Source 2.9.8 / ABI f9104b7b1d2fa5de: d270-pair.out fully builds runtime/tracker/layer/backend; tracker 720 and all four version/ABI stamps pass. d270-regression.out capture-no-null passes ordinary depth history 27, actual feature-disabled history 32, capture snapshots 31, tracker 720, validation 76 and text 666. Warm and warm-restored scenes now complete 98 captures/composites; cold and cold-then-restored scenes retain zero as the original incomplete-layer rule requires. All normal exits and clean core/sync errors; known interface warnings remain visible.
- d270-parity.out independently compares every final target of all five history scenes with nullDescriptor on and off: all bytes match. New fixture flags affect only the test child's Vulkan device request. No game, module exchange, deployment, launcher, system or remote action occurred.
- Continue combined output/constant-buffer native lists. prepare_native_targets currently refuses every descriptor assignment mixed with outputs, even a typed constant-buffer binding that cannot alias an image. Read the original OM/constant-buffer setters, reproduce against 2.9.8, then permit only the independently validated buffer case while retaining image-alias guards. Capture dimension changes, broader pass/resolve/persistent-image and queue adapters remain open; they are offline engineering work.


## BUG-132 / BUG-133: Ordinary captures omitted and pinned validation logging race (D-269; offline fix/workaround)

- D-268 committed and main fast-forwarded as 6db3757. BUG-132: d269-before.out on 2.9.6/f910 passes 10/13: ordinary meshes execute but no deferred captures/composites do. Source 2.9.7/f910 shares retained capture preparation with merged meshes, keeps the ordinary application's vertex/bone inputs, prepares four private capture variants, snapshots each accepted output marker and validates immediate output work before publishing deferred captures.
- Original CommandList.cpp 1642 draws with the current context; EFMITTL Capture 983/984 retains SceneColor/SceneDepth and saves/restores outputs after the layer is complete, without requiring a Core component. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan owns the retained geometry, image views, descriptor snapshots and recording-order output transaction explicitly; there are no character-specific runtime tables.
- d269-pair.out fully builds runtime/tracker/layer/backend; tracker 720 and four 2.9.7/f910 stamps pass. d269-focused.out reaches 12/13, with 29 captures/composites but identical original-mesh pixels. That fixture has no independent visibility proof. The final owned box retains actual game shaders and valid ordinary bone inputs: low/high opacity changes forward color0 only, mask channels/inversion have exact equivalent bytes, and every target readback is complete.
- d269-regression.out passes own shadows 48, merged deferred outputs 54, tracker 720, matched Mod outputs 158, validation 76 and text 665; its own-capture step crashes. A fresh retry also crashes. BUG-133: the second symbolized stack directly names pinned VVL logging.cpp 130, where duplicate_message_count_map is read/written before debug_output_mutex. The first stack names VVL allocation/free operations; the same cause is possible but not proven. Matching the existing Sandbox child setting, replay_capture now sets VK_LAYER_DUPLICATE_MESSAGE_LIMIT=0, preserving all diagnostics and bypassing that racy map without disabling validation. The independent no-EFMI/no-device messenger probe completes 100 rounds with both limits and does not reproduce the crash. Initial DLL hashes and both symbolized traces are retained under build/d269-*; no system setting or external SDK binary changed.
- d269-unlimited.out completes all nine scenes with clean core/sync errors but passes 62/63: the mixed fixture incorrectly expects an immediate forward draw to contribute depth. The corrected fixture draws the immediate box with depth unbound and restores depth before capture. d269-mixed.out passes 14/14; d269-final-capture.out passes 63/63 for all nine scenes on the unchanged 2.9.7 pair. Mixed immediate/deferred execution changes scene colour while keeping depth intact. The failed reports remain distinct; passing unchanged area steps are not repeated. Known interface warnings remain visible. No real-game or module-exchange acceptance is claimed.
- Continue removing the compositor's nullDescriptor dependency for a retained complete layer whose current scene depth is null. Use original null-SRV zero semantics, retained valid descriptor storage and a separately keyed zero-read composite shader; cold incomplete history must remain unchanged. Changing capture dimensions and broader pass/resolve/persistent-image/queue adapters remain open. No deployment, game, launcher, system or remote action.


## BUG-130 / BUG-131: Missing ordinary shader groups and discarded shadow specialization (D-268; fixed offline)

- D-267 committed and main fast-forwarded as 8a387e9. BUG-130: d268-before.out on 2.9.5/f910 passes 10/13; ordinary own meshes execute, but opacity shadows are zero and 0.25/0.75 produce identical targets. Adding the executor alone still gives 10/13 because noncomponent requests omit the library shader groups. With both connections, d268-final-focused.out reaches 12/13 and 58 shadow draws, but still produces opaque pixels at both opacities.
- BUG-131 affects both mesh routes: D3DPreprocess removed the generated shader's __spirv__ specialization declarations before DXC, leaving alpha=1, channel=0 and invert=0. The original CommandListShadow uses current IniParams and restores its mask; ShaderRegex.cpp 366 links shader groups independently of components, and HackerContext.cpp 492 executes them. CommandList.cpp 2573 compiles original source with stage macros. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Vulkan shares mask ownership/preparation between mesh routes, preserves ordinary vertex/bone inputs and ordered output markers, and opts generated native shaders into separate Vulkan preprocessing. Ordinary Mod preprocessing and the original DXBC reference stay unchanged; native preprocessing has a distinct cache key.
- The pre-specialization 2.9.6 pair is retained in build/d268-pre-specialization-dist with verified SHA-256 hashes. d268-merged-before.out reproduces 12/13 on that pair: 56 shadow draws but identical low/high opacity targets. Visibility diagnostics used an owned box and temporary projection shaders; they are not the final acceptance fixture. The early suspicion that invisibility explained equal pixels was disproved by extreme-opacity checks and parameter tracing. No diagnostic logging or projection replacement remains in production or the final tests.
- Source 2.9.6 / ABI f9104b7b1d2fa5de: d268-specialization-pair.out fully rebuilds runtime/tracker/layer/backend, tracker 720 and four matching stamps pass. d268-specialization-focused.out and d268-merged-focused.out both pass the original 13 checks: opacity now changes only shadow depth. The compiler check passes 194 checks and independently validates 61 SPIR-V modules, including native specialization, unchanged DXBC, distinct cache keys and native-preprocessing failure containment.
- d268-regression.out own-shadow passes ordinary opacity 48, merged opacity 48, existing shadow outputs 35, matched Mod outputs 158, compiler 194/61 modules, tracker 720, validation 76 and text 664. Seven cases per mesh route cover low/high opacity, output restoration, zero/one mask channels and inversion. Equivalent cases preserve every target byte, a zero mask removes visible depth, and all target readbacks are complete. The ordinary route uses an owned box with valid original vertex/bone inputs and actual game shaders; neither route requires runtime character tables. Normal exits and clean core/sync errors; known interface warnings remain separate. No game or new module-exchange acceptance is claimed for these new opacity cases.
- Continue ordinary own-mesh forward captures. build/d269-before.py is ready to reproduce that separate executor gap; build/d269-edit.py is a staged, unexecuted implementation sketch that requires the baseline first. Changing capture dimensions and broader pass/resolve/persistent-image/queue adapters remain open. No deployment, game, launcher, system or remote action.


## BUG-129: Output events in a deferred capture list rejected its geometry (D-267; fixed offline)

- D-266 committed and main fast-forwarded as 5e19e00. BUG-129: fresh d267-before.out on 2.9.4/f910 passes 10/13. Nulling then restoring depth before a deferred capture still rejects 28 plans, drops items from 3108 to 2744 and loses all 28 captures/composites. Normal exits, clean core/sync errors and complete fenced targets; the original baseline folders remain distinct from d267-focused-*.
- Original CommandList.cpp 10094/10112 preserves other output bindings; DrawCommand 1642 uses that point's context. EFMITTL Capture 983/984 retains scene references only after the layer is complete. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. The merged executor now snapshots outputs at each accepted capture marker, retains immediate and deferred positions separately, and validates the immediate draw transaction before publishing captures. Cold incomplete-layer history and private descriptor restoration remain intact.
- Source 2.9.5 / ABI f9104b7b1d2fa5de: d267-pair.out builds tracker/layer/backend, tracker 720/720 and four matching stamps pass. The original failed comparison passes 13/13 in d267-focused.out: both control and restoration before capture produce 3108 items, 56 shadows and 28 captures/composites without refusals.
- d267-regression.out capture-output passes deferred outputs 54, capture snapshots 31, opacity shadows 35, merged outputs 88, original runtime 543, tracker 720, validation 76 and text 661. Eight new replay scenes include capture-only lists (2772 items, 28 captures) and three interleaved captures (84 queued, 28 successful). Restoration before capture, post restoration and the interleaved case match every control target byte. Cold depth absence yields zero captures/composites without error, even if restored afterwards. Normal exits, complete fenced bytes and clean core/sync errors; known unused-interface warnings are separate. No new module-swap or real-game acceptance is claimed.
- Continue the ordinary own-mesh library executor: code inspection found that its material path does not consume opacity-shadow/capture markers. build/d268-before.py prepares an independent noncomponent replay comparison before a fix. Deferred own-mesh output events and changing capture dimensions remain explicitly refused; broader pass/resolve/persistent-image/queue adapters remain open. No deployment, game, launcher, system or remote action.


## BUG-128: Later output changes discarded queued transparent captures (D-266; fixed offline)

- D-265 committed and main fast-forwarded as6defe05. BUG-128: d266-snapshot-before.out on2.9.3/f910 passes11/12. A later ordinary component nulls the depth target after a transparent capture was queued. Both scenes have3108 draw items,56 opacity shadows, no plan refusals, normal exits and clean core/sync errors. Control produces28 captures/composites; late-null produces0/0, although its transparent geometry was queued. Original2.9.3 reports and fenced bytes are preserved under build/d266-baseline-snapshot. Known shader-interface warnings stay separate.
- Original EFMITTL CommandListCapture retains SceneColor/SceneDepth at983/984; CommandListComposite/CustomShaderComposite1212/1241/1258 uses those references later. Original ResourceCopy target bindings retain the other outputs. DX11 supplies reference-counted lifetime with deferred destruction, hazard tracking and one-context order. Each queued Vulkan capture now retains exact live scene view/image identities. Layer-history decisions use each capture's depth presence, and composition uses the last successful capture's scene with layouts proven from actual recorded commands. Private depth-view and framebuffer caches retain their source allocation/view until their own objects are destroyed.
- Source2.9.4/ABIf9104b7b1d2fa5de: d266-pair.out fully builds tracker/layer/backend and passes the existing713 tracker checks. Seven new CPU checks validate subpass mapping, immutable snapshots after null, recycled-handle refusal, supported subresource bounds, required ownership, deferred application destruction and view/image/allocation release order. The first added fixture had an enum-to-flags aggregate compile error, corrected before execution. d266-tracker-final.out recompiles tracker_test only against verified unchanged production objects and passes720/720; four matching stamps pass.
- Final d266-regression.out capture-snapshot passes new31, existing depth-history27, opacity-shadow35, tracker720, validation76 and text660. Five owned replay scenes cover later depth null, colour null, both null and pre-restored outputs; restoring before the later ordinary draw preserves every control target byte. Every scene retains forward captures/composites with complete fenced targets, normal exits and clean core/sync errors. Snapshot lifetime is also checked on CPU; no new game or module-exchange acceptance is claimed.
- Deferred output events within the capture's own list remain intentionally refused. The earlier d266-before.out gives10/13 on2.9.3: even null/restore before capture rejects28 plans and loses transparency. That is the next adapter, not fixed by this snapshot change. build/d267-before.py prepares a fresh baseline with unique output folders. Broader pass/resolve/persistent-image/queue adapters remain open. No deployment, game, launcher, system or remote action.


## BUG-127: Output events wrongly rejected immediate opacity shadows (D-265; fixed offline)

- D-264 committed and main fast-forwarded as1b3925b. BUG-127 valid d265-before-valid.out on2.9.2/f910:9/11. Control executes56 opacity-shadow draws and28 captures/composites; nulling depth before the library draw and restoring it afterwards rejects56 component plans and executes no opacity shadows, while forward capture/composition stays28. Both runs exit normally with core/sync errors0, known shader-interface warnings separate, and complete fenced target readbacks. The earlier absolute-path pilot could not load its remap assets, and the next report read the wrong single-package status location; neither is an acceptance baseline. The valid fixture copies referenced assets with their original relative declarations and reads top-level counters.
- Original CommandList.cpp10094/10112 preserves other outputs, DrawCommand1642 uses the current context, and HackerContext3154 keeps BeforeDraw/caller/AfterDraw order. The installed library's shadow CustomShaders draw immediately. DX11 supplies retained references with deferred destruction, hazard tracking and one-context order. The planner now distinguishes immediate opacity shadows from deferred captures, and the merged executor retains shadow markers in the output sequence. Existing whole-plan target preparation, full shadow/bone interfaces, private descriptor restoration and command-owner lifetime are reused.
- Source2.9.3/ABIf9104b7b1d2fa5de: d265-pair.out fully builds tracker/layer/backend; tracker713 and four matching binary stamps pass. The original failed comparison passes11/11 in d265-focused.out, with56 shadows,28 captures/composites and no refused plans in both cases. The complete shadow-output area in d265-regression.out passes new35, merged88, original runtime543, tracker713, validation76 and text658.
- Five owned replay scenes cover control, persistent null, draw-time null with restoration, restoration before the shadow and post-draw null/restoration. The latter two equal every control target byte. Null/restoration changes only shadow depth targets, and restoring preserves later depth geometry that persistent null removes. All normal exits and clean core/sync errors; shader-interface warnings are retained separately. This verifies merged opacity-shadow outputs in the sandbox, not a new game session or deferred transparent output execution.
- Continue deferred capture outputs. Original EFMITTL captures SceneColor/SceneDepth by reference at each capture, then composites into retained SceneColor with retained SceneDepth. Current pass_end derives them from the final pass; ordered output support therefore needs explicit capture-time attachment snapshots, not just removal of the planner refusal. build/d266-before.py prepares the harmless null/restore-before-capture baseline. Broader pass/resolve/persistent-image/queue adapters remain open. No deployment, game, launcher, system or remote action.


## BUG-126: Native capture and shadow callbacks bypassed hunting (D-264; fixed offline)

- D-263 committed and main fast-forwarded as d496f71. BUG-126: the actual library bridge fixture on2.9.1/f910 fails at its first hidden native capture (d264-before.out). Independent host helper coverage gives74 passed/4 failed in d264-host-before-full.out: indexed, translucent, shadow and invalid-parameter callbacks bypass hunting. The attempted incremental host build was refused because common.h had changed; no stale executable was used for that baseline.
- Original CommandList.cpp1601 skips every custom DrawCommand before argument evaluation. The installed library's Shadow/CustomShaderShadow lists use those draws, as do its capture shaders. DX11 supplies retained references with deferred destruction, hazard tracking and one-context order; Vulkan keeps the existing command owners and ordering, and suppresses native draw production before parameter/resource work. Native capture/shadow entry points also skip count conversion. Mod pre/post lists and shader-group cleanup continue.
- Source2.9.2/ABIf9104b7b1d2fa5de: d264-build.py sequentially builds the original host, runtime tests and production tracker/layer/backend. Host planner78, other host groups, runtime543, tracker713 and four matching binary stamps pass. The new runtime cases cover capture, both shadow groups, ordinary shadow fallback, hidden invalid counts and visible/hidden/visible recovery; the host cases independently exercise direct native helpers and an invalid opacity channel.
- d264-regression.out hunting-library passes original host/runtime, the captured library-call replay, native hunting, tracker, validation and text checks. The library replay preserves ordinary transparency/shadow execution and has clean core/sync errors; known unused shader interface/output warnings are distinct. New library hunting assertions are CPU plan evidence, not a claimed GPU hidden-transparent pixel capture. Existing hunting fixtures use controlled termination. No deployment, game, launcher, system or remote action.
- Continue output events around opacity-controlled shadows, which render immediately and currently share the deferred-capture refusal. build/d265-before.py prepares an owned INI fixture around the original library draw, retaining read-only source assets and complete replay target readbacks. Reproduce before changing the planner. Deferred transparent segmentation and broader pass/resolve/persistent-image/queue adapters remain open.


## BUG-125: Merged output events dropped geometry and native queries lacked attachment context (D-263; fixed offline)

- D-262 committed and main fast-forwarded as e9f7206. BUG-125 baseline d263-before.out on 2.9.0/f910: 15/18. Adding oD=null inside a generic merged component drops its geometry, while later texture and independent geometry still write depth. The negative process exits1 for its independent geometry assertion; validation is0/0 and fenced depth readback is complete. A positive aggregate merged counter did not prove the affected component rendered.
- Original CommandList.cpp10094/10112 retains other outputs when changing one, and DrawCommand1642 uses the current context. DX11 supplies retained references with deferred destruction, hazard tracking and one-context order. Merged plans now query actual outputs, prepare the whole target sequence and compatible full merged interfaces, then share the ordered output/draw executor with ordinary meshes. Bone imports, identity, private descriptors and command-owner lifetime remain intact; output-only component plans use the native caller executor.
- Source2.9.1/ABIf9104b7b1d2fa5de: d263-pair.out fully builds tracker/layer/backend. Initial focused22/22 passes; initial merged-output regression passes merged88, beside131, texture45, tracker713, validation76 and text657 but fails translucency23/27. Native output conditions lacked attachment context when there were no ShaderOverrides or hunting, so the library selected the wrong branch. Backend activation now enables attachment/layout tracking before subsequent passes. d263-final-pair.out rebuilds this changed unit and relinks verified unchanged objects; tracker713 and four matching stamps pass.
- Final d263-tracking-final.out merged-output-tracking passes merged88, translucency27, tracker713, validation76 and text657. All eight merged scenes now omit empty ShaderOverrides, proving tracking without incidental hooks. They cover pre/post null, save/restore, interleaved components, RenderPass2/inject and two completed module exchanges, with complete fenced depth bytes and clean core/sync validation. The unchanged beside131 and texture45 results belong to the initial regression; that initial regression as a whole failed. No real-game acceptance is claimed.
- Continue the native bridge hunting audit: direct translucent and indexed helper emissions lack the parsed DrawCommand hunting guard. Reproduce before fixing. Transparent output events and broader pass/resolve/persistent-image/queue adapters remain open. No deployment, game, launcher, system or remote action; root deployed files remain historical.


## BUG-124: Output commands refused Mod draws beside the original (D-262; fixed offline)

- D-261 committed and main fast-forwarded as afd05af. BUG-124 baseline d262-before.out on the initial 2.8.9/f910 pair (same output executor): 21/24. Beside-original control retains 6418 original depth pixels and 1152 independent moved Mod depth pixels. Adding oD=null refuses the Mod and leaves all 6418 original depth pixels instead of 5516. Both exits and core/sync validation are clean, with complete fenced color/depth readbacks.
- Original HackerContext.cpp3154 preserves BeforeDraw, caller, AfterDraw order; CommandList.cpp10094/10112 changes one output while retaining the others. DX11 supplies retained/deferred lifetime, hazard tracking and one-context order. The Vulkan plan now retains the true pre/post position among accepted draw markers and output events. The executor prepares every target/variant and validates the whole draw transaction, then emits the original indexed caller once at that boundary. Original geometry and the occupied private descriptor slot are restored before that caller. Transient output order stays outside mesh/material cache keys.
- Source 2.9.0 / ABI f9104b7b1d2fa5de: d262-pair.out fully builds tracker/layer/backend; tracker 713 and four matching stamps pass. d262-focused.out passes 40/40 with independent pre-Mod, original and post-Mod color/depth regions: pre-null suppresses current depth, post-null preserves original depth, and post restoration re-enables the third mesh. Final d262-regression.out mod-output-beside passes beside117, interleaved159, merged/texture14, tracker713, validation76 and text656. The added empty-prefix/post-only mesh case passes14/14 separately in d262-empty-post.out, proving the caller at output position zero. Normal exits, complete fenced readbacks and clean core/sync validation; module exchange is completed, not merely requested.
- Continue merged-skeleton output events. build/d263-before.py is prepared to add oD=null inside the generic fixture's component draw list while retaining the separate texture Mod as a control. Run it after these processes exit before changing that executor. Transparent output events and broader pass/resolve/persistent-image/queue adapters remain open. No deployment, game, launcher, system or remote action; root deployment remains historical.


## BUG-123: Matched Mod draws survived hunting suppression (D-261; fixed offline)

- D-260 committed and main fast-forwarded as e2b4722. BUG-123 baseline d261-before.out on 2.8.8/f910: 11/12 checks. Selected hunting skip still leaves 902 Mod pixels; clearing selection restores 6418. Both sandbox captures have clean core/sync validation. The existing fixture uses controlled process termination, not a claimed normal exit.
- Original HackerContext.cpp1005/1014 sets the separate skip/hunting flags while retaining graphics pre/post lists; CommandList.cpp1601 suppresses custom draws before argument evaluation. DX11 supplies retained/deferred lifetime, hazard tracking and one-context order. Both matched planning entries now pass hunting into RuntimeDraw and DrawCallInfo. The planner suppresses the implicit caller and custom draw commands while continuing both list phases. The historical comment/document claiming Mod draws survive hunting was incorrect and is corrected.
- The first GPU fix passed 14/14, including complete readbacks. The native Core keep-original edge then failed exactly one planner check (69/70 in d261-keep-before.out): it could override hunting. That request can no longer emit or revive the implicit caller while hidden; clearing hunting still restores its effect. d261-host-final.out compiles the two changed host units after checking every reused input digest. The final affected hunting-mods area in d261-regression.out passes original CPU host/runtime, hunting, matched output-only calls, tracker, validation and text checks.
- Source 2.8.9 / ABI f9104b7b1d2fa5de: d261-pair.out builds tracker/layer/backend. d261-final-pair.out recompiles only the reviewed frontend correction and relinks verified unchanged production objects; tracker 713 and all four stamps pass. GPU hiding coverage is the ordinary Mod mesh; both component planning entries share the flag, but hidden merged-character GPU acceptance is not claimed. No deployment, game, launcher, system or remote action.
- Continue beside-original output events. build/d262-before.py against the initial D-261 pair (the same output executor) gives 21/24: control has original plus independent moved mesh, while adding oD=null refuses the Mod and preserves unwanted original depth. Both exits, complete color/depth readbacks and core/sync validation are clean. The remaining merged/transparent and broader pass/resolve/persistent-image/queue adapters remain open.


## BUG-122: Hunting discarded native graphics resource events (D-260; fixed offline)

- D-259 committed and main fast-forwarded as f36a8c2. BUG-122: d260-before-complete.out on 2.8.7/f910 reproduces 694/698 tracker checks. Hidden native graphics calls lose both pre/post output events, and an invalid hidden draw argument also refuses the resource list. These are CPU command-recording proofs, not GPU pixels or game evidence.
- Original HackerContext.cpp1005/1029,3154 and1087 still executes graphics lists around the hidden caller; CommandList.cpp1601 returns from DrawCommand before evaluating its arguments. DX11 supplies retained references with deferred destruction, hazard tracking and one-context order. Vulkan now preflights and records graphics resource events under hunting, skips hidden draw arguments and resolves files only for resource operations. Compute suppression keeps its existing behavior. Existing target/descriptor owners retain the recorded resources through completion.
- Source 2.8.8 / ABI f9104b7b1d2fa5de: d260-pair.out fully builds tracker/layer/backend, tracker 713/713 and all four stamps pass. d260-regression.out hunting-resources passes the affected native caller, pre/post/restored outputs, hunting, tracker, validation and text checks. The new hidden-argument/pre/post assertions are CPU coverage; the output-phase GPU scenes have complete fenced depth bytes, normal exits and clean core/sync validation. Existing hunting fixtures use their controlled shutdown behavior.
- Continue the matched-Mod hunting audit: character_record's comment claims custom draws survive hunting, contrary to the original DrawCommand. build/d261-before.py prepares a real sandbox skip/clear comparison before any fix. Beside-original, merged/transparent output events and broader pass/resolve/persistent-image/queue adapters remain open. No deployment, game, launcher, system or remote action; root deployment remains historical.


## BUG-121: Matched output-only lists could not change application targets (D-259; fixed offline)

- D-258 committed and main fast-forwarded as02a62d9. BUG-121 baseline d259-before.out on2.8.6/f910:16/18 checks. Removing the image replacement leaves output-only lists refused; current and following independent geometry still write depth after oD=null. Both scenes finish with complete fenced depth bytes and clean core/sync validation; the control and original images remain correct.
- Original HackerContext.cpp1005/1029,3154 and1087 preserves command-list execution around a hidden application draw; CommandList.cpp10094/10112 changes one output while retaining the others. DX11 supplies retained/deferred lifetime, hazard tracking and one-context order. Pure NativeBind plans now preserve skip_original and the exact pre/post boundary and run through the existing native application-call executor, without mesh activation or a private image variant. The executor reacquires and revalidates its resource proofs after the planner releases its shared lock. output_calls reports these executions. A nested hunting-suppression snapshot hides the caller while allowing its resource lists to run.
- Source2.8.7/ABIf9104b7b1d2fa5de: d259-pair.out fully builds tracker/layer/backend; tracker689 and four matching stamps pass. Four new CPU checks cover nested hunting suppression. Focused30/30 passes, including handling skip. Final d259-regression.out mod-output-call passes output-only70, texture-only45, native hunting45, tracker689, VVL76 and text655. The new scenes have normal exits, complete depth bytes and validation0/0. Their declarations contain no image or mesh assets; pre/post null, retained restoration, explicit hide, RenderPass2/inject and two completed module exchanges pass. Existing hunting fixtures retain their own controlled shutdown behavior.
- Continue the original native_calls hunting audit: its hunting_skip branch currently bypasses resource events as well as draw calls, whereas the original graphics pre/post lists still run. Reproduce that branch before changing it. Remaining beside-original/merged/transparent and broader pass/resolve/persistent-image/queue adapters stay open. No game, deployment, launcher, system or remote action; do not stop at this commit.


## BUG-120: Output commands rejected matched texture-only plans (D-258; fixed offline)

- D-257 committed and main fast-forwarded asaa5a43f. BUG-120 valid d258-before-valid.out on2.8.5/f910 has14/18 checks: control passes; a matched texture-only list with oD=null falls back to the original image and keeps writing depth for its own and the following independent geometry. Both runs have validation0/0 and complete fenced depth bytes; the negative scene exits1 for expected pixel assertions. Preceding merged geometry remains intact.
- Original CommandList.cpp10035/10051 binds the pixel SRV independently;10094/10112 changes the named output while preserving other outputs; DrawCommand1601/1642 uses that current context. DX11 supplies retained/deferred lifetime, hazard tracking and one-context order. Vulkan now extracts output events around the original draw's phase boundary independently of its private image declarations, prepares targets and the complete texture variant before recording, and restores the actual compatible application pipeline before post events. Geometry is still the application's. Output-only, beside-original and transparent plans keep explicit refusals.
- The new owned headless fixture reads every depth texel and checks preceding merged, current texture-only and following independent geometry. The first pilot d258-before.out lacked the depth-write exit dependency and failed validation; the second d258-before-fixed.out had clean validation but missed the fixture's early-return exports. Both are excluded as acceptance baselines. Depth store/final dependencies and transfer/host visibility are now explicit, and a scoped export writes the completed readback on short fixture paths. Original FrameAnalysis.cpp717 CopyResource was reread for that transfer; DX11 supplies its hazard tracking. Only the fixture changed before the valid baseline.
- Source2.8.6/ABIf9104b7b1d2fa5de: d258-pair.out fully builds tracker/layer/backend; four matching stamps and tracker685 pass. Focused18/18 passes. Final d258-regression.out texture-output passes new45, existing merged/texture14, tracker685, VVL76 and text654. Pre-null suppresses current and following depth, post-null preserves current depth, restoration resumes following depth, and RenderPass2/inject pass; every case has complete readbacks and validation0/0. All processes exited. No game, deployment, launcher, system or remote action.
- Continue output-only matched lists with the native application-call executor; build/d259-before.py removes the texture replacement and preserves the independent depth assertions. Then continue remaining beside-original/merged/transparent and broader pass/resolve/persistent-image/queue adapters. Do not stop at this commit.


## BUG-119: Output changes between own-mesh draws rejected the whole plan (D-257; fixed offline)

- D-256 committed and main fast-forwarded as5adfe69. BUG-119 baseline d257-before.out on2.8.4/f910:22/28 checks. The two independent Mod triangles work in control; inserting a depth unbind between them rejects the plan, leaves6418 original center pixels and neither Mod triangle. Both exits and core/sync validation are clean, with complete independent color/depth bytes.
- Original CommandList.cpp1601/1642 executes each draw against that point's context; SetResource10094/10112 preserves the other outputs. DX11 supplies retained/deferred lifetime, hazard tracking and one-context order. The Vulkan plan now retains accepted draw markers interleaved with output events; skipped or zero draws cannot shift a later event. Geometry/material caches remain independent of transient outputs. All target changes and every per-draw compatible pipeline are prepared before any command; emission restores the actual selected application pipeline between segments and after the final draw.
- Source2.8.5 retains ABIf9104b7b1d2fa5de. d257-pair.out fully builds tracker/layer/backend; tracker685 and four2.8.5/f910 stamps pass. d257-focused.out58/58 proves separate first/second depth1152/0, restored depth on a third mesh, and an empty first draw with no offset drift. Final d257-regression.out mod-output-order passes interleaved159, prefix/postfix158, tracker685, VVL76 and text653; all processes exit normally and core/sync errors0. Cases include depth and MRT null/restoration, actual filter selection, RenderPass2, inject and completed module exchange.
- Ordinary hiding own-mesh plans now support output changes between individual draws. Output-only, beside-original, texture-only and transparent plans, plus the existing broader pass/resolve/persistent-image/queue adapters, remain separate work. Continue by adapting outputs around texture-only application geometry, then the remaining native routes. No game, deployment, launcher, system or remote action; root deployed artifacts remain historical and distinct.


## BUG-118: Matched own-mesh output assignment suppressed geometry (D-256; prefix/postfix fixed offline)

- D-255 committed and main fast-forwarded as7c6d755. BUG-118: d256-before-focused.out on2.8.3/ecc has20/22 checks: placing oD=null in the matched own-mesh list suppresses the Mod geometry. Both processes exit normally with clean core/sync validation and complete independent readbacks. The broader exploratory pilot contained irrelevant unmatched-override assertions; the focused report is the acceptance baseline.
- Original CommandList.cpp10094/10112 preserves every other output while replacing the named RTV/DSV; DrawCommand1601/1642 executes each draw in list order. DX11 supplies retained/deferred lifetime, hazard tracking and one-context order. Vulkan now collects explicit output events independently from cached mesh/material plans, preflights the whole target sequence and every draw, prepares compatible Mod interfaces against the future pre-draw pass, and retains them through completion. The caller-held resource lock is explicit; Mod clones preserve capacity for later output-event references. Pre/post recording restores the actual selected compatible application pipeline.
- Source2.8.4/ABIf9104b7b1d2fa5de: ordinary hiding own-mesh plans accept output commands before all draws and after all draws. Other native descriptors, changes between draws, transparent plans, beside-original plans and output-only plans remain explicitly refused by this executor. Existing merged and texture paths retain their scope. No per-character rules are introduced.
- d256-pair.out fully builds tracker/layer/backend and recipe/variant/cache/snapshot/character tools. The reviewed capacity correction rebuilds native_targets plus tracker in d256-final-pair.out, reusing only unchanged objects: tracker681 and recipe404 pass; four2.8.4/f910 stamps pass. Focused GPU24/24 includes independent moved depth1152 in control and0 when unbound, with both meshes visible. Other new cases134/134 pass. Final mod-output area in d256-regression.out passes native events158, old Mod geometry132/character14, recipe404/cache81/snapshot52/variant63, tracker681, VVL76 and text652. Core/sync errors0, completed module exchange and normal exits; no real-game acceptance is implied.
- Continue D-257: preserve output changes between individual ordinary Mod draws. The two-region fixture is build/d257-before.py; run its baseline after this area's processes exit, then implement ordered draw markers, per-draw compatible passes and complete preflight. Remaining merged/texture/transparent/pass/resolve/persistent-image/queue adapters follow. No deployment, game, launcher, system or remote action.


## BUG-117: Native warm transparency refused null scene depth (D-255; fixed offline in2.8.3)

- BUG-117: valid d255-before.out on2.8.2/ecc reproduces cold/warm missing-depth refusal in the native compositor. Control/cold/warm have93/0/54 captures and composites over100 frames; warm stops at the depth removal. All exit normally with core/sync errors0;4/4/8 known unused shader-interface/output warnings remain separate. The earlier d254-before.out had replay synchronization hazards and is excluded; D-254 repaired those tools and committed/main fast-forwarded243864b.
- Original CommandList.cpp12458, EFMITTL Capture963/SelectLayer1053 and FrameReset153 were reread. Null copy_desc with UNLESS_NULL preserves existing resources; creation only while FrontColor is null leaves a cold missing-depth layer incomplete even after depth is restored. Size changes release the layers. DX11 supplies retained/deferred lifetime, hazard tracking and one-context order. d255-original.out90 checks confirms cold/warm/recovery plus an additional partial layer, every pixel across three frames and validation0/0. The tracked original-library suite adds size recovery and exact counters, not an invented opaque fallback.
- Source2.8.3/ABIeccaa3c3c47d32e4 stores logical layer history separately from evictable Vulkan allocations. The compositor filters incomplete layers in call order; existing complete layers accept null scene depth through the enabled nullDescriptor feature and cache identity0. Absent depth emits no depth barriers or retained stale DSV. Actual nonnull depth still requires complete metadata, sampled usage, known layouts and existing lifetime retention/restoration. Color0 remains required; other pass/queue adapters retain their prior scope.
- d255-pair.out fully builds tracker/layer/backend; four2.8.3/ecc stamps and tracker674 pass. d255-regression.out translucency-depth passes original-library446 in each cached/source mode, native27, tracker674, VVL76 and text651. Native five100-frame cases prove control93/cold0/warm92/recover0/warm-restore93 captures/composites, no missing-depth error, complete owned color/depth readbacks, warm-vs-cold differences confined to forward color0, and every restored target byte identical to control. Core/sync errors0; historical unused shader-interface/output warnings are retained in reports. All processes exited, diff and ownership paths reviewed. No deployment/game/launcher/system/remote action.
- Continue matched-Mod output commands: original SetResource10094/10112 changes one output while preserving the others. Current standalone native-call execution already has ordered target preflight/recording, but CharacterProgram::plan still flattens Mod draws and rejects native operations. Preserve pre/post command order, whole-plan preflight and per-recording owners while extending that executor. Remaining pass/resolve/persistent-image/queue adapters follow; do not stop at this commit.


## BUG-115/BUG-116: Replay readback and cross-queue transition hazards (D-254; fixed offline)

- Original FrameAnalysis.cpp717/1733 stages with CopyResource then maps the staging resource. DX11 supplies retained/deferred lifetime, hazard tracking and one-context ordering. The owned Vulkan replay must order final render-pass layout writes before transfer reads, and order initial transitions after staged cross-queue waits. The Vulkan synchronization specification confirms the explicit memory-dependency requirement: https://docs.vulkan.org/spec/latest/chapters/synchronization.html .
- BUG-115 baseline d254-disabled-sync-before.out: replacement disabled,4 replay frames,15 transfer READ_AFTER_WRITE errors despite exit0. The first disabled probe omitted sync validation and is not the failing baseline. The earlier native null-depth probe also had readback hazards and is excluded as clean rendering evidence. Added a final ALL_COMMANDS/MEMORY_WRITE to TRANSFER/TRANSFER_READ barrier; validation errors now fail the executable, and replay_capture.py enables child-only sync validation by default. d254-disabled-fixed.out passes4 frames with errors0/warnings0.
- The affected replay-readback area exposed BUG-116: two-queue disabled/GPU-bones/CPU-bones paths each report10 initial-transition WRITE_AFTER_WRITE errors in d254-regression.out; the library-call step passes. Replay-owned passes now add an external dependency while preserving the original staged semaphore waits and queue graph. The first correction omitted the same dependencies from the pipeline's compatibility pass, producing10 render-pass-compatibility errors (d254-queues-fixed.out); this fixture error was corrected before acceptance. d254-queues-final.out passes all three120-frame paths with errors0/warnings0 and identical GPU/CPU bone pictures. d254-library-final.out passes the affected120-frame transparency/shadow replay and text649. Unused shader-interface/output performance warnings in that historical shader replay remain distinct from validation errors. No production DLL source changed:2.8.2/ABIeccaa3c3c47d32e4 remains active. All processes exited; no game/deployment/launcher/system/remote action.
- Native missing-depth baseline is now valid in d255-before.out: normal exits and errors0 for control/cold/warm;93/0/54 captures and composites respectively, with4/4/8 known shader-interface/output warnings. Cold and warm-null report missing-depth errors; warm stops compositing during the last40 frames. Original-library d254-original.out75 checks proves cold-null then restored depth also remains uncaptured, with all pixels checked and validation0/0. Continue D-255 against build/d255-plan.md, then remaining offline output-plan/pass/resolve/persistent-image/queue adapters. No stopping point at this commit.


## BUG-114: Standalone runtime missing source-register object (D-253; fixed offline)

- BUG-114 reproduced by the tracked standalone check_efmittl.py: d253-link-before.out fails LNK2019/LNK1120 on image_depth_reference before GPU execution. shader_commands.cpp uses the definition in source_registers.cpp, but build_runtime omitted its object; main production links happened to supply it through MATERIAL_SOURCES. The original DirectX11.vcxproj compiles CommandList/HackerContext into their common runtime. The Vulkan build now owns source_registers in the common runtime object list and removes its duplicate from MATERIAL_SOURCES. Compiler-only consumers filter their object subsets and are unaffected. No shader or shared-header implementation changes; source stays 2.8.2 / ABI eccaa3c3c47d32e4.
- d253-link-fixed.out passes both unchanged-library EFMITTL modes (356 checks each), installed inputs unchanged. d253-resources.out passes original INI GPU resources11288 and text649; core/synchronization validation is clean. d253-relink.out relinks tracker/layer/backend/recipe using verified unchanged D-252 objects and the corrected common object, asserting exactly one source_registers object per link; tracker666 and recipe402 pass. Four2.8.2/ecc stamps and d253-validation.out76/76 pass. All processes exited. No deployment/game/launcher/system/remote action.
- Original null-depth evidence is separate: CommandList.cpp12458 preserves an existing destination for a null source only with UNLESS_NULL. Installed EFMITTL uses copy_desc oD unless_null for layer depths. DX11 supplies retained/deferred lifetime, hazard tracking and one-context order; Vulkan must reproduce the resource history explicitly. d253-original.out runs the unchanged library on owned Vulkan GPU targets: cold-null frames have captures/composites0 and unchanged RGBA(.125,.25,.5,.625); warm-then-null frames keep captures/composites1 and RGBA(.78125,.0625,.125,.625). All64 pixels per frame,60 checks and validation0/0 pass. This is library/backend evidence, not native-layer replay or real-game evidence. Initial pilot builds failed the same missing symbol (one intermediate wrong source addition also failed); only d253-original-build-complete.out successfully builds that pilot.
- Continue native cold/warm missing-depth reproduction, then matched-Mod output-plan/pass/resolve/persistent-image/queue adapters. Do not infer an unconditional opaque fallback from absent depth and do not stop at this commit.


## BUG-113: Mod variants filter against original rather than actual depth (D-252; fixed offline in 2.8.2)

- D-251 committed/main fast-forwarded0a08037. BUG-113 reproduced in d252-before.out26/28 on2.8.1/ABIeccaa3c3c47d32e4: two clean normal exits, independent moved geometry and original/source color/depth correct, application shader filter correct, but both active/inactive filters select the wrong shader on the Mod triangle after depth unbind. Original HackerContext.cpp456 reread: ProcessShaderOverride reads the actual DSV before command lists. DX11 supplies retained/deferred lifetime, hazards and context order. Vulkan must snapshot that actual state for all Mod variants, including cache keys, without reevaluating it after lists.
- CPU baseline d252-contract-before.out661/666 confirms five missing before-list/nested-scope depth selections. The first build used an incorrect fixture enum DK_DRAW_INDEXED and failed before execution; DK_INDEXED fixes only that test. The valid baseline reuses verified same-ABI tracker objects except the changed character_backend test compilation.
- Source2.8.2/final ABIeccaa3c3c47d32e4: HuntingDrawScope snapshots actual classic or dynamic output depth before lists, scopes it across nested draws/dispatch, and propagates it with shader/hunting state to all three Mod variant routes. The merged-skeleton fast cache key includes the snapshot. Compute retains unknown/bypassed depth semantics. d252-pair.out ended without a completion record after an environment interruption. d252-pair-resumed.out built tracker/layer/backend with intermediate whitespace-dependent ABI397f8b2bbea3a324 and tracker666 passed, then recipe_test linking failed LNK1163 on native_targets.obj. After all build processes exited, trailing blank lines were removed and final ABI returned to eccaa3c3c47d32e4; no shared object layout changed from D-251. d252-final-pair.out recompiled every EFVK_ABI use plus native_targets.cpp, reused only other already-built identical D-252 objects, and successfully rebuilt tracker/layer/backend/recipe. Four2.8.2 final stamps, tracker666 and recipe402 pass. Failed GPU subset d252-gpu-fixed.out28/28 now selects both actual depth filters correctly while retaining independent pixels and clean validation. Full affected native-mod-depth area passes in d252-regression.out: Mod geometry132/merged-texture14/depth null92/hunting45/tracker666/VVL76/text649. Diff and cache retirement paths reviewed; all GPU processes exited. No deployment, real game, launcher, system or remote action.
- Continue the transparent-capture readiness investigation after this verified commit: the current compositor requires actual color0 and depth, whereas capture preparation currently tests color0 only. D-251 did not claim transparency without those targets. Reproduce the missing-target fallback against the original before changing it, then continue other offline output-plan/pass/resolve/queue adapters.


## BUG-112: Shared classic pipeline cache layouts omitted from the module ABI (D-250; fixed offline in 2.8.0)

- BUG-112 found in D-250 review: host native_target_bind now reads backend-created NativeClassicTarget/NativeClassicPipeline/PipelineVariant cache objects, but their complete definitions are outside the shared ABI closure. Original RunCustomShaderCommand2874/2940/2965 uses retained stable COM shader interfaces; DX11 supplies deferred lifetime, hazards and context order, whereas cross-DLL C++ layouts need an explicit version gate. d250-abi-objects-before.out1/4 confirms the omission (earlier exploratory header-only audit1/3). This is a build-contract defect, not an observed GPU failure. build/d250-abi-shared.py APPLIED after the initial area exited: complete cache object layouts are now in native_targets.h, which includes pipeline_variant.h; private preparation functions remain outside the ABI. Final source stamp050cd66ae13d56cf (after clarifying actual-output filter comments); audit4/4 passes. d250-final-pair.out completed in isolated build/d250-final-pair. After both build and corrected GPU processes exited, the four verified binaries were published to worktree dist only. Four2.8.0/ABI050cd66ae13d56cf stamps, final tracker651/651 and ABI audit4/4 pass. d250-abi-swap.out passes9/9: old ABI module refused without generation change, matching module accepted, completed exchange still renders color0/source902 with normal exit and clean validation. The old backend is preserved as build/d250-backend-abi11c2740f4953bedc.dll. Do not overwrite loaded DLLs or repeat already-passed unchanged rendering areas merely for the expanded ABI coverage.


## BUG-111: Cross-slot classic originals lost their own final layout (D-248; fixed offline in 2.7.8)

- BUG-111 caught before commit: isolated d248-cross-slot-before.out617/619 proves original-image layouts are incorrectly taken from the destination slot when an original moves to another vacated slot. Original SetResource10094 reread; DX11 retained/deferred lifetime, hazard tracking and context order leave image ownership independent of output slot. The Vulkan first End already performed that image's original implicit final transition. Applied d248-cross-slot-fix.py: locate the original by view identity across the complete original set, for both incoming transitions and host restoration. No observed GPU corruption or VVL failure is claimed. Final d248-final-pair.out completed with changed native_targets/tracker objects and verified unchanged same-ABI objects. Four2.7.8/ABI19a2b58b0100d8c1 stamps and tracker619 pass. Affected native-classic-attachments area passes in d248-regression.out: classic attachments173/classic outputs73/dynamic MRT39/validation76/text644. All processes exited; diff reviewed and memory synchronized for local commit/FF. No game/deployment/system/launcher/remote action. Continue the null-output pipeline compatibility adapter; the concrete next design is build/d249-plan.md, still UNAPPLIED.

## BUG-110: New bindings could activate ignored null-attachment resolve fields (D-245; fixed offline in 2.7.5)

- Original SetResource10094 replaces the RTV and does not perform a multisample resolve. DX11 supplies retained/deferred lifetime, hazard tracking and one-context order. Vulkan ignores ordinary resolve fields while imageView is null; D-242 allowed that valid original context but replaced only the view/layout/load/store, leaving ignored resolve data potentially active on a new single-sample view.
- Original source and official VkRenderingAttachmentInfo rules read. Isolated d245-resolve-before.out543/544 proves the resulting actual attachment still carries ignored resolve state. Normalize mode/handle/layout to NONE/null/undefined and clear resolve view metadata. Final d245-final-pair.out completed, four2.7.5/ABI8af8de0e24586cd2 stamps and tracker544 pass. Native-depth-only area d245-regression.out passes zero-color76/null DSV92/MRT39/tracker544/validation76/text639. All processes exited; diff reviewed and memory synchronized for local commit/FF. Initial zero-color GPU34/34 was already clean and is not a GPU reproduction of this defect. No real-game behavior or GPU validation failure is claimed.

## BUG-109: Initially null output contexts lacked pipeline sample proof (D-242; fixed offline in 2.7.2)

- Original SetResource delegates RTV compatibility to DX11's OM runtime, with retained/deferred resource lifetime, hazard tracking and one-context order. Vulkan requires each introduced color attachment's sample count to match the bound pipeline absent separately modeled mixed-sample adapters. D-242's newly allowed initially null slots do not provide an actual attachment sample count to infer that contract from.
- Original OMSet code and official vkCmdDraw sample requirements read. Isolated d242-sample-before.out513/514 accepts binding a single-sample view into an initially all-null context whose complete pipeline recipe has rasterizationSamples4. This is CPU preflight evidence, not observed GPU corruption or validation failure; the initial actual single-sample MRT11/11 already passed.
- Require the exact single-sample pipeline contract before any output event records, and set realistic explicit sample counts in valid CPU fixtures. Shared ABI remains8af8de0e24586cd2. Final changed-object pair/four stamps/tracker514/failed GPU11 and native-output-mrt area pass (MRT39, preserved41, absent27, null38, output52, validation76/text636). Caught before commit/deployment; no real-game verification.

## BUG-108: Output pass splitting lacked attachment-mapping proof (D-238; fixed offline in 2.6.8)

- Original SetResource preserves the DX11 pipeline and output slots, with runtime deferred/reference-counted lifetime, hazards and context order. Vulkan BeginRendering resets dynamic attachment locations to identity. The initial D-238 adapter accepted a missing/incomplete pipeline recipe and did not observe local-read mapping commands, so it lacked proof that restarting preserved the output contract.
- Isolated CPU d238-scope-before.out457/458 proves acceptance without any pipeline recipe. This is missing-proof evidence, not an observed GPU corruption or validation failure. The complete initial native-outputs area had already passed with ordinary default-mapped pipelines.
- Require a complete geometry/native recipe with the exact single-color dynamic rendering contract. Core/KHR attachment-location and input-index commands invalidate the recording's splitting proof; unknown static chains also refuse. No GPU command is emitted for refused plans. Final full pair/four stamps/tracker460 and focused outputs52/validation76/text630 pass; caught before D-238 commit/deployment. No real-game verification.

## BUG-107: Scoped compute writes were not visible to uniform aliases (fixed during D-235, 2.6.5)

- Original command-list Dispatch uses the DX11 context, whose runtime provides retained/deferred lifetime, hazard tracking and context order. Vulkan UNIFORM_READ is separate from SHADER_READ. The intermediate D-235 compute-only destination mask omitted the uniform access class.
- Valid isolated d235-uniform-before.out5/6: an unchanged application buffer is both a storage output and uniform input; the original single dispatch returns1, but three native dispatches return1 instead of3. Validation is clean, so this is actual GPU data evidence, not a reproduced validation error. No replacement descriptor is introduced by this fixture.
- Add UNIFORM_READ to the scoped compute destination access while retaining COMPUTE_SHADER execution scopes. The same original/core GPU cases now6/6, and the full alias matrix15/15 covers KHR, push and secondary. Full pair/area evidence is in CURRENT_STATE; the defect was caught before D-235 was committed or deployed. No real-game evidence.

## BUG-106: Native barriers crossed suspended rendering boundaries (fixed offline in D-232, 2.6.2)

- Original DX11 context supplies retained/deferred lifetime, hazards and context order. Vulkan disallows synchronization between suspended/resumed rendering. CPU370/376 proves closing/resume barriers and lost/manufactured proof; the initial GPU primary case already passes4/4 with6418 green pixels/clean validation and is not a reproduced GPU hazard.
- Preserve scope/proof across same-recording segments without boundary barriers; no proof is inferred across recordings. Final end/reset close state normally. Follow-up CPU378/380 exposed temporary layouts whose restoration could escape to another recording, including non-attachment images; these segmented transitions now refuse while already-readable layouts remain usable.
- Final tracker380 and scoped native-rendering-boundaries checks pass. Initial secondary GPU failure was a fixture inheritance-flags mismatch, corrected before final verification. Exact evidence and ABI are in CURRENT_STATE; no deployment/game evidence.

## BUG-105: Graphics uniform references assumed an unrecorded dependency (fixed offline in D-221, 2.5.1)

- CPU baseline299/300 (d221-before.out) accepts an application constant-buffer reference without a pre-pass dependency. GPU baseline14/16 (d221-gpu-before.out) shows inherited secondary replacement without parent proof; primary/push and immutable-file cases already passed. This is missing-proof/fallback evidence, not a reproduced GPU validation hazard.
- Original constant-buffer getters/setters rely on DX11 deferred lifetime, hazards and context order. Current alias flags cannot prove a past Vulkan barrier, especially across configuration/module activation. Both pass-begin paths now retain actual emission proof; descriptor planning checks it, pass end/reset clears it and secondary execution cannot import it.
- Persistent graphics uniforms retain the next-pass dependency requirement. Immutable files retain upload publication semantics. Corrected tracker310/310 and rendered colors16/16 pass; pair ABI/version and affected checks are recorded in CURRENT_STATE. No deployment or game verification.

## BUG-104: Candidate and retired backends overwrote active host dispatch flags (D-220, 2.5.0)

- Isolated CPU baseline294/296 (d220-before.out) reproduces shared Device.shader_draws/native_binding_order changes from shader_hooks_frame(...,false), and shader_draws clearing from character_stop on a rejected/retired backend. The old module could thereby disable current callbacks until a later presentation. Module-local discard state is separate.
- Original ReloadConfig holds its global critical section across configuration replacement. DX11 supplies deferred object lifetime, hazards and context order. Vulkan now publishes prepared flags through an explicit ABI-checked activation callback at initial installation and promotion, under host ownership; active frames refresh them. Candidate preparation/retired cleanup do not publish, and host teardown clears flags while detaching the active backend.
- Activation does not simulate Present or fire analysis/display callbacks. Exact builds and the verified scope are recorded in CURRENT_STATE. No game/deployment evidence.

## BUG-103: Persistent mutable descriptors lacked dependencies outside native plans (fixed offline in D-219, 2.4.9)

- During D-219 review, direct descriptor preparation/binding followed by the host's next compute call produced no memory dependency when that call had no native plan. Tracker291/292 in d219-dependency-before.out proves the missing entry-point behavior. Narrow-original-buffer-barrier GPU cases already pass8/8, and explicit sync-validation4/4; those observations are not represented as failing GPU evidence.
- Original HackerContext BeforeDispatch/Dispatch/AfterDispatch uses the same bound context even without matching commands. DX11 handles reference-counted deferred destruction, hazards and context order. Vulkan records mutable-buffer dependency state and explicit pre/post compute barriers, including secondary propagation and reset; upload owners remain retained. Existing one-queue, compute and sync2 boundaries remain.
- Corrected tracker294/294 (d219-dependency-after.out); final pair and affected GPU/upload checks are recorded in CURRENT_STATE. No real-game or deployment result.

## BUG-102: Hunting idle clearing and persistent counts diverged from the original (fixed offline in D-218, 2.4.8)

- CPU baseline278/280 proves that collections stayed populated before the first key and between successive idle frames; the overlay baseline2/4 has no counts after transient notices expire. Original HackerDXGI RunFrameActions and input.cpp use registered dispatch events, not only hunting keys, and clear every idle frame. Overlay renders counts before clearing.
- Append InputActivity35 through the existing bounded action queue, preserve the idle timestamp, and publish a bounded per-device count line independent of notice expiry. Backend preparation does not publish display or analysis callbacks until presentation. DX11 supplies deferred lifetime, hazards and context order; Vulkan retains existing overlay slot owners, fences and presentation dependencies.
- Final pair/tracker/planner d218-final-pair.out has version2.4.8/ABI70b005da5d190964 in all four binaries. Tracker286 and steady overlay8 pass; the affected hunting-overlay regression and exact scope are in CURRENT_STATE. No game or deployment claim.

## BUG-101: A later storage alias could invalidate a sampled descriptor's layout (fixed offline in D-216, 2.4.6)

- D-216 review found that a binding already in SHADER_READ_ONLY_OPTIMAL created no transition cell, so a later alias in the same plan could move it to GENERAL while its earlier descriptor still named the old layout. Tracker baseline263/264 in d216-conflict-before.out reproduces the conflicting plan.
- Original GetResource/SetResource retains views while DX11 handles deferred destruction, resource hazards and context order. Vulkan must preserve every descriptor's layout contract explicitly. NativeImagePlan now reserves all used cells, including those needing no barrier; plans and active scopes reject conflicting reservations before GPU work. A sampled descriptor may share GENERAL already selected by an earlier storage reference.
- Final pair/tracker/planner d216-final-pair.out uses ABI0fb4637af70f4449. Tracker264/264 and all affected native-storage-images checks pass in d216-final-regression.out (CURRENT_STATE). No game/deployment result.



## BUG-100: Native descriptor cloning wrote standalone immutable samplers (fixed offline in D-209, 2.3.9)

- The new native array/cube fixtures reached native execution but passed only3/12 (d209-after.out): VUID-VkWriteDescriptorSet-descriptorType-02752 rejected the cloned descriptor update, followed by invalid descriptor dispatches. Upload/cache readback itself passed394/394.
- Original CustomShader Run saves/restores separate sampler states through PSGetSamplers/PSSetSamplers (CommandList.cpp2917-2918,2960). DX11 handles reference-counted deferred lifetime, hazards and context order. Vulkan standalone immutable samplers are initialized by descriptor allocation and must not be written through vkUpdateDescriptorSets.
- Skip only standalone immutable sampler writes for allocated sets. Keep image writes for combined samplers and explicit initialization for push descriptors. Existing retained layout/sampler ownership remains. The fixture now explicitly initializes its immutable push sampler as Vulkan requires.
- Final host/module/tracker/sandbox built in d209-sampler-pair.out, push fixture corrected in d209-push-fixture.out. Native shapes33, bindings48, file images46/files42, character115/377 and validation76 pass; application-image62 non-exchange checks plus corrected exchange14 pass. The exchange fixture must wait for native execution, not merely shader binding. Exact artifacts are in CURRENT_STATE.

## BUG-099: Exchanged code relocated relative Mods/settings and shader paths (fixed offline in D-207, 2.3.7)

- The 2.3.6 relative-path exchange fixture passes only 3/7: the new module activates but loses its
  configured Mods and ShaderFixes. module_folder used the exchanged DLL's own address; additionally,
  load_shader_fixes interpreted relative m2_settings against the process working directory. The
  original IniHandler resolves both configuration and relative shader paths from migoto_handle.
- D-207 makes the module's existing directory helper return the host installation identified by the
  host log callback. All backend source paths keep that base, and ShaderFixes uses absolute_folder
  consistently. Tool lookup remains separate. No module ABI fields, user config or system state change.
  CURRENT_STATE records the verification scope; no real-game exchange is implied.

## BUG-098: Exchanged native shaders could not locate the deployed validator (fixed offline in 2.3.6, D-206)

- A backend copied to a test folder still ran native image commands, but its ShaderFixes had loaded=0 and warned Cannot load native SPIR-V validator. load_shader_fixes used module_folder for the validation DLL. Original ReloadShader uses the installed runtime/device; DX11 supplies deferred resource lifetime, hazards and context order independently of configuration reload.
- Use the existing owned_tools_directory fallback to the loaded host's toolchain. On the corrected pair, native game shaders pass 92/92. The dedicated module-exchange check passes 7/7: active new module, one validated ShaderFixes module with actual binds, a completed post-exchange image, correct GPU pixels and clean Vulkan validation. A second apparent pixel failure was an expired test-overlay notice; trigger capture after observed shader binds instead of an arbitrary two-second delay. No game swap occurred.

## BUG-097: Frame analysis dropped known output attachments without a primary shader pipeline (fixed offline in 2.3.4, D-204)

- D-202 images() returned before gathering render-pass attachments when no current graphics pipeline existed; reflection failures had the same premature return. A clear-only pass and a primary that only executes secondary draws both legitimately lack that pipeline. Dedicated baseline 9/15 produces zero owned target exports in those two cases while the ordinary-graphics control passes. This is sandbox evidence, not a real-game diagnosis.
- Original DumpRenderTargets reads output views independently. D-204 separates pass attachments from descriptor reflection and preserves retained source/staging lifetime, explicit barriers and GPU completion. Known stored targets still need transfer usage and all existing capture guards. Final targets 22/22, GPU resources 55/55, geometry 65/65 and Vulkan validation 76/76 pass; target/resource/geometry fixtures also enable synchronization validation. No game acceptance is implied.

## BUG-096: Native color texture shaders rejected DXC depth uncertainty and push layouts (fixed offline in 2.3.1, D-201)

- Native file-image tests exposed two preexisting variant boundaries: OpTypeImage Depth=2 from ordinary DXC Texture2D did not match the application's Depth=0, and native-only twins refused retained push descriptor layouts. Original DeferredShaderReplacementBeforeDraw/SetShader uses the device context with its current resource bindings; DX11 supplies lifetime, hazards and context order. Vulkan keeps those owned layouts and permits 0/2 compatibility only when neither module uses a depth-reference opcode. Other view types/access classes and non-native variant restrictions remain.
- Final actual pixels and compute images 46/46, native shader regression 92/92, character 115/377 and tracker 206 pass. A separate F10 test error was the overlay readback's one-shot capture; it now triggers only after reload. VVL 1.4.321 lacks the YCbCr check in its immutable-sampler null-image validation: the null fixture uses a mutable sampler without disabling validation.

## BUG-095: Native-only and empty configurations were refused by module exchange (fixed offline in 2.3.0, D-200)

- The native-file swap fixture fails before staging: the backend says no character Mod exists even though its ShaderOverride generation runs. character_usable/character_ready required a usable character controller. Original ReloadConfig/LoadConfigFile registers the installation and includes without a character requirement; DX11's resource lifetime, hazard and single-context guarantees are separate from this initialization gate.
- A valid native-only folder program or existing empty-folder fallback now supplies usability/readiness when there are no character controllers. Nonempty invalid character sets retain the old rejection policy. GPU swap/empty-swap pass within native files 42/42 in d200-swap-regression.out; character checks 115/377 also pass. No game exchange occurred.

## BUG-092: Notices flickered: presented images went without them when more than three presentations were pending (the user in the game 2026-11-11, 2.1.3; fixed in 2.1.4, sandbox only)

- Seen: the user, in the 2.1.3 session: while refreshing (F10) the error texts flicker all the time. The status of that session: `notices.drawn` 4211 and `busy` 1290 (457 and 63 in its first minutes): about a quarter of the presentations that had a notice went out without it. The game ran with frame generation: 206 presentations per second for 52.6 rendered frames.
- The original (Overlay.cpp `DrawOverlay`, called from the Present hook in HackerDXGI.cpp before the real Present): the notices are drawn with the immediate context onto the back buffer at every Present while one is shown. What D3D11 does for it: the context takes those draws in order behind the frame's, and the driver keeps what they use alive (the sprite batch's dynamic buffer is renamed at each map). 3DMigoto owns nothing whose reuse depends on the GPU having finished an earlier frame.
- The layer: the overlay's work for one presentation is a command buffer of its own that waits for the presentation's semaphores, with a fence and a text image (a slot). There were three, and with none free the presentation went out as it was (no wait in a presentation, BUG-062). The work on an image waits for that image's rendering, so as many are pending as the swap chain has images in flight; with a frame generator several presentations arrive for one rendered frame.
- Fix (`layer/src/notices.cpp`): a slot is made when none is free, up to 32 (more than a swap chain has images; past it `busy` counts as before), the first free one taken so that the text images number what was pending at once; the text images are released when no notice is shown. Status: `notices.slots`, `notices.text_bytes`.
- Failing first: `sandbox/owned_present_test.py`, notices_on_every_presentation, with the sandbox's new `--present-burst 5` (five further images presented behind a frame made about 300 ms long on the GPU by blits, counted at the start: 1023 here). The build before: 24 drawn, 24 without (`busy` 24). Now: 42 drawn, `busy` 0, six slots, 786432 bytes of text images while shown and 0 after, validation clean, through both entries (14 checks).
- A host-held gate would have made the scene exact, and is not valid Vulkan: setting an event a pending command buffer waits for (VUID-vkSetEvent-event-09543), and a binary semaphore wait that depends on an unsignalled timeline wait (VUID-vkQueueSubmit-pWaitSemaphores-03238, VUID-vkQueuePresentKHR-pWaitSemaphores-03268). Hence the frame that is slow in earnest.
- Not checked in the game: the fix is in the layer's DLL, which is replaced only while the game is closed.

## BUG-094: EFMITTL's translucent parts cast their full shadow on Vulkan (the user in the game 2026-11-12, 2.1.7; FIXED in the work tree, D-185, sandbox only)

- Seen: Zhuangfangyi Summer Collection 1.1's see-through shirt and sarong drawn see-through, but the skin under them darker and patchy, unlike DX11 (the user's two screenshots, Vulkan and DX11).
- Cause: the library's shadow vertex groups were never recognised on Vulkan, so its CommandListShadow took its branch for an unrecognised pass and drew the full shadow (docs/efmittl-transparency.md listed it as a limit).
- Fixed by D-185 (sandbox): the shadow is drawn by the opacity as the library's shadow shaders do. To see in the game.

## BUG-093: A Mod whose LOD components share bone spans, or with a LOD 0 remap, is left out whole (the user's Yvonne, 2026-11-12; FIXED in the work tree 2026-11-12, sandbox only, not deployed)

- Seen offline 2026-11-12 08:30 on the installed folder, on the 2.1.4 binaries and on the work tree's alike: `Yvonne Ensnared Heart 1.8\yvonne.ini` (changed 2026-11-12 00:11; the folder holds `backup-20261112-before-shared-lod-spans`, and perlica's a `backup-20261111-before-shared-lod-spans` with tools named share_lod_spans.py, patch_ini.py, validate_shared.py) is not loaded: "Skeleton components overlap". Perlica, changed the same way the evening before, loads. Not seen in the game: the 2.1.3 session was before the change.
- The Mod now: six components, 225 bones. Components 0 to 3 at 0+120, 125+38, 163+49, 212+13; component 4 at 0+116 and component 5 at 117+8, inside component 0's span (the LOD 1 parts share the LOD 0 part's bones); component 0 has a LOD 0 remap file and no LOD 1 entry, the others a LOD 1 remap.
- The layer refuses each of these at load: `read_skeleton_plan` ("Skeleton components overlap", frontend/xxmi/skeleton_plan.cpp:53), `discover_contract` ("Original components overlap", "Original component has a LOD0 remap or no LOD1 assignment"), and the package format (components disjoint and in order). The review's R-04 lists them.
- The original: the Core's pools give each component an offset and a count into the merged skeleton and a remap per LOD; nothing requires the spans apart, and a remap at LOD 0 is one more entry of the pool. To read first: the Core's MergedSkeleton.ini (how AttachComponent copies a component's bones, what a null and a file remap mean per LOD), then what the layer's skeleton needs of disjoint ranges (the import jobs, coverage per component, the bone masks of streams).
- While it is open: the vk01 replay "captured frame with a library's texture slots" and "installed Mods offline plans in every key state" are red on this folder.
- Fixed (D-175): components may share bones and either LOD may have a remap. Failing first: the scene `shared_spans` of `sandbox/runtime_test.py` (the fixture written as the tool writes a Mod: a fourth component on component 0's bones with the LOD 1 remap, component 0 with a LOD 0 remap), 2 of 10 checks on the build before, 10 of 10 after, with the bones imported on the device and on the CPU. The installed Yvonne loads offline again and plans in every key state, and the vk01 replay passes. Not seen in the game.
- Left of R-04 after D-177 (2026-11-12): the reading is static; a component chosen by a value computed at draw time is not seen at load.

## BUG-091: With two Mods of one character and one namespace installed, a part of one kept the game's look (seen in the game 2026-11-11, 2.1.3; FIXED in the work tree 2026-11-12, D-178, sandbox only)

- Seen: the user added `last rite Ethereal Prayer 2.3` (`namespace = last rite`) beside the installed `lastrite` (its `last rite.ini`; the folder is gone now, its namespace was not read) and pressed F10 at 19:37:10. The exchange completed for 20 packages; 0.1 s later the package `lastrite` logged "INI plan: INI image not in prepared resource catalogue" and the notice said that part keeps the game's look. The user removed the old folder and pressed F10 at 19:37:39; the notice did not come again.
- The message is BUG-076's (`layer/src/character_program.cpp`: a planned image that is not in the package's prepared catalogue). A guess, not checked: both INIs define the same resource names in one namespace, the shared INI runtime keeps one definition, and the other Mod's package is asked for an image of a file it never prepared.
- To do, offline: read what the original does with two INIs of one namespace and the same TextureOverride hashes (IniHandler.cpp: duplicate sections and duplicate hashes), then two copies of a small Mod with one namespace in the Mods-folder scene, then the same result as the original's. Counters of the moment are lost (the next F10 reset them); no frame was captured.
- Fixed 2026-11-12 (D-178): the message comes from a plan that binds a texture whose file is not under the package's folders, which a section of the other Mod's INI for the same draw does. Reproduced first with the exact message: `sandbox/runtime_test.py` other_folder_image (a section of `props\prop.ini` binds `..\fixture\red.dds` at the scene's unmatched draw), 2 of 3 before, 3 of 3 after; the picture shows the borrowed image. Not seen in the game.

## BUG-090: `vs` as a value reads 0 in a character generation and 1 under the original with the EFMI Core (found by the judge's sweep 2026-11-11, FIXED 2026-11-12 in the sandbox, no installed Mod affected)

- The original's `process_shader_filter`: a bound shader that a ShaderOverride or ShaderRegex section matches reads that section's `filter_index`, or 1 without one; 0 only when no section matches. The EFMI Core's main.ini has `[ShaderRegexEnableTextureOverrides]` with `shader_model = vs_4_0 vs_4_1 vs_5_0 vs_5_1` and no pattern, which matches every vertex shader, so under the user's DX11 installation `vs` reads 1 for every draw. The judge's d3dx.ini has the same kind of section, and its sweep line `if vs == 0` takes the other branch under the original.
- The layer: the character Core's extension answers `vs`, `ps` and the other stages with 0 for a bound stage (`frontend/xxmi/character_core.cpp`, plan_shader_queries: "no ShaderOverride is active"). For `ps`, `hs`, `ds`, `gs` that is the original's value (the Core's ShaderRegex names vertex shaders only); for `vs` it is not.
- Installed Mods: none uses `vs` or `ps` as a bare value (searched 2026-11-11).
- Not fixed here: the shader queries are the other session's part of this round's split (BUG-088). To do: `vs` as a filter value reads 1 where the generation has the Core's patternless ShaderRegex, from the parsed section, not a constant. The sweep line `if_vertex_shader` stays recorded as not judged by the scene.
- Fixed 2026-11-12: `vs` for a bound vertex shader reads the filter_index (or 1) of a patternless ShaderRegex section over vertex shader models, from the generation's own sections (`RuntimeVertexShaderFilter`), else from the installed Core's main.ini, which the generation does not load (`CharacterProgram::core_vertex_filter`, `RuntimeConfig::vertex_shader_filter`); the character Core's extension and the plan's fallback answer it (`DrawPlanAdapter::vertex_shader_filter`). `ps` and the other stages keep 0. Checks failing first: the live scene's lists store `vs` in a replaced draw (`$vs_value`, 0 before, 1 now); the sweep's `if_vertex_shader` is `same` now, with the installation's d3dx.ini beside the judge's Mods folder on the layer's side as on the original's.

## BUG-089: A twin bound in a recording drops the character packages' owners the recording has not used yet (read 2026-11-10; reproduced and FIXED in the work tree 2026-11-12, sandbox only)

- `character_bind` (D-133, `character_backend_twins.cpp`) keeps the twins a recording bound in an owner of its own (`TwinOwner`), taken with the generic `owner_for` (`command_owner.h`). That function removes every other backend's owner the recording has not used yet. A character package's owner (`CharacterCommand`: its snapshot arenas in that command buffer) is unused from the reset of a recording until the package's first draw in the next one. `character_owner_for` (`character_backend_record.cpp`) was written to keep exactly these: dropping them destroyed the arenas and had the workers make new ones for every recording (D-099, 142 us per draw in the 1.6.8 session).
- Expected effect, only while a discard gate is set (RabbitFX's anti-dither on, `g_dither_off`): in every recording, the first bind of a dithered pipeline before a package's first draw drops that package's batch from the command buffer; the batch is destroyed there, on the recording thread, the package takes another at its draw (`command()`), and its worker makes a new one. With the gate unset `character_bind` returns before it takes an owner, so nothing changes then.
- Not reproduced: this is a reading of the code, and nothing was run for it. To check first: the status of the live INI scene or of a replay with the gate set against unset (`batches_made_at_draw`, `available_batches`, the arenas a worker makes per recording); in the game, frame pacing with the `\` key on, which TASKS lists as still to be seen (2.0.6).
- Not fixed in the split, which moves code only (D-160). Its comment on `character_owner_for` also says an owner goes "when it is not this code's", while the code keeps an owner that is no `CharacterCommand`.
- Also noticed, harmless: `character_backend.h` declares `character_frame(CharacterBackend&, bool)`, which nothing defines (the module's function takes the presentation as a third parameter).
- Reproduced 2026-11-12: a package's status counts the snapshot batches made (`batches_made`). In the live INI scene's Mods folder with RabbitFX's gate set, the fixture made 33 batches in 33 frames, one per recording (32 twin binds); with RabbitFX gone, 3 in 6 frames.
- The original: D3D11 keeps what a pending command list uses whatever else is bound; nothing of one binding is released by another.
- Fixed: `character_owner_for` (the rule that keeps a package's owner while its backend still uses the recording, gone only after kIdleRecordings resets unused) is shared in `character_backend_internal.h`, and the twins' bind takes it instead of the generic `owner_for`. The scene's check (`batches_made` at most 6 in 20 or more frames with the gate set) fails before and passes after. Not seen in the game (frame pacing with the `\` key on).

## BUG-088: The EFMITTL installed on 2026-11-10 refuses every draw it runs on (found by the suite 2026-11-10; fixed 2026-11-11 in the sandbox, not checked in the game)

- The user's update of Mods\EFMITTL\translucency.ini (2026-11-10 13:15) asks `ps->srv_type(n)` and `srv_dimension(n)` for slots 8 to 21 and in a loop below t24. The layer refused a declaration query on a slot the draw's SPIR-V has no register for (D-124), and a refused query refuses the draw: in the replay with a library call no translucency layer was captured and 226 plans were refused. The binaries of 2026-10-27 fail the same step with the new file, so every build so far is affected in the game, not only this round's.
- Fixed (D-159): such a slot below t60 is not declared, as the original answers a slot the DXBC does not declare; and a buffer of 32-bit words (structured or raw under DX11, the same SPIR-V) no longer refuses a condition that is the same with either answer.
- The third cause, fixed (D-170, 2026-11-11): the library's `[CommandListSlotStep]` reads the game's texture at each declared slot, `ps-t[$slot_i]->Format`, `->Width`, `->Height`, `->Mips`, to tell the diffuse and normal maps by their formats. The plan path answered no property of any resource ("Unadapted original buffer query (target type 4, evaluation mode 0x200)"). It answers them now as the original's getters, from the host's description of the image and view the draw binds and from a Mod's own file. In the replay with a library call the 220 refused plans are gone and the library's scan finds Yvonne's lit slots as 17, 18 and 19, which is what the library's own notes give for cloth under DX11.
- What the replay showed next (2026-11-11), none of it a refusal:
  - The Yvonne the user installed on 2026-11-10 13:46 no longer calls the library's translucent draw (it calls SetTextures only), so the suite step looked for something the Mod no longer does. The step now replays ardelia (vk11), which does; Yvonne's scene is a step of its own for the texture slots.
  - The fourth cause, fixed (D-171): the library's shader groups. The bridge gave only the lit vertex group's `$vs_kind`; the new library also has a lit group for skin, an outline group and pixel groups that answer the diffuse slot, and its vertex groups clear its per-draw marks. Parts drawn with the skin's lit vertex shader looked like no lit pass, and the library drew them opaque. The bridge now runs the groups' own lists, chosen by rules read off SPIR-V and checked against the groups' patterns on the paired catalogue.
  - The fifth cause, fixed (D-172): the library has a TextureOverride section since 2026-11-10, and the folder generation, applying a rule made for generations of one Mod, recorded no Mod as calling it. The renderer compiles the library's shaders from the called library's folder, got none, and every capture failed ("HLSL source and working directory must be absolute").
- Evidence (sandbox, 2026-11-11): the replays of last rite (vk10), ardelia (vk11) and Zhuangfangyi (vk08) with the installed Mods: 113, 112 and 113 captures and as many composites in 120 frames (one per frame once the Mod is up), no refused plan, Vulkan validation 0 errors; on the build before D-171 and D-172 none of them captured. Yvonne's scene (vk01): no refused plan (220 before D-170). The suite step "captured frame with a library call" passes.
- Not checked in the game. To see there: the translucent parts of last rite, ardelia and Zhuangfangyi drawn see-through from the first frame, the library's `$fallback` 0, the translucency status (captures, composites, no error), and SetTextures putting the Mods' textures at the slots the library finds (the outline's colour from the Mod's diffuse).
- Until then the suite step "captured frame with a library call" is red with the installed library (on the binaries of 2026-10-27 as well), and in the game this EFMITTL leaves the draws it hooks to the game on every build. The EFMITTL owned-transparency step (the project's own copy of the earlier library) passes.

## BUG-087: `ResourceX = ref ps-tN` took the game's texture after the lists had bound the slot (found by the judge's sweep 2026-11-10, fixed)

- A list that binds `ps-t0 = ResourceRed`, saves the slot (`ResourceSaved = ref ps-t0`), binds something else and puts the saved one back drew with the game's texture: the plan's reference to a pixel slot was always the application's texture of the matched draw. The original reads the slot as it is bound when the line runs (ResourceCopyOperation::run).
- Fixed: the plan keeps what the lists bound at each pixel slot (`pixel_held` in draw_plan.cpp: a declaration, null after an unbind, the stand-in of another application slot) and the reference holds that; the application's texture where the lists bound nothing. Sweep line `save_restore_pixel`: red as the original (grey before). Sandbox only.

## BUG-086: A matched section's post list ran after the pre list bound an index buffer of its own (found by the judge's sweep 2026-11-10, fixed)

- The original reaches a TextureOverride's post list by checking again: `CheckTextureOverride = ib` stands in both phases of the list that checks, and the post phase looks up the index buffer bound by then (CheckTextureOverrideCommand::run, CommandList.cpp:1389). After `ib = ResourceX` that is the Mod's own buffer, which has no hash and meets no section, so the post list does not run. The layer ran the post lists of the sections matched before the draw. Sweep line `post_draw`: 2 primitives under the original, 3 in the plan.
- Fixed in DrawPlanAdapter::execute_matches: the post lists run only when the pre lists bound no index buffer. No installed Mod is affected today (209 `post` lines in the Mods folder, none in a TextureOverride or CommandList section). Sandbox only.

## BUG-084: A Mod that assigns no LOD 1 remap was left out whole (found 2026-11-10; fixed on engine-work, checked offline)

- The installed Mod lastrite assigns only `Pool_MergedSkeleton_Component_LodRemaps[c * levels + 0] = null`. Discovery refused it: "Original component has a LOD0 remap or no LOD1 assignment". The original Core binds nothing for a pool element no line assigned and takes no remap (MergedSkeleton.ini: `cs-t1 = ref Pool...`, `z0 = @cs-t1 != 0`). Fixed in load_palette_files: unassigned is no remap. check_mod_states.py on the installed folder: NOT LOADED before, every section plans after. A LOD 0 remap from a file still refuses the Mod.

## BUG-085: The owned GPU runtime's test step is red since D-155 (found 2026-11-10, fixed the same day)

- scripts/check_resource_commands.py on c69c1b6: "FAIL after 119 checks: GPU commands cannot run without explicit host scope". The step asserts that a Present with a refused command fails; since D-155 the command is skipped and the Present goes on. About 16 such assertions; resource_gpu.exe was last built before D-155. Nothing after check 119 had run since.
- Fixed: the assertions state the per-command rule (the helper `declined` in sandbox/resource_gpu.cpp: the Present goes on, the generation says what it did not do, the scope submits its other work or is dropped when the command failed after recording, and the next scope runs). 11291 checks pass, validation clean; of the declined commands 17 scopes went on and 4 were dropped. Two assertions of the INI runtime test and one of the operand cases said the old rule as well.

## BUG-079: The INI execution matrix step is red since D-142 (found 2026-11-10, fixed the same day)

- `scripts/check_ini_execution_matrix.py` on the build of 2026-10-30: 32 classes, 41 probed uses, 39 failures. `docs/ini-execution-matrix.json` (2026-10-18) expects the refusals that D-142 to D-155 turned into plans or no-ops ("Planner supports direct indexed draws only" is in no source file).
- The step is in run_all's `ini` group; the verification lists of those decisions do not name it. The judge step is in no group (docs/architecture-review-2.1.md, V-03 and V-04).
- Fixed (P3-0): the 11 uses state what the layer does now (a decision `degraded`: the command does nothing, with the warning it leaves, which the check requires); 0 failures. The judge is in the `ini` and `owned` groups; `--only quick` (text, units, evaluation, matrix, judge) takes about a minute.

## BUG-080: A failed activation leaves the character invisible (found by reading 2026-11-10; reproduced with a test hook and fixed, sandbox)

- `activate()` sets `activated` once; the worker returns when the mesh or the first batch cannot be made or on an exception (character_backend.cpp:1062, :1083, :1090); `ready` stays false and `record()` skips every matched draw (:1898). Neither the Mod's mesh nor the game's until the game restarts. L-01.
- Fixed (P0-7, D-158): a worker that ends without `ready` marks the package failed; `record()` then leaves the draw to the game, counts it (`activation_failed`) and starts the activation again after a pause that grows with the failures. Live INI rendering: with EFMI_VK_TEST_ACTIVATION_FAILS (test executables only) the counter rises, and once the hook is cleared the Mod draws (`runtime-reactivated.json`). Not run in the game.

## BUG-081: After a folder refused at start, F10 does nothing (found by reading 2026-11-10; reproduced in the sandbox and fixed)

- The reload request is taken from the folder program only (character_backend.cpp:3580-3581); a refused first load has none. The original always reloads. L-02.
- Fixed (P0-8, D-158): the host watches F10 itself while the folder has no program and reloads. Live INI rendering: every character Mod disabled, F10 (an empty generation), the Mods enabled again, F10: the Mod draws. Not run in the game.

## BUG-082: One failed owned command ended the folder's owned GPU work until F10 (found by reading 2026-11-10; fixed on engine-work)

- `GpuResourceRuntime::failed_` is set by any exception in a resource command, by `execution_failed()` and by `discard()` (gpu_resources.cpp:636, :153, gpu_resources.h:50) and is never cleared; `begin` and `finish` then throw. Made for one Mod's generation (BUG-017); the folder generation has one runtime for every Mod (D-131). The per-command rule of D-155 catches `Refused` only (draw_plan.cpp:1604); another exception ends the Present and sets the latch. R-12, R-13.
- Fixed (P0-4): the failure is the scope's. A command that fails before recording GPU work (ResourceCommands::recorded) leaves the scope as it was; one that fails after recording drops that scope's work, is not run again, and the next scope starts clean (GpuResourceRuntime::failed, discard and suspend clear it; OwnedPresenter::finish discards a failed scope). Every command of a Present, Constants or key list is on its own at every depth (BridgeCommand::run), whatever it raises. Checked by resource_gpu's declined checks and the judge's `failure` case; not yet in the game.

## BUG-083: RegisterMod refused a Mod that requires more than EFMI 1.48; the installed Core is 1.49 (found by reading 2026-11-10; fixed on engine-work)

- character_core.cpp:465-470 against `global $version = 1.49` in Core\EFMI\main.ini. No installed Mod requires more than 1.48. R-06.
- Fixed (P0-2): the native RegisterMod compares with the installed Core's `$version` (CharacterProgram::core_version reads its main.ini) and sets `$mod_id` 1 or -1 as the original's list; a Mod requiring more than the installed Core stays off and is told. check_mod_containment.py: a Mod requiring 1.49 draws, one requiring 9.99 does not and is told (both failing before).

## LIMIT-048: Remaining native route boundaries (D-187 to D-217, docs/vulkan-native-mods.md)

- D-205 adds recording-local image layout facts. D-206 uses them for retained optimal color 2D sampled application images, persistent descriptor refresh/fallback and temporary layout restoration on one application graphics queue with synchronization2 disabled. An original application's statically accessed descriptor can establish a valid local precondition. D-210 extends exact sampled array/cube shapes and partial mip/layer ranges with per-cell transitions. D-216 checks reflected numeric classes and adds exact typed compute storage 2D/array views, writable GENERAL transitions and conflict checks for all planned descriptor layouts. D-217 adds formatless read/write queries with enabled device/per-format features and rejects ShaderFix access expansion. Unknown fresh-recording layouts, other queues/signal scopes, unknown storage provenance, mutable file images, graphics writes and unresolved attachment overlap remain explicit boundaries; recording order is not a global synchronization graph.

- At a draw no Mod section made the Mod's, D-196 supplies explicit `vk_binding` aliases and native descriptor metadata queries/fuzzy matching for all classic stages. D-197 executes explicit direct draws/dispatches and caller replay (including indirect/count), preserving pre/original/post order. D-198/D-199 add retained allocation chains and persistent native buffer references/bindings through ordinary/template/pushed descriptors. D-200 adds immutable file buffers and null descriptors using the enabled feature, including publication ownership after module exchange. D-201 adds published sampled 2D file images and native-only pushed-layout shader twins; D-209 adds exact sampled DDS array/cube views through a distinct upload mode, with immutable sampler cloning corrected. D-211 adds R8/R16/RGBA16 UNORM DDS and aligned mip/layer staging on both upload paths. Remaining: broader application image layouts/ownership across recordings and queues, mutable file resources and additional DDS/scalar formats, exceptional descriptor flags, and cross-queue application hazards. Read-only published uploads support queues of the tracked graphics family; moved application resources still require one application queue. Graphics storage writes, physical pointers, transform feedback, automatic/stream-output draws and explicit indirect-resource commands are not implemented by the call-only step. GPU-writable resources have no recording-time content hash.
- D-190 implements native graphics shader replacement by original SPIR-V hash, F10, last-good reload and offline HLSL compilation. It requires tracked graphics recipes, matching single entry points, supported capabilities and compatible resource/stage interfaces. Unsupported binding flags/extension state and immutable samplers/layouts with custom allocators are refused. D-192 confirms empty Mods supports both initial files and F10 through the existing host fallback; the earlier INI requirement was an incorrect limitation statement. D-193 adds native compute replacement with owned classic pipeline recipes, matching fixed local sizes and device-limit validation; unsupported pipeline flags/extensions and specialized local sizes are refused. D-195 adds GS/HS/DS recipes and hunting, preserving primitive/patch/invocation contracts; all six classic shader stages now have sandbox replacement coverage.
- D-191 implements native `depth_filter` by captured subpass attachment presence, including filter-only F10 revisions. `model` remains an explicit offline HLSL compiler option; the INI key is reported unused. D-192 adds `pink`/`original` marking; pink requires a compatible float4 location-0 output. Custom shader passes keep their own shader, and `mono` has no stereo separation to change.
- Hunting (D-189 added index and vertex buffers, render targets, the `hunting` value, dumps, frame analysis's log): D-193 adds compute hunting/replacement and dispatch logs; D-194 loads installation hunting keys even with no Mod INI, using the configured Core; D-195 adds geometry/hull/domain keys and dumps. Render targets are only known where a draw is recorded inside its render pass instance, D-202 frame analysis exports completed GPU descriptor-buffer and owned image copies for one-time recordings on one unprotected application graphics queue with synchronization2 disabled (the real game's two queues remain outside it). D-203 adds end-of-pass IA buffers and metadata; D-208 adds GPU indirect arguments/counts and associated geometry uses, without inventing GPU draw counts. D-212 exports bounded mip/layer view cells and exact framebuffer/multiview attachment layers, and retains classic multiview recipe state. D-213 applies global original analyse_options resource selection, including log-only and F10. D-214 adds requested DDS/JPEG/binary/text/description exports from completed staging beside canonical raw files; unsupported typed conversions are explicit. D-215 adds held sessions/release, frame IDs and partial/F10 log preservation. Other control flags, full typed IA text and per-draw analysis commands remain. Dynamic vertex input and untracked slots remain separate; swapchain/depth images, image-descriptor arrays and unsupported resource/descriptor extension state remain refused. The 60-second timeout counts hunting keys only; the original counts registered input-action events, including Mod keys, and clears on every idle frame before/after the first key. D-218 original-source audit also found that the current timestamp reset after an idle clear incorrectly postpones the next clear. The overlay shows a notice per key, not the original's steady `VS:n/m PS:n/m` line.
- A module created inline in a pipeline (`VkShaderModuleCreateInfo` chained into a stage) or a shader object has no hash here; none was seen in this game.
- Nothing of it has run in the game.

## LIMIT-047: What makes a Mod a Mod, and the all-or-nothing plan (2026-11-10, docs/architecture-review-2.1.md)

- A character Mod is an enabled INI with a section named `[CommandListInitializeMergedSkeleton]` whose lists have the exporter's shape (R-03, R-04); every other INI's mesh sections matched no draw (since D-173, 2026-11-11, those of the INIs of a character Mod's folder are matched; those of an INI outside every such folder since D-174, 2026-11-12, in a package without a skeleton; a mesh such a section draws since D-176). A replaced draw's lists run all or nothing against six plan operations (R-01), and the plan conversion refuses vertex slots from 4 on and draws without the blend-index edit (R-08). A refused folder or a Mod left out at the first load is logged, not shown (R-11). 64 packages (L-03).

## BUG-077: A broken buffer file dropped the whole Mod (fixed in the 2.1.2 tree)

- Found by D-137's classification of refusal points (2026-10-28), shown failing by check_mod_degradation.py on a copy of an installed Mod: a mesh file that is not there refused the draws binding it ("INI mesh not in prepared stream profile"); a file one byte short of whole elements, or one file declared twice with another stride, left the whole Mod out of the folder generation ("Mesh size or stride invalid", "One mesh file declared with two meanings").
- The original makes a buffer per [Resource] at first use; one it cannot load stays null (CustomResource::LoadBufferFromFile), a draw without an index buffer or with a null position buffer reads index or position zero and shows nothing, an element past the buffer's end reads zero, and two declarations of one file are two buffers.
- Fixed: a missing or empty file gives no stream and binds nothing (whoever declares it: the original resolves a missing file to its own folder); a draw whose index or position binding is nothing is left out, the list's other draws go on; a last partial element reads zero (CharacterSource::file_bytes, the rest of the stream zero on every read); streams are keyed by file, kind and stride. A list may hold 1024 draws (was 64).
- The program's cache of distinct draw lists refused past 1024 lists or 16384 draws (a Mod whose keys or Present reach more binding states; check_mod_degradation's counter variant refused at frame 196). Now the cache starts again: lists, draws and pixels cleared under the plan lock and the plan reported as added, so the backend rebuilds what it keeps by draw index (materials, variant lookups), as it does for any added draw.

## BUG-076: A texture file the INI names but the Mod does not ship refused the draw (fixed in the 2.1.1 tree)

- 2026-10-28, in the game (2.0.8 development build, user-started): tangtang Ramune v1.9 drew its arm in broken strips. Status: refused_state 18579 of 92767 matched, "INI plan: INI image not in prepared resource catalogue". Its component 0 at LOD 0 binds `ps-t15 = Resource-9a09f1f0-22587-0-NormalMap`, whose file is not in the Mod.
- The original resolves the file relative to the INI, else to its own folder (IniHandler.cpp ParseResourceSections), loads it at first use, and where that fails warns in the overlay and leaves the resource null (CustomResource::LoadFromFile); the assignment binds nothing and the draw goes on.
- The layer: in the folder generation the fallback path lay outside the Mod's folder, so the catalogue left it out and the plan refused the draw; in the Mod's own generation it was catalogued as an image to load. Fixed: a file that is not there is an explicit null binding in both (the catalogue leaves it out, image_index answers none and logs once). Failing check: check_folder_generation.py (the Mod's sections differed alone and in the folder; planned False in the folder).

## BUG-075: Character meshes in the host's window of device memory, whatever its size (fixed in 2.1.1)

- Found in the review of 2026-10-28. GpuMesh took host-visible device memory first and wrote the mesh through a mapping. With resizable BAR (this machine) that is the whole card; without it (many desktops, AMD without Smart Access Memory) a heap of about 256 MiB the game uses as well: meshes of a few characters (4 were 110 MB here) fill it, and the game's own allocations there or ours fail.
- The original creates a Mod's buffer with D3D11_USAGE_DEFAULT and the file's bytes as initial data (CustomResource::SubstantiateBuffer); the driver copies them into video memory.
- 2.1.1: mapped only when that memory is on the largest device-local heap; otherwise device memory filled by a copy on the layer's upload queue; host memory without one. Failing check: the character test with the window simulated as 256 MiB.

## BUG-074: A proxy DLL named vulkan-1.dll hid the loader (fixed in 2.1.1)

- 2026-10-28, an AMD player's 2.0.9 log (relayed by the user): "bootstrap: no free memory near vulkan-1.dll for 0 stubs; nothing redirected", then nothing: no device seen, no Mod. The first module named vulkan-1.dll mapped held none of the 144 names as code: a proxy that forwards them. The layer latched onto it and ignored the loader it forwards to; the message also misnamed the cause.
- 2.1.1: a module counts as the loader only when it exports vkGetInstanceProcAddr as code; a proxy's forwarders name the module waited for; every candidate is logged with its path. The sandbox next to a generated forwarding proxy reproduces the AMD log line exactly on 2.1.0.
- Not verified on the player's machine: which proxy it is is unknown until 2.1.1's log names it.

## BUG-078: The vk01 replay with a library call captures no translucency (found 2026-10-30, pre-existing)

- The suite step "captured frame with a library call" (replay_capture.py on vk01, Yvonne, `--copy-passes --expect-translucency`) fails: `translucency {captures 0, composites 0}` with 3540 draws matched and 2668 replaced, no refusal, no "translucency:" line in the log, the EFMITTL library loaded. The same on HEAD 97aa7f5 built in a worktree of its own (identical counters), so not the INI review's change (D-154). The installed `Mods\EFMITTL\translucency.ini` was modified on 2026-10-29 22:52 (the Yvonne folder's Textures, Meshes and k on 2026-10-28 19:41; yvonne.ini unchanged since 10-16); the step last passed in the suite on 2026-10-27 (2.0.6b). To be traced from what changed in the library's INI against the bridge's requirements (EfmittlBridge::install: its Capture, Draw and Composite lists, its mask resource and eight variables) and the replay's copy-pass detection.

## LIMIT-046: Enabled INIs that belong to no character Mod are not loaded (lifted the same day, D-155)

- Lifted 2026-10-30 by D-155: every enabled INI of the Mods folder is an input (its Present and keys run, its persistent variables are saved, its texture-hash sections act in replaced draws). Left: a folder without any character Mod loads nothing; a plain INI's sections act on no draw but the characters' replaced ones.
- Found 2026-10-30 (D-154; docs/folder-generation.md had claimed otherwise). The original loads every enabled INI of the Mods folder into one space. The folder generation takes the character Mods' INIs (those with `[CommandListInitializeMergedSkeleton]` and the other enabled INIs of their folders), the libraries they call by namespace (D-112) and the key and ShaderRegex sections of the native libraries' files (D-133); any other enabled INI (a texture replacement by hash on a draw that is not a character's, a post-process Mod with its own CustomShaders and Present, a standalone key panel) is not read, without a notice. Of the installed folder's 48 enabled INIs none is such a file (EFMITTL is a called library, RabbitFX's three files are native or inert). Its TextureOverride sections by texture hash would act only in replaced draws (the layer matches no other draw); its Present would run as owned work.

## LIMIT-045: DXGI D24 depth formats where the device has no D24_UNORM_S8_UINT

- AMD GPUs do not support VK_FORMAT_D24_UNORM_S8_UINT as a depth attachment; a CustomResource an INI declares with `format = D24_UNORM_S8_UINT` (or R24/X24 views) is created in it and fails there. DXVK substitutes D32_SFLOAT_S8_UINT. No installed Mod declares one (2026-10-28); copies of the game's depth use the game's own format. Not changed: the substitution touches view and copy compatibility.

## BUG-073: The v2.0.9 release unpacked into EFMI\Mods loaded no Mod (fixed in 2.1.0)

- 2026-10-27, the user's test of the published v2.0.9: unpacked into EFMI\Mods\EFMI-Vulkan, the configuration's `../Mods` named EFMI\Mods\Mods ("folder generation: cannot read the Mods folder ... Mods\Mods"). Inside the Mods folder the release's settings file would also have been read as a Mod's INI. The log also called the game's own handled start-up fault (EndfieldBase.dll, every start since the first sessions) a crash.
- 2.1.0: `m2_mods = auto`, the layer's folder left out of every Mods scan, foreign first-chance faults logged as `exception:`. Release check failing on the 2.0.9 archive, passing on 2.1.0.

## BUG-067: An F10 after a drawn character's Mod was removed ended the game (fixed in 2.0.8)

- 2026-10-27 19:25:49, PID 30992, 2.0.7: the user deleted a Typhoeus Mod and Si SorceressV2 and added another Typhoeus Mod, then pressed F10. The exchange logged the three packages removed, then the presenting thread rethrew a std::system_error (errc 3, no_such_process: std::thread::join's `_Thrd_join` failure) from the old generation's retirement future (`folder_frame_and_exchange`); Unity's crash handler took it (Player.log, crash dump; the layer's own log has no crash line, only the watchdog's stall lines while the handler ran).
- Cause: a removed Mod's package was stopped by its controller (`stop_later`, a thread of its own) and, with every other package of the generation it left, by that generation's retirement (another thread); both joined the package's worker at once and the second join failed. The original releases each object once, through its reference count, on one thread.
- 2.0.8: `CharacterGeneration::stop` and `FolderGeneration::stop` run once (std::call_once; a concurrent caller waits until it is done); the retirement and removal tasks log an error instead of passing it to the presenting thread. Test failing on 2.0.7: the Mods-folder scene disables its drawn Mod's INI and presses F10 (the process ended with 0xC0000409).

## BUG-068: A Mod enabled again by F10 took a new slot, 64 in all, and a removed package kept its objects (fixed in 2.0.8)

- Found going through docs/f10-operations.md. Controllers are never removed (draws index them without a lock); every return of a removed Mod added one, so toggling Mods with F10 ran out of the 64 slots, and the stopped controller kept its last generation (and through it the old generation's INI state). 2.0.8: the stop takes the generations with it; a Mod back takes its old slot (a new generation for the index). Test failing on 2.0.7: the scene's Mod enabled again showed a third package entry.

## BUG-069: An F10 pressed while a reload loaded was lost (fixed in 2.0.8)

- Found the same way. The request was taken every presentation, also while a load ran, and dropped; it also lived in the old generation's runtime, which the exchange replaces. The original reloads once per press. 2.0.8: the request is taken only when no reload runs, and one left in the old generation at the exchange starts the next load. Test failing on 2.0.7: two F10 with an INI edit between them, the second during the first's load: the edit must be drawn.

## BUG-070: A reload candidate set the dither gate while it loaded (fixed in 2.0.8)

- folder_create set the global gate for every generation it made, the F10 candidates included, until the next presentation set it from the live one again (one frame; a refused candidate's gate). 2.0.8: the gate is set at the start and from the live generation only. No test can see the one frame; the scene checks the gate after RabbitFX was disabled and enabled again by F10.

## BUG-071: The anti-dither removed the field's alpha tests (fixed in 2.0.8)

- 2026-10-27, 2.0.6/2.0.7 in the game: with `$censor` on, grass and bushes drew as solid quads. Their fragment shaders kill on `min(LOD cross-fade - noise, alpha - cutoff) < 0`: the cross-fade comes from the instance window too, so the rule took the kill and the patch removed the alpha test with it. RabbitFX's patterns match only the character materials (their constant-buffer declarations and the camera fade `max r, cb.x, cb.x; add r, -r, 1`). 2.0.8: the rule needs that fade (the larger of two instance window values) in the kill's condition. Field frame of 2026-10-27 saved as captures/vk-2026-10-27-field; recipe test failing on 2.0.7 (a foliage-shaped fixture and eleven alpha-tested field modules).

## BUG-072: RabbitFX's key took a second or two (fixed in 2.0.8)

- The twins and the replaced draws' other-state variants were compiled when the gate turned, and the first binds waited for them (up to 4 s each, serially). The original's replaced shader exists from the game's shader creation; the key flips a branch. 2.0.8: twins are prepared when the game creates a dithered pipeline (or when a gate appears: start, module exchange, RabbitFX added by F10), and a replaced draw prepares its other state with the first, both behind requested variants (VariantCache::prepare). Status `dither_twins_prepared`. Test failing on 2.0.7: the scene's dithered pipeline prepared before any bind.

## LIMIT-043: A broken texture file refuses the whole F10 candidate

- The original reloads and only that resource fails at its first use; the layer keeps the live generation (scene: broken DDS). Recorded in docs/f10-operations.md.
- 2026-10-28 (D-139): a texture file the loader makes nothing of (a broken DDS, a file WIC cannot decode) is found when the generation loads and bound as nothing, as the original's null resource; it is never declared to the residency and no longer refuses an F10 candidate. A load that fails on the GPU for another reason (device memory, a budget's ten-second wait) still refuses the candidate and the draw.

## LIMIT-044: Files deleted while their reload reads them

- A package of the candidate whose mesh files went during the load fails to come up; the candidate is refused after 30 s. The original loads what is there.

## BUG-064: SiModGB drew nothing: its declarations and a texture query refused (fixed in 1.9.7)

- 2026-10-26 22:03, PID 8552, 1.9.6, user-started session: the user installed SiModGB (Mods/SiModGB/mod1.ini) and saw nothing of it. The log: its plans refused with "Index stride conflicts with its declared format" and "Unadapted original buffer query", then the owned Present's generation failed. Offline (dry run of the installed Mods) after the first fix: 25 of its 26 sections refused with "Typed vertex resource format requires a declaration adapter".
- The original, for each:
  - Its index buffers are declared `format = DXGI_FORMAT_R16_UINT` with `stride = 6`. The original binds an index buffer with `IASetIndexBuffer(buffer, format, offset)` (CommandList.cpp `ResourceCopyTarget::SetResource`); the stride only becomes the buffer description's StructureByteStride and never reaches the index fetch. The layer refused the combination.
  - Its vertex buffers are declared with a typed format (`DXGI_FORMAT_R16G16B16A16_UNORM`, `stride = 16`). The original binds a vertex buffer with the stride `CopyResourceToResource` arrived at: the declared stride (`OverrideOutOfBandInfo`), else what `FillInMissingInfo` fills in (StructureByteStride, then the format's size, then the whole buffer). The format is a view's, not the vertex fetch's. The layer accepted vertex files only with an explicit byte stride and no format.
  - Its draw lists ask `if ps-t5 == 905031` (the `filter_index` of one of its TextureOverride sections) to find which slot holds that texture. In a replaced draw's plan this is the original's FindTextureOverrides on the texture bound at the slot (`ResourceCopyTarget` evaluated in RESOURCE mode); the layer answered texture queries only inside an owned scope and refused them in a plan.
- 1.9.7: an index file takes its format's index size whatever its stride; a vertex file strides by its declared stride, else its format's size (an unknown format without a stride still refuses: the original's whole-buffer stride would draw one vertex everywhere); a file's roles come from the assignments that bind it (vertex slot, index buffer, shader slot), as the original's buffer takes the bind flags of those assignments; a pixel slot's texture query in a plan, where no CPU answer exists, goes to the host's texture matcher. The buffer query refusal names the target type and evaluation mode.
- Tests that fail on 1.9.6: the resource planner's index file with `stride = 6` and the typed vertex files (with a stride and with the format alone, and an index format bound to a vertex slot, which the original strides by 2); run_all's "captured frame with texture filter queries" (vk04 with only SiModGB, no refusal allowed: 3444 of 3486 matched draws replaced, none refused).

## BUG-063: A Mod whose panel has a long else-if chain never finished loading; the engine lock it held froze the game (fixed in 1.9.6; the lock in 1.9.8)

- 2026-10-26 21:35:51, PID 11492, 1.9.5 (and the same at 20:46:26 with 1.9.4, PID 14500): an F10 found the new Typhoeus Mod (folder 30478105ea01553e6a5cff4ae0bd1896_-AmIhB1, its key panel res_ini/Tifluos_panel.ini). Its generation's load never logged "character Mod read"; the other loaders stopped behind the global engine lock; the presenting thread, in an F10 exchange (`suspend_persistence`), waited for the same lock; the render thread waited for that controller's lock in `character_pass_end`. Sampled in the frozen game through the probe (`profile`): the presenting thread in `Scope` under `IniRuntime::suspend_persistence`, the render thread in `RtlAcquireSRWLockShared` under `character_pass_end`. Offline: the sandbox with the actual configuration hung at device creation with this Mod installed (startup loads in order and this folder sorts first); a stack dump of that sandbox process showed `IniRuntime::load` -> `HostCompileRoots` in a recursion dozens of frames deep.
- Cause: `compile_graph` (frontend/xxmi/ini_module.cpp) visited all four bodies of every `IfCommand` without remembering the lists it had seen. The original (CommandList.cpp `_ParseEndIfCommand`) puts one nested `if` into both the pre and the post body of the `if` around it and nests each `else if` in the else before it, so the n-th branch of an else-if chain was reached 2^n times. The original never walks like that: `optimise_command_lists` goes over `registered_command_lists` flat, and a list runs only the branch it takes.
- 1.9.6: every list is visited once. Test that fails on 1.9.5: owned_present_test, a Mod folder whose second INI's [Present] holds a 40-option else-if chain must bring the device up, run the chain and end normally (the device never came up on 1.9.5).
- The deadlock class (open after 1.9.6): background loads held the global engine lock for their whole INI load, which every presentation and matched draw also took; anything slow inside a load (a pathological INI, a driver call) stalled the game that long. 3DMigoto loads in Present, on one thread, under `G->mCriticalSection`.
- 1.9.8 (D-129, docs/engine-generations.md): each generation's original engine state is its own; a scope takes only its generation's lock, a load also the load mutex, which no drawing, presenting or saving thread takes. Test that fails on 1.9.7: runtime_concurrency_cases (a load held in its scope while another generation presents and draws).

## BUG-062: A removed Mod's package was stopped on the presenting thread, waiting for the GPU there (fixed in 1.9.5; the freezes it was taken for were BUG-063)

- 2026-10-26 20:46:26, PID 14500, 1.9.4: the user replaced the Typhoeus Mod (the old folder gone, a new one added) and pressed F10. The rescan found the old package's Mod gone and stopped it on the presenting thread (`CharacterController::stop` from `character_rescan`): the generation's `OwnedPresenter::stop` waited without a limit (vkWaitForFences, UINT64_MAX) for its Present work, including the shape-key compute submitted in this very frame. That work could complete only after the presentation went, which the wait held: no presentation from then on (stall watchdog: the presenting thread inside vkQueuePresentKHR at "presentation work (frame, character Present)"); the probe's status request then blocked on the same locks. An exchange (`retire`) already stopped old generations on a thread of their own; only the removal path did not. gilberta's removal at 20:39:04 did not freeze: it had no Present work.
- The original: 3DMigoto's F10 is `ReloadConfig` (IniHandler.cpp), called from Present (HackerDXGI.cpp); it clears the old configuration, whose CustomResources only `Release()` their D3D11 objects (CommandList.cpp `CustomResource::~CustomResource`). The D3D11 runtime destroys an object once the GPU is done with it. Nothing waits for the GPU in Present.
- The Vulkan form: the presenting thread only takes the package out; its generations stop on a thread of their own (`stop_later`), where waiting for their GPU work holds nothing it depends on, the deferred destruction D3D11 does for 3DMigoto.
- Test: every wait for the GPU the layer's own code makes goes through `Device::owned_gdpa`, whose wrappers count one made on a thread inside vkQueuePresentKHR (`replacement.present_gpu_waits`, logged with the thread's stage); owned_present_test runs a Mods folder whose Mod has Present work, renames it to DISABLED_, presses F10 (the test key file now also serves efmi_vk_sandbox.exe) and requires the count to stay 0. A sandbox gate (a batch waiting until the presentation returned) was tried first and dropped: a presentation that depends on a wait whose signal is not yet submitted is invalid usage (VUID-vkQueuePresentKHR-pWaitSemaphores-03268).

## BUG-060: With the bones on the GPU a Mod's parts fell to the ground in some frames (fixed in 1.9.3)

- 2026-10-26 18:36-18:45, PID 13144, 1.9.2: Typhoeus (Nya Tech & GUI) showed its wings and other parts lying on the ground in some frames and in place in others; with the bones switched to the CPU through the probe (diagnosis only) they stayed in place.
- Cause: BUG-054's repair gave a recording, for a component another recording imported, that import again: a dispatch reading the game's bone buffer at this recording's submission, at the place the other recording's draw had bound, often the frame before. The game keeps a frame's bone region valid from its CPU write (before the draw is recorded) until that frame completed and then reuses it; the CPU writes frame N+1 while the GPU runs frame N. The second read found another frame's data. The deeper cause is cause A of docs/design-review-1.9.md: the layer's GPU work ran in submissions of its own, outside the game's order, and kept per-frame slots instead of the original's persistent merged skeleton.
- Test that fails on 1.9.2: replay vk06 with only this Mod, `--split-passes --rolling-bones --bones-identical --check` (two copies of every bone buffer, frame f binding copy f%2, the copy of the frame before zeroed before a frame is submitted): GPU and CPU pictures differ, the GPU picture with the lower body and the hands' parts drawn down to the bottom of the image as in the game. With `--animate-bones` alone 1.9.2 also differs from the CPU (a pose one frame newer), without the collapse.
- Fix (1.9.3, D-127): one persistent merged skeleton per object, written only by imports run in the batch of the recording that drew the component; per recording a snapshot copied after its batch's imports; the work at the front of the application's batch with the batch's waits widened to cover it.

## BUG-066: Panel settings of Mods without a namespace were not restored in the folder generation (fixed in 2.0.5)

- 2026-10-27, the 2.0.3 game session: 105 "Unrecognised entry" warnings from the folder's settings file, all `$\k\1.ini\...` and similar: the panel INI several Mods ship (`k\1.ini`, no namespace line) with persistent position, fade and minimized state. A generation per Mod named that INI by its path from the Mod folder; the folder generation names it by its path from the Mods folder's parent (`$\mods\<Mod>\k\1.ini\z`), as the original's d3dx_user.ini does. The adoption of 2.0.0 copied the per-Mod lines unchanged, so the values were not read, and lines of several Mods' panels became one name.
- Fixed in 2.0.5: adoption renames such lines (a namespace that is an INI file of the Mod's folder); a folder file still holding the old names is adopted again once, its own lines first, the old names dropped. Test failing on 2.0.4 (the Mods-folder scene with a `k\1.ini` panel, the Mod's own settings and a folder file adopted the old way).
- Also found: the save refuses every persistent variable whose name is outside ASCII (a Mod folder or an implicit namespace outside ASCII), which would stop the whole folder's save. No installed Mod has one now; recorded as LIMIT-042.

## LIMIT-042: A persistent variable named outside ASCII stopped the folder's settings save (fixed in 2.0.7)

- 2.0.7: the names are written as UTF-8, as the original writes d3dx_user.ini (setlocale UTF-8, %ls), and read back by the runtime's UTF-8 reader. Test failing on 2.0.6: the Mods-folder scene with a panel INI in a folder named with a full-width colon (its settings and every other Mod's were not saved).

- 2026-10-27. IniRuntime::save throws "Persistent variable name is not ASCII" for the first such name, and the folder generation saves every Mod's persistent variables in one file. The original writes d3dx_user.ini whatever the names. Next: write such names as UTF-8 and check that the parser reads them back.

## BUG-065: Half of Typhoeus' draws kept the game's mesh in the folder generation (fixed in 2.0.4)

- 2026-10-27, user-started session with 2.0.3 (PID 25552). Typhoeus (`30478105..._-AmIhB1`, namespace TifluosMain): matched 193,566 draws, refused 99,993 with "Original Core request differs from the resolved native identity, bone or LOD contract"; every other activated Mod (SiModGB 554,457 of 554,548 replaced, Yvonne, arcane, Liino, mifu) refused none. The previous session (1.9.x, a generation per Mod) logged no such refusal for the same INI.
- Cause: the folder generation has one variable space as the original's, so `$\EFMIv1\lod_level` is one variable all Mods share. Every installed Mod hands its LOD to the Core in its `CommandList_Component_DrawInstances` (`$\EFMIv1\lod_level = $lod_level`) except Typhoeus, whose LOD1 parts are components of their own; its draws found the value another Mod's draw left (1). The layer resolves a draw's LOD before the plan (it imports the bones with that LOD's remap) and refused when the Core's value was another number. DX11 draws them: the Core reads `lod_level` only to pick `LodRemaps[component * levels + lod_level]` (MergedSkeleton.ini), and Typhoeus' remaps are all null.
- Fixed in 2.0.4: a draw agrees with the Core when the remap the Core picks is the one the bones were imported with (LOD0 has none by the contract, LOD1 the component's, which most components lack); only a different remap refuses. Of the installed Mods only ardelia has LOD1 remaps, and it hands its LOD to the Core. Test failing on 2.0.3 (runtime test, `--plan-folder`): a Mod that never hands its LOD over, after another Mod left the Core at LOD 1, plans its components without remaps; its component with a LOD1 remap still must agree.

## BUG-061: Turning Frame Generation off and on left the game very slow: an F10 brought every package up twice (textures fixed in 1.9.4; open part below)

- 2026-10-26 18:46-18:48, PID 13144, 1.9.2. The user turned DLSS Frame Generation off and on (two swap chain recreations, 18:46:41 and 18:47:16); at 18:47:33 an F10 exchange ran (`INI generation 1 Ready; F10 exchange complete`; whether the user pressed F10 is not known). Every package whose live generation had ever been active brought its successor up at once (activate and `preload_matching`: the images the live one showed, loaded again under the new generation's tag), 20 packages: textures 2.45 GB, meshes 369 MB, 141 evictions, about 5 to 12 frames per second afterwards.
- Cause B of docs/design-review-1.9.md: images and mesh files belong to a generation, so a reload reads the same files a second time while the old copies live on in the old generation's recordings. The residency's declarations also only grew (4096 images): repeated F10s would have refused packages.
- 1.9.4: an image is the device's, keyed by its file's path, size and write time; generations count their declarations and an image goes when the last one forgot it; a load that failed is tried again by a later generation. Test that fails on 1.9.3 (all four entry and queue configurations): the live INI scene's F10 with unchanged texture files must read none again and declare no second image; on 1.9.4 the scene's three reloads read 4 files instead of 8 (the two originals, the broken candidate and the restored file).
- Open: every package whose live generation was ever activated still brings its successor up at once (mesh upload, arenas; 369 MB of meshes for 20 packages in the session). Bringing up only the packages drawn lately was tried and withdrawn: off-screen packages would then skip the candidate's image check before the exchange, and no test shows the rule yet. One generation for the Mods folder (step 5) changes this anyway.
- 2.0.3: meshes as the images: a successor whose streams are the same bytes at the same places as the live generation's (the same files, offsets and validated checksums, D-131 step 5) keeps the live mesh on the device instead of reading and uploading it again; a discovered package lays its mesh files out by file, so the same files make the same mesh whatever order a parse keeps its resources in (they differed from one parse to the next). Test failing on 2.0.2 in all four configurations: the live INI scene's F10 with unchanged mesh files must upload no mesh and keep one (`record.mesh_uploads`, `record.meshes_kept`). Still open: the successor's snapshot arenas (32 MiB per activated package).

## BUG-058: The game froze with a Mod's key panel open: the Present path held the locks the game needed while the driver waited for the GPU (fixed in 1.9.1; open part below)

- 2026-10-26 16:36, PID 20392, 1.9.0, DLSS Frame Generation on (the game's presentations reach the layer from Streamline's present thread). After options were switched in several Mods' Ctrl+K panels the game froze. Stall watchdog: the game's render thread inside vkQueueSubmit at "character draw work submission (generation lock)", Streamline's present thread inside vkQueuePresentKHR at "character Present (owned scope, INI Present, owned submission)". A 30 s profile taken during the freeze: the present thread spinning in the NVIDIA driver (NtGdiDdDDIEscape, NtGdiDdDDIGetDeviceState) inside vkAllocateDescriptorSets, called from ShaderCommands::descriptors for the panel's CustomShader, while holding the generation's lock and the engine lock; the render thread waiting for that generation's lock in submit_draw_work. The driver waited for the GPU, the GPU for work the blocked thread was to submit.
- Causes: (1) the owned runtime created a descriptor pool and allocated a set for every CustomShader draw and called vkAllocateMemory for every upload, so every frame a panel is visible (and every frame of a Mod with Present work, Typhoeus) made driver allocations that can wait for the GPU when the card is full; (2) every application submission took every generation's lock, whether or not that generation had owned draw work waiting.
- 1.9.1 (D-125): descriptor sets are reused per pipeline layout once their submission completed (pools of 16 sets); upload buffers up to 1 MiB are reused from completed submissions (32 MiB kept); the presenter reports `allocations`, `descriptor_pools`, `staging_reused`, `sets_reused`. A submission takes a generation's lock only when a replaced draw left owned work waiting (`draw_work_waiting`). Test: the owned Present test's panel changes an INI parameter and binds a file image every frame; in 1.5 s of frames neither allocations nor descriptor pools grow, and uploads and sets are reused (the counters are missing on 1.9.0).
- Open: what the Present path makes the first time (an image a panel loads, the first pool of a layout, an upload above 1 MiB) is still created on the present thread under the engine lock and the generation's lock. A driver call there that waits for the GPU while the game's thread needs one of those locks for a draw can still deadlock. Candidates: memory reserved and suballocated outside the locks; Present-time first uses made outside them.

## BUG-059: Persistent INI variables were saved every frame while they changed (fixed in 1.9.1)

- Same session: the panels animate persist variables ($fade, $minimizeAnim, the dragged $z and $w), so they changed every frame. Since 1.8.9 the save worker wrote the settings file whenever they were dirty, with FlushFileBuffers and MOVEFILE_WRITE_THROUGH inside the engine lock, which every draw and presentation takes: 457 saves in 2.5 s in the new test on 1.9.0; in the game the engine lock's waits rose to 60-113 ms per 2 s during panel use (normally under 1 ms). The original saves from its Present at most every settings_auto_save_interval seconds (60 in d3dx.ini and by default), besides reloads and exit.
- 1.9.1: the same cadence (`settings_save_seconds`, default 60); the text is taken under the engine lock and the file written outside it (a save mutex keeps the order; suspend_persistence waits for a write in progress). Test: runtime_test's save cadence (no save in 2.5 s of a variable changed every frame, the exit save holds its latest value).

## BUG-057: A newly installed Mod (Tangtang) was refused at every draw (fixed in 1.9.0)

- Found by the suite's offline plans on 2026-10-26: `Tangtang - Uniform NSFW`, installed by the user, refused all 12 sections. Three things the planner did not take, each the original's behaviour: `if vs->srv_stride(0) == 16` (a declaration query, D-124); `vb3 = vb0` (a vertex slot given the buffer bound at another, by reference, the original's default between two slots of one kind); `vb2->ElementFormat(BLENDINDICES, 0)` given twice before a draw (once in the skeleton callback, once in the draw list; the original applies both, the same change, while the character program took exactly one blend edit).
- Each with a check that fails without it: the TangTang replay with only this Mod (`--only-mod`, vk02; 3420 refusals on 1.9.0's first build, then 2736, then none and 2736 draws replaced), the planner's host test (`vb3 = vb0` and the application's own buffer at another slot refusing), the pixel-checked runtime fixture (repeated edit and `vb3 = vb0` in its lists: refused before, 76/76 after), the DXBC comparison of the declarations. New suite steps: `DX11 declarations from SPIR-V`, `captured frame with shader declaration queries`.
- Not changed (D-089): `ps != 1718.2` (RabbitFX's shadow filter) and `ps != 203 && vs != 203` (the Mod's outline ShaderOverrides) read a game shader named by no ShaderOverride, so the Mod binds its RabbitFX textures in the shadow pass too and, with its key 3 on, draws one part in the outline pass too.

## BUG-054: With the bones on the GPU a Mod's parts lost the bones of other components (fixed in 1.9.0)

- 2026-10-26, session PID 45144 with 1.8.9: the Si NightveilTrancer Mod drawn without its legs (the shadow too); switching the bones to the CPU through the probe brought them back.
- Cause: a recording's ring slot got dispatches only for the components the recording itself drew, but an EFMI part's vertices read bones of several components (Si: 6dc25c11 reads 2, 3, 4 and 10; c345bccf reads 2, 3 and 13). The game records one character over several command buffers, so a slot held those other components from whichever recording last wrote it, possibly many frames old, and the legs were drawn where the character had been. The CPU master never had this: it accumulates every component and each draw copies it whole.
- The replay could not show it: one command buffer per frame, and static stand-in bones, under which a stale slot holds the right values. New: every component's latest import is kept per object, and each draw adds, for every component its list reads that the recording does not dispatch yet, that import into its slot (from a buffer the game still has; the component's own import in the recording replaces it). The invariant is counted at submission (`bones.components_missing`) and the replay's --check fails on it: on two queues 113 before the fix, 0 after; GPU and CPU pictures identical in one queue, two queues and --split-passes. Replay modes added on the way: --split-passes (a pass's draws alternating between two command buffers recorded whole one after the other), --animate-bones (stand-ins moving every frame; GPU and CPU then legitimately differ by the original's one-frame lag of components imported later in the frame, so it is no equality test).

## BUG-055: The probe's bone switch did not reach a swapped-in module (fixed in 1.9.0)

- The module read the host's configuration only while its entry ran and kept its own copy, so `set gpu_bones` after an exchange changed nothing (seen in PID 36252). The host API gives the live value (HostApi::gpu_bones); the character test now switches the module's bones both ways after its exchanges (it failed before the fix).

## BUG-056: On a full card the Mods waited for device memory and showed the game's mesh (fixed in 1.9.0)

- Same session: in the character screen many Mods did not show and others turned from the game's model into the Mod. The device had 27 to 60 MiB free (the game uses most of the 8 GiB); the activation gate of D-092 waited for free memory beyond the reserve, and meanwhile the game's mesh was drawn (Typhoeus 340760 draws). With pageable device memory (D-122) the gate no longer waits, as 3DMigoto creates resources whatever the free memory. Replay step `--reserve-mb 1000000`: 1.8.9 replaced nothing (`waiting_memory` 4800), 1.9.0 replaces.
- The rest of the "original first": the Core's identification after an object's first frame and an EFMI Mod's registration (`$mod_enabled` turns on at the Present after `$object_detected`, the game recording frames ahead) kept the game's mesh for a few frames at every new appearance. D-123: such draws are skipped until one draw of the object was the Mod's, for 30 presents at most (`held_before_shown`; `original_before_shown` past that). Replay check --mod-first: 1.8.9 drew the game's mesh 12 times in vk03, 1.9.0 none.

## BUG-053: The Si NightveilTrancer Mod was refused at every draw in the game (fixed in 1.8.5)

- 2026-10-25, session PID 26184 with 1.8.4 and two module exchanges: Si's 500657 matched draws were all refused, the status showing only the Present's later error (`CustomShader needs the host's owned GPU scope`), which hid the first one.
- Reproduced offline once the tooling could take the Mod (below): (1) every TextureOverride guards its draw with `if rt_width != 0`; the original's `if` runs in the pre and the post phase, and the post phase runs in a fresh state (RunCommandListComplete) that asks OMGetRenderTargets again. The layer gave its answer only to the pre state, so the post state asked the owned domain and refused. (2) That refusal came after the draw's scope had recorded GPU work, which marks the generation's GPU side failed for good (a design rule of the owned runtime), so the Mod's Present failed every frame after and every draw with it. (3) The Mod's shape keys: its Present runs a compute pass into `cs-u5 = copy Resource_..._Position_0`, then `Resource_..._Position = ref cs-u5`, and its draws bind that by reference as vb0; the owned buffer had no vertex usage (the original creates the copy with the UAV bind flag alone, and DX11 without its debug layer draws from it anyway).
- Fixed in 1.8.5: every fresh state of a draw's execution (the post root, the roots of intercepted calls) starts with the host's answer for the draw through a hook the runtime installs (DrawPlanAdapter::fresh_state; a first attempt copied the pre state's value, which an extension's before_draw may have written, and the original-runtime fixture caught it); `rt_width`/`rt_height` are adapted in every generation, so a CPU-only one answers from the host too (fixture: the pre and post roots, none bound, unknown refusing); the host's answer falls back to the framebuffer's size, then the render area, and logs once when it has none; owned storage and texel buffers also get vertex and index usage (frontend/xxmi/gpu_resources.cpp); the original's built-in sections (BuiltInCommandListUnbindAllRenderTargets and the scissor custom shaders) are inserted before the included files as the original does (the Mod's watch GUI runs the first). Each distinct reason a package records is logged once per generation (16 at most), so the first refusal is never hidden again. Replay vk04 with the Mod alone: 4444 of 4568 matched draws replaced, none refused, 10014 owned vertex streams bound, the Present clean, validation clean; a suite step now replays it with `--no-refusal`.
- Tooling on the way: the dry run read its path arguments in the ANSI code page and aborted on the Mod's full-width colon (now wide arguments); it refused any Mod with CustomShader sections or GPU declarations (now `declarations_only`, which lists the sections the override registry declares even when a declaration then needs the GPU); the replay's project copy of a Mod takes a plain folder name.
- Open: a single refusal inside a draw's GPU scope still ends the generation's GPU side for the session (the Mod then shows the game's mesh until F10). The original only skips the failing command. To be designed with the per-draw work of D-121 step 4.

## BUG-052: The 1.8.3 game session hung the GPU: one timeline signalled from two queues (fixed in 1.8.4)

- 2026-10-25 21:40, user-started Vulkan session (PID 35560) with 1.8.3: about two minutes in, no presentation; the stall watchdog had the game's thread inside vkWaitForFences and the submitting thread inside our "character submission work" for 18 s; then the NVIDIA driver (nvoglv64.dll) faulted and the process ended. The crash logger's budget had been used up by eight identical handled faults the game raises at start-up, so the crash itself has no stack (the logger now keeps the first eight distinct addresses).
- Cause: the bone dispatches signalled one timeline semaphore from both graphics queues; the game alternates them, so a later signal on one queue could complete before an earlier one on the other, which the specification leaves undefined and this driver answers with a hang. Fixed in 1.8.4 by the per-recording design (D-117 amended): a dispatch goes to the queue its recording is submitted to, each queue has its own timeline, and nothing waits across queues.

## BUG-050: The 1.8.2 game session deadlocked on the GPU at a cross-queue wait (fixed in 1.8.3)

- 2026-10-25 21:01, user-started Vulkan session (PID 19260) with 1.8.2: the picture froze about two minutes in; the stall watchdog showed the game's thread inside vkWaitForFences and no presentation for over 35 s; the user ended the process. Status through the probe: `bones.queue_waits` 474 of 778 submissions, `submitted` 778 against `completed` 773: five of the layer's bone submissions never finished.
- Cause: the game alternates its two graphics queues between frames, so a frame's dispatches (submitted on the queue of the first submission after they were queued) mostly ran on the other queue than the frame's draws. D-117's first version made the draws' queue wait for the dispatches' queue through the timeline; with the game's own semaphores between its queues that is a cycle, and the GPU stopped. D-091 had already ruled such waits out for the texture uploads; the bone merge broke the rule.
- Fixed in 1.8.3: a queue never waits for the other. When the other queue's dispatches completed, this queue takes an already satisfied wait (visibility); when they still run, this queue dispatches the same jobs again before its draws (`bones.redispatches`); both write the same rows of the same slot.

## BUG-051: A Mod whose draw lists read rt_width was refused at every draw (fixed in 1.8.3)

- 2026-10-25, the same session: the new Si Mod matched 22607 draws and 22572 were refused with "Original draw command refused: Render target size needs the owned GPU domain". Its watch GUI places itself by `rt_width` and `rt_height` inside `if` blocks of its TextureOverride lists. The original caches the two off the bound render target at first use; the layer answered them only in the owned GPU domain (Present), so a draw's list that asked refused.
- Fixed in 1.8.3: the renderer gives the draw request the size of its render pass's colour attachment 0 (RuntimeDraw::target_width/height) and the INI runtime starts the draw's state with it, as the original's cache would hold it. General: any Mod reading the render target size in a draw.

## BUG-049: A Mod with a large skeleton never drew: its arenas exceeded the batch budget (fixed in 1.8.2)

- 2026-10-25, 1.8.1 session (PID 44092): the new Si Mod configured and activated but drew nothing; its package said "character material arenas exceed the batch budget". Each batch makes one arena per image count (nine for a package with runtime materials), each with 64 bone copies of the master (512 + 2 x 1299 bones x 48 bytes = 150 KiB each): 9 x 64 x 150 KiB far above the 64 MiB a batch may take. Skeletons above about 900 bones hit it.
- Fixed in 1.8.2: the copies per arena follow from the bone count and the number of arenas (arena_slices, at least eight), and with the import on the GPU (D-117) the arenas no longer hold copies of the skeleton at all for GPU-imported identities.

## ISSUE-012: The game's own frame time under Vulkan, and what of it is ours (2026-10-25, open)

- Measured with the interleaved A/B (1.8.4, PID 26184): the bones on the GPU save 2.8 ms per frame (13.8 against 16.6 ms) and 2.6 ms of recording work in one scene; that was the largest single term.
- 1.8.1 session, open world, four Mods active: mean frame 15.9 ms, p50 14 ms, p95 31 ms, max 192 ms; the game at four cores of CPU with the GPU at 68%. The layer's steady work: about 5 ms per frame on the recording threads (4 ms for 55 matched draws at 50-90 us, 1 ms for the unmatched draws, submissions and 22 Present lists); draw_record max 321 ms (a first-appearance texture wait, 308 ms); present_work max 176 ms and submit_tracking max 50 ms without attribution yet. An on/off A/B of 8 s each was inconclusive (the scene changed: 216 against 690 draws per frame). The user: "it stutters badly". Next: the interleaved A/B (ab_interleaved.py: 10 rounds of 3 s on and off, medians) for the replacement and for the bones (`set gpu_bones`), attribution of the two unattributed maxima (the slow-call record should carry the stage that took the time), then the INI plan (15-37 us per matched draw) by memoizing a section's result on its inputs.

## ISSUE-013: A replay crashed once inside the validation layer's vkCreateGraphicsPipelines (2026-10-25, sporadic)

- The suite's vk01 step (replay, 1.8.2 candidate, validation and sync validation on) ended with an access violation reading address 4 inside VkLayer_khronos_validation.dll, called from GraphicsRecipe::create on a variant builder thread (VariantCache::run); the crash logger named the frames (build/replay/vk01/enabled/layer.log, 20:24:36). The same step passed twice before and three times right after, with the bones on the GPU and on the CPU; the other replay steps passed throughout. Not seen before 1.8.2 in the suite's logs, but the crash logger is new, so an earlier silent crash would have shown only as a failed step.
- The create info's objects are the variant's own (its render pass, layout and modules are made by the builder itself), so no other thread destroys them; what the builders share is the device's VkPipelineCache, which Vulkan synchronizes internally, and the validation layer's own state. Next: the step in a loop to see its frequency (six runs right after passed), with `m2_gpu_bones = 0` as well; if it recurs, the validation layer alone (a newer build) and one builder thread as a test.

## LIMIT-041: A section whose component is chosen by `vbN->stride` keeps the game's mesh (lifted in 2.0.2, D-132)

- Found 2026-10-27 offline (folder generation work, D-131): avywenna's `[TextureOverride_EntryPoint_Component0]` (hash 22a60235) assigns `$\EFMIv1\component_id = 0` and `$lod_level = 0` when `vb1->stride == 12`, component 3 and LOD 1 when it is 8 (an EFMI export telling a component's LODs apart by the vertex layout). Discovery finds two constants for the component and takes the section as one without a component; when the branch draws, the Core's contract wants the resolved component and the plan refuses, so the game's mesh stays.
- Until 2026-10-27 the dry run hid this: the section happened to be planned first, before the Mod registered, so the branch never ran. Discovery now gives sections in one order (by hash, count, first index), and the dry run shows it.
- The original answers `vb1->stride` from IAGetVertexBuffers. Since D-131's step 1 the layer answers it in a plan from the draw's pipeline (binding N's stride, RuntimeDraw::vertex_stride); the dry run, which has no pipeline, probes the game's common strides and checks that the answer reaches the list (it plans with 4, 16-64 and refuses with 8 and 12).
- Next: discovery records such a section's stride alternatives (stride, component, LOD); the layer resolves the component from the draw's pipeline before identification.
- Lifted in 2.0.2 (D-132): a section per branch, matched by the draw's pipeline binding. Dry run: avywenna's 22a60235 plans as component 0 at LOD 0 (stride 12) and component 3 at LOD 1 (stride 8), each with 2 draws and 2 images; no section of the installed Mods is refused any more. Not seen in the game.

## LIMIT-040: Fixed budgets that may refuse a Mod

- Constants that stand for a size a Mod declares, to be gone through by D-118: 32 components, 256 streams, 256 sections, 1024 draws, 16 layout edits per package; 64 draws per list; 128 pixel bindings; 256 profiles; 4096 bones (the Core allows 65536 merged bones); 8 image slots per runtime material; 4096 CustomShaders; 16 identities per package; 1024 jobs per bone submission. A Mod that crosses one is refused as a whole with a message naming it, which the status shows as `last_reason`.

## BUG-048: A Mod whose skeleton components leave bones between them is refused (fixed in 1.8.1)

- The Si Mod "Si" + U+FF1A + "NightveilTrancer & Gui" declares 1299 merged-skeleton bones in 24 components with bones between them (0-25, 361-425, 1142-1261 and others), none overlapping. The Core's initializer writes the 512 implicit bones and each component writes only its own range, so bones between components keep their initial value; every Mod vertex weights only component bones. The layer required the components to partition the skeleton (palette bank, Core bridge, skeleton plan, character package, native merged skeleton) and refuses the package: "Native generation preparation refused: Skeleton components overlap or leave a gap" (actual-config sandbox at Vulkan 1.3).
- Fixed in 1.8.1: the five checks take disjoint ranges in order and leave the bones between them as initialized (zero, the implicit ones identity), as the Core does; overlaps still refuse. A bone between components belongs to no component, so no draw list waits for it (it used to count for the next component). The same Mod then stopped at "Runtime image catalogue exceeds budget": its INIs declare 83 DDS images (the folder holds 133, the watch GUI's among them) and a package held at most 64; the budget is now 1024 (kCharacterImages), images still load only when a draw binds them. Actual-config sandbox at --api 1.3: all 22 packages configure, the new Si Mod included, no validation message (build/smoke-1.8.1-api13.json in the worktree). Its watch GUI calls `BuiltInCommandListUnbindAllRenderTargets`, a command list of the installation's d3dx.ini that the runtime does not load (an unrecognised-entry warning; LIMIT-039).

## BUG-046: A Mod folder named outside ASCII ended the game inside DXC (fixed in 1.8.0)

- 2026-10-25, user-started Vulkan session (PID 19920, 1.7.9). The user installed "Si" + U+FF1A + "NightveilTrancer & Gui" (a full-width colon in the folder name) and pressed F10; the game ended at 16:08:39 with an access violation in dist/dxcompiler.dll (offset 0x561cc1). The user started the game again; it ended the same way 19 s after start (16:09:37), right after the first package configured. Windows logged "virtual memory too low" at 16:08:36 during the F10 reload (see ISSUE-011).
- Cause, reproduced offline: the Mod's four shape-key compute shaders read `RWStructuredBuffer.Length`, which the original compiler (fxc) takes and DXC refuses; DXC faults while it reports that error for a file whose path holds any character outside ASCII (an accented letter, a full-width sign or a CJK character all fault; the same file in an ASCII path gives a clean error). The sandbox injection with the deployed 1.7.9 and the installed Mods folder ends with 0xC0000005 at the same point.
- Fixed in 1.8.0 (D-116): DXC sees its files by ASCII aliases and its diagnostics name them again; a fault inside DXC is contained (the compile refuses, DXC takes no further work in the process); a source DXC refuses runs as the original compiler's bytecode through the translation; a CustomShader that compiles nowhere is skipped with the original's warning instead of refusing the generation. Installed Mods' HLSL: 75 of 75 programs compile and validate, 4 of them (the shape-key shaders) through the bytecode translation.

## BUG-047: Characters reverted to the game's model in the open world; switching back took one to two seconds (fixed in 1.8.0)

- 2026-10-25, the same 1.7.9 session, 19 packages activated, EFMITTL translucency working (27119 captures, 6701 composites, no error). The user: characters in the open world sometimes revert to the original model, and switching back to the controlled character takes one to two seconds before the Mod shows.
- Status at frames 12290 and 25681: device free memory 188 MiB, then 0; textures resident 184 then 117 MiB against a target at its 64 MiB floor; budget_waits 44397 then 214832; evictions 195 then 317, loads 219 then 336; draw waits up to 538 ms; Yvonne and TangTang with no image resident and their gates closed (the game's mesh drawn). A load needed half the reserve free (256 MiB) besides its own bytes, so with the game at its budget nothing loaded, and every image no frame bound for 600 frames was evicted under the target: a party member out of view lost its textures and came back through the disk. The EFMITTL layer set of a target size took about 400 MiB at 2560x1600 at once, up to three sizes, never freed.
- Fixed in 1.8.0 (D-114, D-115). To be seen in the next game session: `textures.floor_bytes`, `budget_waits`, `evictions`, gated frames, `translucency.layer_bytes`.

## ISSUE-011: F10 with twenty packages ran the system's commit out (observed 2026-10-25, not measured)

- Measured offline (footprint.py, actual configuration, 22 packages, none activated): the layer takes 565 MiB of private memory in the sandbox, 418 MiB of it the packages' mesh payloads held in host memory. The game's 11 GiB of private memory in the 1.8.1 session is therefore mostly the game's own; an F10 adds a second copy of the 565 MiB while the generations exchange. Next: free a package's host payload after its mesh is on the device (re-read on activation), reload only the Mod whose folder changed.
- 2.0.1 (D-131 step 5): packages hold no payload once validated; the activation upload and a bone stream's adoption read the files again. Sandbox with the actual configuration and the installed Mods folder (20 character Mods, none activated): private memory 712 MiB on 2.0.0, 298 MiB on 2.0.1 (build/footprint-2.0.*.json in the worktree). An F10 now holds one Mod's payload at a time while the successor loads. Open: meshes of unchanged files are still read and uploaded again by a successor generation (BUG-061's open part).

- At the F10 reload of 16:08:23 (19 activated packages, one Mod folder exchanged) Windows reported the virtual memory minimum too low at 16:08:36 and grew the page file; the stall watchdog saw the submitting thread inside vkQueueSubmit for 25 s at the character draw work submission before the DXC fault ended the process. Each generation reloads its Mod and meshes while the old one still lives; how much commit that takes per package is not measured. Next: the commit and device memory per generation across an F10 in the sandbox with the installed Mods folder.

## BUG-045: Parts a Mod draws through EFMITTL were not drawn on Vulkan (fixed in 1.7.8)

- Up to 1.7.7 a character generation held the Core and the Mod's own folder only. `run = CommandList\EFMITTL\Draw` and the `$\EFMITTL\...` assignments were unrecognised entries, so every part drawn through the library was missing on Vulkan (Yvonne's first section, TangTang's and ardelia's translucent parts); DX11 shows them translucent.
- Fixed in 1.7.8 (D-112): the generation loads the libraries its INIs call by namespace, and a replaced draw's plan answers `ResourceX !== null` from its references (without that answer every such draw was refused and kept the game's mesh). With the game's ShaderRegex hooks inert the library's own fallback draws the parts opaque from the second frame of a generation. Replay vk01 (Yvonne, 120 frames), 1.7.7 against 1.7.8: covered texels shadow 162226 to 173968, G-buffer 85342 to 92233, forward 44179 to 71365, no refused plan (575 refusals before the declaration answer); vk02 (TangTang) replays with no refused plan.

## BUG-044: Cached transparency shaders reject core derivative capability (fixed offline in 1.7.7)

- EFMITTL 0.5.2's unchanged cached alpha pixel shader translates to SPIR-V capability 51 (DerivativeControl). The executor omitted it from the core allowlist and rejected actual drawing although source HLSL compilation passed.
- The capability is core Vulkan 1.0 and now accepted; stage/instruction validation and optional feature checks remain. Unchanged-library owned GPU tests cover cached/source transparency, nonuniform mip masks and shadows. Full-game transparency is separate (LIMIT-038).

## LIMIT-039: The installation's built-in command lists are not loaded (fixed in the 2.1.2 tree, D-154)

- d3dx.ini of the XXMI installation defines `[BuiltInCommandList...]` sections (for example `BuiltInCommandListUnbindAllRenderTargets`) that Mods and libraries run (EFMITTL, the 2026-10-25 Si Mod's watch GUI). A generation parses the Mod's own INIs, the Core and the libraries it calls, not d3dx.ini, so such a `run` line is an unrecognised entry and is skipped. Seen as warnings only; whether a skipped unbind changes a picture is not checked. Lifting it means reading those sections from d3dx.ini read-only.
- 2026-10-30: the `BuiltIn...` lists themselves were always inserted (HostInsertBuiltins, as the original's InsertBuiltInIniSections); what was missing was d3dx.ini's own `[CommandListUnbindAllRenderTargets]` that wraps one, and every other section the installation's d3dx.ini declares. The generation now reads d3dx.ini's sections first, except [Include...] and [Loader] (D-154); the live INI scene's fixture runs that list.

## LIMIT-038: EFMITTL's shared, pass-local game integration is not implemented (implemented offline in 1.7.9, game check open)

- 1.7.9 (D-113): translucency through the library's own policy with the layer's hook facts, native capture and composite and device-wide layers. Replay vk01 (Yvonne, 120 frames, --copy-passes): 456 captures, 114 composites, the library's $fallback staying 0, Vulkan validation 0 errors (4 performance notes); against 1.7.8's opaque fallback 30265 forward-target pixels differ, the sleeves (alpha 0.2) most, the dress (0.7) less (build/replay-1.7.9/). vk02 (TangTang, translucent variant on): 114 captures, 114 composites. Hook rules on the paired catalogue: the 7 Vulkan vertex shaders pairing with 1479b2b5 lit, 57 others not; copy rule names 4b4220a1 alone. Sandbox only. Open: a user-started Vulkan session (the game's real copy pass and targets, frames in flight, cost); shadows stay full by design.

- The installed plugin compiles and its unchanged INI executes on owned targets after BUG-044. This is not live-game support. Since 1.7.8 each consumer generation loads the library (D-112) but its frame state is per generation, not shared; the game's ShaderRegex sections remain inert (LIMIT-036), and original shaders/targets plus ordered work inside application render passes are not imported (LIMIT-035).
- Candidate 1.7.7 records attachment identities and subpass layouts for a user-started Vulkan capture. Existing game frames cannot prove scene-color/depth aliasing or the depth-copy/composite boundary. No per-character table or synthetic DXBC disassembly is used as a substitute. See docs/efmittl-compatibility.md and D-111.

> Last updated: 2026-10-24 (BUG-040 through BUG-043 fixed and verified offline in 1.7.5/1.7.6; game acceptance remains open).

## LIMIT-001: Probe data and long-duration stability are not verified in the real game

Historical passthrough launches established injection timing and device interception with 0.2.0. Run 3 passed the earlier crash point and included scene changes, but the user exited before 30 minutes. The user accepts that run as initial G1 loading evidence (D-014); long-duration stability is deferred, not established. Layer 0.5.0 is prepared in `dist/`, but all its capture/resource-tracking results are sandbox-only; no probe-mode game capture or real-character verification exists. G2/G3 remain open.

## LIMIT-002: Validation dependency installed and sandbox validated

Khronos VVL 1.4.321 and SPIRV-Tools are built under `F:/vulkan`. The sandbox passes core/synchronization validation through both EFMI entries and all four descriptor modes. The full suite now requires it. No full SDK or glslc is installed; GPU-assisted and real-game validation remain unverified. See `docs/validation.md`, including the initial temporary-cache incident.

## LIMIT-003: The existing DX11 frame dump is not a merged-skeleton baseline

`FrameAnalysis-2026-10-07-123937` has zero CustomShader executions and no mod entry point hit; only the ShaderRegex checks of EFMI and RabbitFX ran. It was good for checking the region hash rule on unmodified draws, not for the merged-skeleton comparison. Since 2026-10-12 the comparison has its own frame: `captures/dx11-baseline/frame-a` (LIMIT-015).

## LIMIT-004: RabbitFX mods are out of reach for the first version

6 of the 9 local character mods pass their textures only through RabbitFX, which patches DX11 shader assembly. They cannot be degraded gracefully by stubbing RabbitFX.

2026-10-19 (1.5.2): six of the twelve directory packages that load in the sandbox reference RabbitFX (akekuri, avywenna, estella, mifu, perlica, rossiy). The library is a ShaderRegex Mod in its own folder (`Mods/RabbitFX -ENDMI-`), outside the per-Mod generation, so its references stay original warnings and its textures (ps-t70 to t74, t60, t61) never bind. Belongs with the phase 4 shader work. Assessment with ground truth from the catalogue: docs/rabbitfx-assessment.md (no exact SPIR-V rule; diffuse by the first sample 79 of 83).

2026-10-20 (1.5.8, D-082): partly lifted. The layer's generations carry a native RabbitFX namespace; SetTextures binds a Mod's diffuse, lightmap and normal map at the roles the draw's fragment shader and textures give (docs/rabbitfx-roles.md). Still open: the glow/FX/hue features that need RabbitFX's shader rewrite. Sections that keep the game's mesh and only change its textures: done in 1.5.9 (D-083).

## LIMIT-005: Dynamic offsets attributed to the first set (fixed in 0.4.0)

Offsets now split using the set layouts in set/binding/array order. The sandbox checks two sets with a dynamic uniform buffer and a dynamic storage-buffer array, both multi-set and nonzero-firstSet binds, through both entries and with secondary command buffers. Missing layouts produce an explicit unknown reason instead of guessed offsets.

## LIMIT-006: Extended descriptor paths and full reflection remain incomplete

Layer 0.5.0 adds templates and direct/template push descriptors to ordinary writes/copies and resource metadata. Descriptor buffers, specialized descriptor types and dynamic inline pipeline layouts remain unknown; unsupported paths discard stale knowledge. Only graphics and compute bind points are tracked. At most 256 stored elements per set per draw are emitted, with explicit counts/truncation. Swapchain images are not tracked as images yet. Unknown layout extensions have conservative compatibility across different handles. Core 1.4 and maintenance6 push wrappers and KHR template lifecycle/update aliases lack GPU coverage; see LIMIT-011.

`efvk_shader` exports retained SPIR-V and extracts direct declared names/bindings; it is not full reflection (no decoration groups, type/member layout, specializations, pipeline-library linkage or shader objects). See `docs/probe-resources.md`. G3 remains unverified in the game; b/c also need record-time availability evidence (LIMIT-008).

## LIMIT-007: A shadow knows only what passed the CPU

Sources are staging copies (source mapped when the copy is recorded or submitted, or snapshotted at unmap when at most 16 MiB), `vkCmdUpdateBuffer` and `vkCmdFillBuffer`. A buffer filled by compute, or by a copy whose source was never CPU-visible, is reported with `src: none` and counted under `unknown_writes`. There is no read-back from the GPU (T-013). `unknown_writes` in the frame statistics is the number to watch in the game.

## LIMIT-008: Hash and read-back are "as of now", not "as of the draw"

Seen in the game on 2026-10-12: the uniform scratch buffer alternates between two buffers and is rewritten every frame, so a read a few milliseconds after a capture returned the right bytes for 8 of 12 components once and for 1 of 12 the next time. Since layer 0.6.0 a capture can keep the bytes at submit and compare them with a read at record time (`docs/probe-snapshots.md`); that is verified in the sandbox and ran in the game on 2026-10-12 (fact 21): entries and own palettes were complete at record time in the display scene.

The hashes of a captured frame are computed when the command buffer is submitted, after all of its recorded writes were applied; a copy recorded after a draw in the same command buffer already affects that draw's hash. `read_buffer` returns what the mapping or the shadow holds at the time of the request. For a host-visible buffer the application rewrites every frame, that is not what an earlier draw used.

Ordinary descriptor contents and all `src` visibility remain submit-time snapshots. Push descriptor values in 0.5.0 are retained when recorded, but retaining a handle/range does not prove record-time CPU byte availability. Neither path establishes G3's premise or tracks update-after-bind changes during GPU execution.

## LIMIT-009: What a draw record leaves out

Vertex buffer slots 0 to 3 and descriptor sets 0 to 3 only. Graphics and compute descriptor state are separate in 0.4.0, but compute dispatch has no sandbox GPU coverage yet. No push constants or dynamic state; indirect arguments are not read and indirect draws get no region hash.

## LIMIT-010: The frame boundary is `vkQueuePresentKHR`

An application that does not present makes a capture time out. With several devices, the probe answers for the one that presented last.

Observed on 2026-10-12 with 0.8.0: the user's LOD1 world has many present intervals without scene draws although surrounding intervals and counters contain graphics. All 36 full-snapshot attempts were empty; no-snapshot collection obtains all nine LOD1 sections. An empty interval is not evidence of an absent character or broken scene. T-023's 0.8.1 opt-in server wait now passes native/GPU/full-suite checks and two real LOD1 observations: first attempts succeed after 8/7 empty intervals, with all 58 relevant entries and both bone windows equal at record/submit. Default capture still returns a single possibly empty interval; the exact present producer remains unproven. Do not label LOD0-world depth-only LOD1 matches as distant-LOD1 color coverage. Evidence: docs/m2-game-recipes.md.

## LIMIT-011: Scope of the sandbox evidence

One GPU and driver (NVIDIA RTX 5060 Laptop). The sandbox uses classic render passes, one queue/swapchain, `vkQueueSubmit`, `vkCmdCopyBuffer`, `vkMapMemory`, two descriptor slots, storage arrays, an immutable sampler, a sampled texture and shader modules destroyed before capture. In 0.5.0 it exercises ordinary writes/copies, core 1.1 templates and KHR direct/template pushes through both entries with primary and secondary command buffers. Dynamic rendering, `2`/maintenance6 variants, core 1.4 push wrappers, KHR template lifecycle/update aliases, indirect draws, dispatch, compute/texel pushes, `vkCmdUpdateBuffer`, `vkCmdFillBuffer` and inline pipeline modules have no GPU coverage. Native C++ tests are separate CPU evidence. The rendered picture was not inspected. Core/synchronization validation and synthetic overhead measurements are in `docs/validation.md` and `docs/probe-performance.md`; neither is game evidence.

## LIMIT-012: The layer entry needs a registry entry for an elevated process

The Vulkan loader ignores path variables such as `VK_LAYER_PATH` in an elevated process, so the manifest in `dist/` would not be found there. The game probably runs elevated (EFMI's `d3dx.ini` has `require_admin = true`, and the EFMI MCP notes say the game runs as administrator). The layer entry would therefore need an implicit layer entry under HKLM, a system-wide change. The inject entry avoids this and is the entry planned for the game (D-011); the layer entry stays the fallback.

## LIMIT-013: What is and is not known about the inject entry in the game

Known from three historical launches on 2026-10-11 (CURRENT_STATE): injection precedes `vulkan-1.dll` by about 4 to 5.5 seconds, and the engine's creation calls reach the hooks. Only 0.2.0 passthrough has been in the game; probe mode and later builds have not.

Still open:

- Long-duration stability. Run 3 survived the previous crash point and included scene changes but ended early. The user accepts it as initial loading evidence, so repeating 30 minutes is not a prerequisite (D-014). BUG-001 remains strongly associated with the removed DX11 hooks.
- A module that statically imports `vulkan-1.dll` and was loaded earlier would keep its old addresses. Not observed so far: the engine's calls arrived.
- Other Vulkan users in the process (Streamline created instances of its own in run 2) go through the same redirected exports. Devices they create are tracked like the game's; the probe answers for the device that presented last.
- The sandbox tests still simulate injection with `LoadLibrary` inside the sandbox.

## BUG-001: The game crashed in the NVIDIA driver in the first Vulkan run with three libraries injected (avoided; strongly indicated cause: the DX11 hooks)

- **Seen**: 2026-10-11 20:29, PID 24040, about 5 s after device creation, while the engine set up its swapchain. Stack: `unityplayer` -> `vulkan-1` -> `nvoglv64`. Crash report: `%LOCALAPPDATA%\Temp\Hypergryph\Endfield\Crashes\Crash_2026-10-11_122943556`.
- **In the process**: XXMI `d3d11.dll` (3Dmigoto), `efmi_probe.dll` (DX11 probe; had redirected `D3D11CreateDevice` of the system `d3d11.dll`), `efmi_vk_inject.dll` in passthrough mode.
- **Not on the stack**: `efmi_vk_inject.dll`.
- **Suspect**: D3D11 / DXGI hooks active while the NVIDIA Vulkan driver creates D3D objects for presentation (D3D modules were loaded right before the crash). Unproven.
- **Not excluded**: this DLL; the game's Vulkan path by itself.
- **How to tell**: run Vulkan with only `efmi_vk_inject.dll` injected (launcher: XXMI DLL injection mode Skip); if that crashes, run Vulkan with nothing injected.
- A Vulkan session never needs XXMI's `d3d11.dll` or the DX11 probe, so the configuration for G1 is the one of the test above regardless of the outcome.
- **Attribution narrowed on 2026-10-12**: a Vulkan launch at 09:44 with XXMI `d3d11.dll`, the DX11 probe and this DLL crashed again (crash report `Crash_2026-10-12_014543454`, about 50 s after start); the launch at 09:47 with the DX11 probe and this DLL, but without XXMI's `d3d11.dll` (injection mode Skip), ran for more than nine minutes in probe mode. The DX11 probe is therefore not the cause; XXMI's `d3d11.dll` in a Vulkan process is. Two crashes with it, two clean runs without it.
- **Result of the test (run 3, 20:38)**: with only `efmi_vk_inject.dll` injected the game passed the point of the crash and kept running. Rule from now on: for a Vulkan session the launcher's XXMI DLL injection mode is Skip and the DX11 probe is not in the injected libraries.

## ISSUE-001: Other implicit Vulkan layers are registered on this machine

Steam overlay, Steam Fossilize and the Epic Online Services overlay are registered under HKLM, the OBS hook under HKCU. They may be active in the game's Vulkan path as well. When reading real-game results, remember that this layer is not the only one in the chain. The sandbox tests switch them off for their child processes.

## LIMIT-014: The cost of full tracking in the game is known for one scene only

Third session, 0.7.0: the user reports intermittent stutter despite about 120 average presents/s. Upload processing accumulated 8.1 seconds; a later steady three-second interval adds only 596 microseconds. Later 0.7.1 game observations confirm spikes with hashing off and with both hashing/resources off (ISSUE-005 / T-020). Attribution remains open; next-launch passthrough is prepared and sandbox-verified only.

Second figures, 2026-10-12 with 0.6.0, same scene: 120 frames per second while idle and during about 1,100 snapshot captures, working set 11.8 GB and flat over 25 minutes, 18.9 MB of SPIR-V for 1015 shaders, about 2,200 live descriptor sets, 3.4 MB kept by a capture of the mod's 45 draws and 14.1 MB by one of all 210 indexed draws.

First figures, 2026-10-12, character display scene: about 120 frames per second with full tracking (no figure without the probe; it may be the game's cap), working set flat at 11.5 to 11.6 GB over 7 minutes, 22.9 MB of SPIR-V kept for 1106 shaders, about 190 descriptor set allocations and template updates per frame. The paragraph below describes what remains unmeasured.

Sandbox overhead figures (`docs/probe-performance.md`) come from a program that does not update descriptor sets per frame and has two shader modules. In the game, every descriptor update takes the exclusive resource lock and updates ordered maps per written element, and every bind takes the lock shared. Memory is bounded for SPIR-V (512 MiB default) and for one capture's descriptor contents (64 MiB default), not for the number of live descriptor sets, images or views. The fallbacks are `resources = 0` and `mode = passthrough` (D-017, `docs/first-probe-session.md`).

## ISSUE-002: The tracker test once failed to link with LNK1163 and linked on the next attempt

2026-11-15 recurrence during D-289: d289-pair.out fails at state.obj COMDAT 0x4906. The preceding build session had exited and no concurrent compiler/linker was running. The failure was reported, source/capacity handling was reviewed, and d289-pair-retry.out rebuilt all artifacts successfully with tracker 801/801. This does not establish a compiler root cause or justify automatic retries.

2026-10-11, building into `build/next/`: `state.obj : fatal error LNK1163` (invalid selection for a COMDAT section) while linking `efmi_vk_tracker_test.exe`; `state.cpp` had not changed since a build that linked. The same command succeeded immediately afterwards and in every later run. Cause unknown. If it comes back, look at whether two builds can write `sandbox/build/tracker/` at the same time; do not add a silent retry.

## BUG-002: Native regression appended to the historical game log (fixed)

The tracker test links the layer logger, whose default path is beside its executable. Running the final suite against dist/ appended one test startup line to the existing game log. The original 2246-byte prefix exactly matched the saved backup; no historical bytes were lost. Preserved the mixed log in build/backups/native-log-repair-20261011-223459/mixed.log and restored the exact original bytes from the before-0.5 backup.

sandbox/native_test.py now always sets EFMI_VK_LOG and its working directory to a new directory under build/tests/. The corrected native test passes 62 checks, with the historical log unchanged before/after (SHA-256 2e8a46cf4985363ce1b5daea4942b5195e4dee1ffaefbe67e1543946e8f961b4).

## LIMIT-015: Scope of the DX11 baseline

Four frames of one DX11 session (mods on twice, off, on again), one scene, one character, the mod's key states as found; the Vulkan frames come from separate sessions with another window size. The third Vulkan session adds content hashes and resolves all four sample ambiguities (T-018); unique shape/size/format matches still have weaker evidence than content matches. Earlier frames with differing per-component draw counts cannot be paired, so both frames need the same scene state. The slot table covers this scene's observed fragment shaders only, and a fragment shader whose pipeline was not exported is listed under its pipeline number. M2 must not guess mappings for new shaders.

## ISSUE-003: `python` on the PATH of this machine is the Windows Store stub

`python` and `python3` resolve to `%LOCALAPPDATA%\Microsoft\WindowsApps` and exit with code 49 without output. Use `D:\Python312\python.exe` (as `.mcp.json` does) or `py`. Seen on 2026-10-12 when a capture script appeared to do nothing.

## LIMIT-016: What the snapshot of 0.6.0 does not see

Indexed draws only, sets 0 to 3, uniform and storage buffers. Record time is represented by a checksum, so `different` does not say which bytes changed. `not_recorded` covers three cases that the item does not tell apart: the draw was recorded before the captured frame began, its index bytes were unreadable when it was recorded, or they had another hash then. The submit-time bytes are the CPU side at `vkQueueSubmit`; a write to mapped memory after that and data produced by compute are not seen. The follow window has a fixed size chosen by the caller (12,288 bytes in the game preset), not the size of a component's bone palette, which the instance entry does not give; in the game the preset reaches behind the last palettes and reports `different` for them in most frames although the palettes themselves are complete (measure with a window of the mod's bone count instead). With `resources = 0` a selected draw has no ranges. The MCP tools do not offer the snapshot.

## LIMIT-017: What the content hash of 0.7.0 covers

Only images filled by `vkCmdCopyBufferToImage` (and its `2` forms) over the whole of mip 0, layer 0; a blit, an image copy, a clear or rendering leaves the state as it was. Formats outside the list in `format_block` report `unsupported_format`. The value is of the upload as submitted, not of what the image holds after later partial writes (those set `partial`). The third session confirms cross-API content matches for this scene and resolves T-018; it does not prove identity for every texture, streaming state or scene. Reading/copying/hashing upload memory accumulated about 8.1 seconds for 2.55 GiB. The prepared dist/ cfg therefore disables hashing after preserving the required evidence (D-023). Counters and CRC equality alone cannot identify intermittent frame-time spikes or guarantee collision-free identity.

## ISSUE-004: The sandbox's expected file could not be opened right after it appeared (handled)

2026-10-12, once in a full suite run: `PermissionError` in `harness.py` when opening `expected.json` immediately after its existence was seen. The sandbox writes a temporary file, closes it and renames it, so its own handle is gone by then; something else held the fresh file for a moment. The harness now waits out a sharing violation inside the 30 s readiness deadline it already has. This is a wait for a known condition with a deadline, not a retry of a failed build step (compare ISSUE-002).

## ISSUE-005: User-visible intermittent stutter with probe tracking (resolved on 2026-10-12 with the streaming loads of 0.7.3, confirmed in the game with 0.7.4)

Resolution: PID 27188, 0.7.4, open world: 24 / 3 / 2 of 87,005 present intervals over 16 / 33 / 50 ms; copies at unmap at 3.6 GB/s; the user reports the stutter gone. Evidence and conditions in `docs/unmap-snapshots.md`. Earlier record follows.

Latest game comparison: 0.7.2 probe/inject PID 41044 still stutters with resources/hash off and profiling on. A clean 24-second world sample has 17 host intervals over 50 ms, retained max 129.125 ms. Expensive unmap snapshots span 128.807 ms of that peak, while a zero-byte copy-record operation waits about 44 ms; the paths contend on res_mx. This strongly identifies a contributor. The bounded history rolls over, so uncorrelated peaks do not exclude lost work. Evidence: captures/pacing-tracker-work-20261012/, docs/pacing-tracker-session.md. 0.7.3 is fully sandbox-verified and deployed after game exit, accelerating detected WC reads without removing snapshots. No post-fix game result yet. Earlier user-confirmed smooth 0.7.1 passthrough remains the control.

The user reports spikes during loading and steady display in PID 19264 on 2026-10-12. Upload processing accumulates about 8.1 seconds for 2.55 GiB; in a later three-second interval it adds only 596 microseconds while 360 presents advance. The average does not measure tail latency, and 0.7.0 has no interval history. Cause of the steady-scene spikes is not established. The texture identity data is now complete, so dist/ disables image hashing for the next launch; it does not hot-reload. 0.7.1 adds bounded host interval diagnostics for a controlled comparison. No game-tested stutter fix is claimed. See docs/third-probe-session.md and docs/frame-pacing.md.

Follow-up with 0.7.1, PID 31124, hashing off/resources on: the user says open-world movement stutters severely and the static display cannot reveal it. Foreground observation over 39.992 seconds: 27 intervals over 33 ms, 12 over 50 ms, maximum 111.204 ms, no captures and zero hash time. This confirms persistent host interval spikes without hashing; it does not identify the cause or establish GPU/display frame times. At that point resource tracking had no off-baseline. A supplementary quiet interval changed focus and is not a clean comparison. See docs/pacing-hash-off-session.md.

Resource-off follow-up, PID 33204: user reports continuing stutter after restart; live settings confirm resources and hashing off. A 30.000261-second selected foreground interval (300/300 polls) has 74 intervals over 33 ms, 26 over 50 ms and a retained peak of 105.000 ms. Hash work, tracked images/descriptors and captures remain zero. Earlier focus-confounded segments are excluded. Different world workloads prevent causal comparison of the counts. Remaining draw/buffer tracking, injection, the game and the rest of the graphics stack are unisolated. Prepared passthrough for the next process after a successful actual-config sandbox smoke; current game remains probe until exit. Passthrough has no pipe and still includes injection/setup/present accounting. See docs/pacing-resources-off-session.md.

## BUG-003: Unreadable buffer copies allocated fallback storage (fixed in 0.7.2; game impact unknown)

The record-time path allocated and zero-filled the requested range before discovering that its source was not CPU-readable; clearing the vector left its capacity until command reset. Native coverage now proves a 64 MiB unreadable source allocates no fallback or destination byte storage. The submit path also no longer copies through a temporary vector. Submit priority, unmap snapshots, record-time fallback and self-copy semantics pass native/GPU regression. These were real unnecessary costs, but their role in ISSUE-005 is not established. Optional profiling adds clock/counter overhead and omits some hook paths; nested/concurrent wall times must not be summed. See docs/tracker-work.md.

## BUG-004: Slow mapped reads hold the resource lock during full staging snapshots (fixed in 0.7.3; confirmed in the game on 2026-10-12)

Confirmed: with the streaming loads the same volume of staging copies takes 7.7 s per 27.9 GB instead of 213.8 s per 69.7 GB, and the long present intervals are gone. The next paragraph was written in between and is wrong: the copied buffers were staging buffers, not mesh pools (`passed_over` 0 of 144,103; D-027).

Wrong attribution of 2026-10-12, kept as record: the title this entry had spoke of staging snapshots. The buffer inventory of the same session shows the copied buffers were the game's 16 MiB index and vertex pools on memory type 4 (2,612 generations on 22 pools; 87 on the 76 staging buffers). They matched the rule because they carry the transfer-source bit and are exactly the limit in size. 0.7.3 accelerates memory type 2 by a factor of 19 and memory type 4 by 1.5 in its own benchmark, so it does not reach them. 0.7.4 copies transfer-only buffers; see `docs/unmap-snapshots.md` and D-026. Not game-tested.

Earlier text:

0.7.2's unmap path copies every eligible mapped transfer-source buffer under exclusive res_mx, using ordinary memcpy. A foreground sample copies 3.46 GiB in 11.625 cumulative wall seconds; large snapshots and shared-lock waits overlap the largest present peaks. Local Vulkan type-2 WC memory reproduces slow reads: 8 MiB takes 24.023 ms normally and 1.270 ms with the production 0.7.3 helper. Snapshot capability is retained; the helper selects streaming loads only after OS WC classification and CPU support checks. Cached/unknown mappings keep memmove; exact bounds, remap and fallback paths pass native/regression coverage. GPU-local mapped type 4 remains expensive even with this helper (150.952 ms for 8 MiB), so it is not a universal mapped-memory performance fix. Game improvement and removal of all stutter remain unverified; keep ISSUE-005 open. D-025 and docs/pacing-tracker-session.md.

## ISSUE-006: The suite in dist/ read the diagnostic settings file and replaced the tested DLL (handled)

2026-10-12: running `sandbox/run_all.py` without `EFMI_VK_DIST` builds into `dist/` and tests there. With the diagnostic settings of a game session in `dist/efmi_vk.cfg` (reduced tracking, profiling) most steps failed, and the build step had replaced the DLL with a new, untested build of the same sources (builds are not byte-identical). The tested files from `build/next` were copied back. The harness now pins every setting to the defaults of the source for a test that names its mode (`PINNED_SETTINGS`), and the native test does the same; `mode=None` still means "use the settings file". Deployment is: full suite against `build/next`, copy those files to `dist/`, then `run_all.py --no-build` there and a smoke run with the settings file as the game reads it.

## LIMIT-018: A draw whose pool is unmapped has no hash (0.7.4 only; not observed, and the default is back in 0.7.5)

Status: in the game no pool is ever unmapped and every open-world hash comes from the mapping, so this did not occur. With 0.7.5 the copy covers every transfer source again; the text below applies only with `unmap_snapshot_all = 0`.

Index bytes are read from the application's mapping or from a shadow fed by tracked buffer copies. Since 0.7.4 a pool is not copied at unmap and its shadow is dropped then, so a draw that is hashed while its pool is unmapped reports `hash: null` with source `none`. Before 0.7.4 it would have reported a hash from the copy, at the price of the stalls of BUG-004. How often this happens in the open world is not measured (T-021). `unmap_snapshot_all = 1` brings the old behaviour back for a capture that needs it.

## LIMIT-019: The 0.7.4 game figures are not a foreground-only sample

The 87,005 present intervals with 3 over 33 ms span everything between two status reads about twelve minutes apart, with the game partly in the background; the 0.7.2 figures it is compared with are a 24-second foreground window. Two attempts to wait for a foreground window found none, then the user ran the route. The user's own report and the copy speed measured inside the process do not depend on this, but a like-for-like interval comparison does not exist.

## ISSUE-007: A wrong attribution was acted on before it was checked (2026-10-12)

The review of the stutter work concluded from per-buffer generation counters that the game's mesh pools were being copied at unmap, changed the copy rule (0.7.4), recorded D-026 and rewrote BUG-004 accordingly. `generation` also counts tracked buffer copies, and the inventory used could not show short-lived staging buffers. The counters added in the same change showed the error in the next game session. Corrected in D-027 and `docs/unmap-snapshots.md`; the code default is restored in 0.7.5. No harm in the game: the wrong rule skipped nothing there.

## BUG-005: The patched bone offsets were one matrix too high (0.8.2 to 0.9.0; fixed in 0.9.1 before any game run)

`MergedSkeleton::patch_entry` wrote the row of the instance's first merged bone into constant 5. The game's vertex shaders read bone `i` at `offset + 3 + 3 * i`, so the entry has to name the matrix before it: the core passes `$merged_skeleton_offset - 3` (in `MergedSkeleton.ini`, not in the HLSL), the DX11 baseline holds `(1533, 3030)` in every patched entry of the 499-bone sample, and the disassembled Vulkan vertex shader of the component 7 draw adds the 3. With 0.9.0 every vertex of a replaced draw would have used the next bone's matrix and the last bone a zero matrix. Found in review on 2026-10-12; 0.9.0 was deployed with the replacement enabled but no game process had loaded it.

Why the suite passed: the native test, the Python reference and the sandbox vertex shader were all written from the same reading (the fixture read `offset + 3 * index` and put its original mesh on bone 1). Now the fixture's shader addresses bones as the game's does, the native test checks the pair measured under DX11, and the reference follows the INI. With the corrected fixture 0.9.0 fails all four GPU configurations. Account: `docs/m2-bone-import.md`. What remains unverified is unchanged: no DX11 compute output was compared and no real character has been drawn.

## LIMIT-020: A capture lists a replaced draw as the application recorded it

With the M2 backend active, `vkCmdDrawIndexed` is recorded for captures before the replacement decision, and the record is not marked. A captured frame therefore shows the original draw (index range, pipeline, bindings) whether or not it reached the driver. Which draws were replaced is known only in total, from `status.replacement` (`matched`, `replaced` and the refusal counters). A per-draw mark would need the layer's own draw to be recorded as well; not built.

## LIMIT-021: Three original texture correspondences do not admit the sample's 2D replacement

The display slot table has 19 original-resource correspondences. Shader typing reveals three 3D targets: 427e2365 ps-t15 to (0,35), 4e9748d0 ps-t15 to (0,36), 50c1e4bb ps-t14 to (0,36). The saved pairing confirms 1x1x1 3D sRGB RGBA8 originals on both APIs; the mapping itself is not disproven. Both saved DX11 mod-on captures bind 2D custom assets in those slots (draws 162, 145, 126). Binding metadata does not prove the DX11 shader's actual interpretation/pixel output. The Vulkan image adapter refuses these assignments and the slot table marks their incompatible dimensions. Do not silently treat the entire material callback as ready after dropping these operations. Sixteen other assignments pass the 2D rewriter offline. Details/evidence: docs/fragment-materials.md and build/fragment-images/report.json. Source/API semantics can be investigated offline before requesting more game input.

Follow-up: matching saved DX11 bytecode was located and FNV-1 checked; each affected declaration and SampleLevel instruction is 3D. A 2D resource therefore violates the D3D11 declaration contract. Headless DX11 controls pass on NVIDIA and WARP, but mismatched views yield zero on NVIDIA and sampled color on WARP, while both retain the bound view. Debug runtime is absent, so no DX11 validation pass is claimed. Evidence: build/dx11-dimensions/report.json. Default refusal remains; an explicit device/profile compatibility policy must not be confused with valid explicit-null semantics or actual game pixel proof.

0.9.6 static-package policy: dx11-invalid-2d-to-3d-zero explicitly marks these three exact profiles and binds the package to the measured vendor/device (4318/11545); other hardware refuses. Default material selection still refuses mismatches. All eleven affected historical contracts and their pipelines pass offline with this named policy, but game pixels remain unverified. See D-037 and docs/character-materials.md.

## LIMIT-022: Full frontend source profiles remain incomplete

The three formerly missing actual LOD1 fragments (0aeef59b, 310c25c3, a6d7088a)
have verified original DX11 correspondence from the user-positioned session of
2026-10-13. Both historical Vulkan LOD1 frames reproduce the same eight rules;
see docs/lod1-material-capture.md. Material replay now accepts 359 of 370 draws,
with eleven LIMIT-021 dimension refusals and no unknown sample profile.
Original Core texture queries need proven source-API identities, including the
effects of prior bindings. The historical runner intentionally supplies no such
provider and retains 283 unresolved calls. Native query mechanics are tested with
synthetic identities only. Full runtime/generation and DLL integration are still
implementation work and must not be marked done by a new capture. Do not suppress unknown runtime queries or broaden scope to LOD2+.

0.9.6 links static materials and shared resource helpers into the DLL under T-028;
it deliberately does not execute original Core source-identity queries. The full
hosted runtime remains open. Its packaged policy accepts all 370 historical draw
contracts; this does not resolve the historical runner's 283 source-query calls.

## LIMIT-023: The variant design assumes three application sets; a matched draw uses four (lifted for the character backend in 0.9.8)

Status 2026-10-13: 0.9.8 takes the private set's index from the layout (three to eight sets) and the character backend replaces draws on four-set pipelines; see BUG-009. The single-component backend of 0.9.1 and `MaterialProfile` still name set 3; they are not in use. The text below is the review finding as written.

Found in review on 2026-10-13 from saved game frames, no new session. `prepare_vertex_variant`
requires exactly three source sets, moves the vertex buffers to a private set 3 and expects the
instance window at set 2 binding 0; the fragment patch and `MaterialProfile` also name set 3. In
the 0.9.1 display frame (`captures/m2-component-0.9.1-20261012/.../display-a/attempt-01`) matched
draw 38, component 10, is recorded with pipeline 437: four sets, depth only (no color attachment),
bone buffer at set 3 binding 2. Its preflight refuses. Four-set pipelines are ordinary in this
game: 73 to 120 draws per saved frame have a set 3 bound. The other four saved frames have no
matched draw on such a pipeline, so the case depends on the scene or the pass.

Consequences. The sample deletes the mesh of component 10, so the right result for that draw is a
skip, and a skip needs the frontend to run for the draw, which needs its entry and bone windows
located without the variant preflight. If the draw falls back, the original mesh stays in that
depth pass while the color passes omit it. A mod that draws geometry for such a component needs a
variant whose private set index follows the layout (set count, below `maxBoundDescriptorSets`)
instead of the constant 3. Related and unexplained: the live 0.9.1 session counted 227 refusals of
57,751 matches under one combined reason string; whether a bound set 3 was the condition is not
known, and no saved frame shows a three-set matched draw with a set 3 still bound. A counter per
refusal condition would answer it in the next session. A whole-character acceptance has to
account for every refused draw, because one fallback is one frame of the original mesh.

## LIMIT-024: Scope of the static whole-character backend

1.0.2 follow-up: an opt-in runtime sidecar supplies original main-INI conditions,
keys, persistence and F10; see docs/live-ini.md. Without a sidecar the package
remains static. General image-hash override/CustomShader/Include support and the
HLSL panel drawing adapter remain open; do not claim full-folder compatibility.
The native rendering limits below still apply. Actual-character acceptance of
the live main INI is pending.

One scenario is fixed when the package is built; keys, conditions, reload and runtime Core texture-identity queries remain absent. A draw that is instanced, in a secondary command buffer, in dynamic rendering, on or with pushed descriptors at the private set's slot retains the original. Pipelines with three to eight sets are replaced since 0.9.8 (private set at slot 3 or above; slots past the tracked four only while no application layout is that large). Ordinary bound application set 3 is restored since 0.9.5. Stream elements may extend up to 64 bytes past the file stride with padded backing storage. Identity follows position across frames; within a frame it requires the same exact position. LOD0/LOD1 only. The earlier 0.10.0 mesh build ran in the game, 0.9.6 adds static materials with sandbox/offline evidence only. See docs/character-backend.md and docs/character-materials.md.

## BUG-006: Character draws refused while the application had a set 3 bound (build 0.10.0; fixed in 0.9.5; NOT the cause of the flicker)

Correction of 2026-10-13: the refused draws were the character's draws on four-set pipelines in the visibility pass (BUG-009). A set 3 was bound there because the pipeline itself uses it. 0.9.5 let those draws pass this check and they failed the next one, so the flicker stayed. The restoration of an application set after a three-set replacement is still right and still tested. The sentence below that explains the flicker and the 227 refusals of 0.9.1 by this condition names the wrong mechanism.

The replacement binds its private set at slot 3. Both backends refused a draw when the tracker showed an application set there, to avoid overwriting it. In this game most draws bind four sets and a set 3 stays valid across the three-set character draws, so the refusal hit about one character draw in 150 (82 in 378 frames, live counters of 2026-10-13) and each one showed the game's own mesh for a frame: the flicker the user saw at LOD 0. The same condition explains the 227 refusals of the 0.9.1 session. 0.9.5 keeps the handle of the layout a set was bound with and binds the application's set 3 again after the replacement. Verified in the sandbox with a scene that fails against the old build; the game confirmation is pending. The single-component backend of 0.9.1 still refuses; it is not in use.

## LIMIT-025: Shadow passes of a LOD 0 character read partly LOD 1 poses

Found offline on 2026-10-13 from two saved LOD 0 world frames while looking for the flicker; not its cause. The first shadow pass draws LOD 1 meshes and imports their remapped poses; the second shadow pass draws LOD 0 meshes, and a component drawn early there reads the LOD 1 poses of components that come later (component 2 for components 1 and 9, matrix terms differing by up to 0.4). Both passes are depth only on a D16 target. The passes with color attachments read the same bones as the frame's final state. The core imports by the same rule under DX11. A master per LOD would separate them.

## BUG-007: Internal upload command buffers lacked loader initialization (fixed before 0.9.6 deployment)

The first 0.9.6 full sandbox regression exposed access violations in all three material cases through the layer entry; the inject cases passed. The new integrated uploader allocated internal command buffers through downstream Vulkan functions without initializing their loader dispatch data. Device setup now retains the loader callback, and internal queue/command-buffer setup calls it where available. Both material entries, all ten character hook cases and the final full suite pass. This was a pre-release sandbox failure, not a game crash. Evidence: build/material-0.9.6-regression.txt, build/material-loader-fix-tests.txt and build/material-0.9.6-regression-final.txt.

## LIMIT-026: Static material queue scope (multiple queues supported in 0.9.7)

0.9.6 required exactly one enabled unprotected graphics queue and refused the actual game at initialization (BUG-008). 0.9.7 supports up to 32 enabled queues in one unprotected graphics family, a single uploader and explicit one-time publication to all consumers. Multiple graphics families and protected queues still refuse. The chosen uploader must continue submitting during file preparation. BC compression uses enabled features, not merely hardware support. Both entries, cross-queue drawing and actual DDS uploads pass offline; the new version's game result remains pending.

## BUG-008: The static material backend refuses the game's graphics queue setup (fixed offline in 0.9.7)

User-started PID 18868 on 2026-10-13 loads 0.9.6 but shows the original model. Log and status prove initialization refused at "materials require one unprotected graphics queue", before any mesh or image upload, with zero replacements. No shader/asset fault or game crash is established. The exact requested queue count was not retained by that build. The two-queue sandbox reproduces the same refusal through both old DLL entries. 0.9.7 removes the single-queue restriction within one graphics family and adds semaphore publication before consumer work. Eighteen character-hook cases pass 261 checks, including queue 1 only and upload/render on separate queues through all submit APIs; full suite and real-file/config smoke pass. Deployed at 16:23 after the user's normal exit, with dist smoke passing; no 0.9.7 game acceptance yet. Evidence: captures/materials-0.9.6-20261013/, build/material-queues-0.9.6-control.json and build/material-0.9.7-regression.txt.

## BUG-009: Character draws in the visibility pass keep the original mesh; flicker at LOD 0 (0.10.0 to 0.9.7; replaced since 0.9.8; effect on the flicker not established)

Game, 2026-10-13 with 0.9.8: twelve seconds standing show no refusal, so these draws are replaced. The user first found the normal state free of flicker, later not. Whether this pass ever caused the visible flicker is therefore open; the text below gives the inference as it was made.

User report with 0.10.0 and again with 0.9.7. Measured on the running 0.9.7 process: `refused_material` 85 in 393 frames, nothing else refused; in forty captured frames every refused character draw is on four-set pipelines 399/402/404/406 in render pass 12 (no color, depth EQUAL without write on the G-buffer's depth, a storage buffer written by the fragment shader, no images). The backend required three sets (0.10.0: refused under `refused_set3`; 0.9.5 to 0.9.7: `refused_variant` or `refused_material`), so the game's own mesh was tested there against the mod's depth. Inferred, not yet observed: the game reads the result as "part hidden" and omits the part for some frames. 0.9.8 puts the private set above the pipeline's own sets and accepts a fragment shader whose layout has no fragment-stage image without a material profile. Sandbox scene red against 0.9.7 and green with 0.9.8; the four real vertex shaders rewrite to set 4 and validate. Evidence: captures/flicker-0.9.7-20261013/, docs/character-backend.md (last section).

## ISSUE-008: The cause of the flicker was named before the refused draws were looked at (2026-10-13)

The first diagnosis took the counter name `refused_set3` for the mechanism and "fixed" it in 0.9.5 without capturing a frame that contained a refused draw; the user reported the flicker unchanged two builds later. Same kind of error as ISSUE-007. Rule: a refusal counter names the check that fired, not why the draw is what it is; before acting on one, capture a frame with such a draw and read its pipeline and pass. Also: `scripts/smoke_m2_config.py` shows `images_failed` for a material package because its sandbox device has no BC compression enabled; `scripts/check_character_assets.py` is the check for materials.

## BUG-010: A fragment shader without a profile kept the game's mesh (0.9.6 to 0.9.8; fixed in 0.9.9, confirmed in the game for the view from below)

Game, 2026-10-13 with 0.9.9: the user finds the view from below right; no draw is refused; 26 fragment shaders took a sibling's rules.

Game, 2026-10-13 with 0.9.8. The dithered fade (camera close to the character; dash) switches every pass of every component to another fragment shader. The material package knows shaders by their exact bytes, and the backend refused a draw whose shader it did not know, keeping the game's mesh; components the mod hides stayed hidden. On screen: the original model mixed with the mod's, as a flicker in the dash and steadily when looking up from below. 0.9.9 takes the rules from the profiled sibling (docs/character-materials.md) and otherwise draws the mod's mesh with the application's images. Verified on the captured game data and in the sandbox; game result pending.

## LIMIT-027: After-images of the dash keep the game's mesh (D-186 in the sandbox, 2026-11-12; not yet in the game)

- D-186 (sandbox only, not in dist): the bones of a uniform block of 4096 vectors, as the original Core's cb3/cb4 source: the rewrite takes the block at b4, then b3, as a read-only storage block at the private set's binding 0, the component's bones are imported from the block on the CPU, and the draw binds [the game's four config rows][the merged bones]. The three dash vertex shaders of captures/dash-2.1.7-20261112 are accepted and valid after the rewrite; the live scene draws a whole character by such a pipeline as the Mod's. To see in the game: Zhuangfangyi's after-images with the Mod's mesh and textures while she dashes, the package's `constant_bones` rising, `refused_variant` with `spirv_required_buffer_missing` gone, `incomplete` and `bones_unavailable` not rising during the dash. Left: such a draw for an object whose other components the device imports reads only what the CPU master holds (refused as `incomplete` otherwise); if the game draws an after-image with ordinary pipelines as well, that is where it shows.

- Cause found 2026-11-12 (2.1.7 in the game, the user: Zhuangfangyi's textures turn into the game's while she dashes): the dash effect pipelines' vertex shaders (5370913c, 863d7ba9, 5eb8bfbc on game 1.6.3; blended colour passes with two targets) have no bone storage buffer: they read the bones and their offsets from a uniform block of 4096 vec4 at set 3 binding 0 (the first vec4 the offsets, the bones at offset + 4 + 3 * bone). The original Core supports this source (MergedSkeleton.ini: `DetectBoneDataSource` cb3/cb4, "Sometimes used for FX shaders"; the bones imported by BoneDataImporter_ConstantBuffer, and a copy of [the four config rows][the merged bones] bound in the cb's place). The layer's vertex rewrite requires a storage buffer (`spirv_required_buffer_missing`). Their SPIR-V and pipelines: captures/dash-2.1.7-20261112.

Seen in captures/flicker-0.9.8-20261013/ (frame 150562 and others): character draws on four-set pipelines whose vertex shaders (8d5133a1, 2cf51393) have no storage buffer; the variant preflight says `spirv_required_buffer_missing` and the draw is refused under `refused_variant` (954 in that session). Where these shaders take the pose from is not known. The after-image therefore has the shape of the original model.

1.0.1 follow-up, PID 19452: the user reports that dashing also seems fine. In a requested 24.156-second dash interval, 58 captured frames contain 3,766 character matches on 98 supported pipelines; runtime refusal, pending and identification deltas are all zero while sibling/inherited material rules execute. Neither historical after-image vertex shader appears in these draws. Other parts of the session have variant refusals with the same retained missing-buffer reason, but were not captured at draw level. Keep the historical path as unsupported/uncovered; do not describe the sampled dash as visually broken or this path as newly fixed. Evidence: captures/stability-1.0.1-20261013-204259-19452/dash/.

2026-11-11, 2.1.3 in the game: the reason was noticed on screen for the first time (notices exist since 2.1.2): Zhuangfangyi Summer Collection 1.1 at 19:35:20, "character variant: vertex_rewrite:spirv_required_buffer_missing", and 58 ms later "character draw reads bones of a component no device import wrote" for the same Mod, both once (a notice is shown once per Mod and reason). What the user was doing, how many draws and whether it showed is not known; no frame was captured and the counters were reset by a later F10. To take it further a frame with such a draw has to be captured in the game: its vertex shader and where it takes the pose from.

## ISSUE-009: The normal-state flicker has no measured cause (2026-10-13; answered the same day, see BUG-011)

With 0.9.8 the user reports an occasional flicker while standing or walking. Twelve seconds of counters in that state: nothing refused, nothing identifying, nothing pending, one position for all character draws. Candidates not yet tested: lost identities (2,653 `identifying` draws in the session; 0.9.9 makes identities survive gaps and logs each new one), poses of other components being one import behind within a frame, the picture of generated frames. Do not act on any of these before the 0.9.9 counters and log say which.

## BUG-011: Frames counted at the present; a moving character became a new object for a frame (fixed in 1.0.0; sampled 1.0.1 game checks pass)

The flicker the user reported from the first whole-character session on, in the normal state. Measured with the identity lines of 0.9.9 on 2026-10-13: 16 of 19 new identities next to a replaced object were made within the same counted frame as that object's last draw, 4 to 8 ms after it and 1 to 3 cells away. The backend read a present-based frame count when a draw was recorded; the game records its next frame before the previous one is presented, at irregular moments (frame generation). Within a frame an identity matches only the same place, so the character that had moved was a new object, and the game drew it for a frame. Earlier explanations (BUG-006, BUG-009) removed real faults that were not this one. 1.0.0 takes the recording of the command buffer as the frame. Sandbox phase red against 0.9.9 and green with 1.0.0; game result pending.

Follow-up on 2026-10-13: the latest log proves 1.0.1 loaded in PID 27816; the user now says models and textures seem fine. The session still records 50 nearby identity creations, 38 within zero counted frames. No game process or probe remains at review time, and the events have not been correlated with actions or pixels. This neither establishes a new flicker diagnosis nor closes full movement acceptance. Evidence: captures/user-review-1.0.1-20261013/.

New session PID 19452: the user reports no apparent flicker. Before exchange, 1,534 LOD0 and 101 LOD1 drawing frames have no refusal/identification/pending; after the unchanged-code exchange, 710 LOD0 frames and a requested 964-frame dash interval also stay clean. The user confirms normal appearance after exchange. This is positive real-game evidence for the sampled paths. Clustered identity lines elsewhere, with user-reported dashing, remain uncorrelated with individual effect draws and are not proof that ordinary flicker persists. See captures/stability-1.0.1-20261013-204259-19452/; LIMIT-027 remains a separate uncovered path.

## BUG-012: Expanded INI dependency graphs exposed freed optimizer branches (fixed in candidate 1.0.4)

The original optimizer owns conditional branch lists temporarily and releases
them when finished. The adapter retained every raw registered pointer, including
removed branches, and the expanded Core dependency graph produced an RTTI access
violation during adaptation. Filter temporary branch roots before optimization,
then recurse through surviving owning commands. Nested eliminated-branch and
original Core fixtures cover the repair. This was reproduced offline; no game
crash is attributed to it.

## BUG-013: Key/preset-only callees were left uncompiled (fixed in candidate 1.0.4)

Dependency discovery previously started from scene/Present roots. A command
called only by a key or preset could stay empty; cycle parsing also copies its
temporary activation lists, so retaining those list addresses is invalid.
Record stable named callees at the original Override parser boundary and compile
their dependency graphs once. Enumerate presets before Constants. Fixtures
exercise hold activation/release, callbacks from Constants presets and both
cycle entries. Preserve original accumulated cycle callbacks and smart selection.

## LIMIT-028: Original INI syntax coverage is not complete Vulkan execution

Candidate 1.0.4 adds source-derived parsing/evaluation checks and CPU directory
generations. The full-folder inspector parses shader/resource metadata that the
production character runtime cannot yet execute. Non-native CustomShader,
ShaderOverride and ShaderRegex sections refuse; general copy/clear/readback,
non-indexed/indirect draws, dispatch and GPU parameters remain open. Global
system/driver settings and every argument combination are not exhaustively
validated. docs/ini-language-coverage.md is the current operation-family table.

Through 1.1.1, original owned-buffer commands, whole-pool operations and keyed/
expiring pool lifetime now have GPU execution tests. The 1.1.2 image foundation
adds directly tested owned allocation, views, pitched transfers, clears and
MSAA resolve. The 1.1.3 adapter connects original INI declarations, DDS, image
copy pools, view-specific clears and properties. Remaining formats/conversions,
general shader execution, application import and inline placement stay open.
Do not treat these owned-domain tests, 184 registry format records or 726 syntax
tests as complete Mod support. Logical image slots do not emit shader descriptors.

## ISSUE-010: A mode fixture exited before its first probe request

During the first 1.0.4 full regression, inject-late sandbox PID 3612 ended
normally after two frames (build/tests/sandbox-vv5mn581/stdout.txt). Its output
was already closed within the startup second, before the scheduled status
request; the client reported a closed-pipe error. The mode code's normal timeout
is four seconds, so timeout expiration is not established. No failure cause or
game implication is inferred. The unmodified standalone modes test and the next
complete suite both passed all 40 checks. Keep the original failure record in
build/next/run_all-1.0.4.out; no blind retry or production IPC change was added.

## BUG-014: A scalar pool write discarded unrelated resource aliases (fixed in candidate 1.0.5)

The adapter conservatively erased every cached reference in a pool when any
variable in that pool was assigned. This lost live bindings in other slots and
the resource half of mixed elements. Respect the original per-resource is_null
state instead: reference assignment sets it, the original pool engine changes it
only for affected elements, and subsequent binding resolution observes it.
Source-driven cases fail before the fix and pass afterward, including variable
and resource reset, mixed values, independent slots and proxy behavior. The
separate production-runtime fixtures check owned plans and generation isolation.
No historical game symptom has been attributed to this offline-found defect.
The full rebuilt regression passes in build/next/run_all-1.0.5.out; not deployed.

## BUG-015: Referenced buffers were reused as writable copy-cache allocations (fixed in candidate 1.0.8)

The owned GPU adapter reused a destination's current binding when its buffer
description matched. If that binding came from ref, a subsequent copy overwrote
the aliased source. It also retained only the current description, unlike the
original ResourcePool, so copy_desc could lose previous cached data after a size
change. The GPU byte-result test reproduces the first defect against 1.0.7 in
build/resource-commands/copy-alias-before-fix.{json,txt}. Separate owned pools now
retain allocations per description and instruction/custom resource. Reference
assignments change the binding without entering borrowed allocations in a copy
pool. The fix and region-copy fixtures pass 243 GPU checks with clean validation;
the full rebuilt suite passes in build/next/run_all-1.0.8.out. This backend was
never deployed into the game.

## BUG-016: Custom pool view metadata and null caches differed from the source (fixed in candidate 1.0.9)

The adapter inferred format/offset from every source view. Original GetViewInfo
only interprets stage target views, so a typed UAV fetched into a pool by a plain
range still has unknown custom-resource format. Rebinding it as an SRV can select
a raw view. The GPU fixture fails against 1.0.8; preserved in
build/resource-commands/metadata-before-fix.{json,txt}. Target-specific metadata
now matches the original helper. Null keeps its cached resource/view, and
source_stride is set by copies rather than overwritten by generic SetResource.
Format/stride-only unassigned aliases do not create zero-sized buffers. The
303-check GPU suite passes with clean validation; the complete rebuilt regression
passes in build/next/run_all-1.0.9.out.
No deployed/game result is implied.

## BUG-017: A CPU command failure left recorded GPU work submittable (fixed in candidate 1.1.1)

The GPU extension marked its scope failed only when its own command/query
callback failed. An original CPU pool expression could fail after earlier GPU
commands had been recorded, return failure from IniRuntime, and still allow
GpuResourceRuntime::finish to submit that partial work. The new fixture fails
before the fix in build/resource-commands/cpu-scope-before-fix.{json,txt}.

IniRuntime now notifies its extension on any failed Present/draw execution,
including failures outside GPU callbacks. An active GPU scope is poisoned and
cannot submit; discard releases its pending owners. The targeted suite passes
485 checks with clean core/sync validation, including CPU failures in both
Present and draw, retained bindings and rejected frame duration. Full rebuilt
regression passes in build/next/run_all-1.1.1.out. No deployment or game result
is implied.

## BUG-018: Stencil-only clears forwarded an invalid unused depth value (fixed in candidate 1.1.3)

The owned image foundation validated the depth field only when the clear range
included depth. A legal original `clear = od stencil 11` therefore forwarded
depth 11 into VkClearDepthStencilValue, whose range requirement applies even
to stencil-only commands. Validation reported VUID-depth-00022; the preserved
failure is build/resource-commands/stencil-before-fix.{json,txt}. This was an
offline test failure, not a game observation.

The low-level executor supplies depth zero when that aspect is unused. The INI
adapter separately applies D3D11's depth clamping when it is used. Actual depth
and stencil readbacks, ignored nonfinite depth and the original explicit-flag
priority pass in the 1682-check targeted suite with zero validation messages.
The final full-suite checkpoint is recorded in CURRENT_STATE.md.

## BUG-019: Float depth uploads lacked the required range/extension check (fixed in candidate 1.1.4)

The owned host-image uploader accepted arbitrary D32 and D32/S8 depth-plane
bytes on devices without VK_EXT_depth_range_unrestricted. Vulkan requires
copied depth in [0,1] in that case, so accepting the upload did not establish
defined results. An older packed D32/S8 DDS fixture also generated random depth
bits; zero core/sync validation messages did not verify that payload constraint.

Host uploads now inspect only copied texels, ignoring row/image padding, and
reject out-of-range or NaN values before recording. The ordinary DDS fixture
uses valid depths; a separately enabled test device covers arbitrary float
bits. The new color-to-D32 bridge requires actual logical-device extension
enablement because it cannot inspect device-only bytes without a readback.
Depth-to-color and the R16 family do not require that extension. The explicit
low-level buffer-to-depth primitive still relies on the caller's range proof.
This is offline resource work, not a defect observed in the deployed game.

## LIMIT-029: Native integer RTV overflow differs between reference and hardware

Candidate 1.1.4 follows native WARP truncation, NaN-to-zero and channel saturation.
The observed hardware agrees for fractions/ordinary values but can differ for
finite inputs outside the 32-bit range. The adapter chooses WARP's deterministic
saturation rather than an undefined C++ float-to-integer cast or a vendor rule.
Tests compare 19 integer formats and two typeless-view cases against WARP. The
archived DDI specification's rounding wording also differs from both measured
native devices; no cross-vendor bit-identical overflow behavior is claimed.
See docs/gpu-resource-commands.md and build/resource-commands/dx11-clear-*.txt.

## BUG-020: Constants post was omitted and root abort leaked into post (fixed in candidate 1.1.5)

RuntimeCompileConstants previously supplied no post list. execute_pair reused
the pre phase's aborted flag for post, although original RunCommandListComplete
creates a fresh state for each root invocation. The adapter now compiles/runs
both Constants phases, clears default persist dirty state between them, and
isolates root abort while preserving nested abort behavior. A plain run can
execute the nested abort in both phases; the new root-isolation fixture uses
explicit pre run to distinguish that legitimate case from leaked state.

The GPU fixture verifies separate zero/pre/post publication, post-persist dirty
state, aborted-pre values and post execution. The CPU fixture also exercises
pre-only nested abort with post execution. Full regression passes in
build/next/run_all-1.1.5.out. This was an offline source-audit finding, not a newly
observed game defect.

## LIMIT-030: HLSL compilation is not complete CustomShader execution

Candidate 1.1.6 compiles all six original stage kinds and retains native source
registers, but the general INI shader executor is not connected yet. DXC refuses
the original enable_backwards_compatibility flag in SPIR-V mode. Native model-5
compilation itself rejects the old software/preshader flags. ieee_strictness
passes for the tested Vulkan 1.2 target, not the 1.1 target; the eventual executor
must check actual logical-device support and float-control properties. Shader
storage formats, device limits, null resources, graphics conventions/state and
application placement require execution adapters. See docs/hlsl-compiler.md.
This is an offline compatibility boundary, not a new game regression.

## BUG-021: Draw argument guards omitted direct compute queries (fixed in candidate1.1.7)

Only indexed arguments were traversed. A valid dispatch using iniparams->width
reached an unadapted original query and failed the signed argument check. The
common walker now covers all parsed draw/dispatch forms and indirect targets.
Before-fix evidence: build/resource-commands/dispatch-query-before-fix/. The
original INI GPU fixture and final full regression pass after correction. This
does not enable unimplemented indirect/graphics execution. No game defect claim.

## LIMIT-031: Owned compute is a partial CustomShader execution adapter

Candidate1.1.7 executes direct compute with original lists and owned resources.
Graphics/state/draws, indirect dispatch, workgroup storage, added capabilities,
null source resources and alignment adaptation remain. Low-level sampler/counter
descriptors work; original source-state mapping remains. Storage views require
declared format agreement. Sampler validation conservatively checks every image
against every sampler; exact usage analysis is next. Actual enabled features and
serialized queue scope are explicit host inputs. General directory rendering,
application import/render-pass integration are not enabled. See custom-shaders.md.


## BUG-022: Source binding hazards and batch publication (fixed in 1.1.9)

Native WARP proves UAV binding clears conflicting SRV/VB/IB and output slots;
input binding against an output yields null. Buffer view ranges still conflict.
Texture mips/layers can be independent. Original RTV/DSV assignment calls
OMSetRenderTargets with eight slots, clearing pixel UAVs even for null RTVs.
Current logical slots only overwrite entries; ranges publish individually despite
source SetSlotRange's batch behavior. OM restore must clear conflicting inputs
and compute UAVs. The old scope fixture's UAV+SRV coexistence assertion is wrong.
Probe source/results:build/source-state-probe/hazards.cpp and hazards.txt.
Invalid duplicate-UAV batches preserve prior bindings, but can contaminate later
native hazard observations even after ClearState. Isolate invalid cases or run
last. New1.1.8 distinct-resource compute/native output comparisons remain valid.
This was an offline semantic gap, not a game regression. Reproduced by the new
native reference before the fix at 3728 checks; evidence is retained under
build/resource-commands/binding-hazards-before-fix/. Candidate 1.1.9 fixes source
hazards, atomic publication and OM restoration. Additional native probes show
pixel UAV assignments replace their whole set (unlike compute), DSV dimensions
may exceed color dimensions, and complete OM binding discards high pixel UAVs.
The pinned save helper only preserves slots 0-7. Old fixtures now unbind writers
before reading, without removing byte assertions. Full rebuilt suite passes:
resources 4006, clean core/sync through shutdown and all remaining steps in
build/next/run_all-1.1.9.out. This does not implement application binding import,
read-only application DSV flags or graphics drawing.

LIMIT-031 follow-up1.1.8: ordinary fixed shared storage, constructor/array sample
pairs, indirect dispatch and owned sampler defaults/host snapshots now work.
Special layouts/provenance, graphics/null/alignment cases and application import
remain. Original owned INI UAV creation lacks append/counter flags; do not invent
those semantics from low-level counter support.

## BUG-023: Raw Vulkan graphics built-ins differ from SM5 (fixed1.2.1)

Native DX11 WARP pixel references expose missing compiler conversions. For clip
w=2, source SV_Position.w is2 while raw Vulkan returned0.5. Nonzero start vertex/
instance offsets similarly leaked into SV_VertexID/SV_InstanceID. Evidence for
all three independent failures is under build/graphics-coordinate-before-fix/.
DXC options -fvk-use-dx-position-w, -fvk-support-nonzero-base-vertex and
-fvk-support-nonzero-base-instance correct these. Native/Vulkan direct and indexed
pixel comparisons, signed base vertices, multiple instances and winding pass.
The generated DrawParameters capability needs actual shaderDrawParameters enabled;
production graphics must gate it or implement an explicit lowering fallback.
Full rebuilt1.2.1 suite passes, resource4559/core+sync clean. This is an offline
compiler defect discovered before original graphics integration, not a game claim.


## LIMIT-032: Owned INI graphics is a measured subset of native execution

Candidate1.2.3 executes original VS/PS direct, indexed, instanced, indirect and
automatic/from-caller graphics on owned targets, with native pixel references.
Vertex layouts/edits, other graphics stages/topologies, depth bias, multisampling,
attachment-free passes, source comparison sampling, null fetches and imported
caller indirect/dispatch/stream-output state still require adapters. Arbitrary
invalid native blend/raster/sampler state creation and fallback have not been
reproduced; only measured inactive-state normalization and invalid active stencil
fallback are implemented. Pipeline/sampler counts are bounded; aggregate shader
CPU payload and repeated comparison-op scans still need review before broad use.
Application state import, source shader matching, complete directory scheduling
and actual game command placement remain separate work. Do not infer all-INI
compatibility from parser or owned graphics success. PNG remains paused.


LIMIT-032 follow-up1.2.5: owned geometry now accepts point/line/triangle input
with triangle-strip output and enabled features/limits. Original TextPrinter
HLSL/font executes against owned sandbox resources with native pixel/formatter
comparisons, including scalar stride72 and unclipped packed z/w. The full Core
INI, application backbuffer/window queries and real placement are not proven.
Input layouts/edits remain next; docs/input-layout-audit.md records native
creation-register matching and lifetime behavior. Do not restore edited layouts
at each CustomShader or reinterpret valid large native offsets as creation failure.


LIMIT-032 follow-up1.2.6: measured original input-layout execution now covers
creation signatures, offset/format edits, step0/1, APPEND and proper intercepted
AfterDraw lifetime. Remaining native reinterpretation, null/OOB vertex fetch,
packed16 BGR and arbitrary/indirect divisors need explicit lowering/features.
Both local GPUs expose KHR divisors, but packed16 BGR vertex-format support is
absent. Do not map those formats blindly or classify valid native offsets above
Vulkan limits as failed native creation. Source helper realignment uses an enum-
based boundary; intentionally preserved (docs/input-layout-audit.md).


LIMIT-032 follow-up1.2.7: enabled KHR/core instance divisors now cover source
steps0/2/3/max for direct/indexed/indirect draws with native pixels. Unknown
indirect bases require actual nonzero-base property support; unavailable features
still refuse. Type reinterpretation/BGRX, packed16 BGR and null/OOB fetch remain
next, independently from application feature enablement and directory execution.


LIMIT-032 follow-up1.2.8: owned float/int/uint reinterpretation, fetched defaults
and BGRX alpha1 now execute with exact native pixels. Scalar/vector widths1..4
and ordinary/component/aliased loads are covered. Arrays/packed signatures,
null/OOB fetch and pointer-function/copy/selection forms remain explicit.
Packed16 BGR is optional native IA support: the local NVIDIA hardware rejects
it, while WARP accepts but produces defaults rather than decoded data. Do not
promote its creation-only evidence to pixel compatibility. See input layout audit.

## BUG-024: Direct multi-match entry interleaves pre and post phases (fixed1.2.9)

Original defect: IniRuntime::draw executed each
matched TextureOverride pre/post pair separately. Original texture checking runs
all matches of the current phase before the later root post invocation. A trace
fixture yields1324 instead of1234. Per-match resets also split shared planner
bindings, pending layout edits and execution budgets. Root post must receive
fresh transient state while keeping already-applied device bindings/layout.
Evidence:docs/matched-command-order.md; build/source-state-probe/matched-order
.cpp/.txt. No game evidence is required to reproduce or correct this ordering.

Correction1.2.9 groups both matched phases into one transaction, keeps shared
budgets/bindings and uses a fresh independent post root. CPU118 and resource9020
checks pass, including native pixel comparisons and partial-submit refusal.
Full rebuilt regression passes in build/next/run_all-1.2.9.out; no game result.


LIMIT-032 follow-up1.3.0: opt-in original ShaderOverride stage roots and tracked
shader queries execute on owned resources, with native pixel/compute references.
Legacy depth/model substitution, ShaderRegex assembly patching and original
indirect/stream-output import still refuse. Original source hash/declaration
facts must be supplied separately from translated executables. Present's owned
frame/counter boundary is not application Present integration. General resource
identity, descriptor/region matching, directory scheduling and application
placement remain next; docs/resource-matching-audit.md records source semantics.


LIMIT-032 follow-up1.3.1: general resource matching now reuses original hash,
descriptor and region/draw predicates with source target/range/pool semantics.
Owned reference identities and native cached-matcher comparisons pass. This is
not provenance for application resource hashes or import of stream-output,
swapchain/indirect state. Dynamic source hash-policy updates remain the host's
responsibility. Original ShaderRegex linkage and application placement follow.


LIMIT-032 follow-up1.3.2: original patternless ShaderRegex links now drive owned
Core IB replacement, including key enable/disable, repeated binding and native
pixels. Pattern-bearing model matching still requires original assembly evidence;
patched execution also needs bytecode translation and exact source association.
General application/window/backbuffer state, directory scheduling and actual
command placement remain separate. Full rebuilt1.3.2 suite passes; no game result.


LIMIT-032 follow-up1.3.3: original text matching and patch/reassembly are now
verified with native WARP execution and bounded isolated preparation. Pattern-
only matching drives owned Vulkan pixels. Bytecode-changing rules still refuse
runtime publication until translation is verified. No application/game evidence
is implied; source shader provenance remains independent from executable hashes.


LIMIT-032 follow-up1.3.4: exact original/reassembled DXBC executes in owned
compute/VS-PS tests with native references, including raw/structured/typed buffers
and counters. Regex runtime publication and native view stride/type adaptation
remain next. Packed signature components, extra stages/features and application
shader correspondence remain explicit boundaries.

## LIMIT-033: Native float32 denormal control differs across local devices

RTX5060 Laptop reports no explicit float32 preserve or flush-to-zero property;
Intel RaptorLake-S reports both. Strict translated modules therefore execute only
on the selected Intel test device. GPU capability checks reject unsupported mode
or missing extension enablement before creating a pipeline.

A separate NVIDIA default-mode experiment agrees with native WARP for16 boundary
inputs under raw copies/add/multiply/equality, but differs in6 positive/negative
subnormal rows for negation and absolute value. Native outputs signed zero; the
NVIDIA default retains denormal bits. Intel strict mode agrees for all16 rows.
Evidence:build/float-controls/default_mode.cpp/.txt and sign_mode.cpp,
sign_nvidia.txt, sign_intel.txt; core/sync clean. This is offline evidence only.
Original vkd3d/state.c uses ANY mode, but that alone does not meet this measured
source contract. Next implement bounded float32 lowering and broaden native
arithmetic/comparison tests; do not infer universal default-mode equivalence.

LIMIT-033 follow-up1.3.5: bounded software denormal handling now matches native
WARP on NVIDIA; strict handling also matches on Intel. Fract's upper boundary
needed an ordered clamp, and both modes need SignedZeroInfNanPreserve32 and
RoundingModeRTE32. Without the former Intel optimized a NaN self-comparison.
Targeted GPU10650 passes with core/sync clean. Pure moves retain their bits.
The supported scalar/vector matrix is measured, not full arithmetic conformance;
other shapes/widths refuse. Actual application logical enablement remains open.

## INPUT-001: Installed Fangyi INI has duplicate resource sections (resolved by D-067 on 2026-10-18)

Resolution: generations and the folder inspection now follow the original loader, which warns, keeps the first section and continues. The installed folder (now `zhuangfangyi lilith vice 2.0`, still with the same duplicates) parses with the warnings listed; the two corpus steps pass on the original installed files. The text below is the history.

2026-10-15 full1.3.5 regression found duplicate DiffuseMap1 and DiffuseMap2
resource sections in Mods/zhuangfangyi lilith vice1.9/fangyi ul.ini. Actual
directory name has a space between vice and1.9. Original parser reports four
diagnostics: two duplicate sections and their now-outside-section filename
lines. The two repeated bodies are identical. Source inspection is unchanged;
installed files are read-only. The original full-folder syntax test fails and
its dependent HLSL corpus step cannot run. This is not a new Vulkan language
gap. A separately labelled project-local source copy can remove only the two
duplicate blocks to continue compiler testing, while retaining the original
failure and original/normalized file hashes.

INPUT-001 verification: full1.3.5 has exactly the two dependent corpus failures;
all other steps pass. The project-local source copy under build/corpus-1.3.5
removes only two byte-identical duplicate blocks (188 bytes). Original/normalized
hashes are in manifest.json. Its original parser succeeds and all55 distinct
programs from70 declarations compile/validate. All88 installed source hashes
remain unchanged. This does not change the acceptance result of the original
installed files or establish runtime/game compatibility.

INPUT-001 source audit1.3.6: original IniHandler.cpp warns and skips duplicate
sections, retains the first section and continues parsing. It does not abort the
whole Mod. The hosted folder parser and runtime currently reject any warning;
thus the two full-suite failures reflect strict host policy, not a native fatal
syntax error. Keep their evidence; audit severity/recoverability before changing
policy. Full1.3.6 repeats exactly those two failures; the unchanged project-only
copy compiles/validates55/55 programs and all88 original input hashes still match.

LIMIT-032 follow-up1.3.6: owned INI buffer views now preserve native declaration
class/stride independently of scalar storage. Native comparisons cover typed
conversion, raw/structured sources and destinations, and nonzero regions.
Per-binding unknown-format feature checks refuse before submission. Source
ShaderRegex executable publication, unrepresentable view offsets, imported UAV
counters and application shader correspondence remain separate work.

## BUG-025: Retiring a generation with `special = upscaling_switch_bb` crashed the host (fixed in candidate 1.3.9)

Original defect: `~UpscalingFlipBBCommand` in the pinned CommandList.cpp writes `G->upscaling_command_list_using_explicit_bb_flip`. IniRuntime's destructor destroyed the generation with `G` null (only a Scope sets it), so any generation holding the command crashed the process when it was retired: F10, a module exchange or exit. The bridge also refused the command at run time, so the crash came at teardown. Found by the execution matrix probe; it is the only original destructor that touches `G`. No installed Mod uses the command; no game observation.

Correction 1.3.9: the destructor points `G` at the generation's own globals, which outlive every other member, for the teardown. The same fixture segfaults (exit 139) on a build without the fix and completes with it; the matrix probe keeps it covered.

## BUG-026: A refused character package crashes the process after device creation (fixed in candidate 1.4.9)

- Found 2026-10-19 in the sandbox (inject entry), before catalogue session B. With m2_package naming build/character/arcane-live.bin, whose Mod folder `arcane fes 1.2` is no longer installed, the layer logs "character package refused: Runtime resource missing ..." at device creation and the process then ends with an access violation (0xC0000005). m2_enabled = 0 does not prevent it: the package is still loaded. Both the deployed 1.0.2 and candidate 1.4.7 crash; without m2_package the same layer runs and exits normally.
- Consequence: the deployed dist configuration of 1.0.2 would have crashed the game under Vulkan with the current Mods folder. The session B configuration has no package.
- Cause: CharacterGeneration::init returned false after program->init had failed, with the half-made program kept; generation_frame then ran its Present at every presentation.
- Fixed in candidate 1.4.9: a refused init abandons the program and its presenter (generation_create). Suite case in sandbox/runtime_test.py. m2_enabled = 0 still loads the package (it can be enabled at run time); a refused one now does nothing. Deployed 1.0.2 keeps the defect until a new deployment.

## BUG-027: Live INI plans never replaced a real EFMI Mod (fixed in candidate 1.5.4)

- Found 2026-10-19 offline, by the dry run of the installed Mods through the production planner (`efmi_vk_character_test --plan-mods`): every component section of every Mod planned "not replaced".
- Cause: EFMI exports draw only under `if $\EFMIv1\bones_data_source != 0 && $\EFMIv1\instance_config_vs_cb_slot != -1`. The layer's native Core (CharacterCore) sets both from source facts the backend never supplied, so both stayed unknown. The sandbox fixture has no such guard, so the scene passed; the live INI path (from 1.x) was never in the game with a real Mod, so no game result is affected.
- Fix: D-079, the facts by the Core's rules on the vertex module's D-076 registers (docs/core-facts.md). Dry run after the fix: 137 component sections draw. Two smaller findings of the same run: section component/LOD assignments sit inside `if` branches (discovery reads them there), and a 24-byte blend stream (Typhoeus) needs the pipeline's element offset instead of the packager's stride rule.
- Not covered by the sandbox scene: its vertex shader has no instance window; the rule is covered by unit checks, the catalogue check (57 vertex shaders) and the dry run.


## BUG-028: Directory-mode characters were never replaced with real pipelines (fixed in candidate 1.5.6)

- Found 2026-10-20 offline by the capture replay (scripts/replay_capture.py, arcane bundle vk03): every matched draw stayed the game's.
- Causes, in the order they showed: (1) identification waited for four components (the Core's default) while EFMI exports set `identification_min_components = 3` in DrawInstances, and adapted command lists (BridgeCommand) hid that assignment from the reader; (2) the resident image budget of 512 MiB was below arcane's 576 MiB of images, so materials stayed pending forever; (3) in directory mode no package carried the DX11 dimension policy, so material slots with a 2D file at a 3D shader image were refused.
- Fix: D-080. After it the replay replaces the draws of all three passes (prepass, G-buffer, forward) without validation errors.
- Not covered before: the sandbox scene's fixture draws every one of its few components and has small images; the offline dry run has no images and no GPU.

## LIMIT-034: A Mod that replaces the Core's instance identification with its own list is refused at its draws (resolved in candidate 1.6.2, D-086)

- Found 2026-10-20 by the capture replay (Si Lunabelle, bundle vk04). `Si_BossGuard.ini`, an optional INI in the Mod's folder, sets `CommandList\si_lunabelle\_OptionalInstanceIdentification` in Present to its own list, which runs the Core's spatial-identity internals itself (`Resource\EFMIv1\Original_VS_CB->SpatialHash(...)`, the Core's spatial pools, `SpatialIdentity_IdentifyObject`) to identify only the player's body and not a boss with the same parts.
- The layer identifies objects natively (the Core's `SpatialIdentity_IdentifyComponentInstances` is a native call); a resource query on the original VS constant buffer is not answered in a replaced draw's plan, so every draw of the Mod is refused ("Resource query adapter is unavailable or over budget") and the game draws its own mesh. Safe, but the Mod does not apply.
- The offline dry run does not show it: its generations hold the character INI alone. Supporting it means running the Core's spatial identification for real in the plan, with the instance entries as `Original_VS_CB`. The Mod's own comment says the file may be removed to restore the base behaviour; Mods are read-only to this project.
- Resolved 2026-10-20 in candidate 1.6.2 (D-086): the plan answers the spatial query from the instance entry and the native draw call honours the Core's result; vk04 replays with the guard (10334 replaced at 360 frames), and a copy whose guard rejects every object keeps the game's draws. Sandbox only.

## BUG-029: One failed character image blocked every replacement of the character (fixed in candidate 1.5.7)

- Found 2026-10-20 by reading the publication rule while measuring first appearance in the capture replay: the uploader published once all of a package's images were Ready, and material draws waited for that publication. A failed image (truncated or unsupported DDS, a file over 128 MiB, the resident budget) therefore left the whole character to the game for the rest of the generation, without a refusal that named it.
- Fix: D-081. Shown by the replay of arcane with one truncated diffuse map in a project-owned copy (docs/character-materials.md).

## BUG-030: One odd slot refused a shader's whole material profile (fixed in candidate 1.5.8)

- Found 2026-10-20 while adding RabbitFX roles (D-082): a Mod's material slots are those of all its draws, and every fragment shader derives rules for all of them. A slot that held a buffer or an array in some shader made rule_rules refuse that shader ("register tN is not a single image"); one whose image could take neither a file nor a null (an integer image, a depth comparison) made MaterialProfile::prepare refuse it. That shader's draws then kept the game's textures on the Mod's mesh. In the mifu replay the shirt went black once the hair's RabbitFX roles added slots t0/t1.
- Fix: such a slot is absent for that shader (rule_rules), or marked as taking neither (prepare); only a draw that binds it is refused (select). A binding the shader does not declare, or a module the material path cannot read, still refuses the profile. Material plan test 261 checks (4 new).

## BUG-031: A translated shader with a division ended the process in NVIDIA's shader compiler (fixed in candidate 1.6.3)

- Found 2026-10-20 by the vk06 replay with P2: Typhoeus' deformation CustomShader (its DXBC cache through the exact translation) crashed the replay with a breakpoint exception inside nvgpucomp64 during vkCreateComputePipelines. Isolated with a standalone pipeline tool and SPIR-V variants: RoundingModeRTE 32 with OpFDiv, OpFRem or OpFMod. Fixed by D-088. Every translated shader with a division was affected, including in a real game session.

## LIMIT-035: Owned GPU work in a replaced draw cannot read the game's bindings

- 2026-10-20 (candidate 1.6.3, D-087). Owned work in a draw refuses when it reads or binds `vs-t0`, `vs-cbN`, render targets or other application bindings, and `store` refuses. Typhoeus' chest physics and detection read the game's bones and camera constant buffers; they stay off (the classification finds no camera pass), so the chest is drawn from the state at rest. Needs the game's bindings in Vulkan terms (shader catalogue, T-030).
- 2026-10-28: store in a draw reads a frame late instead of refusing (D-147). The application's binding at such a slot saved into a declaration, nulled and put back around owned work leaves the draw as the application bound it (D-152, Snowshine's LOD 1 correction); owned work naming such a declaration, or a slot left changed, still refuses.

## LIMIT-036: ShaderOverride and ShaderRegex sections are inert under Vulkan

- 2026-11-12 (D-187, route 2): a ShaderOverride with a SPIR-V module's hash (the CRC-32C the probe shows, below 2^32) acts at every draw of the pipelines that have the module. Sections with a DX11 hash (all six installed ones) stay as below.

- 2026-11-11 (D-171): EFMITTL's groups run their own lists, chosen by facts read off SPIR-V and checked on the paired catalogue; the general case (any library's pattern on a DX11 listing) stays as below.

- 2026-10-27 (2.0.6, D-133): one ShaderRegex effect is reproduced: a replacement that keeps the shader's discard behind an IniParams element (RabbitFX's anti-dither, 172.w = `$censor`) drops the character dither, found in SPIR-V by its data flow, from every draw while the element is set. The other replacements (RabbitFX's glow, FX and hue branches, other Mods' patterns) stay inert.

- 2026-10-20 (candidate 1.6.4, D-089). The layer cannot tell which Vulkan pipeline a DX11 shader hash means. Xaihi's `ps skip.ini` (two pixel shaders skipped) and Yvonne's EFMITTL translucency (lit-pass capture and composite keyed to a vertex shader and the depth copy shader) do not act: the game draws those shaders, and Yvonne's translucent part appears only in depth-only passes. Lifting this needs a source of DX11 shader identity in Vulkan (a game-wide shader table from the paired catalogue would be one, if the user decides the runtime may depend on it).

## BUG-032: The first game session of 1.6.4 froze

- 2026-10-20 19:10-19:18, user-started Vulkan session (PID 13172), layer 1.6.4 injected by the launcher, directory mode with 22 character Mods, owned GPU tools deployed. The game showed Yvonne replaced (583 replaced draws) and TangTang and Si matched; at probe frame 1535, about a second after Si's package activated on its first match (19:12:33), no presentation followed. The window stopped responding; the user ended the process at 19:17:59 (Application Hang 1002). No display driver timeout in the system log; the game's Player.log ends at 19:12:16 without an error.
- Seen while frozen: the probe answered status twice (the answer takes every generation's mod_mx and the device lock, so none of them was held); the last frame had 2 submissions and no draw.
- Found and fixed in 1.6.5 (D-090), cause of the freeze not proven: the game presented on one graphics queue for about 692 frames and on the other afterwards; every generation's owned runtime was bound to the first queue (its load-time work was submitted there by the draw-work flush), so every later owned Present failed ("GPU resource scope cannot change queues", 749 times per generation); image uploads were also bound to the first submitting queue (Si: 12 of 17 images uploaded).
- Also seen: frames of 3 to 4 seconds while characters load (frames 673, 1186, 1419). Not yet attributed.
- Most likely cause found afterwards (D-091): the consumer-queue waits for image publications could hold one graphics queue behind work on the other that waits for it; 1.6.5 only enqueues waits that are already satisfied.
- Third session (1.6.6, 21:00): no freeze; three characters replaced, the rest waited for device memory (405 MiB to 0 free after 1.4 GiB of images), frames of 50 to 663 ms while free memory was near zero. The user: under DX11 twenty Mods never exhaust memory. Root cause of the whole series: every DDS of a package loaded at activation and nothing freed. Fixed by D-094/D-095 in 1.6.7 (sandbox); to be verified in the game.
- Next session: the stall watchdog of 1.6.5 logs where every thread is when no presentation returns for five seconds; `m2_owned = 0` separates owned execution from the rest.

## BUG-033: The game session of 1.6.5 froze after eight character packages activated within 17 seconds

- 2026-10-20 20:04-20:07, user-started Vulkan session (PID 27384), layer 1.6.5. The user switched through characters; between 20:05:56 and 20:06:13 the packages of Typhoeus, mifu, zhuangfangyi, gilberta, ardelia, last rite, rossiy and Xaihi activated (Yvonne, TangTang, Si, arcane and Liino before). From 20:06:14 the stall watchdog logged the submitting thread inside vkQueueSubmit and the presenting thread inside vkQueuePresentKHR, both waiting on kernel objects; the probe still answered (our generation and controller locks free); no display driver timeout. Evidence kept in build/backups/session-1.6.5-freeze-*.
- Device memory: the active packages held about 2.8 GiB of images and 0.26 GiB of meshes, plus snapshot batches in device-local host-visible memory (about 15 MiB per batch, up to 8 per package); the last five packages could no longer place their meshes in device memory (mesh_in_device_memory false). The laptop GPU has 8 GiB beside the game. Oversubscribed device memory is the likely cause of both freezes (the 1.6.4 one came after three characters with about 1.4 GiB of images). Fixed by D-092; to be verified in the game.
- Seen in the same session: characters switching between the original and the Mod, and Mods drawn in the character menu but not in the open world: one command buffer accepted one package's replacements only ("recording holds another backend's work"), and packages took command buffers from one another, emptying their snapshot pools ("snapshot pool is at capacity or refilling"). Fixed by D-093.

## BUG-034: The game session of 1.6.7 thrashed its textures, showed black characters, and froze at F10

- 2026-10-20 22:46-22:52, user-started Vulkan session (PID 28148), layer 1.6.7 with `profile_tracker = 1`. 17 of 22 packages activated. Status at frame 27698: textures resident 7 (6 reduced), loading 6, 157 MiB resident against a 356 MiB target (1 GiB reserve, 1.37 GiB free), 591 loads, 574 evictions, 13519 budget waits; almost every package with `images_ready` 0 and `image_pending` counts; owned Present submitted every frame by Typhoeus only. The user saw characters turn black now and then: the two-frame eviction rule reloaded what the next frame bound, and a draw whose image was pending went to the game while the other passes of the object were the Mod's (depth EQUAL failed). Fixed by D-097.
- Performance: frames 23.8 ms mean, 10.5 ms median, 56 ms p95, 83 ms p99, 989 ms max over 2048 frames. The layer's own work: `draw_record` 3.34 million calls, 80 s in total (24 us per draw, max 92 ms), every activated package with hash-only sections hashing each draw's index region under its own lock; `buffer_unmap` 19704 calls, 7.1 s, 26 GB copied (max 32 ms, under the exclusive resource lock) for the probe's shadow copies the replacement never reads; `submit_tracking` 3.0 s (max 56 ms), `command_reset` 1.4 s (max 61 ms). D-097 (index by hash) and `unmap_snapshot_limit = 0` in the game configuration; the maxima of submit tracking and command reset remain to be looked at.
- F10 at 22:52:41: the 12 activated packages exchanged within 0.4 s; from 22:52:42 the watchdog showed the submitting thread inside vkQueueSubmit at "character submission work" and the presenting thread inside vkQueuePresentKHR at "character Present (owned scope, INI Present, owned submission)" for over 100 s, other threads idle, the process not responding with 0 CPU; the user ended it. Not proven; the mechanisms found are in D-098 (controller held exclusively during Present work, fence waits on the presenting thread, `done` semaphores destroyed while a presentation could still wait on them). Evidence: build/backups/session-1.6.7-freeze-*.log and session-1.6.7-status-before-freeze.json.

## BUG-035: The 1.6.8 session: slow character switches, LOD1 reverts, half the frame rate

- 2026-10-21 08:06-09:20, user-started Vulkan session (PID 31884), layer 1.6.8, `unmap_snapshot_limit = 0`. 17 of 22 packages activated. The user: a switched character shows after two to three seconds, characters within LOD1 range revert to the game's mesh now and then, performance better than before but not acceptable.
- Causes found in the session (all measured): the 1 GiB reserve above the 889 MiB free put the texture target at its 64 MiB floor, so every image two seconds idle was evicted and reloaded (164 evictions); the edge fingerprint read cost 12.5 us per indexed draw (13 ms per frame); `owner_for` destroyed other packages' arenas at the first draw of every recording (arcane 142 us per draw); the INI plan runs about a hundred commands per matched draw (50-63 us); bone reads 18-25 us. A module exchanged into the game lost its owned GPU work (tools looked up beside the module). Fixed by D-099 in the running game; the frame rate went from 41 ms to 24.5 ms against a 20.9 ms baseline. The switch delay is now the pipeline variants compiling per package (LIMIT-037).

## LIMIT-037: What remains of the recording cost

- 2026-10-22, after D-106 (1.7.2): the INI plan of a matched draw fell from 33 to about 15 us of list execution offline; the bones (write-combined reads), the index hashing of unmatched draws and the Present lists of every package remain (docs/recording-performance.md).

- After D-099, in the open world with four characters: plan 50 us and bones 18 us per matched draw, 150-180 matched draws per frame, 15.8 ms of recording work per frame over two threads, 1.6 ms of Present work. The INI dispatch and list overhead (about 0.5 us per command for a hundred commands per draw), the bone reads over the bus, one variant cache per package (a switched character waits for its variants) and 22 packages' Present lists are the items; the first two are measurable offline through the replay's `stage_us`.

## BUG-036: A Mod's key panel flickered in fullscreen

- 2026-10-22, seventh game session (1.7.0, PID 20568). The panel a Mod draws at presentation time (Yvonne's `k\1.ini`) flickered in fullscreen and was steady in a window. The game presented four to ten times per rendered frame there (frame generation, frames presented again), and the layer ran the Present lists and the owned Present work only for a presentation whose interval had a draw: the panel was in one presented image of four.
- Fixed in 1.7.1 (D-104): the Present lists run once per rendered frame as before, and a presentation without a draw replays the last Present's drawing into its image when that Present drew into the back buffer, the INI's variables and parameters restored afterwards (`replays` in `owned_present` and `ini`). Verified by module exchange in the session (the panel steady in fullscreen; the Present once per rendered frame, Typhoeus' per-frame GPU work no longer repeated on generated frames) and by the suite.

## BUG-037: F10 did not take a Mod folder added or removed after the start

- 2026-10-22. The user replaced Yvonne's Mod folder with another one and pressed F10: the packages found at the start reloaded their INIs, the new folder was not read, the removed one failed its reload. The original reads the whole Mods folder on F10.
- Fixed in 1.7.1: an F10 taken by any package rescans the Mods folder on the presenting thread: a Mod added becomes a package read in the background (`character package found by F10`, `added by F10`), one whose folder is gone stops (`removed with its Mod`); the controllers list grows within a capacity reserved at creation, so a recording thread's index into it stays valid. Verified in the session (the new folder came up through a module exchange and later through F10) and by the suite.

## BUG-038: Variant parts of Typhoeus' clothes hollow, spiky or invisible

- 2026-10-22. Typhoeus' clothes component (its stockings, the cloth variants of Alt+5..8) drew as a hollow shell, spikes, or nothing, the body and hair fine. The game's clothes pipeline declares BLENDINDICES as R32G32B32A32_UINT at offset 16 and BLENDWEIGHT as R32G32B32A32_SFLOAT at offset 0 (stride 32); the Mod's blend-index edit narrows the indices to R16G16B16A16_UINT (its stream: four float weights, four 16-bit indices, 24 bytes). The layer replaced only an R8G8B8A8_UINT element and knew only 16-bit normalized weights, so the variant pipeline read 32-bit indices out of the 24-byte element: wrong bones for every vertex of that component. The body's pipeline has the common layout (R16G16B16A16_UNORM weights, R8G8B8A8_UINT indices, 12 bytes), hence the difference.
- Fixed in 1.7.1: BLENDINDICES is the pipeline's unsigned four-component element of binding 2 of any width, BLENDWEIGHT its normalized or float one; the live plan's edit replaces the element whatever the game's width; float weights are read for the component mask. Reproduced and verified offline with the vk06 replay and persistent variables set through the replay's new `--settings` option (the clothes and the stockings drawn), then in the session by module exchange.

## BUG-039: A Mod inherited the persisted toggles of another Mod of the same namespace

- 2026-10-22. `yvonne sinicat 2.4` replaced `Yvonne Ensnared Heart 1.8`; both declare `namespace = yvonne`, and the settings file `settings/yvonne.ini` written by the old one (its cloth parts toggled off) came up in the new one: its clothes, shown by default under DX11, were hidden. The original keeps one `d3dx_user.ini` by namespace and would inherit them the same way.
- Fixed in 1.7.1 (D-105): the settings file is named by the namespace and the Mod's folder (`yvonne-yvonne_sinicat_2_4.ini`). The files of 1.7.0 and before stay unused: every Mod starts once from its INI's defaults.

## BUG-040: Stable positive index matches can outlive coherent mapped writes (fixed offline in 1.7.5, P1)

- 2026-10-24: The former stable-positive shortcut is removed. Native tests call the production hash path after 160 unchanged observations, then change the head and tail without a tracked write and verify immediate recognition; a tracked interior write and negative backoff are also covered. Native 158/158 and the complete 35-step suite pass. No game test. The 1.7.5 baseline was not deployed; 1.7.6 was deployed after offline verification. Earlier review evidence below is historical.

- Introduced by 09c6b59, present in b5c12c3. character_backend.cpp:2191 skips the edge fingerprint of a positive Mod match after 120 stable reads until next_quick. A direct write to a coherent mapped pool need not update region_gen, so another object's same-size region can select the old Mod section. Native source-extracted probes: old version detects the change at frame 121; 1.7.4 returns the old hash through frame 135 and detects it at 136. A tracked write still invalidates immediately.
- Fix direction: preserve validation for positive matches on memory without an independently reliable write/immutability guarantee. Cover stable same-region reuse. See docs/performance-review-1.7.4.md; build/review-performance-1.7.4/probes.json. No real-game reproduction or runtime fix in this review.

## BUG-041: Fast variant lookup retains retired Vulkan objects (fixed offline in 1.7.5, P2)

- 2026-10-24: Retirement erases every fast lookup key for the pipeline identity. Production retirement tests cover 256 identities with multiple keys, independent identities and a command owner surviving retirement until completion. Native 158/158 and the complete 35-step suite pass. No game test. The 1.7.5 baseline was not deployed; 1.7.6 was deployed after offline verification. Earlier review evidence below is historical.

- Introduced by 09c6b59, present in b5c12c3. variant_lookup stores strong PipelineVariant owners, but generation_retire clears only the other pipeline tables. Retired pipeline/layout resources survive command completion until material refresh or generation destruction. The table's 65,536-entry limit is not retirement.
- Native ownership probe using the actual retirement function: all 256 retired variant stand-ins remain at 1.7.4 after cache/command owners release, versus zero before this lookup. Clearing it releases all. GPU bytes were not measured. Erase matching identities or use weak ownership, retaining completed/in-flight command ownership correctly; add character-backend lifetime coverage. Evidence as BUG-040. No runtime fix.

## BUG-042: Persistent material scratch pins images after eviction (fixed offline in 1.7.5, P2)

- 2026-10-24: A scope guard clears image and variant owners on all draw exits, including exceptions, while keeping inner vector capacity. Production scratch cleanup tests cover an inactive character and completed command owners. Image owners in this CPU test are stand-ins; GPU residency is not inferred from their byte counts. Native 158/158 and the complete 35-step suite pass. No game test. The 1.7.5 baseline was not deployed; 1.7.6 was deployed after offline verification. Earlier review evidence below is historical.

- Introduced by b5c12c3. scratch_material_views owns each SnapshotImage until the next matched record() call. A character that leaves the scene after a material draw can retain those images after commands complete and the residency cache has evicted and uncharged them. Retention is limited to the last draw's images; it is empty if that draw bound no Mod images.
- Native model of the committed SnapshotImage and storage lifetime: before 1.7.4 all three stand-in images release after command/residency release; at 1.7.4 none release until scratch clear. Not an actual GPU allocation measurement. Release resource references on all draw exit paths while retaining container capacity. Cover inactivity, completed commands and eviction. Evidence as BUG-040. No runtime fix.

## BUG-043: A texture wait held a vector element across concurrent declarations (fixed offline in 1.7.6)

- Found while batching first-use texture loads: TextureResidency::bind saved an Image reference, unlocked mutex_ and then used image.handle while declare() could append and reallocate images_ (for example an F10 candidate declaring its paths on another generation's worker). This is a source-level invalidation hazard; no game crash is attributed to it.
- The batch path keeps only ImageId indices and shared ResourceRequest handles while unlocked, and resolves Image again under the residency mutex. It also checks stopped_ before any timeline access after reacquiring the lock. Completed-only timeline publication is unchanged. The 35-step suite, including live reloads and dual-queue validation, and 266 standalone resource/cache GPU checks passed. No game verification this round.

## I-06 evidence refinement: asset metadata exists outside stripped captures (D-292)

Initial assets contain paired API programs and source-channel tables. The generic
offline reader proves 1,682 binary pairs, including 211 with different register
and location sets; 323 unique vertex modules pass independent validation. Only
3 unique modules match historical captures. Broad asset discovery and runtime
integration remain open. Missing SPIR-V names alone no longer justify declaring
input identity irrecoverable. See docs/shader-input-metadata.md; runtime is 3.1.7.

## I-06 corpus limits and ambiguous empty-input shader (D-293)

Initial VFS discovery validates 5,266 unique vertex modules and matches 101/169
historical vertex modules. The unmatched 68, 1,276 unpaired platform entries and
runtime semantic integration remain open. 512 ProceduralSky pairs share a Vulkan
empty-input module with incompatible DXBC signatures; that identity is excluded
globally. None of the 101 historical matches has the ambiguity. Compute programs
can occupy the serialized vertex-program container; inspect actual entry points.
