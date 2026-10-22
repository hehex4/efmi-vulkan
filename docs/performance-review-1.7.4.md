# Performance review of 1.7.2 through 1.7.4

2026-10-22. Reviewed committed HEAD `b5c12c3`, with `09c6b59`, `4cb072c`,
`27da667` and earlier resource lifetime paths. The main working tree's existing
uncommitted 1.3.7 work was excluded and preserved. No runtime fix or deployment
was made. No game process was started, stopped, inspected or injected.

The changes reduce CPU work, but performance is not ready for acceptance. Three
cache lifetime/validity regressions need correction. First-use waits remain on
the rendering path, and the 120 fps target has no game measurement for 1.7.4.

## Findings

### P1 / BUG-040: A stable matched index region can select the wrong Mod

Introduced by `09c6b59`. In `layer/src/character_backend.cpp:2191`, reaching
`kStableReads` lets a positive Mod match bypass its edge fingerprint until
`next_quick`, even when that memory is a coherent, persistently mapped pool.
Direct host writes to such a pool need not change the tracker's `region_gen`.
The old hash then selects a section in `character_record`, and can replace or
hide a different object of the same index count. This affects positive matches,
not merely delayed discovery of an unmatched object.

A native probe compiles the exact committed `IndexHashTable` and
`index_region_hash` with simulated mapped storage. After 120 matching reads,
it changes the first byte at the same buffer/offset/count without changing the
tracked generation. At 1.7.1 the next query sees the new hash; at 1.7.4 queries
for frames 121 through 135 return the old hash, and frame 136 detects the change.
A tracked write is a positive control and invalidates immediately in both.
This proves the cache regression in isolation, not a particular game's symptom.

Keep revalidation on positive matches whose memory may change without an
observable write. Backoff requires an independently reliable immutability or
write-generation guarantee; a history of matching bytes does not provide one.
Add coverage for same-region coherent writes after the stability threshold.

### P2 / BUG-041: Retired pipeline variants remain owned by the fast lookup

Introduced by `09c6b59`. `variant_lookup` owns `shared_ptr<const PipelineVariant>`
and is populated at `layer/src/character_backend.cpp:1448`.
`generation_retire` at line 1719 clears the other pipeline tables but leaves
this lookup intact. Removing the device cache's entry and completing all command
buffers therefore cannot destroy these variants. Their pipelines and descriptor
layouts survive until a material refresh or generation destruction. The 65,536
entry cap bounds the table but is not retirement and is per generation.

A native ownership probe uses the exact `generation_retire` and lookup
declaration with destructor-counting stand-ins for variants. After retiring 256
identities and releasing the cache and command owners, 1.7.1 releases all 256;
1.7.4 releases none until the lookup is cleared. This measures C++ ownership,
not GPU bytes or an observed game memory leak. Actual `PipelineVariant::clear`
destroys the Vulkan pipeline and layouts, so delaying its destructor retains
those driver resources.

Erase entries for the retired identity under `mod_mx`, preserving the separate
command-buffer owners until completion, or use a non-owning lookup with expiry.
Exercise repeated create/use/retire cycles through the character backend; the
existing `VariantCache` unit test does not cover this additional owner.

### P2 / BUG-042: Reused material scratch storage pins evicted textures

Introduced by `b5c12c3`. The former local `material_views` is now the generation's
`scratch_material_views` (`layer/src/character_backend.cpp:1196`). Each
`SnapshotImage` owns its image. These owners are copied into the command
snapshot, but the scratch references are only cleared at the next matched
`record()` call. A character that leaves the scene can have no such next call.
Even after commands complete and residency evicts its last draw's textures,
the scratch vector retains those images while `resident_bytes` no longer counts
them. If that last draw binds no Mod images, there is nothing to retain.

A native ownership probe uses the committed `SnapshotImage` declaration and
the old local/new member storage lifetimes with three destructor-counted image
owners. Releasing residency and completed-command references releases 3/3 images
before 1.7.4 and 0/3 at 1.7.4; clearing the scratch vector releases the remainder.
This is isolated ownership evidence, not a Vulkan eviction benchmark or a
measurement of the amount of game VRAM affected.

Release scratch resource owners at every draw exit, including refusal paths,
while retaining reusable container capacity. Keep the command snapshot's owners
until completion. Test a material draw followed by character inactivity,
completed command recycling and eviction. The current outer-vector `clear()`
also destroys the inner vectors, so it does not preserve their allocations.

## Measured benefit and its limits

Recalculated the six saved runs in
`.worktrees/ini/build/timing2/ab-vk14.json`. These are medians of three run-level
averages per version, not percentile distributions of individual frame times.

| Metric | Before, us | After, us | Reduction |
|---|---:|---:|---:|
| Recording per frame | 1977.14 | 1620.95 | 18.0% |
| INI plan per frame | 1223.55 | 971.82 | 20.6% |
| INI Present for 22 packages | 805.68 | 567.27 | 29.6% |
| Owned GPU submission CPU work | 106.64 | 64.29 | 39.7% |
| Replay host frame interval | 10082 | 9429 | 6.5% |

The structural changes and all three recording measurements support a CPU
improvement. The frame-interval result is much less conclusive: another game
was running during the comparison, and the first pair regressed from 10131 to
10769 us while the others improved. Interleaving helps but does not remove
uncontrolled load or prove a 6.5% game improvement. No 1.7.4 game result or
controlled tail-latency comparison is present in this evidence.

The stored `build/next/run_all-1.7.4.out` in the Claude worktree contains 35
successful steps and the final all-passed marker. This review checked that log;
it did not independently rerun the full suite. A passing suite does not exercise
the new lifetime and coherent-memory reuse cases above.

## Remaining performance work

- Texture binds still wait synchronously for up to four seconds per image
  (`layer/src/texture_residency.cpp:239`), and variant creation can wait four
  seconds per requested variant (`character_backend.cpp:1447`, also 1569).
  These are deliberate D-103 behavior from 1.7.0, not regressions introduced by
  1.7.4. They remain a frame-pacing limitation. The saved development replay
  `status-vk14-h-9000.json` contains four draw texture waits totaling 479066 us,
  with one lasting 158410 us, and 26 variant waits totaling 26618 us. The status
  still labels the development build 1.7.3; do not call it a released 1.7.4 game
  measurement. Move discovery, loading and variant preparation ahead of the
  draw, with a bounded render-thread handoff that preserves consistent character
  rendering. Command-pool reuse does not remove these waits.
- Bone matrices still cross mapped write-combined memory into CPU imports.
  The earlier game session attributed about 0.4 ms per rendered frame to this
  work. GPU remapping remains planned, not implemented by these optimizations.
- All loaded packages still execute their required Present semantics. Their
  measured cost is reduced, not eliminated. Any further pruning must preserve
  time-dependent variables, input and owned GPU work in the original INI.
- D-107 tests whether a package has ever activated in this generation, not
  whether it is visible now. `activate()` never resets that flag. Once a
  hash-only package has appeared, whole-frame hashing remains enabled for that
  generation. Do not extrapolate the cold single-character replay's savings to
  a session that has visited every character.

First fix the three validity/lifetime findings and cover their scenarios, then
repeat matched-condition offline measurements with startup/character switching
separate from steady state, including p50/p95/p99 and worst intervals. Finally,
in a user-started session, compare the same scene and rendering configuration
with replacement off/on against 120 fps (8.33 ms). Generated presentation
intervals and rendered-frame CPU work must remain separate metrics.

## Evidence from this review

`build/review-performance-1.7.4/run_probes.py` extracts committed source and
compiles native CPU-only probes for `56d2634`, `27da667`, and `b5c12c3`.
`probes.json` holds the three results. `review.json` holds the A/B calculations,
suite-log check and SHA-256 fingerprints of the inputs. Generated C++ and
executables stay in that ignored directory. No production source was modified,
and no GPU or game result is inferred from the destructor-counting probes.
