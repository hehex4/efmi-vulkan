"""Core and synchronization validation, with a canary proving the error path is live."""

from harness import Checks, Sandbox
from efmi_vk.probe_client import ProbeClient


def main() -> int:
    c = Checks("Vulkan validation")
    for entry in ("none", "layer", "inject"):
        with Sandbox(with_layer=entry != "none", entry=entry, validation=True,
                     extra_args=["--validation-canary"]) as box:
            c.equal(box.proc.wait(timeout=15), 0, "validation canary exited as expected")
            c.check("VUID-VkBufferCreateInfo-size-00912" in box.output(), "known invalid call was diagnosed")
            c.check("validation canary: intercepted" in box.output(), "invalid call was aborted")
            c.check("validation: errors=1 warnings=0" in box.output(), "only the expected canary error")
    for entry in ("none", "layer", "inject"):
        for mode in ("ordinary", "template", "push", "push-template"):
            for threads in (0, 2):
                with Sandbox(with_layer=entry != "none", entry=entry, validation=True, descriptors=mode,
                             threads=threads, seconds=1) as box:
                    box.rewrite_index_buffer()
                    if entry != "none":
                        with ProbeClient(box.pid) as client:
                            c.equal(len(client.capture()["draws"]), 5, "capture with validation")
                    c.equal(box.proc.wait(timeout=20), 0, f"{entry}/{mode}/{threads}: exit")
                    c.check("validation: errors=0 warnings=0" in box.output(),
                            f"{entry}/{mode}/{threads}: no validation diagnostics", box.output()[-6000:])
    return c.finish()


if __name__ == "__main__":
    raise SystemExit(main())
