# Hash-off game session: open-world spikes confirmed

2026-10-12, user-started Endfield PID 31124, layer 0.7.1, probe/inject, resource tracking on,
image hashing off. The log confirms loading at 12:32:24 (Asia/Shanghai). No capture was requested
in this session; capture sequence remains zero and all image hashing counters remain zero.
The layer, configuration and launcher were not changed during these observations.

Evidence is under `captures/pacing-hash-off-20261012/` (ignored by Git), with file fingerprints
in the manifests. `analysis.json` records the primary interval's calculations. All times below
are Asia/Shanghai; raw observation timestamps are UTC.

## Initial display observations are not the open-world result

At 12:35:37 the latest 2048 intervals had p50 8.330 ms, p99 10.410 ms, maximum 13.076 ms and
none over 16 ms. At 12:35:57 the recent maximum was 21.729 ms, p99 10.032 ms and four intervals
were over 16 ms, none over 33 ms. The game was visible but not foreground at both reads.

The user clarified that the mostly static character display cannot reveal the problem, while
the open world stutters severely. These readings cannot establish improved playability.
The session lifetime peak, 3362.513 ms at frame 427, occurred much earlier and is not a peak
of the sampled steady interval.

## Primary open-world observation

After the user confirmed the open world, allowed five seconds to return to the game, then read
status every five seconds for about 40 seconds. From 12:38:46 to 12:39:26, all 400 foreground
checks and all nine status endpoints identified the game as foreground. No input was sent.

| Measurement between first and final reads | Result |
| --- | --- |
| Elapsed observation | 39.992 s |
| Host present intervals | 11,681 |
| Over 16 ms | 142 |
| Over 33 ms | 27 |
| Over 50 ms | 12 |
| Largest retained interval inside the observation | 111.204 ms, ending at frame 69089 |
| Hash time for that interval | 0 |
| Capture intervals | 0 |

Rolling windows overlap and cover the full observed frame range, 59151 through 70831; their
peaks cover that interval. The final 2048-sample window has p50 2.989 ms, p95 7.235 ms, p99
24.590 ms and maximum 111.204 ms. These quantiles belong to that window, not all 40 seconds.
Quiet windows alternate with windows containing frequent spikes, consistent with the report.

Status-query round trips were at most 9.064 ms, including pipe/tracker-lock waiting. This is not
time spent holding the rendering thread. Status reads can perturb timing; the absence of frame
captures alone does not exclude that effect.

## Supplementary quiet interval has a focus confound

A separate 20.080-second interval used only start/end reads (`world-quiet/`). It contains
46 intervals over 33 ms and 13 over 50 ms; the final window maximum is 141.033 ms. However,
only 78 of 200 foreground polls found the game active (first read background, last foreground).
Do not use it as a clean comparison with the primary foreground interval or attribute individual
peaks to a window state. Both query round trips were below 0.54 ms; there were no intermediate
probe requests or captures.

## Conclusion and next isolation step

Large host-side present-interval spikes persist with hashing disabled. The disabled upload-hash
routine is not their cause in this session. Full resource/descriptor tracking, game work, driver,
presentation and external scheduling remain unisolated. Host intervals are not GPU/display
frame times; do not convert their average directly into displayed FPS.

T-020 remains open. Compare the same open-world movement with `resources = 0`, keeping image
hashing off, after a user-chosen process restart. Current cfg still enables resources; no setting
was silently changed. If spikes persist, passthrough/no-probe display timing is the subsequent
isolation step. No rendering or performance fix has been verified.
