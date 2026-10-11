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
