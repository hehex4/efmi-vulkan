# Host present interval diagnostics (0.7.1)

`status.host_pacing` retains frame-time spikes that average present counts cannot show. It measures
host intervals between consecutive `on_present` boundaries, before forwarding `vkQueuePresentKHR`.
It includes the previous driver present wait and CPU work between boundaries. It is not a GPU
timestamp, display scan-out measurement or proof that a frame was shown. Multiple presenting
queues/swapchains on one device share the sequence. A paused/minimized application can produce
a large interval; scene and window state must be recorded alongside the result.

The first boundary initializes the clock; subsequent intervals use QPC and the existing frame
lock. Storage is a fixed 2048-sample ring (about 64 KiB per device). Each present adds one sample
without allocating, sorting, reading texture data or taking the resource lock. Status copies
the ring under the frame lock, then sorts and formats on the probe thread after releasing it.

The result has lifetime interval counts, counts over 16/33/50 ms, the lifetime peak, and a recent
window with mean, nearest-rank p50/p95/p99, maximum, threshold counts and the worst eight samples.
Every peak sample identifies its ending frame, the cumulative upload-hash timer delta during that
interval, and whether a capture was active. A capture may itself add work, so those samples are
explicit. Concurrent upload work can overlap an interval; its counter is a correlation aid, not
an exact partition of frame time. Reset/non-increasing counters cannot underflow into a fake peak.

To inspect an already running 0.7.1 probe, use `server/efvk.py status pid=<PID>` or collect a bundle;
both saved status files include this information. Read after holding the same scene/window state
for at least 20 seconds so the 2048-sample window can move past loading at 120 presents/s. Do not
request repeated captures for performance measurement. The diagnostic cannot recover timings
from an earlier process or from the current 0.7.0 session.

The prepared game fallback disables image hashing in `dist/efmi_vk.cfg`; it needs a process restart.
Compare that first, keeping resource tracking enabled. If the same scene still stutters, a separate
user-started session with `resources = 0` can isolate descriptor/resource tracking. Passthrough has
no probe pipe; a displayed-frame comparison there needs separate measurement or the user's report.
No automatic game launch, reload, mode change or system instrumentation is part of this diagnostic.

Native regression exercises an isolated 80 ms spike among 8 ms intervals, its hash/capture tags,
window rollover, lifetime retention and counter edge cases. Sandbox timing checks wire-level
counts, capture marking and live quantiles. These establish diagnostic behavior, not a fix for
the reported real-game stutter.

The first real 0.7.1 observation is recorded in `pacing-hash-off-session.md`: foreground
open-world host spikes persist with hashing off. The static display/background observations
cannot establish smooth open-world play. T-020 still needs a resources-off comparison.
