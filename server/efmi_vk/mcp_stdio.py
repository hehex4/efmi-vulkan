"""A stdio MCP server over the tools in ``tools.py``. Standard library only.

Messages are JSON-RPC 2.0, one JSON document per line, as the MCP stdio transport defines.
"""

from __future__ import annotations

import argparse
import json
import sys

from . import __version__
from .tools import TOOLS, Session, ToolError

PROTOCOL_VERSION = "2024-11-05"


def _result(msg_id, result) -> dict:
    return {"jsonrpc": "2.0", "id": msg_id, "result": result}


def _error(msg_id, code: int, message: str) -> dict:
    return {"jsonrpc": "2.0", "id": msg_id, "error": {"code": code, "message": message}}


def handle(session: Session, msg: dict):
    """Returns the response for one request, or None for a notification."""
    method = msg.get("method")
    msg_id = msg.get("id")
    params = msg.get("params") or {}
    is_notification = "id" not in msg

    if method == "initialize":
        return _result(msg_id, {
            "protocolVersion": params.get("protocolVersion") or PROTOCOL_VERSION,
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "efmi-vk", "version": __version__},
        })
    if is_notification:
        return None
    if method == "ping":
        return _result(msg_id, {})
    if method == "tools/list":
        return _result(msg_id, {"tools": TOOLS})
    if method == "tools/call":
        name = params.get("name")
        try:
            value = session.call(name, params.get("arguments"))
            text = json.dumps(value, ensure_ascii=True, separators=(",", ":"))
            return _result(msg_id, {"content": [{"type": "text", "text": text}], "isError": False})
        except ToolError as e:
            return _result(msg_id, {"content": [{"type": "text", "text": str(e)}], "isError": True})
        except Exception as e:  # a bug in a tool must not take the server down
            return _result(msg_id, {"content": [{"type": "text", "text": "internal error: %r" % (e,)}], "isError": True})
    return _error(msg_id, -32601, "method not found: %s" % method)


def serve(stdin, stdout, session: Session) -> None:
    for raw in stdin:
        line = raw.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except ValueError:
            reply = _error(None, -32700, "parse error")
        else:
            reply = handle(session, msg) if isinstance(msg, dict) else _error(None, -32600, "invalid request")
        if reply is not None:
            stdout.write(json.dumps(reply, ensure_ascii=True, separators=(",", ":")) + "\n")
            stdout.flush()


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="EFMI Vulkan probe MCP server (stdio)")
    ap.add_argument("--out", help="directory for saved buffers (default: captures/ in the project)")
    args = ap.parse_args(argv)
    session = Session(out_dir=args.out)
    stdin = open(sys.stdin.fileno(), "r", encoding="utf-8", newline="\n", closefd=False)
    stdout = open(sys.stdout.fileno(), "w", encoding="utf-8", newline="\n", closefd=False)
    try:
        serve(stdin, stdout, session)
    finally:
        session.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
