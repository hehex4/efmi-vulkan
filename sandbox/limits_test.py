"""Bounds on what the probe keeps, and the reduced tracking mode.

A real process has far more shader modules, descriptor sets and draws than the sandbox.
These runs check the settings that keep the probe's memory and work bounded there:
the descriptor budget of a capture, the SPIR-V budget, and resource tracking switched off.
"""

from __future__ import annotations

import sys
import time

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
        snap = probe.capture(snapshot={"all": True})
        c.equal(snap["snapshot"]["draws"], 5, tag + "draws are still selected for a snapshot")
        c.check(all(d["snapshot"] == [] for d in snap["draws"]), tag + "without descriptor contents a snapshot has no ranges")
        c.check(sb.proc.poll() is None, tag + "the sandbox keeps running", sb.output())


def snapshot_bounds(c: Checks, entry: str) -> None:
    tag = "snapshot bounds, %s: " % entry
    with Sandbox(threads=2, entry=entry) as sb, ProbeClient(sb.pid) as probe:
        uni = sb.expected["uniform"]
        follow = {"set": 0, "binding": 0, "offset": uni["follow_at"], "count": 2, "stride": uni["follow_stride"],
                  "bytes": uni["follow_bytes"]}

        # Room for one uniform range and nothing else.
        small = probe.capture(snapshot={"all": True, "budget": uni["range"], "follow": follow})
        check_hashes(c, small, sb.expected, tag)
        info = small["snapshot"]
        items = [s for d in small["draws"] for s in d["snapshot"]]
        kept = [s for s in items if s["id"] is not None]
        dropped = [s for s in items if s["id"] is None]
        c.equal((info["budget"], info["bytes"], info["blobs"]), (uni["range"], uni["range"], 1), tag + "the budget bounds what is kept")
        c.equal((len(kept), len(dropped), info["omitted"]), (1, len(items) - 1, len(items) - 1), tag + "the rest is counted as omitted")
        c.check(all(s["crc"] and s["record"]["state"] in ("equal", "different") for s in dropped),
                tag + "a range past the budget is still checksummed and compared", dropped[:1])
        try:
            probe.snapshot(small["seq"], 1)
            c.check(False, tag + "an omitted range has no bytes to fetch")
        except ProbeError:
            c.check(True, tag + "an omitted range has no bytes to fetch")

        # A uniform range above the size limit is listed, not read; the values in it are still followed.
        capped = probe.capture(snapshot={"all": True, "max_range": uni["range"] - 1, "follow": follow})
        firsts = [d["snapshot"][0] for d in capped["draws"]]
        c.check(all(s["kind"] == "uniform" and s.get("skipped") == "larger_than_max_range" and "id" not in s for s in firsts),
                tag + "a range above max_range is skipped with a reason", firsts[:1])
        c.check(all([s["kind"] for s in d["snapshot"][1:]] == ["follow"] * 4 for d in capped["draws"]),
                tag + "the follow values are read independently of the range")
        c.equal(capped["snapshot"]["record"]["different"], 0, tag + "a skipped range is not part of the comparison")

        # A window that fits no storage range of the draw.
        wide = probe.capture(snapshot={"all": True, "follow": dict(follow, bytes=1 << 16)})
        c.check(all([(s["kind"], s.get("skipped"), s["value"]) for s in d["snapshot"][1:]] ==
                    [("follow", "no_storage_range_fits", v) for v in uni["follow_values"]] for d in wide["draws"]),
                tag + "a window that fits nowhere is reported with its value", wide["draws"][0]["snapshot"][1:])

        big = probe.capture(draws=False, snapshot={"all": True, "budget": 1 << 40, "max_range": 1 << 40})
        c.equal((big["snapshot"]["budget"], big["snapshot"]["max_range"]), (256 << 20, 1 << 20), tag + "requested sizes are clamped")

        # Sent raw: the client refuses malformed hashes itself, the layer has to cope with any text.
        nothing = probe.request("capture", timeout_ms=5000, snapshot=1, snapshot_hashes="00000001,not-a-hash,,123456789")[0]
        c.equal((nothing["snapshot"]["hashes"], nothing["snapshot"]["draws"]), (1, 0),
                tag + "malformed list items are dropped and an unknown hash selects no draw")
        c.check(not any("snapshot" in d for d in nothing["draws"]), tag + "unselected draws carry no snapshot")
        c.check(sb.proc.poll() is None, tag + "the sandbox keeps running", sb.output())


def texture_of(frame: dict) -> dict:
    return frame["draws"][0]["sets"][1]["contents"]["bindings"][1]["elements"][0]


def image_hash_settings(c: Checks, entry: str) -> None:
    tag = "image hash, %s: " % entry
    with Sandbox(threads=0, entry=entry, env_extra={"EFMI_VK_IMAGE_HASH": "0"}) as sb, ProbeClient(sb.pid) as probe:
        frame = probe.capture()
        check_hashes(c, frame, sb.expected, tag)
        c.equal(texture_of(frame)["content"], {"state": "none", "bytes": 0, "uploads": 0}, tag + "switched off, nothing is read")
        content = probe.status()["counts"]["image_content"]
        c.equal((content["enabled"], content["hashed"], content["partial"], content["bytes"]), (False, 0, 0, 0),
                tag + "the setting and empty counters are reported")
        c.equal(probe.api_usage()["calls"].get("vkCmdCopyBufferToImage"), 3, tag + "the calls are still counted")
    limit = str(sb.expected["texture"]["bytes"] - 1)
    with Sandbox(threads=0, entry=entry, env_extra={"EFMI_VK_IMAGE_HASH_LIMIT": limit}) as sb, ProbeClient(sb.pid) as probe:
        frame = probe.capture()
        want = sb.expected["texture"]
        c.equal(texture_of(frame)["content"], {"state": "too_large", "bytes": want["bytes"], "uploads": want["uploads"]},
                tag + "an upload above the limit is counted and not hashed")
        content = probe.status()["counts"]["image_content"]
        c.equal((content["hashed"], content["too_large"], content["partial"], content["bytes"]), (0, 2, 1, 0),
                tag + "outcomes with a limit below the texture")
        c.check(sb.proc.poll() is None, tag + "the sandbox keeps running", sb.output())


def unmap_settings(c: Checks, entry: str) -> None:
    tag = "unmap settings, %s: " % entry
    with Sandbox(threads=0, entry=entry) as sb, ProbeClient(sb.pid) as probe:
        status = probe.status()
        c.equal((status["unmap_snapshot_all"], status["unmap_snapshot_limit"]), (True, 16 << 20),
                tag + "defaults: every transfer source up to 16 MiB")
        c.check("set" in status["features"], tag + "the set command is listed")
        c.equal(probe.set(), {"unmap_snapshot_all": True, "unmap_snapshot_limit": 16 << 20}, tag + "an empty request changes nothing")
        c.equal(probe.set(unmap_snapshot_all=False), {"unmap_snapshot_all": False, "unmap_snapshot_limit": 16 << 20},
                tag + "one setting changes, the other stays")
        c.equal(probe.set(unmap_snapshot_limit=1 << 40)["unmap_snapshot_limit"], 256 << 20, tag + "a requested limit is clamped")
        c.equal(probe.status()["unmap_snapshot_all"], False, tag + "the status shows the running value")
        c.equal(probe.set(unmap_snapshot_all=True)["unmap_snapshot_all"], True, tag + "and it can be switched back")
        # Every copy at unmap off. The staging buffer of the rewrite is unmapped before its copy is
        # recorded, so the bytes are gone and the layer has to say so instead of serving older ones.
        c.equal(probe.set(unmap_snapshot_all=False, unmap_snapshot_limit=0), {"unmap_snapshot_all": False, "unmap_snapshot_limit": 0},
                tag + "both settings in one request")
        before = probe.capture()
        sb.rewrite_index_buffer()
        time.sleep(0.1)
        frame = probe.capture()
        rewritten = sb.expected["rewrite_draw"]
        hashes = [d["ib"]["hash"] for d in frame["draws"]]
        c.equal(hashes[rewritten], None, tag + "the rewritten range has no hash without the staging copy")
        c.equal(hashes[3:], [d["hash"] for d in sb.expected["draws"][3:]], tag + "draws from the other index buffer are unaffected")
        index_a = frame["draws"][0]["ib"]["buf"]
        listed = {b["id"]: b for b in probe.buffers()["buffers"]}
        c.equal(listed[index_a]["unknown_writes"], 1, tag + "the lost write is counted on the buffer")
        c.equal(probe.status()["counts"]["unmap"], {"copied": 0, "copied_bytes": 0, "passed_over": 0, "passed_over_bytes": 0,
                                                    "shadows_dropped": 0}, tag + "nothing was copied at unmap")
        c.check(before["frame"] < frame["frame"] and sb.proc.poll() is None, tag + "the sandbox keeps running", sb.output())


def main() -> int:
    c = Checks("limits_test")
    for entry in ("layer", "inject"):
        unmap_settings(c, entry)
        descriptor_budget(c, entry)
        shader_budget(c, entry)
        resources_off(c, entry)
        snapshot_bounds(c, entry)
        image_hash_settings(c, entry)
    return c.finish()


if __name__ == "__main__":
    sys.exit(main())
