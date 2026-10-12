"""Offline tests: CRC-32C, the mod INI parser, override matching and the MCP message handling."""

from __future__ import annotations

import json
import os
import struct
import sys

from harness import Checks, test_dir

from efmi_vk import mcp_stdio  # noqa: E402  (path set up by harness)
from efmi_vk.hashes import crc32c, format_hash, index_region  # noqa: E402
from efmi_vk.mods import find_ini_files, match_overrides, parse_overrides  # noqa: E402
from efmi_vk.tools import TOOLS, Session, ToolError  # noqa: E402
from efmi_vk.spirv import inspect_spirv  # noqa: E402
from efmi_vk import dx11_pair  # noqa: E402

INI = """\
; a comment
[Constants]
global $x = 1

[TextureOverride_Component0]
hash = 2B21A639
match_first_index = 0
match_index_count = 3666
$object_detected = 1
if $x == 1
    hash = ffffffff
endif

[TextureOverride_NoIndexKeys]
hash = 0000abcd

[TextureOverrideWithoutHash]
match_first_index = 3

[CommandList_Draw]
hash = 12345678
"""


def test_hashes(c: Checks) -> None:
    c.equal(crc32c(b"123456789"), 0xE3069283, "CRC-32C check value")
    c.equal(crc32c(b""), 0, "CRC-32C of nothing")
    c.equal(crc32c(bytes(32)), 0x8A9136AA, "CRC-32C of 32 zero bytes")
    c.equal(crc32c(bytes([0xFF] * 32)), 0x62A8AB43, "CRC-32C of 32 0xFF bytes")
    c.equal(crc32c(b"56789", crc32c(b"1234")), 0xE3069283, "CRC-32C continues from a previous result")
    data = bytes(range(256)) * 5
    c.equal(crc32c(data[:13]), crc32c(memoryview(data)[:13]), "bytes and memoryview agree")
    c.equal(format_hash(0xAB), "000000ab", "hash formatting")
    c.equal(index_region(2477008, 2, 390, 30273), (2477008 + 780, 60546), "index region of a 16-bit draw")
    c.equal(index_region(16, 4, 6, 3), (40, 12), "index region of a 32-bit draw")


def test_mods(c: Checks) -> None:
    folder = test_dir("unit-")
    path = os.path.join(folder, "mod.ini")
    with open(path, "w", encoding="utf-8") as f:
        f.write(INI)
    os.makedirs(os.path.join(folder, "DISABLED_old"))
    with open(os.path.join(folder, "DISABLED_old", "old.ini"), "w") as f:
        f.write("[TextureOverrideOld]\nhash = 11111111\n")
    with open(os.path.join(folder, "DISABLED_x.ini"), "w") as f:
        f.write("[TextureOverrideX]\nhash = 22222222\n")

    overrides = parse_overrides(path)
    c.equal(len(overrides), 2, "sections with a hash")
    c.equal(overrides[0]["section"], "TextureOverride_Component0", "section name")
    c.equal(overrides[0]["hash"], "2b21a639", "hash is lower-cased and the first assignment wins")
    c.equal((overrides[0]["match_first_index"], overrides[0]["match_index_count"]), (0, 3666), "match keys")
    c.equal((overrides[1]["match_first_index"], overrides[1]["match_index_count"]), (None, None), "absent match keys")
    c.equal(find_ini_files(folder), [path], "disabled files and folders are skipped")
    c.equal(find_ini_files(path), [path], "a file path is returned as it is")

    draws = [
        {"n": 0, "first": 0, "count": 3666, "ib": {"hash": "2b21a639"}},
        {"n": 1, "first": 12, "count": 3666, "ib": {"hash": "2b21a639"}},
        {"n": 2, "first": 0, "count": 9, "ib": {"hash": "0000abcd"}},
        {"n": 3, "first": 0, "count": 9, "ib": {"hash": None}},
        {"n": 4, "first": 0, "count": 9},
    ]
    results = match_overrides(overrides, draws)
    c.equal(results[0]["draws"], [0], "first index and index count restrict the match")
    c.equal(results[1]["draws"], [2], "an override without match keys hits by hash alone")
    c.equal(match_overrides(overrides, [])[0]["matched"], False, "nothing matches an empty frame")


def test_mcp(c: Checks) -> None:
    session = Session()
    init = mcp_stdio.handle(session, {"jsonrpc": "2.0", "id": 1, "method": "initialize",
                                      "params": {"protocolVersion": "2025-03-26"}})
    c.equal(init["result"]["protocolVersion"], "2025-03-26", "initialize echoes the protocol version")
    c.equal(init["result"]["serverInfo"]["name"], "efmi-vk", "server name")
    c.equal(mcp_stdio.handle(session, {"jsonrpc": "2.0", "method": "notifications/initialized"}), None,
            "a notification gets no reply")
    listing = mcp_stdio.handle(session, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
    names = [t["name"] for t in listing["result"]["tools"]]
    c.equal(names, [t["name"] for t in TOOLS], "tools/list returns every tool")
    c.check(all(n.startswith("efvk_") and hasattr(Session, n) for n in names), "every tool has an implementation")
    c.check(all(t["inputSchema"]["type"] == "object" for t in TOOLS), "every tool has an object schema")
    c.equal(mcp_stdio.handle(session, {"jsonrpc": "2.0", "id": 3, "method": "ping"})["result"], {}, "ping")
    unknown = mcp_stdio.handle(session, {"jsonrpc": "2.0", "id": 4, "method": "nope"})
    c.equal(unknown["error"]["code"], -32601, "unknown method")

    no_capture = mcp_stdio.handle(session, {"jsonrpc": "2.0", "id": 5, "method": "tools/call",
                                            "params": {"name": "efvk_list_draws", "arguments": {}}})
    c.equal(no_capture["result"]["isError"], True, "a tool that needs a capture reports the missing capture")
    c.check("efvk_capture_frame" in no_capture["result"]["content"][0]["text"], "the message says what to call first")
    bad_args = mcp_stdio.handle(session, {"jsonrpc": "2.0", "id": 6, "method": "tools/call",
                                          "params": {"name": "efvk_get_draw", "arguments": {"bogus": 1}}})
    c.equal(bad_args["result"]["isError"], True, "wrong arguments are an error result, not a crash")
    bad_tool = mcp_stdio.handle(session, {"jsonrpc": "2.0", "id": 7, "method": "tools/call",
                                          "params": {"name": "nope", "arguments": {}}})
    c.equal(bad_tool["result"]["isError"], True, "an unknown tool is an error result")

    # Tools that work on a capture, fed with a hand-made one.
    session.capture = {"frame": 7, "draws": [
        {"n": 0, "kind": "indexed", "tid": 1, "cb": 2, "rp": 1, "pipeline": 0, "first": 0, "count": 3, "instances": 1,
         "ib": {"buf": 5, "offset": 0, "index_size": 2, "hash": "aaaaaaaa", "src": "shadow"}},
        {"n": 1, "kind": "draw", "tid": 1, "cb": 2, "rp": 1, "pipeline": 0, "first": 0, "count": 6, "instances": 1},
    ]}
    listed = session.call("efvk_list_draws", {"kind": "indexed"})
    c.equal((listed["total"], listed["draws"][0]["hash"]), (1, "aaaaaaaa"), "list_draws filters by kind")
    c.equal(session.call("efvk_find_hash", {"hash": "AAAAAAAA"})["total"], 1, "find_hash ignores the case")
    c.equal(session.call("efvk_list_draws", {"min_count": 4})["draws"][0]["n"], 1, "list_draws filters by count")
    c.equal(session.call("efvk_get_draw", {"n": 1})["kind"], "draw", "get_draw returns the draw")
    try:
        session.call("efvk_get_draw", {"n": 9})
        c.check(False, "get_draw rejects a draw that does not exist")
    except ToolError:
        c.check(True, "get_draw rejects a draw that does not exist")
    json.dumps(listed)
    session.close()


def test_spirv(c: Checks) -> None:
    header = [0x07230203, 0x00010000, 0, 20, 0]

    def code(words):
        return struct.pack("<%dI" % len(words), *words)

    # A stripped module still has set/binding decorations; names are optional.
    declaration = [4 << 16 | 71, 3, 34, 2, 4 << 16 | 71, 3, 33, 7, 4 << 16 | 59, 1, 3, 0]
    result = inspect_spirv(code(header + declaration))
    c.equal(result["bindings"], [{"id": 3, "set": 2, "binding": 7, "name": None, "storage_class": 0}],
            "stripped SPIR-V keeps declared binding metadata")
    c.equal(result["name_count"], 0, "no invented name for a stripped module")
    malformed = [b"", code([0, *header[1:]]), code(header + [0]), code(header + [8 << 16 | 5, 3]),
                 code(header + [3 << 16 | 5, 3, 0x41414141]), code(header) + b"x"]
    for data in malformed:
        try:
            inspect_spirv(data)
            c.check(False, "malformed SPIR-V is rejected")
        except ValueError:
            c.check(True, "malformed SPIR-V is rejected")


DRAW_INI = """\
[CommandList_Draw_Component0]
ib = Resource_Index
ps-t15 = Resource-Diffuse
ps-t15 = Resource-Other
drawindexed = 75987,0,0

[CommandList_Draw_Component1]
ps-t16 = Resource-Light
if $swapkey0 == 0
  ; [mesh:a]
  drawindexed = 49488,0,0
endif
if $swapkey1 == 0
  drawindexed = 166992, 49488, 0
  drawindexed = 166992, 49488, 0
endif
vs-cb1 = Resource-Config
drawindexed = auto

[CommandListOther]
drawindexed = 3,0,0
"""


def test_dx11_pair(c: Checks) -> None:
    tex = {"kind": "tex2d", "w": 2048, "h": 2048, "mips": 12, "array": 1, "fmt": 98, "fmt_name": "BC7_UNORM", "misc": 0}
    c.equal(dx11_pair.dx_texture_sig(tex, {"format": "BC7_UNORM", "dimension": "tex2d", "mip_levels": 12}),
            ("2d", 2048, 2048, 1, 1, 12, "BC7_UNORM"), "DX11 2D texture signature")
    depth = {"kind": "tex2d", "w": 4096, "h": 2048, "mips": 1, "array": 1, "fmt_name": "R16_TYPELESS", "misc": 0}
    c.equal(dx11_pair.dx_texture_sig(depth, {"format": "R16_UNORM", "dimension": "tex2d", "mip_levels": 1})[6], "R16_UNORM",
            "a typeless texture takes the format of its view")
    c.equal(dx11_pair.dx_texture_sig(depth)[6], "R16_TYPELESS", "without a view the resource format is kept")
    cube = {"kind": "tex2d", "w": 128, "h": 128, "mips": 8, "array": 6, "fmt_name": "BC6H_UF16", "misc": 4}
    c.equal(dx11_pair.dx_texture_sig(cube, {"format": "BC6H_UF16", "dimension": "cube", "mip_levels": 8})[:5],
            ("cube", 128, 128, 1, 6), "DX11 cube signature")
    volume = {"kind": "tex3d", "w": 128, "h": 64, "d": 128, "mips": 1, "fmt_name": "R11G11B10_FLOAT", "misc": 0}
    c.equal(dx11_pair.dx_texture_sig(volume), ("3d", 128, 64, 128, 1, 1, "R11G11B10_FLOAT"), "DX11 3D signature")
    c.equal(dx11_pair.dx_texture_sig({"kind": "buffer", "size": 16}), None, "a buffer has no texture signature")

    def image(fmt, extent, view_type=1, levels=1, layers=1, known=True):
        return {"image_known": known, "extent": extent, "format": fmt, "view_type": view_type,
                "subresource": {"levels": levels, "layers": layers}, "image": 7}

    c.equal(dx11_pair.vk_image_sig(image(145, [2048, 2048, 1], levels=12)), ("2d", 2048, 2048, 1, 1, 12, "BC7_UNORM"),
            "Vulkan image signature equals the DX11 one")
    c.equal(dx11_pair.vk_image_sig(image(143, [128, 128, 1], view_type=3, levels=8, layers=6))[:5], ("cube", 128, 128, 1, 6),
            "Vulkan cube signature")
    c.equal(dx11_pair.vk_image_sig(image(124, [4096, 2048, 1]))[6], "R16_UNORM", "a depth image compares as its DX11 view")
    c.equal(dx11_pair.vk_image_sig(image(999, [4, 4, 1]))[6], "vk999", "an unlisted format never matches by accident")
    c.equal(dx11_pair.vk_image_sig(image(145, [4, 4, 1], known=False)), None, "an unknown image has no signature")

    dx = [{"slot": "ps-t15", "key": "a"}, {"slot": "ps-t16", "key": "b"}, {"slot": "ps-t17", "key": "b"},
          {"slot": "ps-t18", "key": "c"}]
    vk = [{"set": 1, "binding": 6, "key": "a"}, {"set": 1, "binding": 1, "key": "b"}, {"set": 1, "binding": 3, "key": "b"},
          {"set": 0, "binding": 9, "key": "d"}]
    m = dx11_pair.match_by_key(dx, vk)
    c.equal([(p["dx"]["slot"], p["vk"]["binding"]) for p in m["pairs"]], [("ps-t15", 6)], "a unique signature is a pair")
    c.equal([([i["slot"] for i in a["dx"]], [i["binding"] for i in a["vk"]]) for a in m["ambiguous"]],
            [(["ps-t16", "ps-t17"], [1, 3])], "equal signatures stay an ambiguous group, nothing is guessed")
    c.equal([i["slot"] for i in m["dx_only"]], ["ps-t18"], "an item without a counterpart is reported")
    c.equal([i["binding"] for i in m["vk_only"]], [9], "also on the Vulkan side")

    windows = dx11_pair.dx_windows({
        "vs-cb2": {"res": 0, "first_constant": 8704, "num_constants": 4096},
        "ps-cb1": {"res": 0, "first_constant": 8704, "num_constants": 4096},
        "ps-cb3": {"res": 0, "first_constant": 3424, "num_constants": 16},
        "vs-cb1": {"res": 60, "first_constant": 0, "num_constants": 4096}})
    c.equal([(w["slots"], w["stages"], w["bytes"]) for w in windows],
            [(["vs-cb2", "ps-cb1"], 17, 65536), (["ps-cb3"], 16, 256), (["vs-cb1"], 1, 65536)],
            "one window seen by two stages is one item; another buffer is another window")
    c.equal([dx11_pair.round_to_window(n) for n in (1, 256, 257, 1312, 11440, 65536)], [256, 256, 512, 1536, 11520, 65536],
            "a Vulkan range rounds up to whole 16-constant windows")

    sets = [{"contents": {"bindings": [
                {"binding": 18, "type": 7, "stages": 17, "elements": [{"element": 0, "known": True, "buf": 37, "range": 8413184, "src": "mapped"}]},
                {"binding": 2, "type": 0, "stages": 17, "elements": [{"element": 0, "sampler": 3}]}]}},
            {"contents": {"bindings": [
                {"binding": 6, "type": 2, "stages": 16, "elements": [image(146, [2048, 2048, 1], levels=12)]},
                {"binding": 7, "type": 2, "stages": 16, "elements": [image(146, [4, 4, 1], known=False)]}]}},
            {"contents": {"bindings": [
                {"binding": 0, "type": 8, "stages": 17, "elements": [{"element": 0, "known": True, "buf": 64, "range": 65536, "src": "mapped"}]}]}},
            {"contents": None}]
    items = dx11_pair.vk_set_items(sets)
    c.equal([(i["set"], i["binding"], i["sig"][6]) for i in items["images"]], [(1, 6, "BC7_UNORM_SRGB")],
            "sampled images with a known image are listed with their set and binding")
    c.equal([(i["set"], i["binding"], i["range"], i["dynamic"]) for i in items["uniform"]], [(2, 0, 65536, True)],
            "uniform buffers, dynamic ones marked")
    c.equal([(i["set"], i["binding"], i["range"]) for i in items["storage"]], [(0, 18, 8413184)], "storage buffers")

    draws = [{"index": 45, "targets": "shadow", "vs": "a", "ps": "b"}, {"index": 71, "targets": "g", "vs": "c", "ps": "d"},
             {"index": 72, "targets": "g", "vs": "c", "ps": "d"}, {"index": 74, "targets": "g", "vs": "e", "ps": "f"}]
    c.equal([[d["index"] for d in g] for g in dx11_pair.group_uses(draws)], [[45], [71, 72], [74]],
            "ranges drawn with the same shaders into the same targets are one use")

    path = os.path.join(test_dir("unit"), "draw_sections.ini")
    with open(path, "w", encoding="utf-8") as f:
        f.write(DRAW_INI)
    sections = dx11_pair.parse_draw_sections(path)
    c.equal(sorted(sections), [0, 1], "only the component draw sections are read")
    c.equal(sections[0], {"ranges": [(75987, 0)], "slots": {"ps-t15": "Resource-Diffuse"}}, "ranges and the first value of a slot")
    c.equal(sections[1]["ranges"], [(49488, 0), (166992, 49488)], "every range once, whichever condition it is under")
    c.equal(sections[1]["slots"], {"ps-t16": "Resource-Light", "vs-cb1": "Resource-Config"}, "texture and buffer slots")

    listing = "\n".join([
        "ps_5_0", "dcl_globalFlags refactoringAllowed", "dcl_constantbuffer CB0[45], immediateIndexed",
        "  dcl_constantbuffer CB2[4094], dynamicIndexed", "dcl_sampler s0, mode_default",
        "dcl_resource_structured t1, 16", "dcl_resource_texture2d (float,float,float,float) t15",
        "dcl_resource_texturecube (float,float,float,float) t19", "dcl_resource_texture1d (float,float,float,float) t120",
        "dcl_input_ps linear v1.xy", "dcl_temps 12",
        "ld_structured_indexable(structured_buffer, stride=16)(mixed,mixed,mixed,mixed) r0.x, r1.x, l(12), t1.xxxx",
        "sample_indexable(texture2d)(float,float,float,float) r0.xyzw, v1.xyxx, t7.xyzw, s0", "ret"])
    decl = dx11_pair.parse_declarations(listing)
    c.equal(decl, {"t": [1, 15, 19, 120], "cb": {0: 45, 2: 4094}}, "declared slots; a use in an instruction is not a declaration")
    stages = {"vs": "v", "ps": "p"}
    table = {"p": json.loads(json.dumps(decl))}
    c.check(dx11_pair.declared(table, stages, "ps-t15"), "a declared texture slot takes part")
    c.check(not dx11_pair.declared(table, stages, "ps-t16"), "a bound slot the shader does not declare is left out")
    c.check(dx11_pair.declared(table, stages, "ps-cb2") and not dx11_pair.declared(table, stages, "ps-cb1"),
            "constant buffer slots, also after the table went through JSON")
    c.check(dx11_pair.declared(table, stages, "vs-t3"), "a stage whose shader was not listed keeps every slot")
    c.check(dx11_pair.declared(None, stages, "ps-t16"), "without declarations nothing is left out")


def main() -> int:
    c = Checks("unit_test")
    test_hashes(c)
    test_mods(c)
    test_mcp(c)
    test_spirv(c)
    test_dx11_pair(c)
    return c.finish()


if __name__ == "__main__":
    sys.exit(main())
