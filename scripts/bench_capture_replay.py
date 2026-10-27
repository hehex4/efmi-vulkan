"""Interleaved offline A/B using already generated capture replays, with startup separated.

Build replay_gpu.exe/replay.bin with replay_capture.py first. No game or launcher
access. Each run gets its own empty layer pipeline cache and project-owned settings.
Driver/OS caches may remain warm. Validation is disabled only for these timing runs.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import time

import replay_capture as replay


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--scene", action="append", required=True)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--frames", type=int, default=2600)
    parser.add_argument("--baseline-env", action="append", default=[], help="KEY=VALUE for the baseline's runs only")
    parser.add_argument("--candidate-env", action="append", default=[], help="KEY=VALUE for the candidate's runs only")
    args = parser.parse_args()
    if args.rounds < 3 or args.frames < 2400:
        parser.error("require at least three pairs and 2400 frames (warmup plus the full 2048-interval window)")
    validation = Path(os.environ.get("EFMI_VK_VALIDATION_DIR", "F:/vulkan/validation-1.4.321.0/bin"))
    out = replay.ROOT / "build/performance-ab" / time.strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True)
    os.environ.update(EFMI_VK_REPLAY_NO_VALIDATION="1", EFMI_VK_REPLAY_TIMING="1")
    builds = {"baseline": args.baseline.resolve(), "candidate": args.candidate.resolve()}
    report = {"scope": "sandbox capture replay, no game FPS acceptance", "validation": False,
              "frames": args.frames, "rounds": args.rounds, "builds": {}, "runs": [], "summary": {}}
    for name, dist in builds.items():
        report["builds"][name] = {"path": str(dist), "sha256": hashlib.sha256((dist / "efmi_vk_layer.dll").read_bytes()).hexdigest()}

    def total_stages(r: dict) -> dict:
        values = {}
        for package in r.get("packages", [r]):
            for key, value in package.get("stage_us", {}).items():
                values[key] = values.get(key, 0) + value
        return values

    for scene in args.scene:
        source = replay.ROOT / "build/replay" / scene
        exe = source / "replay_gpu.exe"
        report.setdefault("replays", {})[scene] = {file: hashlib.sha256((source / file).read_bytes()).hexdigest()
                                                   for file in ("replay_gpu.exe", "replay.bin")}
        for pair in range(args.rounds):
            for name in (("baseline", "candidate") if pair % 2 == 0 else ("candidate", "baseline")):
                folder = out / scene / (str(pair) + "-" + name)
                folder.mkdir(parents=True)
                shutil.copy2(source / "replay.bin", folder / "replay.bin")
                # A setting of one build's runs (the same binaries with a switch off and on).
                overrides = dict(item.split("=", 1) for item in (args.baseline_env if name == "baseline" else args.candidate_env))
                saved = {k: os.environ.get(k) for k in overrides}
                os.environ.update(overrides)
                try:
                    result = replay.run(exe, folder, builds[name], args.frames, True, validation)
                finally:
                    for k, v in saved.items():
                        if v is None: os.environ.pop(k, None)
                        else: os.environ[k] = v
                if result["returncode"] or " 0 failed;" not in result["output"]:
                    raise RuntimeError("replay failed: " + result["output"][-2000:])
                final = result["status"]["result"]
                report.setdefault("device", final.get("device", {}))
                warm = json.loads((result["folder"] / "warm-status.json").read_text())["result"]
                r, w = final["replacement"], warm["replacement"]
                frames = final["frame"] - warm["frame"]
                recent = final["host_pacing"]["recent"]
                assert recent["first_frame"] > warm["frame"], "startup in steady-state timing window"
                assert frames > 0 and r["replaced"] > w["replaced"], "no steady-state replacement"
                stage, previous = total_stages(r), total_stages(w)
                metrics = {"host_" + p: recent[p + "_us"] for p in ("p50", "p95", "p99")}
                metrics["record_us_per_frame"] = (r["record"]["microseconds"] - w["record"]["microseconds"]) / frames
                if "frame_microseconds" in r["record"] and "frame_microseconds" in w["record"]:
                    # The presentation's work (character_frame): every package's Present, or the folder's once (D-131).
                    metrics["present_us_per_frame"] = (r["record"]["frame_microseconds"] - w["record"]["frame_microseconds"]) / frames
                metrics.update({key + "_us_per_frame": (value - previous.get(key, 0)) / frames for key, value in stage.items()})
                metrics["startup_texture_wait_us"] = w["textures"]["draw_wait_microseconds"]
                metrics["startup_variant_wait_us"] = w["record"]["variant_wait_microseconds"]
                metrics["steady_texture_wait_us"] = r["textures"]["draw_wait_microseconds"] - w["textures"]["draw_wait_microseconds"]
                metrics["steady_variant_wait_us"] = r["record"]["variant_wait_microseconds"] - w["record"]["variant_wait_microseconds"]
                row = {"scene": scene, "pair": pair, "build": name, "version": final["version"], "metrics": metrics,
                       "warmup_frames": warm["frame"], "steady_frames": frames, "host_window": recent,
                       "startup_peak": warm["host_pacing"]["lifetime_peak"],
                       "resident_bytes": r["textures"]["resident_bytes"], "loads": r["textures"]["loads"],
                       "replaced_per_steady_frame": (r["replaced"] - w["replaced"]) / frames,
                       "targets": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in result["folder"].glob("target-*.raw")}}
                report["runs"].append(row)
                (out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
                print(scene, pair, name, json.dumps(metrics), flush=True)
        rows = [row for row in report["runs"] if row["scene"] == scene]
        summary = {}
        for metric in rows[0]["metrics"]:
            med = {name: statistics.median(row["metrics"][metric] for row in rows if row["build"] == name) for name in builds}
            summary[metric] = {**med, "ratio": med["candidate"] / med["baseline"] if med["baseline"] else None}
        summary["equal_final_targets"] = all(row["targets"] == rows[0]["targets"] for row in rows)
        summary["equal_steady_replacement"] = all(row["replaced_per_steady_frame"] == rows[0]["replaced_per_steady_frame"] for row in rows)
        summary["equal_residency"] = all((row["resident_bytes"], row["loads"]) == (rows[0]["resident_bytes"], rows[0]["loads"]) for row in rows)
        report["summary"][scene] = summary
        (out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    print("Offline A/B report:", out / "report.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
