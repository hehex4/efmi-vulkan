"""Bounds on what the probe keeps, and the reduced tracking mode.

A real process has far more shader modules, descriptor sets and draws than the sandbox.
These runs check the settings that keep the probe's memory and work bounded there:
the descriptor budget of a capture, the SPIR-V budget, and resource tracking switched off.
"""

from __future__ import annotations

import sys

from harness import Checks, Sandbox

from efmi_vk.probe_client import ProbeClient, ProbeError  # noqa: E402  (path set up by harness)

OMITTED = {"tracked": False, "reason": "capture_descriptor_budget"}


def check_hashes(c: Checks, frame: dict, expected: dict, tag: str) -> None:
    draws = [d for d in frame["draws"] if d["kind"] == "indexed"]
    c.equal(len(draws), len(expected["draws"]), tag + "every draw is captured")
    c.equal([d["ib"]["hash"] for d in draws], [w["hash"] for w in expected["draws"]], tag + "region hashes are intact")


def descriptor_budget(c: Checks, entry: str) -> None:
    tag = "descriptor budget, %s: " % entry
    with Sandbox(threads=2, entry=entry) as sb, ProbeClient(sb.pid) as probe:
        full = probe.capture()
        check_hashes(c, full, sb.expected, tag)
        c.equal(full["descriptor_budget"], 64 << 20, tag + "default budget is 64 MiB")
        c.equal(full["descriptors_omitted"], 0, tag + "nothing omitted by default")
        sizes = sum(len(str(s["contents"])) for d in full["draws"] for s in d["sets"])
        c.check(full["descriptor_bytes"] > 0 and sizes > 0, tag + "kept bytes are reported", full["descriptor_bytes"])
        c.check(any(s["contents"].get("tracked") for d in full["draws"] for s in d["sets"]), tag + "contents are present")

        none = probe.capture(descriptor_budget=0)
        check_hashes(c, none, sb.expected, tag + "budget 0: ")
        c.equal(none["descriptor_bytes"], 0, tag + "budget 0 keeps no contents")
        c.equal(none["descriptors_omitted"], 4 * len(none["draws"]), tag + "budget 0 omits every set of every draw")
        c.check(all(s["contents"] == OMITTED for d in none["draws"] for s in d["sets"]), tag + "omissions carry the reason")
        c.check(all(d["sets"][0]["dyn"] for d in none["draws"]), tag + "dynamic offsets survive without contents")

        # A budget smaller than one frame: the first sets are kept, the rest are omitted and counted.
        part = probe.capture(descriptor_budget=1)
        kept = [s["contents"] for d in part["draws"] for s in d["sets"] if s["contents"] != OMITTED]
        c.equal(len(kept), 1, tag + "a 1-byte budget keeps exactly the first set")
        c.equal(part["descriptors_omitted"], 4 * len(part["draws"]) - 1, tag + "the rest is counted as omitted")

        again = probe.capture()
        c.equal(again["descriptors_omitted"], 0, tag + "the budget of one capture does not carry over")


def shader_budget(c: Checks, entry: str) -> None:
    tag = "shader budget, %s: " % entry
    with Sandbox(threads=0, entry=entry) as sb, ProbeClient(sb.pid) as probe:
        status = probe.status()
        counts = status["counts"]
        c.equal(status["shader_budget"], 512 << 20, tag + "default budget is 512 MiB")
        c.check(counts["retained_shader_bytes"] > 0, tag + "kept SPIR-V bytes are reported", counts)
        c.equal(counts["shaders_not_retained"], 0, tag + "nothing dropped by default")
        frame = probe.capture()
        pipe = probe.pipeline(frame["draws"][0]["pipeline"])
        c.check(all(s["code_retained"] for s in pipe["shaders"]), tag + "code is kept by default")
        kept_sizes = sorted(s["size"] for s in pipe["shaders"])
        kept_hashes = sorted(s["hash"] for s in pipe["shaders"])

    with Sandbox(threads=0, entry=entry, env_extra={"EFMI_VK_SHADER_BUDGET": "0"}) as sb, ProbeClient(sb.pid) as probe:
        status = probe.status()
        counts = status["counts"]
        c.equal(status["shader_budget"], 0, tag + "the setting is reported")
        c.equal(counts["retained_shader_bytes"], 0, tag + "budget 0 keeps no code")
        c.check(counts["shaders_not_retained"] >= 2, tag + "dropped modules are counted", counts)
        frame = probe.capture()
        check_hashes(c, frame, sb.expected, tag + "budget 0: ")
        pipe = probe.pipeline(frame["draws"][0]["pipeline"])
        c.check(not any(s["code_retained"] for s in pipe["shaders"]), tag + "stages say the code was not kept")
        c.equal(sorted(s["size"] for s in pipe["shaders"]), kept_sizes, tag + "sizes are still known")
        c.equal(sorted(s["hash"] for s in pipe["shaders"]), kept_hashes, tag + "hashes are still known")
        meta, data = probe.shader(pipe["shaders"][0]["id"], code=False)
        c.check(meta["code_retained"] is False and not data, tag + "metadata is available without code")
        try:
            probe.shader(pipe["shaders"][0]["id"])
            c.check(False, tag + "asking for dropped code is an error")
        except ProbeError as e:
            c.check("shader_budget" in str(e), tag + "asking for dropped code is an error", e)

    # A budget between the two modules: the first one created is kept, the second is not.
    middle = str(max(kept_sizes))
    with Sandbox(threads=0, entry=entry, env_extra={"EFMI_VK_SHADER_BUDGET": middle}) as sb, ProbeClient(sb.pid) as probe:
        counts = probe.status()["counts"]
        c.check(0 < counts["retained_shader_bytes"] <= int(middle), tag + "the total stays within the budget", counts)
        c.check(counts["shaders_not_retained"] >= 1, tag + "the module over the budget is dropped", counts)


def resources_off(c: Checks, entry: str) -> None:
    tag = "resource tracking off, %s: " % entry
    with Sandbox(threads=2, entry=entry, env_extra={"EFMI_VK_RESOURCES": "0"}) as sb, ProbeClient(sb.pid) as probe:
        status = probe.status()
        c.equal(status["resources"], False, tag + "the setting is reported")
        frame = probe.capture()
        check_hashes(c, frame, sb.expected, tag)
        counts = probe.status()["counts"]
        for key in ("shader_modules", "retained_shaders", "images", "image_views", "samplers", "descriptor_sets",
                    "descriptor_set_layouts", "pipeline_layouts", "retained_shader_bytes"):
            c.equal(counts[key], 0, tag + "nothing kept for " + key)
        c.check(counts["buffers"] > 0 and counts["pipelines"] > 0, tag + "buffers and pipelines are still tracked", counts)
        contents = [s["contents"] for d in frame["draws"] for s in d["sets"] if s["set"] != "0x0"]
        c.check(bool(contents) and all(x == {**x, "tracked": False, "reason": "resource_tracking_off"} for x in contents),
                tag + "bound sets say why they have no contents", contents[:1])
        c.check(all(d["sets"][0]["set"] != "0x0" for d in frame["draws"]), tag + "set handles are still recorded")
        c.check(all(d["sets"][0]["dyn"] for d in frame["draws"]), tag + "dynamic offsets are still recorded")
        usage = probe.api_usage()["calls"]
        c.check(usage.get("vkUpdateDescriptorSets", 0) >= 1 and usage.get("vkCreateShaderModule", 0) >= 2,
                tag + "the calls are still counted", usage)
        pipe = probe.pipeline(frame["draws"][0]["pipeline"])
        c.equal(pipe["bindings"][0]["stride"], 8, tag + "vertex layout is still known")
        c.check(sb.proc.poll() is None, tag + "the sandbox keeps running", sb.output())


def main() -> int:
    c = Checks("limits_test")
    for entry in ("layer", "inject"):
        descriptor_budget(c, entry)
        shader_budget(c, entry)
        resources_off(c, entry)
    return c.finish()


if __name__ == "__main__":
    sys.exit(main())
