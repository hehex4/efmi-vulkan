"""Check D-079: what the original EFMIv1 Core reads off a vertex shader, derived from its SPIR-V.

The Core decides by the bound vertex resources whether it may replace a draw: Component_ReadConfig
takes the first of vs-cb2, vs-cb1, vs-cb3 bound with 4096 vectors (the per-instance window), and
MergedSkeleton_DetectBoneDataSource the rolling bone buffer when vs-t0 is a 16-byte structured
buffer and such a window exists. The layer has no DX11 bindings; it applies the same rules to the
registers the SPIR-V gives by D-076 (server/efmi_vk/source_slots.py core_facts).

`--refresh` reads the 2026-10-19 catalogue scenes: for every game draw of the DX11 frames the facts
the Core computed from what was bound, and the paired Vulkan vertex module's declarations; it writes
docs/core-facts-truth.json (metadata only). Without it the stored truth is checked: every vertex
shader's derived facts must equal the facts observed for it under DX11. Read-only otherwise.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))
from efmi_vk.source_slots import core_facts, declarations  # noqa: E402

TRUTH = ROOT / "docs" / "core-facts-truth.json"
CAPTURES = Path(r"F:\efmi vulkan\captures\catalogue-20261019")
CATALOGUE = Path(r"F:\efmi vulkan\build\catalogue\catalogue-20261019.json")
VS = 0


def dx_facts(frame: dict, draw: dict) -> tuple[int, bool] | None:
    """The Core's facts for one DX11 draw from what was bound, or None for a draw without a vertex shader."""
    sh = draw.get("sh") or []
    if not sh or sh[VS] < 0:
        return None
    resources = {r["id"]: r for r in frame["resources"]}
    sizes = {}
    for stage, slot, res, _first, num, _snap in draw.get("cb", []):
        if stage == VS:
            sizes[slot] = num * 16 if num else resources.get(res, {}).get("size", 0)
    slot = next((s for s in (2, 1, 3) if sizes.get(s) == 4096 * 16), -1)
    t0 = next((res for stage, s, res in draw.get("srv", []) if stage == VS and s == 0), None)
    rolling = t0 is not None and resources.get(t0, {}).get("stride") == 16 and slot != -1
    return slot, rolling


def refresh() -> dict:
    catalogue = json.loads(CATALOGUE.read_bytes())
    pairs: dict[str, set] = {}
    for program in catalogue["programs"].values():
        for dx in program["dx_vs"]:
            pairs.setdefault(dx, set()).add(program["vk_vs"])
    observed: dict[str, Counter] = {}
    for path in sorted((CAPTURES / "dx11").glob("char*.json")):
        if path.name.count(".") > 1:
            continue
        frame = json.loads(path.read_bytes())
        hashes = [s["hash"] for s in frame["shaders"]]
        for draw in frame["draws"]:
            if draw.get("o") != "game" or draw.get("t", 99) >= 8:
                continue
            facts = dx_facts(frame, draw)
            if facts is None:
                continue
            for vk in pairs.get(hashes[draw["sh"][VS]], ()):
                observed.setdefault(vk, Counter())[facts] += 1
    files = {}
    for bundle in sorted((CAPTURES / "vulkan").glob("vk*")):
        listing = bundle / "shaders.json"
        if listing.exists():
            for s in json.loads(listing.read_text(encoding="utf-8")):
                if s.get("hash") and s.get("file"):
                    files.setdefault(s["hash"], bundle / s["file"])
    shaders = {}
    for vk, seen in sorted(observed.items()):
        if vk not in files:
            continue
        shaders[vk] = {"declarations": declarations(files[vk].read_bytes()),
                       "observed": [[slot, rolling, n] for (slot, rolling), n in sorted(seen.items())]}
    raw = CATALOGUE.read_bytes()
    return {"schema": 1,
            "scope": "Vertex shaders of the 2026-10-19 catalogue: the facts the original Core computed from the DX11 bindings "
                     "(instance window cb slot, rolling vs-t0 bones) and the paired Vulkan module's declarations",
            "catalogue": {"name": CATALOGUE.name, "sha256": hashlib.sha256(raw).hexdigest()},
            "shaders": shaders}


def check(truth: dict) -> tuple[Counter, list[str]]:
    """The decision that matters is the rolling-bone path (the only one the renderer replaces): it
    must agree, and where it holds the instance-window slot must agree too and sit where the layer
    reads the instance entry. Where neither side has rolling bones the Core leaves the mesh to the
    game either way; a different slot there (the Core reads the bound view's size, the module gives
    the declared block) is counted, not failed."""
    stats, problems = Counter(), []
    for vk, entry in truth["shaders"].items():
        got = core_facts(entry["declarations"])
        rolling_seen = {rolling for _slot, rolling, _ in entry["observed"]}
        slots_seen = {slot for slot, _rolling, _ in entry["observed"]}
        if len(rolling_seen) != 1:
            problems.append("%s: DX11 draws disagree on the bone source %s" % (vk, sorted(entry["observed"])))
            continue
        rolling, = rolling_seen
        if got["rolling"] != rolling:
            stats["decision differs"] += 1
            problems.append("%s: derived rolling %s, DX11 rolling %s" % (vk, got["rolling"], rolling))
            continue
        if rolling:
            if slots_seen != {got["slot"]}:
                problems.append("%s: derived slot %d, DX11 slots %s" % (vk, got["slot"], sorted(slots_seen)))
            elif got["config"] != [2, 0]:
                problems.append("%s: the window is at %s, the layer reads the instance entry at set 2, binding 0" % (vk, got["config"]))
            else:
                stats["rolling bones, slot and window agree"] += 1
        else:
            stats["no replacement on both sides"] += 1
            if slots_seen != {got["slot"]}:
                stats["of those, window slot differs (bound view vs declared block)"] += 1
    return stats, problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--refresh", action="store_true", help="rebuild the truth file from the catalogue captures")
    args = ap.parse_args()
    if args.refresh:
        truth = refresh()
        lines = ["{"] + ['"%s": %s,' % (k, json.dumps(v, separators=(",", ":"))) for k, v in truth.items() if k != "shaders"]
        lines.append('"shaders": {')
        items = list(truth["shaders"].items())
        lines += ['"%s": %s%s' % (k, json.dumps(v, separators=(",", ":")), "," if i + 1 < len(items) else "") for i, (k, v) in enumerate(items)]
        lines += ["}", "}"]
        TRUTH.write_text("\n".join(lines) + "\n", encoding="ascii")
        print("written %s: %d vertex shaders" % (TRUTH, len(items)))
    truth = json.loads(TRUTH.read_text(encoding="ascii"))
    stats, problems = check(truth)
    for p in problems[:20]:
        print("  " + p)
    print("core facts: %d vertex shaders; %s" % (len(truth["shaders"]), ", ".join("%s %d" % kv for kv in sorted(stats.items()))))
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
