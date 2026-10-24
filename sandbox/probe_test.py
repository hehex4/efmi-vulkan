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
    pushed = expected["descriptor_mode"] >= 2
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
        c.equal(sets[1]["dyn"], [] if pushed else [256 * ((i + 1) % 5), 256 * (4 - i)], label + " second set dynamic array offsets")
        uniform = sets[0]["contents"]
        material = sets[1]["contents"]
        c.check(uniform["tracked"] and material["tracked"], label + " both sets are tracked")
        c.check(uniform["pipeline_compatible"] and material["pipeline_compatible"], label + " sets compatible with pipeline")
        c.equal(uniform["snapshot"], "submit", label + " descriptor snapshot timing is explicit")
        c.equal(material["snapshot"], "record" if pushed else "submit", label + " material snapshot timing")
        c.equal(material["resource_snapshot"], "submit", label + " resource visibility timing")
        c.equal(material["kind"], "push" if pushed else "set", label + " material binding kind")
        if pushed:
            c.equal(sets[1]["set"], "0x0", label + " push descriptors have no allocated set handle")
        u = uniform["bindings"][0]["elements"][0]
        c.equal((u["effective_offset"], u["range"], u["src"]), (want["dyn"], expected["uniform"]["range"], "mapped"),
                label + " uniform range")
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
        texture = expected["texture"]
        c.equal((tex["format"], tex["image_format"], tex["extent"]), (texture["format"], texture["format"], texture["extent"]),
                label + " texture format and size")
        c.equal(tex["content"], {"state": "hashed", "crc": texture["crc"], "bytes": texture["bytes"], "uploads": texture["uploads"]},
                label + " content hash of the last whole upload, rows without padding")
        # D-077: the description of the sampled RGBA8 image (format 28, shader resource) seeded with the
        # hash of the first width * height bytes of that upload, which the layer reads row by row.
        width, height = texture["extent"][:2]
        description = struct.pack("<11I", width, height, 1, 1, 28, 1, 0, 0, 8, 0, 0)
        c.equal(tex.get("dx11_hash"), "%08x" % crc32c(description, int(texture["prefix_crc"], 16)),
                label + " the image's original DX11 hash from its padded upload")
        c.equal(tex["image_layout"], 5, label + " declared shader-read image layout")
        c.equal(tex["subresource"], {"aspect": 1, "base_mip": 0, "levels": 1, "base_layer": 0, "layers": 1}, label + " image view range")
        c.check(not material["truncated"], label + " all material elements captured")
        c.equal(sets[2]["contents"]["reason"], "unbound", label + " unbound set is explicit")
        c.check(got["vb"][0][0] != 0, label + " vertex buffer resolved")
        c.check(got["pipeline"] != 0, label + " pipeline resolved")
        c.check(got["rp"] == 1, label + " inside the first render pass", got["rp"])
        contexts = capture.get("render_targets", [])
        target_index = got.get("targets", 0)
        c.check(0 < target_index <= len(contexts), label + " attachment context reference")
        if 0 < target_index <= len(contexts):
            target = contexts[target_index - 1]
            c.check(target["known"] and not target["dynamic"], label + " classic render pass attachment context")
            c.equal((target["snapshot"], target["subpass"], target["layers"]), ("record", 0, 1), label + " target snapshot scope")
            c.check(target["framebuffer"] > 0 and target["render_pass"] > 0, label + " stable framebuffer and render pass identities")
            c.equal(target["depth"], None, label + " no depth target")
            c.equal(len(target["colors"]), 1, label + " one color attachment")
            color = target["colors"][0]
            c.check(color["bound"] and color["known"] and color["image"] > 0 and color["view"] > 0,
                    label + " swapchain image and view are tracked")
            c.equal(color["layout"], 2, label + " color attachment layout")
            c.equal(color["extent"][:2], target["area"][2:], label + " target size matches render area")


def check_snapshot(c: Checks, probe: ProbeClient, expected: dict) -> None:
    """Contents kept at submit for selected draws, and the comparison with the read at record time.

    The sandbox writes a frame number into every uniform slot before it records the draws, and into
    the odd slots a second time after recording, before the submit. Storage data never changes.
    """
    uni = expected["uniform"]
    chosen = (0, 1, 3)
    entry_bytes = uni["late_at"]            # ends before the bytes that arrive late
    follow = {"set": 0, "binding": 0, "offset": uni["follow_at"], "count": 2, "stride": uni["follow_stride"],
              "bytes": uni["follow_bytes"], "entry": entry_bytes}
    cap = probe.capture(timeout_ms=5000, snapshot={"hashes": [expected["draws"][i]["hash"] for i in chosen], "follow": follow},
                        wait_draws=True)
    c.check(cap["wait_draws"] and cap["skipped_empty_intervals"] == 0,
            "snapshot: nonempty mode returns this sandbox's one graphics interval")
    draws = [d for d in cap["draws"] if d["kind"] == "indexed"]
    c.equal([i for i, d in enumerate(draws) if "snapshot" in d], list(chosen), "snapshot: only the draws with a listed hash")
    info = cap["snapshot"]
    c.equal((info["all"], info["hashes"], info["draws"], info["omitted"]), (False, 3, 3, 0), "snapshot: summary of the request")
    ids = set()
    states = {"equal": 0, "different": 0}
    for i in chosen:
        tag = "snapshot of draw %d: " % i
        items = draws[i]["snapshot"]
        c.equal([s["kind"] for s in items], ["uniform", "entry", "follow", "follow", "follow", "follow"], tag + "items in order")
        uniform, entry = items[0], items[1]
        late = i % 2 == 1
        c.equal((uniform["set"], uniform["binding"], uniform["offset"], uniform["size"], uniform["src"]),
                (0, 0, uni["slot"] * i, uni["range"], "mapped"), tag + "uniform range of the draw's dynamic offset")
        c.equal(uniform["record"]["state"], "different" if late else "equal", tag + "bytes that arrive after recording are noticed")
        c.equal((entry["offset"], entry["size"], entry["record"]["state"]), (uni["slot"] * i, entry_bytes, "equal"),
                tag + "the entry in front of the late bytes was complete at record time")
        meta, data = probe.snapshot(cap["seq"], uniform["id"])
        c.equal((meta["size"], meta["when"], len(data)), (uni["range"], "submit", uni["range"]), tag + "kept uniform bytes")
        c.equal(format_hash(crc32c(data)), uniform["crc"], tag + "checksum of the kept bytes")
        frame_mark, late_mark = struct.unpack_from("<I", data, uni["frame_at"])[0], struct.unpack_from("<I", data, uni["late_at"])[0]
        c.check(frame_mark > 0, tag + "the per-frame value is in the kept bytes", frame_mark)
        if late:
            c.equal(late_mark, frame_mark, tag + "kept as at submit: the late value of this frame is included")
            c.check(uniform["record"]["crc"] != uniform["crc"], tag + "record-time checksum differs")
        c.equal(list(struct.unpack_from("<2I", data, uni["follow_at"])), uni["follow_values"], tag + "values the windows are located by")
        _, head = probe.snapshot(cap["seq"], entry["id"])
        c.equal(head, data[:entry_bytes], tag + "entry bytes are the start of the range")
        storage = [e["effective_offset"] for e in draws[i]["sets"][1]["contents"]["bindings"][0]["elements"]]
        want = [(w, v, 1, 2, el, storage[el] + v * uni["follow_stride"]) for w, v in enumerate(uni["follow_values"]) for el in (0, 1)]
        c.equal([(s["word"], s["value"], s["set"], s["binding"], s["element"], s["offset"]) for s in items[2:]], want,
                tag + "one window per value and storage range")
        for s in items[2:]:
            c.equal((s["size"], s["src"], s["record"]["state"]), (uni["follow_bytes"], "mapped", "equal"), tag + "window read at both times")
            _, window = probe.snapshot(cap["seq"], s["id"])
            _, live = probe.read_buffer(s["buf"], offset=s["offset"], size=s["size"])
            c.equal(window, live, tag + "window bytes equal the buffer")
        for s in items:
            ids.add(s["id"])
            states[s["record"]["state"]] = states.get(s["record"]["state"], 0) + 1
    c.equal(info["blobs"], len(ids), "snapshot: one kept blob per distinct range")
    c.equal((info["record"]["equal"], info["record"]["different"], info["record"]["not_recorded"]),
            (states["equal"], states["different"], 0), "snapshot: record comparison is summarised")
    c.equal(states["different"], sum(1 for i in chosen if i % 2), "snapshot: only the uniform ranges of odd slots differ")

    everything = probe.capture(timeout_ms=5000, snapshot={"all": True})
    c.equal(everything["snapshot"]["draws"], 5, "snapshot: all indexed draws on request")
    c.equal([[s["kind"] for s in d["snapshot"]] for d in everything["draws"] if d["kind"] == "indexed"], [["uniform"]] * 5,
            "snapshot: without a follow rule only uniform ranges are kept")
    try:
        probe.snapshot(cap["seq"], 0)
        c.check(False, "snapshot: contents of an earlier capture are refused")
    except ProbeError:
        c.check(True, "snapshot: contents of an earlier capture are refused")
    try:
        probe.snapshot(everything["seq"], 9999)
        c.check(False, "snapshot: an unknown id is refused")
    except ProbeError:
        c.check(True, "snapshot: an unknown id is refused")


def run(threads: int, entry: str, descriptors: str = "ordinary") -> int:
    c = Checks("probe_test entry=%s threads=%d descriptors=%s" % (entry, threads, descriptors))
    with Sandbox(threads=threads, entry=entry, descriptors=descriptors) as sb:
        expected = sb.expected
        pushed = expected["descriptor_mode"] >= 2
        c.check(sb.pid in list_probes(), "the sandbox serves a probe pipe", sb.log())
        with ProbeClient(sb.pid) as probe:
            status = probe.status()
            c.equal(status["pid"], sb.pid, "status pid")
            c.equal(status["mode"], "probe", "status mode")
            c.equal(status["exe"], "efmi_vk_sandbox.exe", "status exe")
            c.check(bool(status["device"]["name"]), "device name reported")
            swapchains = status["device"]["swapchains"]
            c.equal(len(swapchains), 1, "the swap chain is recorded at creation")
            if swapchains:
                chain = swapchains[0]
                c.check(chain["window"], "the swap chain's surface window is known", entry)
                c.check(chain["width"] > 0 and chain["height"] > 0 and chain["images"] >= 2, "swap chain extent and images", chain)
                c.check(chain["usage"] & 0x10, "the swap chain images are color attachments", chain["usage"])
            c.check(status["device"]["application_api"] > 0, "the application API version is recorded")
            c.equal(status["device"]["null_descriptors_added"], False, "no extension is added outside character mode")
            c.check("capture" in status["features"], "capture feature listed")
            c.check("capture_wait_draws" in status["features"], "nonempty capture feature listed")
            c.check("render_targets" in status["features"], "attachment capture feature listed")
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
            c.check(not cap["wait_draws"] and cap["skipped_empty_intervals"] == 0,
                    "ordinary capture keeps the original one-interval semantics")
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
            c.check(cap["snapshot"] is None and not any("snapshot" in d for d in cap["draws"]),
                    "no contents are kept unless asked for")
            c.check("snapshot" in status["features"], "snapshot feature listed")
            check_snapshot(c, probe, expected)

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
            c.equal({k: pipe[k] for k in ("create_flags", "subpass", "specialized", "dynamic_states", "push_constants")},
                    {"create_flags": 0, "subpass": 0, "specialized": False, "dynamic_states": [0, 1], "push_constants": []},
                    "what a copy of the pipeline has to reproduce")
            c.check(len(pipe["layout_sets"]) == 2 and all(pipe["layout_sets"]), "set layouts of the pipeline layout", pipe["layout_sets"])
            recipe = pipe["recipe"]
            c.check(recipe["complete"] and recipe["render_pass"]["complete"], "complete owned pipeline and render pass recipe")
            c.equal(recipe["float_encoding"], "ieee754_bits", "recipe floats have lossless encoding")
            c.equal(recipe["render_pass"]["subpasses"][0]["colors"][0]["attachment"], 0, "render pass color attachment")
            c.equal(recipe["depth_stencil"], None, "unused depth state is not read")
            c.equal(recipe["viewports"], [], "dynamic viewport values are not borrowed")
            c.equal(recipe["scissors"], [], "dynamic scissor values are not borrowed")
            c.equal(recipe["blend"]["attachments"][0]["write_mask"], 15, "color write mask retained")
            c.equal(pipe["variant_preflight"]["supported"], False, "ordinary probe fixture is not the three-set M2 profile")
            c.check(pipe["variant_preflight"]["max_bound_descriptor_sets"] >= 4, "device limit used in variant preflight")
            c.equal(len(pipe["layout_recipe"]["sets"]), 2, "descriptor layout recipe exported")
            counts = probe.status()["counts"]
            c.equal((counts["shader_modules"], counts["retained_shaders"]), (0, 2), "pipeline retains destroyed modules")
            content = dict(counts["image_content"])
            c.check(content.pop("microseconds") >= 0, "time spent on image hashing is reported")
            c.equal(content, {"enabled": True, "hashed": 2, "partial": 1, "unreadable": 0, "unsupported_format": 0, "too_large": 0,
                              "bytes": 2 * expected["texture"]["bytes"]}, "image uploads by outcome")
            c.equal(counts["descriptor_sets"], 1 if pushed else 2, "only allocated live descriptor sets are counted")
            c.equal(counts["descriptor_templates"], 1 if descriptors in ("template", "push-template") else 0,
                    "only live descriptor templates are counted")
            c.equal(counts["images"], 1 + sum(chain["images"] for chain in swapchains),
                    "one uploaded image plus every tracked swapchain image")
            c.equal(counts["samplers"], 1, "one immutable sampler")
            storage = draws[0]["sets"][1]["contents"]["bindings"][0]["elements"][0]
            _, data = probe.read_buffer(storage["buf"], offset=storage["effective_offset"], size=storage["range"])
            c.equal(struct.unpack("<4f", data), struct.unpack("<4f", struct.pack("<4f", 0.6, 0.75, 1, 1)),
                    "storage descriptor leads to the expected CPU-visible data")

            usage = probe.api_usage()
            calls = usage["calls"]
            creation = usage["pipeline_creation"]
            c.equal(creation["graphics_pipelines"], 1, "one graphics pipeline creation counted")
            c.equal({k: creation["uses"].get(k) for k in ("flags:0", "stages:2", "subpass:0", "dynamic:0", "dynamic:1", "topology:3",
                                                         "samples:1", "viewports:1", "vertex_bindings:1", "color_blend_given")},
                    {"flags:0": 1, "stages:2": 1, "subpass:0": 1, "dynamic:0": 1, "dynamic:1": 1, "topology:3": 1, "samples:1": 1,
                     "viewports:1": 1, "vertex_bindings:1": 1, "color_blend_given": 1}, "what the pipeline creation used")
            c.check(not any(k.startswith(("next:", "stage_next:")) or k in ("specialization", "base_pipeline", "tessellation")
                            for k in creation["uses"]), "nothing is counted that the sandbox does not use", creation["uses"])
            c.equal(calls.get("vkCmdCopyBufferToImage"), 3, "image uploads are counted")
            c.check(calls.get("vkCmdDrawIndexed", 0) >= 5, "draw calls are counted", calls.get("vkCmdDrawIndexed"))
            c.check(calls.get("vkQueueSubmit", 0) >= 2, "submits are counted", calls.get("vkQueueSubmit"))
            c.check(calls.get("vkQueuePresentKHR", 0) >= 1, "presents are counted")
            c.equal(calls.get("vkUpdateDescriptorSets"), 2 if descriptors == "template" else 1, "ordinary updates are counted")
            if descriptors == "template":
                c.equal(calls.get("vkCreateDescriptorUpdateTemplate"), 2, "both update templates are counted")
                c.equal(calls.get("vkDestroyDescriptorUpdateTemplate"), 1, "initial template was destroyed before drawing")
                c.equal(calls.get("vkUpdateDescriptorSetWithTemplate"), 1, "initial material used a template")
            if pushed:
                name = "vkCmdPushDescriptorSetWithTemplateKHR" if descriptors == "push-template" else "vkCmdPushDescriptorSetKHR"
                c.check(calls.get(name, 0) >= 5, "push descriptor calls reach the selected hook", calls.get(name))
            c.equal(calls.get("vkCreateShaderModule"), 2, "both shader modules are counted")
            c.equal(calls.get("vkAllocateDescriptorSets"), 1, "the descriptor set allocation is counted")
            c.equal(calls.get("vkCreateSwapchainKHR"), 1, "the swapchain creation is counted")
            c.check(calls.get("vkCreateImageView", 0) >= 2, "image view creations are counted")
            c.check("vkCmdDispatch" not in calls, "a function that is never called has no entry")
            c.equal(calls.get("vkCmdExecuteCommands", 0) > 0, threads > 0, "secondary execution is counted when used")
            c.check("vkCmdDrawIndexed" in usage["resolved"], "resolved function names are listed")
            c.equal(usage["extensions"], ["VK_KHR_swapchain"] + (["VK_KHR_push_descriptor"] if pushed else []), "enabled device extensions")

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
            if descriptors == "template":
                c.equal(probe.api_usage()["calls"].get("vkUpdateDescriptorSetWithTemplate"), 2,
                        "rewrite uses the second template")
            info, data = probe.read_buffer(id_a)
            c.equal(struct.unpack("<12H", data), (0, 1, 2, 3, 5, 4, 3, 6, 5, 7, 8, 9), "buffer A content after the rewrite")
            c.equal(probe.status()["counts"]["unmap"], {"copied": 1, "copied_bytes": 12, "passed_over": 0, "passed_over_bytes": 0,
                                                        "shadows_dropped": 0}, "the staging buffer of the rewrite was copied at unmap")

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
    for descriptors in ("template", "push", "push-template"):
        for entry in ("layer", "inject"):
            for threads in (0, 2):
                worst = max(worst, run(threads, entry, descriptors))
    return worst


if __name__ == "__main__":
    sys.exit(main())
