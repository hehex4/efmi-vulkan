"""Measure host wall time with repeated draws; FIFO frame time is not CPU/GPU execution time.

    python sandbox/benchmark.py --draws 500 2000 --threads 0 2 --trials 3

No capture is requested during timed runs. Raw samples and executable fingerprints
are retained under build/tests/. Validation is deliberately separate from timing.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import platform
import statistics
from datetime import datetime, timezone

from harness import DIST, Sandbox, test_dir


def read_timings(path: str | Path) -> dict:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    samples = data["samples"]
    if len(samples) != data["requested_frames"] or not samples:
        raise ValueError("incomplete timing run")
    for row in samples:
        if len(row) != 5 or not all(math.isfinite(v) and v >= 0 for v in row):
            raise ValueError("invalid timing sample")
        if abs(sum(row[:4]) - row[4]) > 0.00001:
            raise ValueError("timing phases do not sum to total")
    metrics = dict(zip(data["columns_ms"], zip(*samples)))
    metrics["record_submit"] = [r[1] + r[2] for r in samples]
    data["statistics_ms"] = {
        name: {"median": statistics.median(values), "mean": statistics.mean(values),
               "p95": sorted(values)[math.ceil(len(values) * 0.95) - 1]}
        for name, values in metrics.items()
    }
    return data


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--draws", type=int, nargs="+", default=[500, 2000])
    ap.add_argument("--threads", type=int, nargs="+", default=[0, 2])
    ap.add_argument("--frames", type=int, default=120)
    ap.add_argument("--warmup", type=int, default=30)
    ap.add_argument("--trials", type=int, default=3)
    ap.add_argument("--entry", choices=["layer", "inject"], default="inject")
    args = ap.parse_args()
    if (args.trials < 1 or not 1 <= args.frames <= 100000 or not 0 <= args.warmup <= 100000
            or any(not 1 <= n <= 100000 for n in args.draws) or any(not 0 <= n <= 5 for n in args.threads)):
        ap.error("invalid sample count, draw count or thread count")
    dest = Path(test_dir("benchmark-"))
    report = {"utc": datetime.now(timezone.utc).isoformat(), "platform": platform.platform(),
              "entry": args.entry, "arguments": vars(args), "binary_sha256": {}, "runs": []}
    for name in ("efmi_vk_sandbox.exe", "efmi_vk_layer.dll", "efmi_vk_inject.dll"):
        report["binary_sha256"][name] = hashlib.sha256((Path(DIST) / name).read_bytes()).hexdigest()
    modes = ["none", "passthrough", "probe"]
    for draws in args.draws:
        for threads in args.threads:
            for trial in range(args.trials):
                # Rotate order so every mode occupies each position in three trials.
                for mode in modes[trial % 3:] + modes[:trial % 3]:
                    raw = dest / f"d{draws}-t{threads}-{mode}-{trial}.json"
                    with Sandbox(draws=draws, threads=threads, seconds=0, entry=args.entry,
                                 with_layer=mode != "none", mode=mode if mode != "none" else None,
                                 extra_args=["--timings", str(raw), "--warmup", str(args.warmup),
                                             "--frames", str(args.frames)]) as box:
                        if box.proc.wait(timeout=120) != 0:
                            raise RuntimeError(box.output())
                        data = read_timings(raw)
                        if mode != "none" and ("mode=" + mode) not in box.log():
                            raise RuntimeError("layer mode was not confirmed in its log")
                        run = dict(data, mode=mode, trial=trial, artifact_dir=box.dir)
                        report["runs"].append(run)
                        (dest / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
                        s = data["statistics_ms"]
                        print(f"draws={draws} threads={threads} {mode} trial={trial}: "
                              f"record+submit={s['record_submit']['median']:.3f} ms "
                              f"frame={s['total']['median']:.3f} ms", flush=True)
    print(f"Raw samples and report: {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
