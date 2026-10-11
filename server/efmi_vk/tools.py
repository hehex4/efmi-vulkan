"""The tools an agent can call, independent of the transport that exposes them."""

from __future__ import annotations

import os
import struct

from . import __version__
from .mods import find_ini_files, match_overrides, parse_overrides
from .probe_client import ProbeClient, ProbeError, list_probes


class ToolError(RuntimeError):
    """A tool could not do what was asked; the message is meant for the caller."""


def _schema(properties: dict, required: list[str] | None = None) -> dict:
    return {"type": "object", "properties": properties, "required": required or [], "additionalProperties": False}


_PID = {"pid": {"type": "integer", "description": "Process id of the target; may be omitted when exactly one probe is running."}}

TOOLS = [
    {
        "name": "efvk_status",
        "description": "State of the EFMI Vulkan probe: process, device, frame counter, the statistics of the last "
                       "frame (submits, command buffers, recording threads, render passes, draws) and their maxima. "
                       "Also lists every process that currently serves a probe.",
        "inputSchema": _schema(_PID),
    },
    {
        "name": "efvk_capture_frame",
        "description": "Records every draw of the next complete frame and keeps it for the other tools. Returns a "
                       "summary: frame statistics, draws per thread / command buffer / render pass, and how many "
                       "index regions could be hashed from mapped memory, from the shadow copy, or not at all.",
        "inputSchema": _schema({**_PID, "timeout_ms": {"type": "integer", "description": "Default 5000."}}),
    },
    {
        "name": "efvk_list_draws",
        "description": "Draws of the captured frame, filtered. Each row: n, kind, thread, command buffer, render pass, "
                       "pipeline, index buffer id / bind offset / index size, first index, index count, instance "
                       "count, region hash.",
        "inputSchema": _schema({
            "kind": {"type": "string", "description": "draw, indexed, indirect, indexed_indirect, dispatch, dispatch_indirect"},
            "hash": {"type": "string", "description": "Region hash, 8 hex digits."},
            "tid": {"type": "integer"},
            "cb": {"type": "integer"},
            "rp": {"type": "integer"},
            "ib": {"type": "integer", "description": "Index buffer id."},
            "min_count": {"type": "integer", "description": "Smallest index or vertex count."},
            "offset": {"type": "integer"},
            "limit": {"type": "integer", "description": "Default 50."},
        }),
    },
    {
        "name": "efvk_get_draw",
        "description": "Everything recorded for one draw of the captured frame: bindings of index and vertex buffers, "
                       "descriptor sets with their dynamic offsets, pipeline vertex layout.",
        "inputSchema": _schema({"n": {"type": "integer"}}, ["n"]),
    },
    {
        "name": "efvk_find_hash",
        "description": "Draws of the captured frame whose index region has the given CRC-32C hash (the hash a mod INI "
                       "puts into a TextureOverride).",
        "inputSchema": _schema({"hash": {"type": "string"}}, ["hash"]),
    },
    {
        "name": "efvk_list_buffers",
        "description": "Buffers the layer tracks: id, size, usage flags, whether the memory is mapped, how many bytes "
                       "the shadow copy knows, unknown writes.",
        "inputSchema": _schema({
            **_PID,
            "usage": {"type": "integer", "description": "VkBufferUsageFlags mask; 0x40 index, 0x80 vertex, 0x10 uniform, 0x20 storage."},
            "min_size": {"type": "integer"},
            "limit": {"type": "integer", "description": "Default 100."},
        }),
    },
    {
        "name": "efvk_read_buffer",
        "description": "Reads bytes of a buffer as they are now, from the application's mapping or from the shadow "
                       "copy. Formats: hex, u16, u32, f32. With save=true the bytes also go to a file.",
        "inputSchema": _schema({
            **_PID,
            "id": {"type": "integer"},
            "offset": {"type": "integer"},
            "size": {"type": "integer", "description": "Default 256; at most 4096 are returned inline."},
            "format": {"type": "string", "description": "hex (default), u16, u32 or f32."},
            "save": {"type": "boolean"},
        }, ["id"]),
    },
    {
        "name": "efvk_pipeline",
        "description": "Vertex input bindings and attributes, stages and topology of a pipeline id.",
        "inputSchema": _schema({**_PID, "id": {"type": "integer"}}, ["id"]),
    },
    {
        "name": "efvk_match_mod",
        "description": "Reads the TextureOverride sections of a mod INI (or of every INI below a folder) and reports "
                       "which of their index buffer hashes occur in the captured frame, with first index and index "
                       "count taken into account. The mod is only read.",
        "inputSchema": _schema({"path": {"type": "string"}}, ["path"]),
    },
]

_FORMATS = {"u16": ("<%dH", 2), "u32": ("<%dI", 4), "f32": ("<%df", 4)}
_INLINE_LIMIT = 4096


def _row(d: dict) -> dict:
    ib = d.get("ib") or {}
    return {
        "n": d["n"], "kind": d["kind"], "tid": d["tid"], "cb": d["cb"], "rp": d["rp"], "pipeline": d["pipeline"],
        "ib": ib.get("buf"), "ib_offset": ib.get("offset"), "index_size": ib.get("index_size"),
        "first": d["first"], "count": d["count"], "instances": d["instances"],
        "hash": ib.get("hash"), "hash_src": ib.get("src"),
    }


def _tally(items) -> dict:
    out: dict = {}
    for k in items:
        out[str(k)] = out.get(str(k), 0) + 1
    return out


class Session:
    """Keeps the pipe connection and the last captured frame between tool calls."""

    def __init__(self, out_dir: str | None = None):
        self.out_dir = out_dir or os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "captures")
        self._client: ProbeClient | None = None
        self.capture: dict | None = None
        self.capture_pid: int | None = None

    def close(self) -> None:
        if self._client is not None:
            self._client.close()
            self._client = None

    def _probe(self, pid=None) -> ProbeClient:
        if self._client is not None and (pid is None or pid == self._client.pid):
            return self._client
        self.close()
        self._client = ProbeClient(pid)
        return self._client

    def _request(self, pid, cmd: str, **params):
        for attempt in (0, 1):
            reused = self._client is not None and (pid is None or pid == self._client.pid)
            try:
                client = self._probe(pid)
            except ProbeError as e:
                raise ToolError(str(e)) from None
            try:
                return client.request(cmd, **params)
            except ProbeError as e:
                broken = "pipe" in str(e) or "closed" in str(e)
                if broken:
                    self.close()
                # A connection kept from an earlier call may have died with its process; reconnect once.
                if not (broken and reused and attempt == 0):
                    raise ToolError(str(e)) from None
        raise ToolError("the probe could not be reached")

    def _draws(self) -> list[dict]:
        if self.capture is None:
            raise ToolError("no frame has been captured yet; call efvk_capture_frame first")
        return self.capture["draws"]

    # Tools -------------------------------------------------------------------

    def efvk_status(self, pid=None) -> dict:
        probes = list_probes()
        if not probes:
            return {"server_version": __version__, "probes": [], "note": "no process with the layer in probe mode is running"}
        result, _ = self._request(pid, "status")
        return {"server_version": __version__, "probes": probes, "status": result}

    def efvk_capture_frame(self, pid=None, timeout_ms: int = 5000) -> dict:
        result, _ = self._request(pid, "capture", timeout_ms=int(timeout_ms), draws=1)
        self.capture = result
        self.capture_pid = self._client.pid if self._client else pid
        draws = result["draws"]
        indexed = [d for d in draws if d.get("ib")]
        return {
            "pid": self.capture_pid,
            "frame": result["frame"],
            "stats": result["stats"],
            "draw_count": result["draw_count"],
            "draws_by_thread": _tally(d["tid"] for d in draws),
            "draws_by_command_buffer": _tally(d["cb"] for d in draws),
            "draws_by_render_pass": _tally(d["rp"] for d in draws),
            "draws_by_kind": _tally(d["kind"] for d in draws),
            "index_buffers": len({d["ib"]["buf"] for d in indexed}),
            "distinct_region_hashes": len({d["ib"]["hash"] for d in indexed if d["ib"].get("hash")}),
            "hash_sources": _tally(d["ib"]["src"] for d in indexed),
        }

    def efvk_list_draws(self, kind=None, hash=None, tid=None, cb=None, rp=None, ib=None, min_count=None,
                        offset: int = 0, limit: int = 50) -> dict:
        rows = []
        wanted_hash = hash.lower() if hash else None
        for d in self._draws():
            dib = d.get("ib") or {}
            if kind is not None and d["kind"] != kind:
                continue
            if wanted_hash is not None and dib.get("hash") != wanted_hash:
                continue
            if tid is not None and d["tid"] != tid:
                continue
            if cb is not None and d["cb"] != cb:
                continue
            if rp is not None and d["rp"] != rp:
                continue
            if ib is not None and dib.get("buf") != ib:
                continue
            if min_count is not None and d["count"] < min_count:
                continue
            rows.append(_row(d))
        return {"frame": self.capture["frame"], "total": len(rows), "offset": offset,
                "draws": rows[offset:offset + max(0, int(limit))]}

    def efvk_get_draw(self, n: int) -> dict:
        draws = self._draws()
        if not 0 <= n < len(draws):
            raise ToolError("draw %d does not exist; the frame has %d" % (n, len(draws)))
        d = dict(draws[n])
        if d.get("pipeline"):
            try:
                d["pipeline_info"], _ = self._request(self.capture_pid, "pipeline", id=d["pipeline"])
            except ToolError as e:
                d["pipeline_info"] = {"error": str(e)}
        return d

    def efvk_find_hash(self, hash: str) -> dict:
        return self.efvk_list_draws(hash=hash, limit=200)

    def efvk_list_buffers(self, pid=None, usage: int = 0, min_size: int = 0, limit: int = 100) -> dict:
        result, _ = self._request(pid, "buffers", usage=int(usage), min_size=int(min_size), limit=int(limit))
        return result

    def efvk_read_buffer(self, id: int, pid=None, offset: int = 0, size: int = 256, format: str = "hex",
                         save: bool = False) -> dict:
        if format != "hex" and format not in _FORMATS:
            raise ToolError("unknown format %r; use hex, u16, u32 or f32" % format)
        info, data = self._request(pid, "read_buffer", id=int(id), offset=int(offset), size=int(size))
        out = dict(info)
        shown = data[:_INLINE_LIMIT]
        out["truncated"] = len(data) > len(shown)
        if format == "hex":
            out["hex"] = shown.hex()
        else:
            fmt, width = _FORMATS[format]
            usable = len(shown) - len(shown) % width
            values = struct.unpack(fmt % (usable // width), shown[:usable])
            out["values"] = [round(v, 6) for v in values] if format == "f32" else list(values)
        if save:
            os.makedirs(self.out_dir, exist_ok=True)
            path = os.path.join(self.out_dir, "buffer-%d-%d-%d.bin" % (info["id"], info["offset"], info["size"]))
            with open(path, "wb") as f:
                f.write(data)
            out["saved"] = path
        return out

    def efvk_pipeline(self, id: int, pid=None) -> dict:
        result, _ = self._request(pid, "pipeline", id=int(id))
        return result

    def efvk_match_mod(self, path: str) -> dict:
        draws = self._draws()
        if not os.path.exists(path):
            raise ToolError("%s does not exist" % path)
        files = find_ini_files(path)
        report = []
        total = matched = 0
        for ini in files:
            overrides = parse_overrides(ini)
            if not overrides:
                continue
            results = match_overrides(overrides, draws)
            total += len(results)
            matched += sum(1 for r in results if r["matched"])
            report.append({
                "ini": ini,
                "overrides": len(results),
                "matched": sum(1 for r in results if r["matched"]),
                "sections": [
                    {"section": r["section"], "hash": r["hash"], "match_first_index": r["match_first_index"],
                     "match_index_count": r["match_index_count"], "draws": r["draws"][:16]}
                    for r in results
                ],
            })
        return {"frame": self.capture["frame"], "ini_files": len(report), "overrides": total, "matched": matched,
                "files": report}

    # Dispatch ----------------------------------------------------------------

    def call(self, name: str, arguments: dict | None) -> dict:
        if not any(t["name"] == name for t in TOOLS):
            raise ToolError("unknown tool %r" % name)
        try:
            return getattr(self, name)(**(arguments or {}))
        except TypeError as e:
            raise ToolError("bad arguments for %s: %s" % (name, e)) from None
        except ProbeError as e:
            raise ToolError(str(e)) from None
