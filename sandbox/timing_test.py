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
            c.check(all(s["host_end_us"] >= s["microseconds"] > 0 for s in recent["worst"]),
                    "present peaks expose the common host clock for work correlation")
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
    for entry in ("layer", "inject"):
        for enabled in (False, True):
            with Sandbox(entry=entry, threads=2, env_extra={"EFMI_VK_PROFILE_TRACKER": "1" if enabled else "0"}) as box:
                with ProbeClient(box.pid) as client:
                    frame = client.capture()
                    # The normal sandbox keeps staging mapped. Its rewrite exercises unmap-before-copy.
                    box.rewrite_index_buffer()
                    status = client.status()
                work = status["tracker_work"]
                c.equal(work["enabled"], enabled, "tracker profiling setting is reported")
                c.equal(work["clock"], "qpc_microseconds", "work timings name their host clock")
                c.equal([d["ib"]["hash"] for d in frame["draws"]],
                        [d["hash"] for d in box.expected["draws"]], "profiling preserves captured bytes")
                totals = work["categories"]
                if enabled:
                    c.check(all(totals[k]["calls"] > 0 for k in ("buffer_unmap", "buffer_copy_record", "buffer_submit",
                                                               "submit_tracking", "command_reset", "draw_record")),
                            "every instrumented category sees sandbox work", totals)
                    c.check(totals["buffer_copy_record"]["copied_bytes"] > 0 and totals["buffer_submit"]["copied_bytes"] > 0,
                            "buffer copy bytes are accounted separately")
                else:
                    c.check(all(all(value == 0 for value in row.values()) for row in totals.values()),
                            "profiling off leaves all work counters at zero")
                c.check(len(work["recent_slow"]) <= work["slow_capacity"] == 64 and
                        all(event["microseconds"] >= work["slow_threshold_us"] for event in work["recent_slow"]),
                        "only slow events enter the bounded history")
                c.equal(status["unmap_snapshot_limit"], 16 << 20, "unmap snapshot bound is visible in status")
                c.equal(status["shadow_usage"], 0x40, "buffer shadow usage mask is visible in status")
    return c.finish()


if __name__ == "__main__":
    raise SystemExit(main())
