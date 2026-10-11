"""Checks the two cases in which no probe must appear: no layer, and passthrough mode."""

from __future__ import annotations

import sys
import time

from harness import Checks, Sandbox

from efmi_vk.probe_client import list_probes  # noqa: E402  (path set up by harness)


def main() -> int:
    c = Checks("modes_test")

    with Sandbox(threads=2, seconds=20, with_layer=False) as sb:
        time.sleep(0.5)
        c.check(sb.pid not in list_probes(), "without the layer there is no probe pipe")
        c.equal(sb.log(), "", "without the layer nothing is logged")
        c.check(sb.proc.poll() is None, "the sandbox runs without the layer", sb.output())

    with Sandbox(threads=2, seconds=3, mode="passthrough") as sb:
        time.sleep(0.5)
        c.check(sb.pid not in list_probes(), "passthrough mode serves no probe pipe")
        code = sb.proc.wait(timeout=30)
        c.equal(code, 0, "the sandbox exits normally in passthrough mode")
        log = sb.log()
        c.check("mode=passthrough" in log, "the layer logged passthrough mode", log)
        c.check("device created" in log, "the layer saw the device")
        c.check("probe pipe" not in log, "no pipe was started")
        destroyed = [ln for ln in log.splitlines() if "device destroyed after" in ln]
        c.check(len(destroyed) == 1, "the device destruction was logged once")
        if destroyed:
            frames = int(destroyed[0].split("after")[1].split()[0])
            done = [ln for ln in sb.output().splitlines() if ln.startswith("done after")]
            c.check(bool(done), "the sandbox reported its frame count", sb.output())
            if done:
                c.equal(frames, int(done[0].split()[2]), "the layer counted the frames the sandbox presented")

    return c.finish()


if __name__ == "__main__":
    sys.exit(main())
