"""Interleaved A/B of a probe setting in a running game the user started: windows of A and B alternate
so the scene's drift cancels, medians over the rounds (ISSUE-012; an 8 s on/off pair was inconclusive).
Run from PowerShell (the Bash sandbox cannot open the probe pipe).

    python scripts/ab_game.py PID OUT.json [--rounds 10] [--seconds 3] [--setting m2|gpu_bones]

m2: the replacement on (A) and off (B). gpu_bones: the merged skeleton on the GPU (A) and the CPU (B)."""
import argparse
import json
import statistics
import sys
import time

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[1] / "server"))
from efmi_vk.probe_client import ProbeClient  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("pid", type=int)
ap.add_argument("out")
ap.add_argument("--rounds", type=int, default=10)
ap.add_argument("--seconds", type=float, default=3.0)
ap.add_argument("--setting", choices=("m2", "gpu_bones"), default="m2")
args = ap.parse_args()


def sample(client):
    s = client.status()
    p = s["host_pacing"]; tw = s["tracker_work"]["categories"]
    return dict(t=time.perf_counter(), intervals=p["total_intervals"], over16=p["total_over_16ms"], over33=p["total_over_33ms"],
                over50=p["total_over_50ms"], draw_us=tw["draw_record"]["microseconds"], draws=tw["draw_record"]["calls"])


def window(a, b):
    n = max(1, b["intervals"] - a["intervals"])
    return dict(mean_ms=(b["t"] - a["t"]) * 1000 / n, presents=n, over16=b["over16"] - a["over16"], over33=b["over33"] - a["over33"],
                over50=b["over50"] - a["over50"], draw_record_ms=(b["draw_us"] - a["draw_us"]) / 1000 / n, draws=(b["draws"] - a["draws"]) / n)


def apply(client, variant):
    if args.setting == "m2":
        client.request("m2", enabled=1 if variant == "A" else 0)
    else:
        client.request("set", gpu_bones=1 if variant == "A" else 0)


windows = {"A": [], "B": []}
with ProbeClient(args.pid) as client:
    for round_index in range(args.rounds):
        for variant in ("A", "B") if round_index % 2 == 0 else ("B", "A"):
            apply(client, variant)
            time.sleep(0.5)  # the switch takes effect at the next frame; settle
            a = sample(client); time.sleep(args.seconds); b = sample(client)
            windows[variant].append(window(a, b))
    apply(client, "A")
summary = {}
for variant, rows in windows.items():
    summary[variant] = {k: round(statistics.median(r[k] for r in rows), 3) for k in rows[0]} if rows else {}
    summary[variant]["windows"] = len(rows)
labels = {"m2": ("replacement on", "replacement off"), "gpu_bones": ("bones on the GPU", "bones on the CPU")}[args.setting]
print("A =", labels[0], json.dumps(summary["A"]))
print("B =", labels[1], json.dumps(summary["B"]))
print("difference A - B: mean frame %.2f ms, draw_record %.2f ms per present" % (summary["A"]["mean_ms"] - summary["B"]["mean_ms"],
                                                                                 summary["A"]["draw_record_ms"] - summary["B"]["draw_record_ms"]))
json.dump(dict(setting=args.setting, labels=labels, windows=windows, medians=summary), open(args.out, "w"), indent=1)
