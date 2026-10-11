"""End-to-end test of the MCP server: sandbox with the layer, the server as a child process."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile

from harness import ROOT, Checks, Sandbox


class McpProcess:
    def __init__(self, out_dir: str):
        self.proc = subprocess.Popen(
            [sys.executable, "-B", os.path.join(ROOT, "server", "run_server.py"), "--out", out_dir],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        self.next_id = 1

    def send(self, method: str, params=None, notify: bool = False):
        msg = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params
        if not notify:
            msg["id"] = self.next_id
            self.next_id += 1
        self.proc.stdin.write((json.dumps(msg) + "\n").encode("utf-8"))
        self.proc.stdin.flush()
        if notify:
            return None
        line = self.proc.stdout.readline()
        if not line:
            raise RuntimeError("the MCP server closed its output: %s" % self.proc.stderr.read().decode("utf-8", "replace"))
        reply = json.loads(line)
        if reply.get("id") != msg["id"]:
            raise RuntimeError("reply id mismatch: %r" % (reply,))
        return reply

    def tool(self, name: str, **arguments):
        reply = self.send("tools/call", {"name": name, "arguments": arguments})
        result = reply["result"]
        text = result["content"][0]["text"]
        return result["isError"], (text if result["isError"] else json.loads(text))

    def close(self):
        try:
            self.proc.stdin.close()
            self.proc.wait(timeout=10)
        except Exception:
            self.proc.kill()


def write_mod(folder: str, expected: dict) -> str:
    """A mod INI whose overrides are known to hit, to miss by index, and to miss by hash."""
    d0, d1, d3 = expected["draws"][0], expected["draws"][1], expected["draws"][3]
    text = "\n".join([
        "[TextureOverride_Hit_Mesh0]",
        "hash = %s" % d0["hash"],
        "match_first_index = %d" % d0["first"],
        "match_index_count = %d" % d0["count"],
        "",
        "[TextureOverride_Hit_Mesh3_HashOnly]",
        "hash = %s" % d3["hash"],
        "",
        "[TextureOverride_Miss_WrongFirstIndex]",
        "hash = %s" % d1["hash"],
        "match_first_index = %d" % (d1["first"] + 1),
        "match_index_count = %d" % d1["count"],
        "",
        "[TextureOverride_Miss_UnknownHash]",
        "hash = deadbeef",
        "match_first_index = 0",
        "match_index_count = 3",
        "",
    ])
    path = os.path.join(folder, "mod.ini")
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return path


def main() -> int:
    c = Checks("mcp_test")
    out_dir = tempfile.mkdtemp(prefix="efvk-mcp-")
    with Sandbox(threads=2) as sb:
        mcp = McpProcess(out_dir)
        try:
            init = mcp.send("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                                           "clientInfo": {"name": "mcp_test", "version": "0"}})
            c.equal(init["result"]["serverInfo"]["name"], "efmi-vk", "initialize")
            mcp.send("notifications/initialized", notify=True)
            tools = mcp.send("tools/list")["result"]["tools"]
            c.equal(len(tools), 9, "nine tools")

            err, status = mcp.tool("efvk_status", pid=sb.pid)
            c.check(not err and status["status"]["pid"] == sb.pid, "efvk_status", status)
            c.check(sb.pid in status["probes"], "efvk_status lists the probe")

            err, text = mcp.tool("efvk_list_draws")
            c.check(err and "efvk_capture_frame" in text, "listing draws before a capture is an error")

            err, summary = mcp.tool("efvk_capture_frame", pid=sb.pid)
            c.check(not err, "efvk_capture_frame", summary)
            if not err:
                c.equal(summary["draw_count"], 5, "capture summary: draws")
                c.equal(summary["stats"]["secondary_command_buffers"], 2, "capture summary: secondary command buffers")
                c.equal(len(summary["draws_by_thread"]), 2, "capture summary: draws per thread")
                c.equal(summary["index_buffers"], 2, "capture summary: index buffers")
                c.equal(summary["distinct_region_hashes"], 5, "capture summary: distinct hashes")
                c.equal(summary["hash_sources"], {"shadow": 3, "mapped": 2}, "capture summary: hash sources")

            err, listed = mcp.tool("efvk_list_draws", kind="indexed", limit=2)
            c.check(not err and listed["total"] == 5 and len(listed["draws"]) == 2, "efvk_list_draws with a limit", listed)

            want = sb.expected["draws"][3]
            err, found = mcp.tool("efvk_find_hash", hash=want["hash"])
            c.check(not err and found["total"] == 1, "efvk_find_hash", found)
            n = found["draws"][0]["n"] if not err and found["draws"] else 0
            c.equal(n, 3, "the hash belongs to the fourth draw")

            err, draw = mcp.tool("efvk_get_draw", n=n)
            c.check(not err and draw["ib"]["offset"] == want["bind_offset"], "efvk_get_draw", draw)
            if not err:
                c.equal(draw["pipeline_info"]["bindings"][0]["stride"], 8, "efvk_get_draw adds the vertex layout")
                c.equal(draw["sets"][0]["dyn"], [want["dyn"]], "efvk_get_draw shows the dynamic offset")
                ib_id = draw["ib"]["buf"]

                err, bufs = mcp.tool("efvk_list_buffers", pid=sb.pid, usage=0x40)
                c.check(not err and bufs["total"] == 2, "efvk_list_buffers", bufs)

                err, data = mcp.tool("efvk_read_buffer", pid=sb.pid, id=ib_id, offset=16, size=36, format="u32", save=True)
                c.check(not err and data["values"] == [10, 11, 12, 10, 12, 13, 14, 15, 16], "efvk_read_buffer as u32", data)
                if not err:
                    c.check(os.path.isfile(data["saved"]) and os.path.getsize(data["saved"]) == 36, "the buffer was saved")
                    c.check(os.path.dirname(data["saved"]) == out_dir, "saved into the output directory")
                err, data = mcp.tool("efvk_read_buffer", pid=sb.pid, id=ib_id, offset=16, size=4)
                c.check(not err and data["hex"] == "0a000000", "efvk_read_buffer as hex", data)
                err, text = mcp.tool("efvk_read_buffer", pid=sb.pid, id=ib_id, format="nope")
                c.check(err, "an unknown format is an error")

                err, pipe = mcp.tool("efvk_pipeline", pid=sb.pid, id=draw["pipeline"])
                c.check(not err and pipe["attributes"][0]["location"] == 0, "efvk_pipeline", pipe)

            mod = write_mod(out_dir, sb.expected)
            err, report = mcp.tool("efvk_match_mod", path=mod)
            c.check(not err, "efvk_match_mod", report)
            if not err:
                c.equal((report["overrides"], report["matched"]), (4, 2), "two of four overrides match")
                by_name = {s["section"]: s["draws"] for s in report["files"][0]["sections"]}
                c.equal(by_name["TextureOverride_Hit_Mesh0"], [0], "exact match")
                c.equal(by_name["TextureOverride_Hit_Mesh3_HashOnly"], [3], "match by hash alone")
                c.equal(by_name["TextureOverride_Miss_WrongFirstIndex"], [], "wrong first index does not match")
                c.equal(by_name["TextureOverride_Miss_UnknownHash"], [], "unknown hash does not match")
            err, report = mcp.tool("efvk_match_mod", path=out_dir)
            c.check(not err and report["ini_files"] == 1, "efvk_match_mod on a folder", report)
            err, text = mcp.tool("efvk_match_mod", path=os.path.join(out_dir, "missing.ini"))
            c.check(err, "a missing mod path is an error")

            err, text = mcp.tool("efvk_status", pid=4)
            c.check(err, "a pid without a probe is an error")
            err, status = mcp.tool("efvk_status", pid=sb.pid)
            c.check(not err, "the server recovers after a failed connection", status)
        finally:
            mcp.close()
    return c.finish()


if __name__ == "__main__":
    sys.exit(main())
