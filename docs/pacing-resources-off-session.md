# Resource-off game session: stutter persists

2026-10-12, user-started Endfield PID 33204, started at 12:50:29 (Asia/Shanghai).
The log and live status confirm layer 0.7.1, probe/inject, resources off and image hashing off.
The user reports that the restarted game still stutters. Tracked images, descriptors and retained
shaders are zero; upload hash counters and capture sequence remain zero throughout measurement.
Buffer shadowing, draw recording, call counters and frame assembly still run in this mode.

Evidence is under `captures/pacing-resources-off-20261012/` (ignored by Git). The root manifest
covers 26 files, including the raw readings, nested manifests, process log and derived analysis.
No capture, game input, focus change, launch, stop or launcher edit was performed by the agent.

## Foreground observation

The first 40-second observation (`world/`) has only 16/400 foreground polls. The second
(`foreground/`) has 372/400, including a brief focus change in its second five-second segment.
Neither whole observation is a clean foreground comparison. Preserve both, but select only
`foreground/status-02.json` through `status-08.json`: 12:57:21.745 to 12:57:51.745 local,
all 300/300 foreground polls and all seven status endpoints foreground. This continues the
requested open-world movement comparison; movement itself is not measured.

| Measurement between the selected reads | Result |
| --- | --- |
| Elapsed observation | 30.000261 s |
| Host present intervals | 6,766 |
| Over 16 ms | 276 |
| Over 33 ms | 74 |
| Over 50 ms | 26 |
| Largest retained interval inside the observation | 105.000 ms, ending at frame 53703 |
| Hash time and capture intervals | 0 |

The selected interval ends at frames 47776 through 54541. Overlapping rolling windows cover
that range. The final 2048-entry window has p50 2.887 ms, p95 13.361 ms, p99 36.888 ms and
maximum 105.000 ms. These quantiles describe only the final window. Peaks of 165.448 ms in
the earlier readings are outside the selected observation and are not counted as its result.
Status-query round trips reach 20.459 ms, including pipe/lock waiting; this is not measured
time blocking the render thread. Status reads themselves remain a possible perturbation.

## What this establishes

Large host interval spikes and user-visible stutter persist with resource/descriptor tracking
and image hashing disabled. Those disabled paths cannot be the sole explanation. This does
not clear the remaining probe work, injection, game work, driver, presentation or scheduling.
Host intervals are not GPU/display timestamps. The scene route and workload were not controlled
across runs, so larger threshold counts than the earlier resource-on session do not establish
that turning resources off made performance worse. T-020 / ISSUE-005 remains open.

## Next-launch preparation

After measurement, backed up `dist/efmi_vk.cfg` to
`build/backups/before-passthrough-20261012-125909/` and changed only the active mode from probe
to passthrough. Hashing and resources remain off. PID 33204 retains probe mode until it exits;
there is no hot reload. The DLL is unchanged, SHA-256
`729d914e1f4a1ac66ae26ee2d421707d898b7426e52c756a5997cec50dfe0b7e` for both entries.

An actual-config inject smoke test, without mode/hash/resource environment overrides, passes:
`build/tests/sandbox-m_8zuprp/passthrough-result.json`. It loaded 0.7.1 in passthrough,
served no probe pipe, presented 546 frames and exited normally; its present and destruction
counts agree. Test logs stay in the test directory. No layer source changed or rebuild was needed.

The next user-started same-world session tests the remaining tracking as a group. Passthrough
removes draw/buffer/resource hooks and the probe service; instance/device setup, present-boundary
accounting and DLL injection remain. It is not a no-DLL baseline. With no pipe, use the user's
movement/smoothness report; do not claim a new host interval trace or automatic performance fix.
If passthrough also stutters, a user-started official-launcher run without this DLL would separate
the remaining injection/present path from the rest of the Vulkan stack. Nothing changes those
launcher settings automatically.

## Passthrough game follow-up

The user restarted again (PID 14804, 13:04:17 local) and reports that stutter is gone. The log
confirms 0.7.1 passthrough/inject at 13:04:19.597 and Vulkan device creation; only seven exports
are redirected and no probe pipe exists. Evidence: `captures/pacing-passthrough-20261012/`.
This is the user's movement/smoothness report, without a host or displayed-frame timing trace.
It implicates the work added by probe mode as a group, not an individual routine. Keep the
current game configuration while investigating buffer snapshots, copy work and draw recording
offline. A later tested probe candidate still needs its own game comparison.
