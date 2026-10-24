# Performance follow-up: 1.7.5 and 1.7.6

Scope: offline implementation and verification after the review of 1.7.4.
Deployed to dist with the game closed after verification; no game load. The baseline is corrected 1.7.5, commit 491107f, rather
than the invalidation/lifetime regressions of 1.7.4. The main tree's unrelated
uncommitted 1.3.7 work remains separate from both builds.

## Correctness baseline (1.7.5)

BUG-040 restores the edge fingerprint on every positive matched draw; a stable
history is not an immutability guarantee. Negative backoff and periodic full
hashes retain the previous policy. BUG-041 clears all fast lookup owners for a
retired pipeline identity. BUG-042 clears draw scratch owners on normal return,
refusal and exceptions without destroying reusable inner vector capacity.

The native test fixture calls the production hash, retirement and scratch paths
directly (158/158, 14 added checks). Image ownership uses CPU stand-ins; variant
objects have no GPU allocations in that fixture. The full 35-step regression
passed, including two recorded-frame replays, 357/357 character replacement,
76/76 live INI and 76/76 Vulkan validation. Binaries:
`build/performance-baseline-1.7.5`; log: `build/performance-run-all-1.7.5.out`.

## Further changes (1.7.6)

- Queue every selected texture in one logical draw before waiting, using the
  existing three loaders and unchanged residency/source/staging limits. Unused
  material variants are not loaded. Ready bindings use one residency lock on
  the ordinary path, retaining storage through a draw and clearing owners at exit.
- Resource completion uses a condition variable. One four-second deadline
  covers the texture batch; a bounded counter recheck preserves completed-only
  timeline publication. No future semaphore value or incomplete image is exposed.
  Only shared request handles and indices cross an unlocked wait; declarations
  during F10 can grow the image vector without invalidating a waiting reference.
- Queue all cold pipeline variants of the selected draw list before awaiting
  them, with one four-second deadline for that list. The ready fast lookup
  remains. Retirement and shutdown wake waiters even while driver compilation
  is still outstanding; the driver work is still drained before destruction.
- Allocate one two-pose staging buffer for the largest component at skeleton
  configuration, then reuse it. SSE2 processes each exact float4 bone row with
  the same multiply, rounding and finite checks. Both poses are committed only
  after every used value passes; unused rows and static poses remain untouched.

`textures.draw_waits`, `draw_wait_max_microseconds` and timeout counts now
describe batches; `draw_wait_images` counts their bindings (duplicates included).
Compare wall time, not the old per-image wait count. Four-second first-use stalls
remain possible when a whole batch is slow; this change does not promise stall-free
first appearance or move skeleton reads to GPU compute.

## Verification and measurement

Both 1.7.5 and 1.7.6 passed all 35 regression steps. Final 1.7.6 checks:

- Native tracker/cache ownership: 158/158; bone contract: 123; asynchronous variant cache: 34.
- Character replacement: 361/361, including no-preload first use; live INI/F10: 76/76.
- Vulkan validation: 76/76; INI GPU resources: 11,189 checks with zero validation errors/warnings.
- Both captured-frame replays passed. Standalone image/resource/cache GPU tests: 266 checks with zero validation errors/warnings.
- Five historical game captures replayed through the deployed importer and the independent Python reference: 283 records, 74 imports, 209 repeats, every master/entry equal after every record. The output SHA-256 also matches the historical verified output. This is saved-input verification, not a new game capture.

Logs are under the main project's build/: performance-build-1.7.6.log,
performance-run-all-1.7.6.out and performance-resource-wait-1.7.6.log. Historical
bone evidence: build/performance-bone-replay-1.7.6/report.json.

`scripts/bench_bone_import.py` compiles actual baseline and working importers
with the same production flags and compares output checksums over interleaved
timing rounds. It measures cached CPU memory, not mapped GPU read latency.

`scripts/bench_capture_replay.py` uses the same replay executable for both DLLs,
an empty layer pipeline cache per process, a 300-frame warmup checkpoint and
at least 2400 total frames. Steady-state CPU counters are deltas after warmup;
p50/p95/p99 use the final 2048 host-present intervals. Startup texture and variant
waits are reported separately. Driver/OS caches can remain warm; no actual game
frame-rate result follows from this sandbox.

## Observed performance

Seven interleaved importer rounds with identical compiler flags reduced the
cached-CPU median by 86.0-89.1% across 16/64/256/1024-bone imports, with and
without remaps. Every output checksum agrees. Full report:
build/performance-worktree/build/bone-bench/report.json.

Three interleaved replay pairs per scene, 2600 frames each, 300-frame warmup;
median of each run's measurements (timing runs have validation disabled):

| Metric | vk03 1.7.5 | vk03 1.7.6 | vk04 1.7.5 | vk04 1.7.6 |
|---|---:|---:|---:|---:|
| Steady recording (us/frame) | 443.24 | 436.46 | 906.38 | 885.57 |
| Bone stage (us/frame) | 28.01 | 13.80 | 70.87 | 44.87 |
| Initial texture waits (ms) | 210.83 | 194.76 | 519.07 | 430.82 |
| Initial variant waits (ms) | 43.88 | 43.43 | 121.00 | 121.95 |
| Host p50 (us) | 5524.00 | 5520.00 | 5529.00 | 5534.00 |
| Host p95 (us) | 10383.00 | 10486.00 | 10729.00 | 9776.00 |
| Host p99 (us) | 11907.00 | 11830.00 | 11809.00 | 11712.00 |

Corrected-baseline comparison: recording CPU -1.5%/-2.3%, bone stage -50.7%/-36.7%,
initial texture waits -7.6%/-17.0%. Variant wait totals are effectively unchanged.
All runs have zero post-warmup texture/variant waits. Final target bytes,
post-warmup replacement counts (12 and 32 per frame), and texture residency
ledgers (201,327,036 and 284,427,848 bytes) agree in every run. Ledger equality
is not a measurement of total process VRAM or long-session GPU retention.

The replay uses a 640x360 FIFO swap chain on the RTX 5060 Laptop GPU; host
intervals are paced by presentation. Their p50 is essentially unchanged, so
these results do not establish game FPS or acceptance against 120 fps. Report:
build/performance-worktree/build/performance-ab/20261024-152518/report.json.

An additional three-pair vk03 control used the previously deployed 1.7.4 against
1.7.6. Initial texture waits: 241.03 to 125.35 ms (variable first-use timings).
Steady recording: 412.30 to 425.09 us/frame, a net **increase of 12.79 us (3.1%)**.
The necessary positive fingerprint checks rise from 3.58 to 29 per frame;
fingerprint-read time rises from 5.68 to 42.79 us/frame. Bone/material savings
partly offset that correctness cost. Host p50: 5531 to 5537 us; p95: 10220 to
9813; p99: 11542 to 11345. Final targets, replacement coverage and residency
again agree. This is not a claim that 1.7.6 has lower total steady CPU cost than
1.7.4. Report: build/performance-worktree/build/performance-ab/20261024-152920/report.json.

## Deployment

After two checks that the game, launcher and project GPU tests had exited,
18 binaries/manifest files were backed up and copied to dist. Every deployed
file matches the tested candidate SHA-256. Backup and manifest:
build/backups/before-1.7.6-20261024-153223/. The configuration is byte-identical
(SHA-256 240d9deb70be415add04cbaf97271a5f81467259da3232227f66e473700f5dc8).

A new sandbox process loaded the deployed injector with the actual dist
configuration: version 1.7.6, 23 packages, replacement/profiling enabled, normal
exit (build/performance-smoke-1.7.6.json). It is not a game load or visual test of
a real character. The launcher was read back as DX11 (DIRECT, -force-d3d11);
its configuration was not changed.

## Required game boundary

The remaining acceptance needs a user-started Vulkan session: real mapped-memory
cost, first character appearance/switches, dashes, F10 and key variants, long-session
GPU ownership, and the same-scene baseline against the user's 120 fps target.
The launcher remains in its last user-requested DX11 configuration. No launcher
switch or game load is part of these offline measurements.
