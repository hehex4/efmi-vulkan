# Resource asset preparation

The general frontend now has a bounded DDS metadata decoder in
frontend/assets/dds.cpp. It returns Vulkan formats and exact mip byte ranges
into the caller-owned file data. It does not decompress, recolor or duplicate
payloads. It contains no mod/component identifiers. The decoder is followed by
the separate GPU upload ticket described below. Neither is linked into the layer
yet; descriptor binding and layer integration remain open. The texture cache
below connects worker preparation to a serialized caller queue.

The supported container is one ordinary 2D texture, at most 16,384 by 16,384 and
128 MiB of source data. Explicit DX10 BC1-7 and RGBA/BGRA8 format mappings retain
linear/sRGB and signed/unsigned distinctions. Legacy DXT1/3/5, BC4/5 and exact
32-bit RGBA/BGRA masks have mappings. Typeless formats, arrays, cubes, volumes,
premultiplied/custom alpha, padded rows, inconsistent headers and extra/truncated
payloads are explicit refusals. Unknown/straight/opaque alpha headers do not
transform payload bytes. A failed decode leaves the previous output unchanged.

The original 23 hosted-frontend and 28 planner tests still pass; 15 new native
asset checks cover the sample formats, small mip block sizes, immutability and
invalid containers. Format-table entries beyond these and the actual sample
have no GPU acceptance claim. Reproduce with scripts/probe_xxmi_host.py.

scripts/check_mod_draw_plans.py now runs the production decoder on every DDS
actually bound by its selected callback scenarios, and independently compares
the returned formats, dimensions and mip ranges against the raw headers and
block sizes. All ten actual files pass: nine DX10 BC7 (linear or sRGB) and one
legacy BC5U. Each has only mip 0. Four are 8K images with 64 MiB compressed data
apiece. The 38 bound mesh/texture files total 315,328,648 bytes, about 301 MiB.
The source INI and assets remain read-only, with fingerprints in
build/xxmi-host/mod-plan-report.json.

Two extra DDS declarations in the INI name absent files, but no tested callback
ever references those declarations: its corresponding draws use other textures.
Do not treat mere declaration as a load request, fail a whole mod for an unused
declaration, or silently ignore a missing resource once it is actually selected.

The size evidence rules out reusing the component-7 package's small allocation
budget for the whole character. GPU preparation needs bounded asynchronous file
loading and upload, device format/extent/feature checks, readiness/failure states,
queue/fence ownership, and retained resources through command completion. No file
read, DDS decode, decompression or pipeline creation belongs in a draw hook. This
is a design requirement for the next integration, not a measured game overhead
or an implemented upload scheduler. No deployment/configuration change occurred.

## GPU upload ticket

frontend/assets/image_upload.cpp now prepares a device-local image/view and a
host-coherent staging buffer, records all decoded mip copies and transitions the
image to shader-read layout. It preserves compressed blocks and color-space
formats. A ticket moves through Empty, Prepared, Recorded, Submitted and Ready
(or Failed). Recording and fence creation are off the submit path; submit only
forwards the already recorded command buffer to QueueSubmit.
Image/view getters return null until fence polling proves upload completion;
poll does not wait and frees staging/fence/command storage once complete. Source
file bytes can be freed immediately after prepare. Unsupported device features,
format capabilities/extents and partial allocation failure publish no image.

Preparation is separate from queue submission. The caller must serialize all
ticket calls and submit within the application's ordinary queue synchronization,
on its graphics queue. No unsynchronized worker submission, cross-queue transfer
or new device feature enablement is provided. The caller passes actual enabled
BC support and format/image-format properties for the intended usage. Ready
image owners must survive every consuming draw. Cancellation/shutdown destruction
may wait for this ticket's upload; the normal readiness path does not wait.

Reproduce the headless validation with:

```powershell
$env:VCVARS = 'D:/Microsoft/VC/Auxiliary/Build/vcvars64.bat'
& 'D:/Python312/python.exe' -B scripts/check_image_upload.py --sample
```

The initial synthetic GPU fixture passed 27 checks with zero core/sync validation errors
or warnings. It checks withheld handles, one-time submission, staged-source
independence, all compressed mip bytes, disabled BC/device-limit refusals and
an injected staging-buffer allocation failure after image creation. A GPU blit
to linear RGBA verifies that source byte 128 stays 128 for UNORM and becomes
about 55 for sRGB, with alpha preserved. This is a format/conversion test, not
an actual game material shader test.

The optional sample pass fingerprints and uploads all ten selected actual DDS
files, including the four 8K images, then compares every compressed payload byte
after GPU readback. All ten pass with clean validation. Reports/logs and tested
binary SHA-256: build/assets-gpu/report.json. No layer is loaded, no window opens,
and no mod/Core/config file is modified. This proves the standalone uploader on
the sandbox's device configuration; it does not establish the game's enabled
features, upload scheduling/overhead, material bindings or rendered appearance.

The cache below adds bounded file/GPU ownership. Layer queue/enabled-device
integration, immutable material bindings, shared character/Core state and full
frontend reload remain open. Neither helper is called by a layer draw/submit hook.

## Bounded asynchronous texture cache

frontend/assets/texture_cache.cpp owns one frontend generation and graphics queue.
Setup resolves absolute paths lexically and deduplicates equal normalized paths;
it does not inspect files or merge differently spelled case/symlink/hardlink aliases.
Declarations are dormant until demanded. An already resolved handle can be demanded
concurrently; it queues at most once, with no file IO, decode or driver work there.
Missing files only fail if demanded, and failures stay recorded until a new generation.

One worker opens each selected file read-only while denying concurrent writes/deletes,
reads at most 128 MiB, validates DDS, prepares memory and records upload commands.
It keeps at most one image awaiting/in upload, then polls that image's fence and frees
staging before preparing the next. Caller-serialized submit_prepared does at most one
QueueSubmit on its first selected queue; a different queue is refused. It creates no
GPU objects and performs no file IO or fence polling. A Ready handle is only published
after worker polling reports completion. This is bounded preparation, not a measured
game-frame overhead guarantee or an automatic layer queue adapter.

AllocationBudget charges actual Vulkan memory requirements before allocation, including
driver alignment. Default aggregate limits: 512 MiB resident, 160 MiB staging and 128 MiB
source bytes. Budgets are copied/shared across overlapping generations. Partial failures
release their charges; a retired cache cannot release an image retained by a command
owner. Stopping is off the render path and drains the one pending upload. Ready owners
still require retention through all consuming GPU work and before device destruction.

The expanded headless fixture passes 57 checks, including 256 concurrent demands of one
file, dormant missing files, null/relative paths and capacity refusal, immutable staged
bytes after a source edit, new-generation reload, retained old allocations, foreign-handle
rejection, source/resident/staging limits, cancellation before/after submission and queue
failure. Instrumented Vulkan seams check that creation/fence polling occur on the worker
while QueueSubmit occurs on the caller. Core/sync validation has zero errors/warnings.

All ten actual selected DDS files also complete through one cache. Actual peak residency
is 283,377,664 bytes; staging peak is 67,108,864 and source peak is 67,109,012. Five sample
cache checks pass, including retention after retirement and final budget release. The ten
independent full-payload readbacks still pass. Build/assets-gpu/report.json fingerprints
the current executable and source inputs. Original assets remain unchanged.

This is standalone texture preparation/ownership only. No mesh-buffer cache, material
descriptor integration, complete Core execution, DLL deployment or new game test is claimed.

## Immutable mesh-buffer upload ticket

frontend/assets/buffer_upload implements device-local vertex/index byte ownership with
the same prepare/record/caller-submit/poll phases as images. It preserves every source
byte without format conversion or padding, bounds input to 128 MiB and charges actual
Vulkan allocation requirements against shared resident/staging ledgers. The prepared
source may be released immediately. Only a completed, polled fence exposes the buffer;
staging and command objects release on completion. Ready owners stay alive through all
recorded uses and device teardown. Destruction can wait only for an outstanding canceled
upload, so cancellation/stop remains off the render path.

The recording inserts transfer-write to vertex/index-read synchronization on the same
graphics queue. Submit alone performs QueueSubmit; no files, allocation, recording or
polling occur there. The frontend still supplies stride/index format, verifies draw
ranges and controls the queue's external synchronization. No texel/storage buffer view,
cross-queue ownership or live hook resource manager is implied by this byte uploader.

scripts/check_image_upload.py --buffers passes 138 headless checks and full byte readback
for all 28 BUF files selected by the verified sample callback plans. Inputs are checked
against the prior asset fingerprints; original files remain read-only. Synthetic 1/3/6/
4101-byte cases exercise exact copies, source destruction, hidden pre-fence handles,
immutable state, cancellation, submit failure and both allocation budgets. The material
pixel fixture now actually draws indexed triangles from these uploaded device-local
vertex/index resources, including its FS/VS isolation and null/query controls. Core/sync
validation reports no errors or warnings. Evidence: build/assets-gpu/report.json and
build/buffer-upload-check.txt. Historical fragment validation also passes with this build.

This ticket remains standalone. The common layer material pipeline/snapshot code is
separately verified in 0.9.3 (docs/fragment-materials.md). Next, unify buffer and image
file scheduling/ownership and connect the hosted frontend to those ready resources.

## Shared resource cache for images and mesh buffers

ResourceCache replaces the texture-only implementation; texture_cache.h retains
aliases for existing callers. A single worker, one outstanding upload and one
caller-serialized queue handoff now serve both resource kinds. All source/staging/
resident budgets are shared across types and generations. Queue lifecycle,
deferred file reads, sticky failures and stopped-generation ownership are unchanged.
The key combines normalized absolute path with buffer usage; zero usage represents
an image declaration. Invalid buffer usages refuse, and declaring one file as a
different kind or usage never reuses an incompatible GPU object.

The headless suite passes 149 checks with no core/sync diagnostics. Added coverage
includes 128 concurrent mesh demands, mixed queued resources, independent typed
readiness, a mesh file deliberately requested as DDS, exact statistics, retained
old-generation buffers, changed file sizes on reload and worker/caller driver
instrumentation. The material pixel test loads both VB/IB and three images through
one ResourceCache, retires that cache before recording, then draws successfully
using its retained resources. This joins file scheduling, upload and actual binding
in a standalone renderer; it still does not run the full XXMI frontend or game hook.

With scripts/check_image_upload.py --resources, all 38 selected sample resources
(28 BUF, 10 DDS) become ready exactly once and pass five cache/validation checks.
Actual peak bytes: resident 315,327,232; staging 67,108,864; source 67,109,012.
After cache retirement, owners keep residency charged; releasing final owners
returns every ledger to zero. Inputs are fingerprint checked against the callback
report. Evidence: build/assets-gpu/report.json, build/assets-gpu/sample-resources.txt
and build/resource-cache-check.txt. Historical fragment validation passes with
the new executable. No layer change or new game result accompanies this work.

Next: resolve hosted command/resource declarations into ready mesh/material
transactions and integrate shared character/Core identity and bone completeness.

The next frontend checkpoint now hands owned file declarations to this cache through
frontend/assets/file_plan. It freezes byte stride/index type/usage, preserves nulls and
native boundaries and publishes typed owners only for a complete ready resource set.
The 161-check headless suite and 384 actual CPU callback plans pass; details and exact
remaining integration boundaries are in docs/frontend-draw-plans.md. Residency eviction
and multi-mod performance tuning are deferred at the user's request; the underlying
ResourceCache behavior from 6dfce2f is unchanged.


Per-draw file resolution (2026-10-13) now retains Ready owners and binding metadata
for every draw. Buffer uploads preserve u16/u32 whole-file extrema, when the file
length permits that interpretation, for conservative vertex bounds without a
second CPU payload or driver readback. The standalone 200-check GPU suite and all
28 actual BUF roundtrips/extrema pass with clean validation; all 404 actual callback
draws fit the conservative bounds. See docs/frontend-draw-plans.md for remaining
native Core and application-state requirements.
