# Probe overhead in the sandbox

Measured on 2026-10-11 after sandbox validation fixes, layer 0.5.0, NVIDIA GeForce RTX 5060 Laptop GPU.
This is synthetic sandbox evidence, not a game performance result or an M2 acceptance result.

## Reproduce

```powershell
$env:EFMI_VK_DIST = 'F:/efmi vulkan/build/next'
& 'D:/Python312/python.exe' -B sandbox/benchmark.py --draws 500 2000 --threads 0 2 --trials 3
```

The sandbox cycles five meshes and their descriptor ranges without increasing resource sizes.
The default entry is injection, like the intended game entry. Each configuration runs without
the DLL, in passthrough mode and in probe mode. The runner rotates their order over three trials.
Each trial excludes the initial readiness frame and 30 warmup frames, then measures 120 frames.
There are no capture requests or validation layers during measurement. Child environments disable
implicit layers and remove inherited explicit-layer activation. No global settings change.

QPC measures host wall time for waiting/acquiring, recording, submitting and presenting separately.
Recording includes command-buffer reset and, with secondary buffers, worker creation/join.
Submission includes time inside the driver; these are not CPU cycle counts or GPU timestamps.
FIFO presentation is retained. A near-constant whole-frame time therefore does not prove zero
probe overhead. Raw samples, p95/mean/median statistics, device/driver numbers, settings, binary
SHA-256 values and per-run artifact locations are in the generated JSON report.

## Result

Median of each configuration's three trial medians, in milliseconds for record plus submit:

| Draws | Recording threads | No layer | Passthrough | Probe | Probe minus no layer |
| --- | --- | --- | --- | --- | --- |
| 500 | 0 (primary) | 0.113 | 0.107 | 0.256 | 0.143 |
| 500 | 2 (secondary) | 0.631 | 0.600 | 0.705 | 0.074 |
| 2000 | 0 (primary) | 0.201 | 0.222 | 0.587 | 0.386 |
| 2000 | 2 (secondary) | 0.613 | 0.682 | 1.357 | 0.744 |

Whole-frame medians are approximately 5.52 to 5.58 ms, limited by FIFO. At 2000 draws, per-trial
record-plus-submit medians span 0.196-0.223 ms without the layer and 0.586-0.598 ms with the probe
on the primary path; corresponding secondary values are 0.596-0.617 and 1.227-1.365 ms.
Passthrough differences vary with scheduling and driver noise; this does not establish the plan's
5% CPU frame-time target. Resource creation, large descriptor banks, capture serialization,
shader export, real scene transitions and long-duration behavior are outside this experiment.

Evidence: `build/tests/benchmark-_junbmel/report.json` (36 successful runs).
Layer SHA-256: `c9a8b41db3234ecebce64300d0c1dd34de259b6ebd23827516001df4c89a8005`.
Sandbox SHA-256: `0d97c52094b83e28ed1b9d00887d6240aef111dc1efbccb649b329088ffe81e5`.
The earlier `build/tests/benchmark-owy1jn92/` numbers predate sandbox validation fixes and are superseded.

The regression suite includes a 37-draw case for primary and secondary recording. It checks
draw identity and hashes across repeated meshes, exact timing sample counts and warmup exclusion.
Performance numbers are reported as observations; the suite has no hardware-dependent speed threshold.
