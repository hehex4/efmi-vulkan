"""Named-pipe client of the EFMI Vulkan probe.

The layer serves ``\\\\.\\pipe\\efmi-vk-<PID>``. One message in either direction is
``[u32 header_len][u32 payload_len][header JSON][payload]``, little endian.
"""

from __future__ import annotations

import json
import os
import struct
import time

PIPE_DIR = "\\\\.\\pipe\\"
PIPE_PREFIX = "efmi-vk-"


class ProbeError(RuntimeError):
    """The probe could not be reached, or it answered with ok=false."""


def list_probes() -> list[int]:
    """Process ids that currently serve a probe pipe."""
    try:
        names = os.listdir(PIPE_DIR)
    except OSError:
        return []
    pids = []
    for name in names:
        if name.startswith(PIPE_PREFIX):
            tail = name[len(PIPE_PREFIX):]
            if tail.isdigit():
                pids.append(int(tail))
    return sorted(pids)


class ProbeClient:
    def __init__(self, pid: int | None = None, connect_timeout: float = 5.0):
        if pid is None:
            pids = list_probes()
            if not pids:
                raise ProbeError("no process with the EFMI Vulkan layer in probe mode was found")
            if len(pids) > 1:
                raise ProbeError("several probes are running (%s); pass a pid" % ", ".join(map(str, pids)))
            pid = pids[0]
        self.pid = pid
        self._f = None
        path = PIPE_DIR + PIPE_PREFIX + str(pid)
        deadline = time.monotonic() + connect_timeout
        last = None
        while True:
            try:
                self._f = open(path, "r+b", buffering=0)
                break
            except OSError as e:  # not there yet, or busy with another client
                last = e
                if time.monotonic() >= deadline:
                    raise ProbeError("cannot open %s: %s" % (path, last)) from None
                time.sleep(0.05)

    def close(self) -> None:
        if self._f is not None:
            try:
                self._f.close()
            finally:
                self._f = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _read(self, n: int) -> bytes:
        chunks = []
        while n:
            chunk = self._f.read(n)
            if not chunk:
                raise ProbeError("the probe closed the pipe")
            chunks.append(chunk)
            n -= len(chunk)
        return b"".join(chunks)

    def request(self, cmd: str, **params):
        """Sends one command. Returns ``(result, payload)``; raises ProbeError on ok=false."""
        if self._f is None:
            raise ProbeError("the client is closed")
        header = dict(params)
        header["cmd"] = cmd
        raw = json.dumps(header, separators=(",", ":")).encode("utf-8")
        try:
            self._f.write(struct.pack("<II", len(raw), 0) + raw)
            header_len, payload_len = struct.unpack("<II", self._read(8))
            reply = json.loads(self._read(header_len).decode("utf-8"))
            payload = self._read(payload_len) if payload_len else b""
        except OSError as e:
            raise ProbeError("pipe error: %s" % e) from None
        if not reply.get("ok"):
            raise ProbeError(reply.get("error", "the probe reported a failure"))
        return reply.get("result"), payload

    # Convenience wrappers ---------------------------------------------------

    def status(self) -> dict:
        return self.request("status")[0]

    def capture(self, timeout_ms: int = 5000, draws: bool = True, descriptor_budget: int | None = None,
                snapshot: dict | None = None) -> dict:
        """One complete frame. ``descriptor_budget`` bounds, in bytes, the descriptor contents the
        layer keeps for the frame (its default is 64 MiB); 0 leaves the contents out.

        ``snapshot`` (layer 0.6.0) has the layer keep what selected draws read from buffers as it is
        when their command buffer is submitted, and compare it with a read taken when the draw was
        recorded: {"hashes": [region hashes] or "all": True, "max_range": largest uniform range kept
        (default 64 KiB), "budget": bytes the capture may keep (default 32 MiB), "follow": {"set",
        "binding", "offset", "count", "stride", "bytes", "entry"}}. ``follow`` reads ``count`` 32-bit
        values at byte ``offset`` of the uniform range at (set, binding) and keeps ``bytes`` at
        value * stride of every storage range of the draw the window fits in; ``entry`` bytes at the
        start of that uniform range are also kept and compared on their own. Each selected draw gets
        a "snapshot" list; the bytes are fetched with :meth:`snapshot` while the capture is the latest.
        """
        params = {"timeout_ms": timeout_ms, "draws": 1 if draws else 0}
        if descriptor_budget is not None:
            params["descriptor_budget"] = int(descriptor_budget)
        if snapshot is not None:
            params["snapshot"] = 1
            if snapshot.get("all"):
                params["snapshot_all"] = 1
            hashes = snapshot.get("hashes") or []
            if hashes:
                params["snapshot_hashes"] = ",".join("%08x" % (int(h, 16) if isinstance(h, str) else int(h)) for h in hashes)
            for key in ("max_range", "budget"):
                if snapshot.get(key) is not None:
                    params["snapshot_" + key] = int(snapshot[key])
            follow = snapshot.get("follow")
            if follow:
                params["follow"] = 1
                for key in ("set", "binding", "offset", "count", "stride", "bytes", "entry"):
                    if follow.get(key) is not None:
                        params["follow_" + key] = int(follow[key])
        return self.request("capture", **params)[0]

    def snapshot(self, seq: int, snapshot_id: int):
        """(description, bytes) of one kept range of the latest capture, by the "id" its draws list."""
        return self.request("snapshot", seq=seq, id=snapshot_id)

    def buffers(self, usage: int = 0, min_size: int = 0, limit: int = 1000) -> dict:
        return self.request("buffers", usage=usage, min_size=min_size, limit=limit)[0]

    def read_buffer(self, buffer_id: int, offset: int = 0, size: int = 0):
        return self.request("read_buffer", id=buffer_id, offset=offset, size=size)

    def hash(self, buffer_id: int, offset: int = 0, size: int = 0) -> dict:
        return self.request("hash", id=buffer_id, offset=offset, size=size)[0]

    def pipeline(self, pipeline_id: int) -> dict:
        return self.request("pipeline", id=pipeline_id)[0]

    def api_usage(self) -> dict:
        return self.request("api_usage")[0]

    def shader(self, shader_id: int, code: bool = True):
        return self.request("shader", id=shader_id, code=int(code))
