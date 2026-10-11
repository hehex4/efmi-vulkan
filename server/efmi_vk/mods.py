"""Read-only look at mod INI files: the index buffer overrides they match on."""

from __future__ import annotations

import os
import re

_SECTION = re.compile(r"^\s*\[([^\]]+)\]\s*$")
_ASSIGN = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")


def _int(text: str):
    try:
        return int(text, 0)
    except ValueError:
        return None


def parse_overrides(path: str) -> list[dict]:
    """TextureOverride sections of one INI with their ``hash`` and index match keys.

    Only the three match keys are read; conditions and command lists are ignored.
    """
    overrides = []
    current = None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for lineno, line in enumerate(f, 1):
            stripped = line.strip()
            if not stripped or stripped.startswith(";"):
                continue
            m = _SECTION.match(line)
            if m:
                name = m.group(1).strip()
                current = None
                if name.lower().startswith("textureoverride"):
                    current = {"section": name, "line": lineno, "hash": None,
                               "match_first_index": None, "match_index_count": None}
                    overrides.append(current)
                continue
            if current is None:
                continue
            m = _ASSIGN.match(line)
            if not m:
                continue
            key, value = m.group(1).lower(), m.group(2)
            if key == "hash" and current["hash"] is None:
                current["hash"] = value.lower()
            elif key == "match_first_index" and current["match_first_index"] is None:
                current["match_first_index"] = _int(value)
            elif key == "match_index_count" and current["match_index_count"] is None:
                current["match_index_count"] = _int(value)
    return [o for o in overrides if o["hash"]]


def find_ini_files(path: str) -> list[str]:
    """``path`` itself when it is a file, otherwise every enabled INI below the folder."""
    if os.path.isfile(path):
        return [path]
    found = []
    for base, dirs, files in os.walk(path):
        dirs[:] = [d for d in dirs if not d.lower().startswith("disabled")]
        for name in files:
            if name.lower().endswith(".ini") and not name.lower().startswith("disabled"):
                found.append(os.path.join(base, name))
    return sorted(found)


def match_overrides(overrides: list[dict], draws: list[dict]) -> list[dict]:
    """For every override, the draws of a captured frame it would hit.

    An override hits an indexed draw when the region hash equals ``hash`` and, where
    given, first index and index count equal the match keys.
    """
    by_hash: dict[str, list[dict]] = {}
    for d in draws:
        ib = d.get("ib")
        if ib and ib.get("hash"):
            by_hash.setdefault(ib["hash"], []).append(d)
    results = []
    for o in overrides:
        hits = []
        for d in by_hash.get(o["hash"], []):
            if o["match_first_index"] is not None and d["first"] != o["match_first_index"]:
                continue
            if o["match_index_count"] is not None and d["count"] != o["match_index_count"]:
                continue
            hits.append(d["n"])
        results.append({**o, "draws": hits, "matched": bool(hits)})
    return results
