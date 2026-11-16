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
integration still needs resource access footprints, application binding import into
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
