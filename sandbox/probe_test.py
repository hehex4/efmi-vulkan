"""End-to-end test of the layer against the sandbox program.

Starts the sandbox with the layer enabled for that process only, connects to the
probe pipe and compares what the probe reports with the values the sandbox wrote.
"""

from __future__ import annotations

import struct
import sys
import time

from harness import Checks, Sandbox

from efmi_vk.hashes import crc32c, format_hash  # noqa: E402  (path set up by harness)
from efmi_vk.probe_client import ProbeClient, ProbeError, list_probes  # noqa: E402
from efmi_vk.spirv import inspect_spirv  # noqa: E402

VK_BUFFER_USAGE_INDEX_BUFFER_BIT = 0x40


def check_draws(c: Checks, capture: dict, expected: dict, rewritten: bool, tag: str) -> None:
    draws = [d for d in capture["draws"] if d["kind"] == "indexed"]
    c.equal(len(draws), len(expected["draws"]), "%s: number of indexed draws" % tag)
    for i, (got, want) in enumerate(zip(draws, expected["draws"])):
        label = "%s: %s" % (tag, want["label"])
        c.equal(got["count"], want["count"], label + " index count")
        c.equal(got["first"], want["first"], label + " first index")
        c.equal(got["instances"], want["instances"], label + " instance count")
        ib = got.get("ib") or {}
        c.equal(ib.get("index_size"), want["index_size"], label + " index size")
        c.equal(ib.get("offset"), want["bind_offset"], label + " bind offset")
        c.equal(ib.get("hash"), want["hash_after_rewrite" if rewritten else "hash"], label + " region hash")
        c.equal(ib.get("src"), want["src"], label + " hash source")
        c.equal(got["sets"][0]["dyn"], [want["dyn"]], label + " dynamic offset")
        sets = got["sets"]
        c.equal(sets[1]["dyn"], [256 * ((i + 1) % 5), 256 * (4 - i)], label + " second set dynamic array offsets")
        uniform = sets[0]["contents"]
        material = sets[1]["contents"]
        c.check(uniform["tracked"] and material["tracked"], label + " both sets are tracked")
        c.check(uniform["pipeline_compatible"] and material["pipeline_compatible"], label + " sets compatible with pipeline")
        c.equal(uniform["snapshot"], "submit", label + " descriptor snapshot timing is explicit")
        u = uniform["bindings"][0]["elements"][0]
        c.equal((u["effective_offset"], u["range"], u["src"]), (want["dyn"], 32, "mapped"), label + " uniform range")
        bindings = material["bindings"]
        c.equal([b["binding"] for b in bindings], [2, 5], label + " sparse bindings are sorted")
        c.equal(bindings[0]["count"], 2, label + " storage descriptor array size")
        c.equal(bindings[0]["unknown_elements"], 0, label + " copied storage descriptors are known")
        storage = bindings[0]["elements"]
        base = 0 if rewritten else 256
        c.equal([e["effective_offset"] for e in storage],
                [base + 256 * ((i + 1) % 5), 256 * (4 - i)], label + " base plus dynamic offsets")
        c.equal([e["range"] for e in storage], [16, 16], label + " storage ranges")
        c.check(all(e["buf"] and e["resource_known"] and e["src"] == "mapped" for e in storage), label + " storage ids and visibility")
        c.check(storage[0]["buf"] == storage[1]["buf"] != u["buf"], label + " resources attributed to the correct set")
        c.equal(bindings[1]["immutable_sampler"], True, label + " immutable sampler layout")
        tex = bindings[1]["elements"][0]
        c.check(tex["sampler"] > 0 and tex["sampler_known"], label + " immutable sampler survives descriptor copy")
        c.check(tex["image"] > 0 and tex["image_known"] and tex["view_known"], label + " image and view are tracked")
        c.equal((tex["format"], tex["image_format"], tex["extent"]), (37, 37, [1, 1, 1]), label + " texture format and size")
        c.equal(tex["image_layout"], 5, label + " declared shader-read image layout")
        c.equal(tex["subresource"], {"aspect": 1, "base_mip": 0, "levels": 1, "base_layer": 0, "layers": 1}, label + " image view range")
        c.check(not material["truncated"], label + " all material elements captured")
        c.equal(sets[2]["contents"]["reason"], "unbound", label + " unbound set is explicit")
        c.check(got["vb"][0][0] != 0, label + " vertex buffer resolved")
        c.check(got["pipeline"] != 0, label + " pipeline resolved")
        c.check(got["rp"] == 1, label + " inside the first render pass", got["rp"])


def run(threads: int, entry: str) -> int:
    c = Checks("probe_test entry=%s threads=%d" % (entry, threads))
    with Sandbox(threads=threads, entry=entry) as sb:
        expected = sb.expected
        c.check(sb.pid in list_probes(), "the sandbox serves a probe pipe", sb.log())
        with ProbeClient(sb.pid) as probe:
            status = probe.status()
            c.equal(status["pid"], sb.pid, "status pid")
            c.equal(status["mode"], "probe", "status mode")
            c.equal(status["exe"], "efmi_vk_sandbox.exe", "status exe")
            c.check(bool(status["device"]["name"]), "device name reported")
            c.check("capture" in status["features"], "capture feature listed")
            c.equal(status["entry"], entry, "status entry")
            attach = status["attach"]
            c.equal(attach["devices_created"], 1, "one device seen at creation")
            c.equal(attach["passive_calls"], 0, "no call for an unknown device")
            if entry == "inject":
                c.equal(attach["loader_found"], True, "vulkan-1.dll was found")
                c.equal(attach["loader_was_loaded"], False, "the DLL arrived before vulkan-1.dll")
                c.check(attach["exports_redirected"] >= 40, "exports were redirected", attach["exports_redirected"])
            else:
                c.equal(attach["exports_redirected"], 0, "a layer redirects no export")

            cap = probe.capture(timeout_ms=5000)
            stats = cap["stats"]
            c.equal(stats["submits"], 1, "one submit per frame")
            c.equal(stats["command_buffers"], 1, "one primary command buffer per frame")
            c.equal(stats["secondary_command_buffers"], threads, "secondary command buffers")
            c.equal(stats["render_passes"], 1, "one render pass")
            c.equal(stats["dynamic_render_passes"], 0, "no dynamic rendering")
            c.equal(stats["draws"], 5, "five draws")
            c.equal(stats["indexed"], 5, "five indexed draws")
            c.equal(stats["unknown_writes"], 0, "no unknown buffer writes")
            c.equal(len(stats["threads"]), max(threads, 1) + (1 if threads else 0), "threads that recorded")
            check_draws(c, cap, expected, False, "initial")

            if threads:
                tids = [d["tid"] for d in cap["draws"]]
                cbs = [d["cb"] for d in cap["draws"]]
                c.equal(len(set(tids)), threads, "draws come from %d recording threads" % threads)
                c.equal(len(set(cbs)), threads, "draws come from %d secondary command buffers" % threads)

            # Two consecutive captures are different frames.
            cap2 = probe.capture(timeout_ms=5000, draws=False)
            c.check(cap2["frame"] > cap["frame"], "second capture is a later frame")
            c.check("draws" not in cap2, "draws omitted when not requested")

            # Buffer list and read-back.
            listing = probe.buffers(usage=VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
            c.equal(listing["total"], 2, "two index buffers")
            by_id = {b["id"]: b for b in listing["buffers"]}
            draws = cap["draws"]
            id_a, id_b = draws[0]["ib"]["buf"], draws[3]["ib"]["buf"]
            c.check(id_a in by_id and id_b in by_id, "index buffers of the draws are listed")
            if id_a in by_id and id_b in by_id:
                c.equal(by_id[id_a]["mapped"], False, "buffer A is not mapped")
                c.equal(by_id[id_a]["shadow_known_bytes"], 24, "buffer A shadow covers the upload")
                c.equal(by_id[id_b]["mapped"], True, "buffer B is mapped")

            info, data = probe.read_buffer(id_a)
            c.equal(info["src"], "shadow", "buffer A is read from the shadow")
            c.equal(struct.unpack("<12H", data), (0, 1, 2, 3, 4, 5, 3, 5, 6, 7, 8, 9), "buffer A content")
            info, data = probe.read_buffer(id_b, offset=16)
            c.equal(info["src"], "mapped", "buffer B is read from the mapping")
            c.equal(struct.unpack("<9I", data), (10, 11, 12, 10, 12, 13, 14, 15, 16), "buffer B content")
            c.equal(probe.hash(id_b, offset=16, size=24)["hash"], format_hash(crc32c(data[:24])),
                    "hash command agrees with the Python CRC-32C")

            # The vertex buffer is neither a shadow target nor unmapped: mapped read works.
            vb_id = draws[0]["vb"][0][0]
            info, data = probe.read_buffer(vb_id, size=8)
            c.equal(struct.unpack("<2f", data), struct.unpack("<2f", struct.pack("<2f", -0.9, 0.1)), "first vertex")

            pipe = probe.pipeline(draws[0]["pipeline"])
            c.equal(pipe["bindings"], [{"binding": 0, "stride": 8, "per_instance": False}], "vertex binding")
            c.equal([(a["location"], a["offset"]) for a in pipe["attributes"]], [(0, 0)], "vertex attribute")
            c.equal(len(pipe["shaders"]), 2, "two retained pipeline shaders")
            for shader, want in zip(pipe["shaders"], expected["shaders"]):
                c.equal({k: shader[k] for k in ("stage", "hash", "size")}, want, "shader matches sandbox fingerprint")
                info, code = probe.shader(shader["id"])
                c.equal((info["hash"], len(code)), (want["hash"], want["size"]), "shader bytes after module destruction")
                c.equal(format_hash(crc32c(code)), want["hash"], "shader CRC agrees with independent Python implementation")
                reflection = inspect_spirv(code)
                wanted = [(0, 0, "instance_config")]
                if shader["stage"] == 16:
                    wanted += [(1, 2, "materials"), (1, 5, "albedo")]
                c.equal([(b["set"], b["binding"], b["name"]) for b in reflection["bindings"]], wanted, "SPIR-V declared bindings and names")
                c.equal(probe.shader(shader["id"], code=False)[1], b"", "metadata-only shader request omits bytes")
            counts = probe.status()["counts"]
            c.equal((counts["shader_modules"], counts["retained_shaders"]), (0, 2), "pipeline retains destroyed modules")
            c.equal(counts["descriptor_sets"], 2, "freed copy source set is removed")
            c.equal(counts["images"], 1, "one non-swapchain image")
            c.equal(counts["samplers"], 1, "one immutable sampler")
            storage = draws[0]["sets"][1]["contents"]["bindings"][0]["elements"][0]
            _, data = probe.read_buffer(storage["buf"], offset=storage["effective_offset"], size=storage["range"])
            c.equal(struct.unpack("<4f", data), struct.unpack("<4f", struct.pack("<4f", 0.6, 0.75, 1, 1)),
                    "storage descriptor leads to the expected CPU-visible data")

            usage = probe.api_usage()
            calls = usage["calls"]
            c.check(calls.get("vkCmdDrawIndexed", 0) >= 5, "draw calls are counted", calls.get("vkCmdDrawIndexed"))
            c.check(calls.get("vkQueueSubmit", 0) >= 2, "submits are counted", calls.get("vkQueueSubmit"))
            c.check(calls.get("vkQueuePresentKHR", 0) >= 1, "presents are counted")
            c.equal(calls.get("vkUpdateDescriptorSets"), 1, "the one descriptor update is counted")
            c.equal(calls.get("vkCreateShaderModule"), 2, "both shader modules are counted")
            c.equal(calls.get("vkAllocateDescriptorSets"), 1, "the descriptor set allocation is counted")
            c.equal(calls.get("vkCreateSwapchainKHR"), 1, "the swapchain creation is counted")
            c.check(calls.get("vkCreateImageView", 0) >= 2, "image view creations are counted")
            c.check("vkCmdDispatch" not in calls, "a function that is never called has no entry")
            c.equal(calls.get("vkCmdExecuteCommands", 0) > 0, threads > 0, "secondary execution is counted when used")
            c.check("vkCmdDrawIndexed" in usage["resolved"], "resolved function names are listed")
            c.equal(usage["extensions"], ["VK_KHR_swapchain"], "enabled device extensions")

            try:
                probe.read_buffer(999999)
                c.check(False, "unknown buffer id is rejected")
            except ProbeError:
                c.check(True, "unknown buffer id is rejected")
            try:
                probe.shader(999999)
                c.check(False, "unknown shader id is rejected")
            except ProbeError:
                c.check(True, "unknown shader id is rejected")

            # Rewrite through a staging buffer that is unmapped before the copy is recorded.
            sb.rewrite_index_buffer()
            time.sleep(0.1)
            cap3 = probe.capture(timeout_ms=5000)
            c.equal(cap3["stats"]["unknown_writes"], 0, "no unknown writes after the rewrite")
            check_draws(c, cap3, expected, True, "after rewrite")
            info, data = probe.read_buffer(id_a)
            c.equal(struct.unpack("<12H", data), (0, 1, 2, 3, 5, 4, 3, 6, 5, 7, 8, 9), "buffer A content after the rewrite")

            status = probe.status()
            c.check(status["frame"] > cap3["frame"], "frames keep counting")
            c.equal(status["maxima"]["secondary_command_buffers"], threads, "maximum of secondary command buffers")

        # A second client can connect after the first one has gone.
        with ProbeClient(sb.pid) as probe:
            c.equal(probe.status()["pid"], sb.pid, "reconnect works")

        c.check(sb.proc.poll() is None, "the sandbox is still running", sb.output())
        log = sb.log()
        c.check("loaded into efmi_vk_sandbox.exe" in log, "the layer logged that it was loaded")
        c.check("device created" in log, "the layer logged the device")
        c.check("entry=%s" % entry in log, "the layer logged its entry")
    return c.finish()


def main() -> int:
    worst = 0
    for entry, threads in (("layer", 0), ("layer", 2), ("layer", 5), ("inject", 0), ("inject", 2)):
        worst = max(worst, run(threads, entry))
    return worst


if __name__ == "__main__":
    sys.exit(main())
