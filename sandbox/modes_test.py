"""Checks the modes around the normal probe run.

No layer and passthrough mode must not serve a probe. A DLL that is injected after the
application has its device must leave the application alone, whether the application keeps
its old function pointers or resolves them again through the redirected exports.
"""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
import time

from harness import INJECT_DLL, Checks, Sandbox

from efmi_vk.probe_client import ProbeClient, ProbeError, list_probes  # noqa: E402  (path set up by harness)


def check_passthrough(c: Checks, entry: str) -> None:
    tag = "passthrough, %s: " % entry
    with Sandbox(threads=2, seconds=3, mode="passthrough", entry=entry) as sb:
        time.sleep(0.5)
        c.check(sb.pid not in list_probes(), tag + "no probe pipe")
        code = sb.proc.wait(timeout=30)
        c.equal(code, 0, tag + "the sandbox exits normally")
        log = sb.log()
        c.check("mode=passthrough" in log and "entry=%s" % entry in log, tag + "mode and entry logged", log)
        c.check("device created" in log, tag + "the device was seen")
        c.check("probe pipe" not in log, tag + "no pipe was started")
        destroyed = [ln for ln in log.splitlines() if "device destroyed after" in ln]
        c.check(len(destroyed) == 1, tag + "the device destruction was logged once")
        done = [ln for ln in sb.output().splitlines() if ln.startswith("done after")]
        c.check(bool(done), tag + "the sandbox reported its frame count", sb.output())
        if destroyed and done:
            frames = int(destroyed[0].split("after")[1].split()[0])
            c.equal(frames, int(done[0].split()[2]), tag + "the frame counts agree")


def check_late(c: Checks, entry: str, expect_passive: bool) -> None:
    tag = "%s: " % entry
    with Sandbox(threads=2, seconds=4, entry=entry) as sb:
        with ProbeClient(sb.pid) as probe:
            time.sleep(1.0)
            status = probe.status()
            attach = status["attach"]
            c.equal(status["device"], None, tag + "no device is tracked")
            c.equal(attach["loader_was_loaded"], True, tag + "vulkan-1.dll was already loaded")
            c.check(attach["exports_redirected"] >= 40, tag + "exports were redirected", attach)
            c.equal(attach["devices_created"], 0, tag + "no device creation was seen")
            if expect_passive:
                c.check(attach["passive_calls"] > 100, tag + "calls for the unknown device are passed on", attach)
            else:
                c.equal(attach["passive_calls"], 0, tag + "no call reaches the hooks")
            try:
                probe.capture(timeout_ms=300)
                c.check(False, tag + "a capture is refused without a device")
            except ProbeError:
                c.check(True, tag + "a capture is refused without a device")
        code = sb.proc.wait(timeout=30)
        c.check(code == 0, tag + "the sandbox runs to its end", "exit code %s\n%s" % (code, sb.output()))
        done = [ln for ln in sb.output().splitlines() if ln.startswith("done after")]
        c.check(bool(done) and int(done[0].split()[2]) > 30, tag + "frames kept being presented", sb.output())
        c.check("already loaded" in sb.log(), tag + "the log says the DLL came late")


def check_settings_file(c: Checks) -> None:
    """A copy of the DLL with efmi_vk.cfg next to it, and no environment variable for the mode."""
    folder = tempfile.mkdtemp(prefix="efvk-cfg-")
    dll = os.path.join(folder, "efmi_vk_inject.dll")
    shutil.copyfile(INJECT_DLL, dll)
    log_path = os.path.join(folder, "from-cfg.log")
    with open(os.path.join(folder, "efmi_vk.cfg"), "w", encoding="utf-8") as f:
        f.write("# test settings\nmode = passthrough   # no tracking\nlog = %s\n" % log_path)
    with Sandbox(threads=2, seconds=2, mode=None, entry="inject", inject_dll=dll, env_extra={"EFMI_VK_LOG": ""}) as sb:
        time.sleep(0.5)
        c.check(sb.pid not in list_probes(), "settings file: passthrough serves no probe pipe")
        sb.proc.wait(timeout=30)
    try:
        with open(log_path, "r", encoding="utf-8", errors="replace") as f:
            log = f.read()
    except OSError:
        log = ""
    c.check("mode=passthrough" in log, "settings file: the mode was taken from efmi_vk.cfg", log)
    c.check("entry=inject" in log, "settings file: the log path was taken from efmi_vk.cfg", log)


def main() -> int:
    c = Checks("modes_test")

    with Sandbox(threads=2, seconds=20, with_layer=False) as sb:
        time.sleep(0.5)
        c.check(sb.pid not in list_probes(), "without the layer there is no probe pipe")
        c.equal(sb.log(), "", "without the layer nothing is logged")
        c.check(sb.proc.poll() is None, "the sandbox runs without the layer", sb.output())

    check_passthrough(c, "layer")
    check_passthrough(c, "inject")
    check_late(c, "inject-late", expect_passive=False)
    check_late(c, "inject-late-exports", expect_passive=True)
    check_settings_file(c)

    return c.finish()


if __name__ == "__main__":
    sys.exit(main())
