# Queue ordering proof and integration boundary

D-286, 2026-11-15. This is a headless sandbox experiment on source 3.1.3,
not a production queue scheduler or a real-game result.

## Why the original does not have this problem

3DMigoto's `HackerContext.cpp::AfterDraw` (1079 onward) runs post command lists,
restores shaders and releases retained references on the same DX11 context.
DX11 supplies reference-counted lifetime with deferred destruction, hazard
tracking and one-context order. Vulkan command owners implement lifetime retention,
but neither those owners nor a per-queue completion counter orders new resource
accesses against another application queue.

## Reproducible experiment

`sandbox/queue_overlap.cpp` uses two distinct application graphics queues in one
family and a third queue only for the experimental scheduler. It creates no window
or swapchain. Synchronization validation is enabled. There is no QueueWaitIdle or
DeviceWaitIdle in the fixture. Fence status proves the consumer is pending before
its producer is submitted; bounded waits and complete word-by-word readback provide
independent liveness and data checks.

1. Submit a copy on queue 0 waiting for application timeline value 1. Then submit
   a fill on queue 1 signaling value 2. Both finish; all 64 copied words match.
2. Add a global completion timeline: the first copy signals global value 1 and
   the later producer also waits for global value 1. Both counters remain zero
   and the consumer's bounded fence wait times out. A host signal of application
   value 1, below the pending device signal 2, legally breaks the experimental
   cycle. The consumer reads the initial zero data; all submitted work then exits.
3. Split application waits into a readiness marker, and hold actual work behind
   a separate gate per queue. The producer becomes ready while the earlier
   consumer still waits. A private GPU queue releases the producer first, then
   the consumer after producer completion. Release batches wait on readiness and
   the previous completion before signaling gates. Actual marker commands connect
   the wait/signal scopes. The copied words match the producer.
4. Submit a producer that signals a binary semaphore but waits for an unsubmitted
   gate signal. A consumer of that binary semaphore is rejected by core validation
   (03238); its callback aborts the call before the driver. Submit the gate release,
   then resubmit the consumer. Both complete with correct data. This intentional
   canary is counted separately from unexpected core/synchronization diagnostics.

The host counter query only chooses work; GPU semaphore edges carry the memory
dependency. This distinction is necessary even when a CPU observation says a
producer has finished. A single global gate value is also insufficient: releasing
value 2 would release every waiter for value 1, including an unready queue.

## What the experiment does not establish

- The simple global completion chain is rejected. It can introduce a cycle into
  a valid future timeline wait. Completion timestamps are not an ordering proof.
- Readiness gating is an experiment, not permission to remove runtime guards.
  Binary semaphore waits and presentation require the producer's dependent
  signal operations to have already been submitted. A future gate queued only
  after the consumer call would violate this rule unless forwarding is adapted.
- Real submission arrays contain separately ordered batches. A wait in a later
  batch must not prevent an earlier producer from being selected independently.
- Imported/exported payloads, host timeline signals, keyed mutex acquisitions,
  sparse work, submission metadata, queue/device idle, error reporting and device
  destruction must retain their original contracts. The historical game capture
  enables timeline and external Win32 semaphore extensions; binary-only or
  no-external-dependency assumptions are not supported by that evidence.
- Reusing an upload queue for release work must not park an unfinished wait ahead
  of an upload required by the selected application work. Dedicated scheduling
  capacity or a proven alternative is required.
- This small transfer experiment does not establish that whole-buffer snapshots,
  new layout transitions or changed render targets are safe in arbitrary graphics
  command buffers. Production integration needs those accesses and lifetimes
  covered, along with stage-limited dependencies and actual overlapping frames.

## References and commands

The [queue submission rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit.html)
define batch scopes and the binary dependency submission requirement (03238).
The [host signal rules](https://docs.vulkan.org/refpages/latest/refpages/source/VkSemaphoreSignalInfo.html)
require the recovery value to exceed the current value and remain below pending
device signals. The [synchronization chapter](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)
defines execution and memory dependency chains.

```text
python -B scripts/build.py --test queue_overlap
python -B sandbox/run_all.py --no-build --only queue-overlap
```

All environment settings are scoped to the child processes. The production
multi-queue guards remain unchanged until a full integration meets these contracts.

## D-289: failed signal submissions

3.1.5 replaces the old raw handle set with host-owned submission contributions.
Provisional visibility remains while the driver runs; no history lock spans the
call. Successful attempts commit their contributions; errors remove only their
own pending records. Prior successes and overlapping attempts survive, and object
destruction detaches the old identity so a late return cannot restore it. The old
whole-map capacity purge is unnecessary with lifetime hooks and has been removed.

The [QueueSubmit contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit.html)
and [sync2 contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit2.html)
require out-of-memory failures to leave referenced synchronization state unaffected.
The extracted old failure behavior passes 793/801 mocked host checks; the corrected
tracker passes 801/801, including controlled overlapping host threads. Actual GPU
overlap, bone replay, module exchange and core/synchronization checks also pass.
This is not a GPU out-of-memory reproduction, payload/value provenance or a
production access scheduler; the original guards remain.

### D-291 acquisition history (3.1.7)

AcquireNextImageKHR and AcquireNextImage2KHR replace the old binary queue-producer
classification only after successful/suboptimal acquisition. Unsuccessful and
fence-only calls preserve other history. The shared ABI remains 347c72b0cc7d0e40,
but the acquisition correction is in the host: exchanging only a backend into
an older host does not install it. Matching 3.1.7 sandbox artifacts, exchange and
the no-probe candidate pass; no game deployment or cross-queue scheduling claim.
