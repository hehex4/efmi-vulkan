# Prepared buffer command execution

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
