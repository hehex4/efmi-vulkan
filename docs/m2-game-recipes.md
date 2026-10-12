# M2 pipeline and bone observations with 0.8.0 and 0.8.1

## Actual LOD1 snapshots with 0.8.1 (2026-10-12)

User-started Endfield PID 34644 loaded 0.8.1 probe/inject at 17:39, with resources on,
image hashing and tracker profiling off. The user explicitly kept the character in LOD1.
Both bounded collections succeed on their first attempt with `wait_draws=True`:

| Bundle under captures/m2-0.8.1-20261012/ | Frame | Empty intervals skipped | Graphics draws | Matched draws | Matched pipelines |
| --- | --- | --- | --- | --- | --- |
| lod1-a/attempt-01 | 19506 | 8 | 818 | 58 | 36 |
| lod1-b/attempt-01 | 20414 | 7 | 819 | 58 | 36 |

Each interval also contains 60 dispatches (raw `draw_count` is 878/879), 35 render passes,
18 submits and five command buffers. Graphics recording uses one thread; the three threads
in aggregate statistics include other work. No unknown buffer writes are reported.

All nine LOD1 sections match in both frames: 47 LOD1 draws, including 25 in subpasses with
used color attachments. Components 4, 6 and 7 keep their base sections (11 more draws).
All matched draws are single-instance; all 36 matched recipes are complete and pass CPU
variant preflight. Unrelated omitted pipelines beyond the export limit are explicit
(130/132); there are no matched pipeline gaps.

For each frame, all 58 instance entries (256 bytes each), all 58 current bone windows and
all 58 previous bone windows (12 KiB each, identified by the VS preflight binding) agree at
record and submit. All actual bone-window floats are finite. Snapshot storage is 173 blobs,
4,430,272 bytes, with no omissions. Unreadable extraneous material/storage ranges are not
missing VS bones. The whole matched snapshot comparison has no `different` or `not_recorded`
items. Constant 5's remaining words include flags 2 or 258 and float 1.0; preserve them when
patching its two bone offsets. Constant 3 translation is shared across the 58 draws.

Both bundles' 147 file fingerprints and each parent series' child-manifest fingerprint pass;
report: `captures/m2-0.8.1-20261012/fingerprint-report.json`. T-023 is verified in this scene,
and the previously missing LOD1 color-pass bone/entry timing is supplied. These two stationary
observations do not establish combat, multiple instances, animation correctness, GPU variant
creation in the game, or replacement rendering. Continue M2 offline without another scene
change or duration-only test. The older evidence and its corrections remain below.

Date: 2026-10-12. User-started Endfield PID 17484, loaded 0.8.0 probe/inject with resources on,
image hashing and tracker profiling off. Evidence root: `captures/m2-0.8.0-20261012/`.
These are original-game observations and CPU preflight results, not replacement rendering.

## Scene labels corrected by the user

The display scene is LOD0. The user initially placed the world character in LOD1, and changed back
to LOD0 after the agent incorrectly interpreted empty captures as no scene drawing. Therefore
`world-rendered-1/2` are the user's LOD0 state, not successful distant-LOD1 observations. Their seven
LOD1 mesh sections occur only in a depth-only subpass. That is useful depth coverage but cannot
stand in for LOD1 color passes. The earlier `world`, `world-b`, `world-c` remain empty attempts;
they do not establish that the visible world had no drawing. The user confirmed normal movement.

## Saved recipes

| Bundle | Frame | Scene draws | Matched draws | Matched pipelines | Scope |
| --- | --- | --- | --- | --- | --- |
| display | 23724 | 179 | 45 | 39 | Display LOD0, all 12 base sections |
| display-b | 29586 | 179 | 45 | 39 | Second display observation |
| world-rendered-1 | 91837 | 1157 | 61 | 45 | User's LOD0 state; 14 LOD1 depth draws |
| world-rendered-2 | 92221 | 1158 | 61 | 45 | Same scope, second observation |
| lod1-recipes/attempt-01 | 369782 | 872 | 58 | 36 | User returned to LOD1; all 9 LOD1 sections, including color passes |

Every matched pipeline above has a complete recipe and supported CPU variant preflight. All
matched pipeline/shader exports exist. The world bundles omit unrelated pipelines beyond the
128-pipeline export limit (185, 188 and 153 respectively), with no query gaps. No omitted matched
pipeline is treated as supported. All bundle file fingerprints were checked against their manifests.

In the LOD1 frame, components 4, 6 and 7 still use the base sections; the other nine use `_LOD1`.
LOD1 matches span passes 5, 6, 9, 27 and 32, rather than just the depth-only pass 5. These are pass
identifiers in this frame, not universal semantic labels. The selector requires a used color attachment
in the actual pipeline subpass; it does not claim pixel visibility or identify the main pass by name.

Offline checks on these current-session exports: the production C++ VS rewrite equals the Python
reference and passes spirv-val for 21 display modules, 21 world-LOD0 modules and 19 world-LOD1
modules. Reports: `build/variant-shaders-ddqu6r34/`, `build/variant-shaders-hwyh0bur/`,
`build/variant-shaders-8depo5y8/`. This does not prove driver compilation of those variants.

## Buffer evidence and the remaining gap

Both LOD0 world frames have all 61 instance entries and both actual VS bone windows equal at record
and submit (12 KiB per bone window, no snapshot omissions). This includes the 14 LOD1 depth draws.
Display frame 23724 has the same result for all 45 draws. In display-b, all entries and previous-bone
windows agree; one current 12 KiB window differs (component 3, hash 879c5214, draw 11). That window
extends beyond the component's own palette. Its mismatch cannot establish that the required palette
was late, and this frame does not retain the record-time prefix bytes needed to settle it.

The successful full LOD1 recipe bundle has **no buffer snapshots**. Current/previous LOD1 bone and
instance availability in its color passes remain unverified. Unreadable material/other storage ranges
in a broad snapshot are not counted as missing VS bones; the preflight's bone set/binding identifies
the relevant ranges.

## Why empty attempts must not change the scene label

The capture boundary is one interval between vkQueuePresentKHR calls, across queues. In the initial
empty `world/world-b/world-c` attempts, surrounding API counters nevertheless add 1149/1837/1214
indexed draws and 40/80/41 render-pass begins. These counters cover more than the captured interval,
so they do not prove those calls belonged to it, but they refute the inference that drawing stopped.

After the user returned to LOD1, a bounded 40-status sample has 30 empty last intervals and 10 with
849-861 draws; total tracked draws grow by 49,507. A separate no-snapshot series keeps 16 raw intervals,
5 with 902-914 draws and all nine LOD1 sections. All files are retained in `lod1-status-series.json` and
`lod1-raw-intervals/` (diagnostic raw frames, not full manifest bundles).

Three 12-attempt series with full snapshot requests (`lod1-selected`, `lod1-selected-spaced`,
`lod1-selected-delayed`) select no scene draw. Delays between attempts or immediately before capture
did not resolve it. Removing the snapshot request then yielded the complete LOD1 recipe bundle on
the first attempt. This points to the short capture window interacting with record-time snapshot
work; the exact present producer and any frame-generation involvement are unproven.

The 0.8.0 state machine always ends at the next present, even when no graphics were submitted in
that interval. The 0.8.1 candidate now keeps the record-time request armed across empty intervals
when explicitly requested, and returns one interval that actually contains draws without merging
frames. It retains the timeout and reports skipped intervals. No running-game code is changed.

## Bounded collector selection

`collect_probe.py --require-section '*_LOD1' --require-color --attempts 12` now saves each attempt
in its own bundle and a `series.json` with child manifest fingerprints, selected bundle or explicit
exhaustion/interruption. Empty or depth-only observations never satisfy that color selection.
Variable short delays before capture avoid a fixed retry cadence but do not solve the snapshot gap
observed above. Default one-frame collection is unchanged. Sandbox evidence tests: 109 checks pass.

## 0.8.1 candidate verification

Source and build/next now add `capture_wait_draws`; dist/ and the running game still use 0.8.0.
See `probe-snapshots.md` and D-029 for the API and limits. Full build/regression passes:
unit 88, native tracker 144, native recipes 120, variant runner 37, 17 probe cases 517-532,
modes 40, limits 174, timing 126, Vulkan validation 76, MCP 47, evidence 113.
Log: `build/capture-wait-draws-regression-20261012.txt`.

Candidate actual-config inject smoke also passes: 0.8.1, resources on, image hashing/profiling off,
five snapshotted draws with the new wait enabled, complete recipe and correct hashes before/after
the sandbox rewrite. Report: `build/capture-wait-actual-config-next.json`. This time the actual
game-log path exists and its before/after SHA-256 is recorded and equal; that proves preservation
only during this smoke. It does not repair the historical missing fingerprint below.

Both candidate DLLs SHA-256: `7e311a9f9f358aa50d90c579abfc1384877a8b391aea8ef886d1e87a90ba50d7`.
No deployment while PID 17484 holds the original DLL. The LOD1 snapshot result remains pending a
new user-started session after deployment; successful sandbox waiting does not supply the game data.

Deployment followed at 17:36 after the user reported normal exit and process absence was checked
twice. Backup: `build/backups/before-0.8.1-20261012-173645/`; manifest: `build/deployment-0.8.1.json`.
All eight deployed files match the tested candidate. The deployed actual-config smoke passes too
(`build/capture-wait-actual-config-dist.json`), including a real-path game-log hash check. The settings
are unchanged. No 0.8.1 game run yet; next observation is the actual LOD1 state, with --wait-draws.

## Correction to the earlier deployment smoke record

The temporary `build/m2_actual_config_smoke.py` used `dist/efmi_vk.log`, not the actual
`dist/efmi_vk_layer.log`. Its historical log equality check compared two missing-file results and
did not prove log preservation. Recipe/hash/settings smoke results remain valid. The script now
requires the actual file and compares its bytes, but it cannot reconstruct the missing old fingerprint.
No historical preservation claim is inferred from the currently appended game log.
