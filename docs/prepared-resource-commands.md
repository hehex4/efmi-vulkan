# Prepared resource command execution

D-308 adds a shared GPU execution primitive for the original-configuration rewrite.
It is not yet connected to production draw interception. It does not close native
queue scheduling, render-pass insertion or original Core compatibility.

Original `DirectX11/CommandList.cpp` 12540-12561 records copies through the same
`mOrigContext1` that receives the surrounding draws. DX11 supplies reference-counted
lifetime with deferred destruction, hazard tracking and one-context order. A Vulkan
consumer inserted before an application's whole submission cannot read a producer
that occurs earlier in that submission's command buffer. Retaining the resource does
not repair that ordering error.

`ResourceCommands::begin_prepared` creates a bounded CPU command sequence. Buffer
copies, fills, staged writes and compute dispatches retain their complete resources,
descriptors and pipelines. `finish_prepared` detaches immutable commands and their
owners. No command-buffer recording, submission, fence wait or feedback occurs during
preparation; driver allocation and shader preparation are still ordinary host work.
Discard emits no partial GPU work. The default owned submission path remains intact.

D-314 extends this boundary to owned images and adds explicit packet admission.
After sealing, accept_prepared publishes logical image layout state and enables
recording. Discard invalidates unadmitted packets. An admitted packet must execute
before later packets that assume its final state; admission does not prove GPU
completion or permit the host to drop a predecessor. Buffer-only callers use the
same seal/admit protocol.

An imported application buffer requires its actual handle, byte extent, usage,
allocation range and complete lifetime owner. It is accepted only in prepared mode
and cannot leak into an ordinary deferred submission. Copies compare allocation
ranges, so distinct wrappers of overlapping storage cannot bypass the overlap guard.
The imported owner's lifetime does not freeze mutable buffer contents.

The host records a prepared sequence at its actual producer/consumer point outside
a render pass on the declared device and queue family. Explicit barriers cover the
surrounding commands on that queue. The host must establish cross-queue access order,
preserve graphics attachments across any split, and restore compute pipeline and
descriptor state when the sequence reports a compute bind. It must retain the sequence
while any command recording that uses it is executable or pending. The object can
outlive the preparing interpreter or engine; its allocation and descriptor owners
remain alive until the host releases the recording.

The initial supported commands contain buffer operations only. Images, graphics,
feedback and automatic submission remain refused in prepared mode. Production
integration still needs application binding import into
the original GPU registry, logical registry rollback, ordered plan items, attachment
split planning, compute-state restoration and recording ownership. Do not enable
`OwnedRuntime::begin_draw` and submit its old pending primary ahead of the application
as a substitute for this integration.

That initial image restriction is superseded for owned images by D-314 and retained
application images by D-316 below. Graphics, feedback and automatic submission
remain outside this prepared boundary.

The headless fixture prepares one copy/compute sequence, checks that output stays
unchanged before recording, then reuses the sequence after application fills of 11
and 13 in one primary. Independent readback must find 22 and 26 in all 64 words. The
preparing engine and caller source reference are destroyed before replay; retained
owners remain until the executable recording is retired. Additional cases cover
ordinary submission behavior, physical aliasing, bounds, forbidden early submission,
readback, empty scopes, cancellation and the operation budget.

The Vulkan [barrier command rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdPipelineBarrier.html)
restrict in-pass transfer/compute barriers. Direct replay avoids the additional
primary-state invalidation of [executing secondary command buffers](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdExecuteCommands.html),
but the commands it actually binds still require explicit restoration.

```text
python -B scripts/check_configuration.py --only prepared
```

The initial focused implementation passes 44 assertions; the final expanded fixture
passes 59 with zero core and synchronization validation errors or warnings. Root,
ordinary GPU and production registry checks also pass, as recorded in CURRENT_STATE.md
and the session log. This evidence is a headless command-order
check, not a production-layer or real-game Core rendering result.

## Prepared buffer access admission (D-309)

Every prepared packet exposes ordered buffer access spans: exact buffer-relative
bytes, retained physical allocation identity, pipeline stages, access masks and
the command position. Transfer reads/writes and fills use their actual ranges;
compute descriptors conservatively mark storage as read/write, uniform buffers
as uniform reads and sampled texel buffers as shader reads. Indirect dispatch
includes its twelve-byte argument range. Null descriptors add no access.

The host can compare these retained spans with application resource allocations,
including different handles bound into the same memory. The overlap helper rejects
empty/overflowing queries and treats adjacent intervals as disjoint. This metadata
does not prove queue order or authorize use while a render pass is active. Admission
and recording ownership remain host obligations.

Commands and access entries have separate budgets. Admission reserves the complete
operation and its final publication barrier before appending metadata. A refused
next operation cannot leave a partial access description or prevent an otherwise
complete packet from being finished. The focused fixture passes 65 checks with clean
core/synchronization validation, including exact spans, physical aliases and budget
boundaries. This is still a prepared-command primitive, not live Core integration.

## Original registry preparation (D-310)

GpuResourceRuntime can open a buffer-only prepared scope. The original command lists
see their own speculative resource assignments in order. Sealing returns the immutable
packet without publishing the logical registry; the host must admit it explicitly or
discard it. A sealed scope cannot start another execution or rebind its queue. This
frontend scope is not yet enabled by production draw interception.

The undo journal records only touched declarations and copy-cache entries, including
lazy initialization, inferred bind flags, out-of-band properties and per-frame copy
allowances. It also retains the bounded current slot state. Discard restores existing
aliases and removes uninitialized new allocations and copy caches, using preallocated
map nodes. Original scalar assignments persist. Speculative aliases are not published
into the parallel file-plan reference registry. Contained interpreter refusals also
invalidate speculative admission; they cannot silently leave a successful partial
packet or permanently disable a GPU command after a transient allocation failure.

Original CommandList.cpp 6407-6466 substantiates a declaration once, and 12490-12579
updates copy counters and performs copies on the same context. DX11 supplies
reference-counted lifetime with deferred destruction, hazard tracking and one-context
order. Vulkan preparation must restore these logical facts if the corresponding GPU
work will never be recorded. Neither the journal nor acceptance supplies GPU completion.

The supported registry subset contains ordinary named buffers. Images/parameter
uploads, feedback and original pool mutations or lookups require their own transactions
before admission. Prepared guards act before pool lookup/reset; ordinary execution
continues using the original pool implementation. These limits are not a new character
loading boundary and do not exclude configuration files. Application imports, packet
insertion, pass splitting, compute restoration and recording lifetime remain required.

The fixture cancels initial uploads and repeated copies, retries in the same frame,
checks independent GPU bytes, retains old aliases across a cancelled clear/null,
checks pool refusal before mutation and verifies ordinary pool execution afterward.
It also exhausts allocation capacity halfway through a copy, discards the partial
preparation and retries the same instruction successfully. Test evidence and the
corrected pool-fixture failure are recorded in CURRENT_STATE.md and SESSION_LOG.md.

## Application buffer snapshots (D-312)

The original registry can begin preparation with a complete buffer-only source
context. Every supplied binding includes the retained Vulkan resource/allocation,
stable resource identity and explicitly verified original buffer description and
view/IA metadata. The caller must prove that all omitted slots are unbound. Unknown
source registers, image/output state or ownership cannot be treated as an empty
context to enable this path. This boundary is not yet used by production interception.

Imports share a weak identity cache, so multiple views and saved named references
retain one source allocation without making the cache a resource owner. Descriptor
and allocation changes under a still-retained identity refuse. Original hash matching
uses supplied tracking facts; newly allocated copies remain untracked. The imported
view does not infer a DX11 structured/raw/typed declaration from Vulkan storage.

Seeding is atomic. Invalid metadata leaves the preceding registry usable. After a
valid seed, cancellation restores the newly observed application binding snapshot,
including known nulls, while undoing speculative named-resource assignments. GPU
bytes are not copied at snapshot time. Copies and CustomShader commands remain in
the prepared packet and consume the application producer at their insertion point.
Imported resources cannot enter the former deferred-submit execution domain.

Original CommandList.cpp::ResourceCopyTarget::GetResource obtains referenced resource
and view objects from the current context; IA getters also supply offset/stride/format.
DX11 provides reference-counted lifetime with deferred destruction, hazard tracking
and one-context order. The Vulkan host must preserve complete owners and prove actual
source facts, queue order and insertion. Buffer snapshots do not establish any of
those missing host proofs. Pool/image/parameter transactions, pass splitting, caller
compute restoration and recording lifetime still gate production Core execution.

The application fixture executes the original INI reference/copy/CustomShader chain,
then records its packet after different application fills in one primary. All 64
words independently read back as 22 and 26. It cancels an initial preparation and a
later source change, checks admitted named-reference preservation, refuses invalid
source metadata/aliases and live handle recycling, destroys the interpreter/caller
before replay and checks owner retirement. The combined configuration GPU suite
passes 126 with zero core/synchronization errors or warnings; root 27, interpreter
570 and prepared primitives 65 also pass. This is a headless frontend result, not
production interception or real-game Core acceptance.

## Original pool transactions (D-313)

Pool lookup is mutable in the original engine: keyed indexing, expiration, type
switching and lazy variable/resource creation can change state even during a query.
Prepared execution checkpoints a pool before that first access, following any pool
proxy to its source. The journal preserves index maps, cached elements, replacement
and expiration clocks, template/element metadata, pool variable values and the pool
copy instruction's failure memo. Ordinary scalar globals continue to persist.

Lazy registrations belong to the transaction. Cancellation restores the admitted
pool state and persistent-variable list, then removes newly registered resources
and variables after their GPU cache entries have been retired. Saved metadata holds
borrowed initialization pointers rather than owning copies of template data. All
restoration uses existing nodes, swaps or erasure; no allocation is required after a
failed preparation. Accepted work keeps the original engine's updated pool state.

The original pool algorithms are compiled unchanged. The runtime build stages its
pinned source under project-owned build/xxmi-source and adds only friend declarations
for OriginalPoolState in CustomResourcePool and PoolCopyOperation. This grants named
field access without guessing object offsets, exposing private members globally or
changing the external dependency checkout. Every runtime translation unit uses the
same staged header; the separate original CPU reference host retains its pristine
header. Pinned dependency and pristine staging digests are still checked.

Admission bounds the number of pools, cached elements and registration snapshots.
The first touch copies a pool's index state; this is a correctness boundary, not a
claim that production recording is allocation-free. Image/parameter transactions and
host queue/pass/recording integration remain separate requirements.

The fixture passes 169 combined configuration GPU assertions, including cancelled
FIFO eviction, lazy persistent variables, expiry refresh, proxy/reset/metadata and
failure after a queued write. It exposed a CPU assignment fast path that checkpointed
too late; the corrected bridge saves state before mutation. A fresh independent repeat
also passes with zero core/synchronization diagnostics. Root 27, interpreter 570,
prepared primitives 65, production 13, tracker 838 and ABI 9 pass on 3.3.1.

## Owned images and parameter publication (D-314)

Prepared packets now retain owned image copies, buffer/image transfers, color and
depth/stencil clears, multisample resolves and sampled/storage compute descriptors.
Every packet exposes the required initial and final layout of each image, plus
ordered subresource accesses. Transfers conservatively cover all texels in their
specified mip/layers; transitions touch the full image range. Allocation handles and
sizes stay valid through their retained owners. Storage image accesses are read/write.

Layout changes remain private while preparing and sealing. Only explicit host
admission publishes the logical GENERAL state for subsequent work. The host must
execute admitted predecessors in that order and keep packets while their command
buffers are executable or pending. Initial UNDEFINED transitions discard contents;
replaying such a packet repeats that initialization contract. Admission is not a
fence/completion proof. Cancelled packets cannot record. A sealed scope cannot be
overwritten before accept/discard, including a scope that emitted no GPU commands.

The original named-resource journal covers lazy images and image copy caches as well
as buffers. IniParams continues using its original float4 Texture1D and original
flush boundaries. CPU parameter assignments survive cancellation. Cancelled or failed
uploads retain a publication retry bit, so the next boundary republishes even when
the new list assigns the same value. This does not restore default bindings that the
INI explicitly cleared. Uploaded bytes are frozen in retained staging storage.

Original CommandListFlushState maps with WRITE_DISCARD, copies the entire parameter
array, then clears update_params; resource copies use the same mOrigContext1. DX11
supplies reference-counted lifetime with deferred destruction, hazard tracking and
one-context order. The Vulkan adapter replaces these implicit facilities with staging
owners, explicit transitions/accesses, retry state and ordered packet admission.

Admission bounds images to 256, commands to 4,096 and combined access entries to
16,384, reserving final GENERAL transitions and publication before each operation.
Counts conservatively include potential newly seen images. Application image wrappers
without allocation owners/layout proofs refuse. Graphics, GPU feedback, source-register
mapping, production pass splitting and queue order remain host integration work.

Verification on 3.3.2: configuration GPU 208, prepared 85 and ordinary image/parameter
3,010 checks pass with zero core/synchronization diagnostics. Fresh root 27 and
interpreter 570, production registry 13, native compute 91, tracker 838 and ABI 9
also pass. The parameter retry failure, invalid initial texture fixture and missing
test-consumer artifact are recorded separately in CURRENT_STATE.md/SESSION_LOG.md.

## Retained application images (D-316)

The low-level command engine accepts complete optimal-image allocation descriptions,
retained owners and a newly observed uniform layout for each prepared scope. It checks
the actual image memory requirements, supported shape/usage and memory span. A view's
layout alone cannot prove the whole-image transitions used here. Missing or undefined
layouts refuse. Saved references must be observed again before any later packet uses
them; this does not copy pixels or turn a previous observation into current evidence.

Live imports of one VkImage share an allocation wrapper and cannot change its
description or memory contract. Each packet captures immutable initial/final layout
values and restores the observed application layout. Later observations do not modify
earlier packets. Ordinary deferred submissions refuse retained application images.
Transfers and clears require their actual usage bits; no application usage is added.
Distinct images sharing physical storage, or image/buffer allocation overlap in a
packet, refuse pending an explicit alias-phase adapter.

Original CommandList.cpp 9691-9818 obtains referenced current SRV/RTV/DSV/UAV views
and their resources. DX11 supplies reference-counted lifetime with deferred destruction,
hazard tracking and one-context order. Vulkan retains the complete owner, exposes
allocation/subresource accesses and preserves layouts at the actual insertion point.
These are host proof inputs, not permission for an in-pass copy or concurrent queues.

The fixture cancels without changing pixels, rejects incomplete/stale/contradictory
observations and allocation aliasing, then consumes two separate application producers
after the preparing engine is destroyed. The attachment-layout packet yields 14 and
22, while an intervening GENERAL-layout copy sees 7. All allocation owners retire
after the executable packets. Full original registry image snapshots and production
ordered insertion remain separate follow-up work.
