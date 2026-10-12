# Tracker work diagnostics and copy changes (0.7.2)

Follow-up: [the 0.7.2 game session and 0.7.3 candidate](pacing-tracker-session.md) locate
large unmap copies and lock contention overlapping the longest foreground spikes. 0.7.3
accelerates detected write-combined mappings while preserving the full snapshots. It passes
sandbox regression and local copy measurements; game smoothness awaits a new session.

The 0.7.1 game comparisons narrow T-020: hashing off still stutters, resources off still
stutters, and the user reports smooth movement in confirmed passthrough. That implicates
probe-only work as a group. It does not yet identify a routine or establish a quantitative
passthrough baseline. Evidence and boundaries: `pacing-resources-off-session.md`.

## Two unnecessary buffer-copy costs removed

Before 0.7.2, recording a copy into a shadowed buffer allocated and zero-filled its whole
source range before checking whether those bytes were CPU-readable. A failed read cleared the
vector's size but retained its capacity until command-buffer reset. A 64 MiB unreadable copy
therefore retained 64 MiB of useless storage. The new path first borrows a validated mapped or
shadow range under the resource lock, and allocates a fallback only when bytes actually exist.

At submit the old path read through a temporary vector and then copied again into the target
shadow. The new path copies directly from the borrowed source into the target under the same
lock, removing the temporary allocation/initialization and intermediate copy. A self-copy's
shadow already has full storage, so target resizing cannot invalidate that borrowed pointer.

Visibility behavior is preserved: submit-time contents take priority; record-time bytes remain
a fallback after an unmap without a snapshot; unmap snapshots still support copies recorded
later; unreadable writes invalidate knowledge. No buffer feature was disabled by this change.
Default unmap snapshots still copy transfer-source buffers of at most 16 MiB and may remain
expensive. These are code-level savings, not a game-verified stutter fix.

## Optional measurements

Set `profile_tracker = 1` beside the DLL, or `EFMI_VK_PROFILE_TRACKER=1` in a test child's
environment. The default is off. This setting is read at startup and does not hot-reload.
`status.tracker_work` reports the setting and these categories:

| Category | Timed work |
| --- | --- |
| `buffer_unmap` | Tracker lock, staging snapshots and mapping bookkeeping before driver unmap |
| `buffer_copy_record` | Tracker lock, source lookup and fallback copy for a recorded buffer copy |
| `buffer_submit` | Applying all shadow writes of one command buffer |
| `submit_tracking` | Frame lock, buffer/image work, stats and any capture work for one submit info |
| `command_reset` | Pool reset tracking or command-buffer begin/reset bookkeeping |
| `draw_record` | Saving a draw/dispatch record, including a selected record-time snapshot |

Each category has calls, cumulative microseconds, maximum duration and copied bytes. Byte
counts describe the explicitly instrumented copies, not all memory traffic. Submit tracking
contains buffer-submit work, so their times must not be added. Draw recording excludes dispatch
lookup and API counter updates; descriptor tracking, pipeline tracking and secondary-record
merging are not individually timed. This is a targeted diagnostic, not a complete CPU profile.

The timer excludes forwarded Vulkan driver calls, but includes waiting inside the tracker and
OS descheduling. It is wall time, not CPU execution time. Enabled profiling adds two QPC reads
and atomic counter updates per measured operation, including each draw; it can perturb timings.
With profiling off, the scopes make no clock reads or counter updates.

Operations taking at least 2000 us enter a fixed 64-event history with category, host start time,
duration and copied bytes. Fast operations do not acquire the history mutex or allocate. Slow
events are retained in insertion order; concurrent completions need not have ordered start times.
`slow_total` exposes rollover. Cumulative counters are atomic but loaded individually, so a live
snapshot is not a transaction across all fields. The status formats a copied history after
releasing its lock; this lock is separate from the resource and frame locks.

Present peaks also gain `host_end_us` on the same QPC clock. A peak spans
`[host_end_us - microseconds, host_end_us]`; work spans
`[host_start_us, host_start_us + microseconds]`. Overlap locates a candidate stall, but does not
prove a worker operation blocked presentation. QPC times are not wall-clock dates. Captures and
focus changes remain confounders, as described in `frame-pacing.md`.

Status now exposes the existing `unmap_snapshot_limit` and `shadow_usage` settings. Disabling
them can test shadow work separately, but loses bytes from unmapped/GPU-only index buffers;
do not silently treat that reduced capture capability as a full probe fix.

## Verification

Native tests cover submit-time changes, fallback after an unsnapshotted unmap, unmap-before-
record, a disjoint self-copy and an unreadable 64 MiB copy with no fallback/destination storage.
Deterministic timing tests cover a slow event, rollover, a retained lifetime peak, concurrent
writers and the disabled path. Live sandbox tests exercise profiling on/off through both
entries, unchanged hashes, every timed category and status/clock fields. The staging rewrite
is needed to exercise unmap: the normal sandbox keeps its staging buffer mapped.

These checks validate semantics and diagnostic reporting. They do not establish a speed-up
in the real game, identify the original stutter source, or replace the next user-started test.
