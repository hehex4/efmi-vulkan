"""The shared shader catalogue (T-030) from scenes captured under both APIs.

    shader_catalogue.py build --summary A.summary.json [--summary B.summary.json ...] --out <catalogue.json>
                              [--check docs/arcane-texture-slots.json]

Each summary is written by ``dx11_baseline.py summarize`` for one scene captured under DX11 and
under Vulkan. Without ``--mod`` it pairs every object both frames draw: objects by the region hash
of their index data, which is the same number under both APIs, their draws by order, and what a
draw binds by content (CRC-32C of mip 0) or, weaker, by look (shape, size, mips, format).

The catalogue is keyed by Vulkan program (vertex and fragment shader hash), because binding
numbers belong to a program. For every DX11 slot a paired draw declares and binds (textures,
constant buffer windows, buffers of every stage) it lists the Vulkan set and binding it was found
in, how often, and by which evidence. A slot seen in more than one place is a conflict and is
reported, never resolved by choice. Slots bound under DX11 without a Vulkan counterpart and
groups that only look alike are counted, not placed.

Only metadata goes into the catalogue: hashes, slot names, binding numbers and counts. Shader
byte code stays in the captures it came from.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
KINDS = (("textures", "texture"), ("constant_buffers", "constant buffer"), ("buffers", "buffer"))


def program_key(pair: dict) -> str:
    vs = pair.get("vk_vs") or "pipeline-%s" % pair.get("vk_pipeline")
    return "%s/%s" % (vs, pair.get("vk_fs") or "-")


def slots_of(item) -> list[str]:
    """A constant buffer window can stand for several DX11 slots bound to the same range."""
    return list(item) if isinstance(item, list) else [item]


def collect(summaries: list[Path]) -> tuple[dict, list[dict]]:
    programs: dict[str, dict] = {}
    sources = []
    for path in summaries:
        raw = path.read_bytes()
        summary = json.loads(raw)
        pairs = [p for o in summary.get("objects", []) if o.get("kind") == "game" for p in o.get("pairs", [])]
        sources.append({"path": str(path), "sha256": hashlib.sha256(raw).hexdigest(),
                        "dx11_frame": (summary.get("dx11") or {}).get("frame"),
                        "vulkan_frame": (summary.get("vulkan") or {}).get("frame"),
                        "objects_paired": sum(1 for o in summary.get("objects", []) if o.get("kind") == "game" and o.get("pairs")),
                        "objects_unpaired": sum(1 for o in summary.get("objects", []) if o.get("kind") == "game" and not o.get("pairs")),
                        "draw_pairs": len(pairs)})
        for pair in pairs:
            p = programs.setdefault(program_key(pair), {
                "vk_vs": pair.get("vk_vs"), "vk_fs": pair.get("vk_fs"), "dx_vs": Counter(), "dx_ps": Counter(),
                "pipelines": set(), "draws": 0, "sources": set(),
                "slots": {kind: defaultdict(Counter) for kind, _ in KINDS},
                "evidence": {kind: defaultdict(Counter) for kind, _ in KINDS},
                "dx_only": {kind: Counter() for kind, _ in KINDS},
                "ambiguous": {kind: Counter() for kind, _ in KINDS},
                "vk_only": {kind: Counter() for kind, _ in KINDS}})
            p["draws"] += 1
            p["sources"].add(len(sources) - 1)
            p["pipelines"].add(pair.get("vk_pipeline"))
            p["dx_vs"][pair.get("dx_vs")] += 1
            p["dx_ps"][pair.get("dx_ps")] += 1
            for kind, _ in KINDS:
                match = pair.get(kind) or {}
                for hit in match.get("pairs", []):
                    if hit.get("dx_origin") not in (None, "game"):
                        continue          # bound by XXMI, not by the game
                    where = tuple(hit["vk"])
                    for slot in slots_of(hit["dx"]):
                        p["slots"][kind][slot][where] += 1
                        p["evidence"][kind][slot][hit.get("how", "look")] += 1
                for group in match.get("ambiguous", []):
                    for item in group.get("dx", []):
                        for slot in slots_of(item):
                            p["ambiguous"][kind][slot] += 1
                for item in match.get("dx_only", []):
                    if item.get("origin") not in (None, "game"):
                        continue
                    for slot in slots_of(item["dx"]):
                        p["dx_only"][kind][slot] += 1
                for item in match.get("vk_only", []):
                    p["vk_only"][kind]["%d.%d" % tuple(item["vk"])] += 1
    return programs, sources


def slot_order(slot: str) -> tuple:
    stage, _, rest = slot.partition("-")
    kind = rest.rstrip("0123456789")
    number = rest[len(kind):]
    return ("vhdgpc".find(stage[0]) if stage else 9, kind, int(number) if number.isdigit() else 0)


def render(programs: dict, sources: list[dict]) -> dict:
    out_programs, conflicts = {}, []
    for key in sorted(programs):
        p = programs[key]
        entry = {"vk_vs": p["vk_vs"], "vk_fs": p["vk_fs"], "draws": p["draws"],
                 "dx_vs": sorted(h for h in p["dx_vs"] if h), "dx_ps": sorted(h for h in p["dx_ps"] if h),
                 "pipelines": sorted(x for x in p["pipelines"] if x is not None), "sources": sorted(p["sources"])}
        for kind, _ in KINDS:
            placed = {}
            for slot in sorted(p["slots"][kind], key=slot_order):
                where = p["slots"][kind][slot]
                placed[slot] = {"where": [{"set": s, "binding": b, "seen": n} for (s, b), n in sorted(where.items())],
                                "evidence": dict(p["evidence"][kind][slot])}
                if len(where) > 1:
                    conflicts.append({"program": key, "kind": kind, "slot": slot, "where": sorted(where)})
            entry[kind] = placed
            for name in ("ambiguous", "dx_only", "vk_only"):
                if p[name][kind]:
                    entry.setdefault(name, {})[kind] = dict(sorted(p[name][kind].items()))
        out_programs[key] = entry
    by_shader = {"fragment": defaultdict(set), "vertex": defaultdict(set), "dx_ps": defaultdict(set), "dx_vs": defaultdict(set)}
    for key, p in programs.items():
        for h in p["dx_ps"]:
            if h and p["vk_fs"]:
                by_shader["fragment"][p["vk_fs"]].add(h)
                by_shader["dx_ps"][h].add(p["vk_fs"])
        for h in p["dx_vs"]:
            if h and p["vk_vs"]:
                by_shader["vertex"][p["vk_vs"]].add(h)
                by_shader["dx_vs"][h].add(p["vk_vs"])
    return {
        "schema": 1,
        "scope": "Shader correspondence observed in scenes captured under both APIs; metadata only, no byte code.",
        "sources": sources,
        "programs": out_programs,
        "shaders": {name: {k: sorted(v) for k, v in sorted(table.items())} for name, table in by_shader.items()},
        "conflicts": conflicts,
    }


def check_known(catalogue: dict, known_path: Path) -> list[str]:
    """Every slot of an earlier verified table must be found where that table has it."""
    known = json.loads(known_path.read_text(encoding="utf-8"))
    by_fragment = defaultdict(lambda: defaultdict(set))
    for p in catalogue["programs"].values():
        for slot, item in p["textures"].items():
            for w in item["where"]:
                by_fragment[p["vk_fs"]][slot].add((w["set"], w["binding"]))
    problems = []
    for fragment, slots in known.get("slots", {}).items():
        if fragment not in by_fragment:
            continue          # not in these scenes
        for slot, place in slots.items():
            found = by_fragment[fragment].get(slot)
            if found and found != {(place["set"], place["binding"])}:
                problems.append("%s %s: catalogue %s, known %d.%d" % (fragment, slot, sorted(found), place["set"], place["binding"]))
    return problems


def cmd_build(args) -> int:
    summaries = [Path(s) for s in args.summary]
    for s in summaries:
        if not s.is_file():
            print("no such summary: %s" % s)
            return 1
    programs, sources = collect(summaries)
    catalogue = render(programs, sources)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(catalogue, indent=1), encoding="utf-8")
    draws = sum(s["draw_pairs"] for s in sources)
    print("%d scenes, %d draw pairs, %d programs (%d fragment shaders, %d vertex shaders)" % (
        len(sources), draws, len(catalogue["programs"]), len(catalogue["shaders"]["fragment"]), len(catalogue["shaders"]["vertex"])))
    for kind, label in KINDS:
        how = Counter()
        for p in catalogue["programs"].values():
            for item in p[kind].values():
                how.update(item["evidence"])
        slots = sum(len(p[kind]) for p in catalogue["programs"].values())
        open_ = sum(len((p.get("ambiguous") or {}).get(kind, {})) for p in catalogue["programs"].values())
        print("  %s slots placed: %d (%s); ambiguous slots: %d" % (label, slots, ", ".join("%s %d" % kv for kv in sorted(how.items())), open_))
    many = {h: v for h, v in catalogue["shaders"]["dx_ps"].items() if len(v) > 1}
    print("  DX11 pixel shaders with several Vulkan fragment shaders: %d of %d" % (len(many), len(catalogue["shaders"]["dx_ps"])))
    print("  conflicts: %d" % len(catalogue["conflicts"]))
    for c in catalogue["conflicts"]:
        print("    %s %s %s: %s" % (c["program"], c["kind"], c["slot"], c["where"]))
    status = 0 if not catalogue["conflicts"] else 2
    if args.check:
        problems = check_known(catalogue, Path(args.check))
        print("  against %s: %s" % (args.check, "consistent" if not problems else "%d differences" % len(problems)))
        for line in problems:
            print("    " + line)
        status = status or (3 if problems else 0)
    print("written:", out)
    return status


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)
    b = sub.add_parser("build")
    b.add_argument("--summary", action="append", required=True, help="a summary of one scene, repeatable")
    b.add_argument("--out", required=True)
    b.add_argument("--check", help="an earlier verified slot table to compare with")
    b.set_defaults(fn=cmd_build)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    raise SystemExit(main())
