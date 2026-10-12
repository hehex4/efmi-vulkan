"""Check repeated-mesh draw identity and the timing output through real sandbox runs."""

import os

from benchmark import read_timings
from harness import Checks, Sandbox
from efmi_vk.probe_client import ProbeClient


def main() -> int:
    c = Checks("many-draw timing")
    for threads in (0, 2):
        with Sandbox(threads=threads, draws=37, extra_args=["--timings", "timing.json", "--warmup", "2",
                                                           "--frames", "60"]) as box:
            with ProbeClient(box.pid) as client:
                frame = client.capture()
                status = client.status()
            pacing = status["host_pacing"]
            c.equal(pacing["measurement"], "host_present_interval", "host timing is labelled separately from display timing")
            c.equal(pacing["total_intervals"], status["frame"] - 1, "every interval after the first present is measured")
            c.equal(pacing["window_intervals"], min(pacing["total_intervals"], pacing["window_capacity"]), "bounded timing window")
            c.check(pacing["total_capture_intervals"] >= 1, "capture overhead is marked in the interval history")
            recent = pacing["recent"]
            c.check(0 < recent["min_us"] <= recent["p50_us"] <= recent["p95_us"] <= recent["p99_us"] <= recent["max_us"],
                    "live timing quantiles are ordered")
            draws = frame["draws"]
            c.equal(len(draws), 37, "all repeated meshes captured")
            for got, want in zip(draws, box.expected["draws"]):
                c.equal((got["first"], got["count"], got["ib"]["hash"], got["sets"][0]["dyn"]),
                        (want["first"], want["count"], want["hash"], [want["dyn"]]), "repeated draw identity")
            c.equal(box.proc.wait(timeout=30), 0, "timing run completed")
            timing = read_timings(os.path.join(box.dir, "timing.json"))
            c.equal(len(timing["samples"]), 60, "exact measured frame count")
            c.equal((timing["draws"], timing["threads"], timing["warmup"]), (37, threads, 2), "timing metadata")
            c.check("after 63 frames" in box.output(), "initial frame and warmup excluded")
    return c.finish()


if __name__ == "__main__":
    raise SystemExit(main())
