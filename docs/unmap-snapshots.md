# What is copied when memory is unmapped (layer 0.7.4)

> 2026-10-12. Review of the stutter diagnosis of 0.7.1 to 0.7.3, from the game data those versions
> collected. The change below is verified in the sandbox only; no game process has loaded 0.7.4.

## What the measurements of 0.7.2 show, read once more

The diagnosis in `docs/pacing-tracker-session.md` stands: in probe mode the layer copies buffers when
their memory is unmapped, with the resource lock held exclusively, and those copies overlap the largest
present intervals (3.46 GiB and 11.6 s of accumulated time in a 24 s sample; passthrough is smooth).

What that report does not say is which buffers were copied. The buffer inventory taken in the same
session (`captures/pacing-tracker-work-20261012/buffers.json`, 22 minutes into PID 41044) does. A
buffer's `generation` counts its copies at unmap and its tracked writes:

| Buffers | Count | Memory type | Generations | Volume if every one was a full copy |
| --- | --- | --- | --- | --- |
| 16 MiB (some 8 MiB) pools with usage index + vertex + storage + transfer source + destination | 22 | 4 (device-local, host-visible) | 2,612, of which 1,491 / 573 / 511 on three pools | about 40 GiB |
| Staging buffers: transfer source only, 75 KiB on average | 76 | 2 | 87 | 7 MiB |

The rule of the unmap copy was "every buffer with the transfer-source bit, up to 16 MiB". It was written
for staging buffers. The game's mesh pools have that bit too and are exactly 16 MiB, and while the open
world streams the game maps and unmaps them several times a second. So nearly all of the copied bytes
were whole pools, read back from device-local memory; three such copies of 43 to 44 ms each make up the
129 ms interval. In the character display scene nothing streams, which is why the first three sessions
ran at an even 120 frames per second.

0.7.3 made reads from write-combined memory faster. Its own measurement shows where: 8 MiB of memory
type 2 fell from 24.0 to 1.3 ms, 8 MiB of memory type 4 from 226 to 151 ms. The staging buffers are type
2; the pools are type 4. 0.7.3 therefore speeds up the part that cost 7 MiB and leaves the part that cost
gigabytes largely as it was. Its code is correct and stays (bounds, alignment, fences and the fallback
were checked again in this review), but it is not a fix for the stutter.

## 0.7.4: copy staging buffers only

At unmap a buffer is copied if it lies wholly inside the mapped range, is at most
`unmap_snapshot_limit` bytes and is a transfer buffer and nothing else (transfer source, optionally
transfer destination). A buffer with any further usage is not copied.

A buffer that is not copied may have been written through the mapping without the layer seeing it. What
its shadow knows from earlier buffer copies is therefore dropped at unmap and no longer served; before,
a buffer above the limit kept a shadow that could be stale.

`unmap_snapshot_all = 1` (`EFMI_VK_UNMAP_SNAPSHOT_ALL`) restores the old rule. Both settings can be
changed in a running process with the new `set` command (`ProbeClient.set`), so that one game session
can show both behaviours on the same route. The status has `unmap_snapshot_all` and
`counts.unmap`: buffers copied and their bytes, buffers passed over that the old rule would have
copied and their bytes, shadows dropped.

## What this gives up

The copy of a pool was read in one case only: when a draw's index bytes had to be hashed while the pool
was unmapped. With 0.7.4 such a draw has no hash (`"hash": null`, source `none`) instead of a hash from
the copy. In the three bundles from the game every hash came from the mapping (215, 210 and 153 draws,
none from a shadow), but those were taken in the display scene. How often a pool is unmapped at the
moment a draw is hashed in the open world is not known and is the first thing the next session measures.

This matters beyond the probe. A layer that replaces draws cannot copy 16 MiB pools either, so the
product needs index identity by another route if pools turn out to be unmapped at draw time (T-021).

## Verification (sandbox)

`sandbox/tracker_test.cpp`, `unmap_selection`: a pool with the transfer-source bit, a staging buffer, a
transfer-only scratch buffer and a partly mapped buffer in one memory object; which are copied, that the
pool's older shadow is no longer served, the counters, the old rule with `unmap_snapshot_all`, and a
limit of zero. `sandbox/limits_test.py`, `unmap_settings`: defaults, `set` with one, both and no
setting, clamping, and with every copy off the rewrite through an unmapped staging buffer yields no hash
and a counted unknown write instead of old bytes. `sandbox/probe_test.py` checks in all 17 configurations
that the staging buffer of the rewrite is the one buffer copied at unmap.

## What the next game session has to show

With the diagnostic settings of the last sessions (probe, `resources = 0`, `image_hash = 0`,
`profile_tracker = 1`) and the same open-world route:

1. `counts.unmap`: `passed_over_bytes` should grow by gigabytes, `copied_bytes` by little.
2. Tracker timings: `buffer_unmap` and `buffer_copy_record` should no longer accumulate seconds.
3. Present intervals over 33 and 50 ms against the 0.7.2 sample (51 and 17 in 24 s).
4. A few captures while moving: how many index hashes have source `none`.
5. Optional, with the user's agreement because it brings the stutter back for its duration:
   `set unmap_snapshot_all=1` for twenty seconds, to see the cause switch on and off in one process.
