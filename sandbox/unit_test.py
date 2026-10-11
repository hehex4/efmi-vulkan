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


def main() -> int:
    c = Checks("unit_test")
    test_hashes(c)
    test_mods(c)
    test_mcp(c)
    test_spirv(c)
    return c.finish()


if __name__ == "__main__":
    sys.exit(main())
