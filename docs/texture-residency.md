# Texture residency: loaded on first bind, evicted under a budget (D-094, D-095)

Candidate 1.6.7, 2026-10-20; eviction policy, frame gate and index by hash of 1.6.8 (D-097, D-098). The 1.6.7 game session is described under BUG-034.

## Why

The second game session of the day (BUG-033) showed the earlier design's limit: each character
package loaded every DDS it declared when its first draw matched (Si: 17 files, 535 MiB; most of
them colour variants the user never selects) and never freed anything. Three characters filled the
8 GiB card beside the game; a fourth froze it. Under DX11 the same 20 Mods work because 3DMigoto
loads a resource only when a command list first binds it (`CustomResource::Substantiate` on first
use) and the DX11 runtime pages textures the frame does not use out of video memory. Both were
missing here.

## What the layer does now

One `TextureResidency` per device (`layer/src/texture_residency.h`), shared by every character
generation:

- **Declaration without IO.** A generation declares its images under a tag of its own (the
  generation's identity); equal paths under one tag share an image.
- **Load on first bind.** `material_draws` asks the residency for each image a draw binds. One not
  resident is demanded (a queued one moves to the front of the load queue) and the draw stays the
  game's until it is published; the game's own mesh shows for those frames, as before while a
  character loaded. `m2_texture_preload = 1` demands every image at activation instead (the
  sandbox tests that count uploads use it).
- **Budget and eviction (D-097).** The residency keeps the images' charged bytes near a target: the
  cap (`m2_texture_budget_mb`, default half the device-local heap) and, read twice a second with
  VK_EXT_memory_budget and corrected by what was loaded since, what the device has free beyond a
  reserve for the game (`m2_reserve_mb`, default the larger of 1 GiB and an eighth of the heap).
  Over the target, the images no frame has bound for 120 frames are released, least recently bound
  first; the GPU object lives on in the recordings that hold it and is freed with the last of them,
  and the next bind loads the image again. What the frames keep binding is never evicted for the
  target: in the 1.6.7 session that produced 574 evictions against 591 loads (the two-frame rule
  of 1.6.7, BUG-034). A load goes ahead beyond the target as long as the cap and half the reserve
  hold it; only beyond those does the image wait (`budget_waits`), after the images not bound in
  the last eight frames went.
- **Reduced loads under pressure.** While the working set is over the target, the largest resident
  full-size texture is demoted once every 30 frames: its next load leaves the top mip level out, a
  quarter of its memory, and demands made over the target (or while the device's free memory is
  below twice the reserve) load reduced as well. Textures of at least `m2_texture_reduce_mb`
  (default 16 MiB on disk, a 4096x4096 BC7 map) take part. Paced, so the frames never lose several
  characters at once and the set converges.
- **Frame gate (D-097).** Each package replaces draws in a frame only while every image and every
  pipeline variant its draws needed in the frame before is in place, and the first bind in a frame
  that finds one missing closes the frame for the package's remaining draws (hides included). The
  passes of one object thus never mix the Mod's mesh with the game's; a texture on its way costs the
  package whole frames (the game's own mesh shows), never parts of one. In 1.6.7 a draw whose texture
  was pending went to the game while the other passes of the same object were already the Mod's, so
  the depth test of the forward pass failed and the character turned black (BUG-034). Counted per
  package as `gated` (draws) and `gated_frames`; `gate_open` and `gate_images` in the status. After an
  F10 exchange the new generation's pipeline variants build first, so the character is the game's
  whole for those frames, where 1.6.7 showed the draws whose variants were ready beside the game's.
- **Publication with a timeline semaphore (D-095).** Every upload submission signals the device's
  timeline with its serial. Before an application submission on a graphics queue, the residency
  submits one prepared upload there, reads the counter (what is published) and, when the queue has
  not waited for that value yet, enqueues a wait for it: already satisfied, so it can never hold
  the queue behind another (D-091), and it gives the queue's later commands the uploads'
  visibility. The per-package batches, binary semaphores and fences of 1.5.7 are gone.
- **Reload (F10).** A candidate declares its images under its own tag, so its files are read again
  (as 3DMigoto re-substantiates on F10); it first brings up the images the live generation shows,
  so the exchange leaves no draw to the game, and a file that fails to load refuses the candidate
  while the live generation keeps its own entries.
- **Device features the layer enables** when supported and not enabled by the application:
  timeline semaphores (core 1.2; the KHR extension for a 1.1 application), VK_EXT_memory_priority
  (the layer's textures get priority 0.25, so the driver pages them before the application's) and
  VK_EXT_pageable_device_local_memory (`m2_pageable = 0` leaves both off).
- **Snapshot arenas in host memory.** The per-recording bone and entry storage lives in system
  memory the device reads over the bus (tens of kilobytes per draw); device memory stays the
  application's and the textures'.
- **Activation gate (D-092)** now counts only the mesh and the first batches, and the packages
  activating at the same moment (`activating_bytes`).

## Status

`textures` under `replacement`: `declared`, `resident`, `resident_reduced`, `loading`, `failed`,
`resident_bytes`, `target_bytes`, `cap_bytes`, `reserve_bytes`, `device_free_bytes`,
`free_estimate_bytes`, `pressure`, `idle_frames`, `protected_frames`, `loads`, `reduced`,
`evictions`, `demotions`, `budget_waits` (binds a draw lost to the budget), `pending`, `uploads`,
`published`, `waits`, `file_reads`, `submissions`, `releases`. Per package:
`image_count`, `images_ready` (resident and published), `images_failed`, `image_resident_bytes`.

## Verification

- Replay of Si (vk04, 535 MiB of textures) with `m2_texture_budget_mb = 200`: 11237 replaced draws
  at 360 frames, the forward pass fully covered, 11 images resident of which 2 reduced, one
  demotion, 19 budget waits while the set converged, 184 MiB under the 200 MiB cap, validation
  clean. Without the cap the same character replaces with every image full size.
- Replay of Typhoeus (vk06, 20 images, 770 MiB on disk): 12 images loaded, the 8 colour variants
  the INI never binds stay on disk; 229 MiB resident, 9301 replaced draws at 240 frames, validation
  clean.
- Character replacement 357 checks (uploads counted through `textures`), live INI rendering 76
  (the broken-DDS reload candidate refused, the live generation kept), full suite
  `build/next/run_all-1.6.7.out`.

## Not yet

- Textures larger than the device can hold at all (a single 64 MiB map on a nearly full card) are
  left to the game; a second reduction level would be the next step.
- Uncompressed RGBA8 textures (TangTang ships 64 MiB maps) could be transcoded to BC7 once into a
  cache the project owns, four times smaller on the device.
- Sparse residency (supported here) would let the mip tail stay resident while the top levels page,
  without any reload from disk.
- A transfer queue of its own for uploads; the uploads still run on the application's graphics
  queues.

Also in 1.6.7 (D-096): one index of the packages' draw counts per device, so a draw asks only the
packages that could match it instead of all of them (22 locks and lookups per draw before). In 1.6.8
(D-097) the index is by hash: a draw's index region is hashed once per device (each recording thread
keeps its own fingerprint table, no lock) and only the packages with a section of that hash are
asked; in 1.6.7 every activated package with hash-only sections took its lock and hashed the region
itself for every indexed draw, 17 times per draw in the game session (`draw_record` 24 us per draw).

## The 1.6.7 game session and the 1.6.8 changes

Session 4 (22:46-22:52, BUG-034): textures thrashed (591 loads, 574 evictions, 13519 budget waits,
target 356 MiB under a 1 GiB reserve), characters turned black now and then, frames of 24 ms mean
and 56 ms p95, and F10 froze the game. 1.6.8: the eviction policy and frame gate above, the index by
hash, the presenting thread never waits for the GPU (a queue change is followed only once the earlier
submissions completed on their own, `queue_waits`), a controller is held shared during its Present
work, and a presenter's semaphores are destroyed a few presentations after it stops (a queued
presentation may still wait on one, D-098). For the game, `unmap_snapshot_limit = 0` in the
configuration stops the probe's shadow copies of unmapped staging buffers (26 GB over the session,
under the exclusive resource lock), which the replacement never reads.

Verification: Si replay (vk04) with a 200 MiB cap at 360 frames: 11 images resident (2 reduced), 184 MiB, 0 evictions (1.6.7 evicted and reloaded throughout), 1 demotion, 579 budget waits while the set came up, 63 gated frames (2353 draws) before the forward pass was fully covered, 9376 replaced draws, validation clean (`--check` passed); character replacement 357 checks, live INI rendering 76 (the broken-DDS reload candidate refused as before); full suite build/next/run_all-1.6.8.out.

