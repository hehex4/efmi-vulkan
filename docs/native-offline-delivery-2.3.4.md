# Native Vulkan offline delivery audit, 2.3.4

Date: 2026-11-13. This audits the continuation from the D-189 review through D-204.
It is a source and sandbox delivery record, not real-game acceptance or a claim of complete
3DMigoto parity. The exact build/check logs are in CURRENT_STATE and SESSION_LOG.

## Version and deployment boundaries

| Item | State |
| --- | --- |
| Reviewed D-189 | 08cf62b, integrated by 783a7c9; exclusive analysis directories preserve earlier captures |
| Current implementation | 2.3.4 on engine-work, fast-forwarded to main after verified commits |
| Worktree host/backend ABI | 0xc5ec321d7d0c1a3f; a matching pair is required |
| Root dist | 2.1.8; unchanged during this continuation |
| Latest historical game evidence | 2.1.7; no result from this continuation is game evidence |
| System and installations | No game load/start/attachment/swap, deployment, launcher changes, registry edits or remote push |

## Delivered capabilities and evidence

The historical checks below identify the change they validated. They are not represented as
reruns against 2.3.4. The last section records the current capture regression separately.

| Capability | Delivered behavior | Evidence and limits |
| --- | --- | --- |
| D-189 hunting/analysis | Buffer/target selection, hunting operand, module dumps and exclusive frame logs | Original analysis/collision compatibility 12/12 on D-202; shader/hunting integration 37/37 on D-189 |
| D-190 through D-195 shader replacement | SPIR-V ShaderFixes, offline HLSL compilation, owned pipeline recipes, F10/fallback, depth filters, original/pink modes; VS/PS/CS/GS/HS/DS | Graphics 92/92 on D-201; compute 83/83 plus 8 shared-memory checks on D-193; GS/HS/DS 80/80 on D-195. Fixed supported pipeline/workgroup/interface contracts apply |
| Empty inputs | Installation hunting/root overrides and F10 with no character Mod; native-only/empty module exchange | Empty inputs 45/45 on D-194; native-only/empty swaps included in D-200 files 42/42 |
| D-196/D-197 native commands | Explicit Vulkan set/binding aliases and original-resource queries; native pre/post draw/dispatch lists at application calls | Native slots/calls fixtures; calls 60/60 on D-199. Unsupported graphics writes, resource/layout work and indirect-resource commands retain explicit boundaries |
| D-198/D-199 ownership and bindings | Retained application allocation chains; persistent context bindings; ordinary/template/pushed sets; app rebinds supersede overrides | Bindings 48/48 on D-201. Moving application references requires one application queue; unsupported descriptor flags and application image layouts are refused |
| D-200 file buffers/null | Shared cache/budget/residency; immutable plain buffers; enabled null descriptors; completed publication waits retained across commands and exchange | Files 42/42 on D-200, including secondary commands and alternating actual queues. Queue fixture serializes presentation completion; no concurrent presentation claim |
| D-201 file textures | Read-only DDS/WIC 2D sampled views, format/mip checks, F10 revisions; color Depth=2 compatibility and native push layouts | Images 46/46; graphics 92/92, character 115/377 and tracker 206 on D-201. No mutable/array/cube/depth/writable-file-image acceptance |
| D-202 GPU resources | Distinct staging copies, retained owners, completion timeline and host worker; buffers/images plus resources.jsonl | GPU analysis 55/55 on D-202/D-203, including distinct 19/38 writes, push/secondary, post-swap capture and explicit scope refusals |
| D-203 GPU geometry | Complete original vertex/index allocations at pass end with per-draw ranges, input attributes and secondary identities | Geometry 65/65 on final D-203: GPU index rewrite, primary/secondary, non-indexed calls and 137-byte allocation tail |
| D-204 owned attachments | Stored output attachments are independent of shader reflection, including clear-only passes and primary state after secondary execution | Baseline 9/15: ordinary graphics control passes, clear-only/after-secondary fail. Final targets 22/22 with synchronization validation; exact checks below |

## Current verification

D-203 host/module and final guarded host are in d203-pair.out and d203-final-host.out;
final geometry 65/65 and text 561 pass in d203-final-checks.out. Existing GPU analysis 55/55
passed immediately before the geometry-only pipeline guard, in d203-regression.out.

D-204 fixture is built in d204-fixture.out; the host/backend build is d204-pair.out.
Final selected capture-area regression passes in d204-regression.out: GPU resources 55/55,
geometry 65/65, owned targets 22/22, unchanged tracker contracts 206/206, Vulkan validation 76/76
and the English-only file check (564 files). Both worktree DLL versions and ABI were checked
directly; the root DLLs still contain 2.1.8.
Capture fixtures enable synchronization validation in their child processes. No validation
check is disabled. The whole regression suite, captured shader corpus and installed-Mod sweep
were not repeated for these capture-only changes.

## Remaining engineering work

These are real architecture boundaries, not assertions that the work needs a game session:

- Application image movement and render-target rebinding need subresource layout/ownership tracking.
- Application references across multiple queues need a represented synchronization graph.
- Capture on synchronization2 devices needs correct treatment of stage-limited app signals;
  arbitrary signal widening can create wait cycles. Replay/simultaneous recordings need separate
  readback ownership. The real game's current two-queue arrangement is outside the capture adapter.
- File array/cube/mutable/writable resources need their view and synchronization adapters.
- Capture excludes swapchain ownership, depth, unsupported image subresources/descriptor arrays,
  indirect argument snapshots, dynamic vertex input/topology and vertex slots beyond those tracked.
- The original analyse_options selection semantics and steady hunting overlay remain incomplete.

Current exports require new one-time recordings, one unprotected graphics application queue,
synchronization2 disabled, original TRANSFER_SRC usage and retained allocations. Copies are
post-application-call or classic-pass-end data, never CPU shadows advertised as GPU output.
Geometry describes original application bindings, not replacement-private meshes.

## Separate game acceptance

After an explicitly authorized session and deployment, acceptance still needs real native
ShaderOverride matching/replacement, hunting usability/cost, F10 behavior and character appearance.
D-185 translucent shadows and D-186 dash after-images also remain on that acceptance list.
Historical captures can check general rules; they cannot establish current visual or performance
acceptance. No account, launch, deployment or session is selected by this audit.
