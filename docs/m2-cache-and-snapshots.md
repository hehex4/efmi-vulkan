# M2 asynchronous variants and immutable draw snapshots

This records the 0.8.3 building-block verification before hook integration. Candidate
0.9.0 connects them to the opt-in [component hook](m2-component-hook.md); that page
describes command ownership and controlled recycling after a successful legal reset.
The immutable public API and the helper evidence below remain applicable.

`VariantCache` owns immutable creation recipes and compiles on one worker. A request
returns queued/building/ready/failed/capacity status without waiting for driver compilation.
The bounded key includes the original pipeline generation and canonical vertex declaration,
including strides, formats, offsets and input rates. A failed key is retained to avoid a
retry on every draw. Retiring a source cancels pending work, prevents an old completion
from replacing a newer request, and releases cache ownership. Command-buffer owners retain
ready pipelines until their GPU uses end. Stop joins the worker before device destruction;
all external command owners must also be released before destroying the Vulkan device.

`DrawSnapshots` allocates private VS storage and dynamic-uniform descriptors off the draw
path. Each appended draw gets its own bone slice and copied 256-byte instance entry. Device
alignments and buffer-range limits are checked; the final dynamic entry has the full 64 KiB
declared uniform range available. Initial padding is zero. It requires host-coherent memory
and has explicit capacity/byte limits. A failed append does not consume a slot or change its
output. Seal prevents further writes and recreation; there is no public reset/reuse API.
In 0.9.0 only the command owner can recycle completed storage after the driver accepts
the application's legal begin/reset, avoiding per-frame allocation.

The hook integration must retain these resources with the command-buffer recording,
seal before first submission, and release only at a legal command-buffer reset/free after GPU
completion. Repeated submissions read the same immutable bytes. It must also keep any mesh
and pipeline ownership required by those recorded commands. These helpers do not themselves
track application command buffers or establish that integration.

## Verification

- Native cache: 21 checks with a deliberately paused driver cover nonblocking requests,
  deduplication, capacity, stride changes, generation changes, retirement during creation,
  canceled queued work, failure cleanup and shutdown while creation is active.
- Native snapshots: 25 checks cover all seven allocation/bind/map failure stages, partial
  cleanup, memory eligibility, bounds, nonzero alignments, last-entry range, source independence
  and immutable sealed storage.
- The real GPU variant test now uses the production cache and snapshot allocator. It builds
  from owned metadata after source handles die, selects the second private snapshot (the first
  is deliberately wrong), poisons the original CPU arrays, retires/stops the cache before submit,
  draws via the retained pipeline and resubmits the executable command buffer. Pixel readback
  still proves private VS data, original FS data and restoration for the next original draw.
- Both render-pass versions pass through no layer, layer and inject, with core/sync validation
  clean. Three negative canaries still confirm active validation. The runner passes 39 checks.
- Full build/regression passes in `build/next`; log `build/variant-cache-regression-20261012.txt`.
  Unit 88, tracker 144, bones 42, recipes 120, cache 21, snapshots 25, variant runner 39,
  17 probe cases 517-532, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 113.

This historical helper verification is sandbox rendering and lifetime evidence, not a game pipeline
driver-build result, GPU completion tracking by the probe, or actual character replacement.
