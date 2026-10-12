"""DX11 baseline of a scene: capture it through the sibling project's probe and pair it with Vulkan.

    dx11_baseline.py capture   --label frame-a
    dx11_baseline.py summarize --label frame-a --mod <mod.ini> --vulkan captures/first-probe

``capture`` asks the DX11 probe of the EFMI MCP project (loaded in a game that runs under DX11)
for one full frame with the constant buffer windows and saves it below ``--out``. ``summarize``
reads such a frame, the mod and a bundle of this project's Vulkan probe and writes
``<label>.summary.json``: which draws belong to which part of the mod, what they bind, and for
every object drawn in both frames which Vulkan set and binding holds what a DX11 slot holds.

The sibling project is used as a library and never written to (no byte code either).
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from collections import Counter, OrderedDict
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))

from efmi_vk import dx11_pair  # noqa: E402
from efmi_vk.mods import parse_overrides  # noqa: E402

DEFAULT_SIBLING = r"F:\EFMI MCP\server"
_COMPONENT = re.compile(r"component(\d+)", re.IGNORECASE)
INSTANCE_WINDOW_CONSTANTS = 4096      # the per-instance array as the game's shaders declare it


def sibling(path: str):
    sys.path.insert(0, path)
    from efmi_mcp import capture_model, probe_client  # noqa: E402
    return capture_model, probe_client


# --------------------------------------------------------------------------- capture
def cmd_capture(args) -> int:
    _model, probe_client = sibling(args.efmi_mcp)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    try:
        with probe_client.ProbeClient.connect(args.pid) as client:
            status = client.status()
            doc, blob = client.capture(detail="full", cb_data=True, timeout_ms=args.timeout_ms)
    except probe_client.ProbeError as exc:
        print("capture failed: %s" % exc)
        return 1
    (out / (args.label + ".json")).write_text(json.dumps(doc), encoding="utf-8")
    (out / (args.label + ".bin")).write_bytes(blob)
    (out / (args.label + ".status.json")).write_text(json.dumps(status, indent=1), encoding="utf-8")
    origins = Counter(d.get("o", "unknown") for d in doc.get("draws", []))
    print("frame %s of PID %s (probe %s): %d draws %s, %d bytes of contents -> %s" % (
        doc.get("frame"), status.get("pid"), status.get("probe"), len(doc.get("draws", [])), dict(origins), len(blob), out))
    return 0


# --------------------------------------------------------------------------- DX11 side
def dx_draw_record(cap, d: dict) -> dict:
    out = d.get("out") or {}
    colour = sorted(k for k in out if k.startswith("o") and k != "oD")
    first = cap.resource(out[colour[0]]["res"]) if colour else None
    depth = cap.resource(out["oD"]["res"]) if "oD" in out else None
    textures, buffers = [], []
    for slot, v in (d.get("tex") or {}).items():
        if int(slot.split("-t")[1]) >= 100:
            continue                      # XXMI's own slots (IniParams and the like)
        res = cap.resource(v["res"]) or {}
        if res.get("kind") == "buffer":
            buffers.append({"slot": slot, "origin": res.get("origin"), "hash": v.get("hash"),
                            "bytes": res.get("size"), "stride": res.get("stride"), "key": res.get("size")})
            continue
        sig = dx11_pair.dx_texture_sig(res, v.get("view"))
        if sig:
            textures.append({"slot": slot, "origin": res.get("origin"), "hash": v.get("hash"), "key": sig})
    windows = dx11_pair.dx_windows(d.get("cb") or {})
    for w in windows:
        w["origin"] = (cap.resource(w["res"]) or {}).get("origin")
        w["key"] = w["bytes"]
    ib = d.get("ib") or {}
    layout = cap.layout(d.get("layout", -1)) or []
    return {
        "index": d["index"], "call": d["call"], "origin": d["origin"],
        "count": d["args"].get("index_count"), "first": d["args"].get("first_index", 0),
        "instances": d["args"].get("instance_count"),
        "ib_hash": ib.get("hash"), "ib_format": ib.get("format"),
        "vs": d["shaders"].get("vs"), "ps": d["shaders"].get("ps"),
        "targets": tuple(sorted((k, v["res"]) for k, v in out.items())),
        "target_text": "%d colour%s%s" % (
            len(colour), "" if not first else " %dx%d %s" % (first.get("w", 0), first.get("h", 0), first.get("fmt_name")),
            "" if not depth else ", depth %dx%d" % (depth.get("w", 0), depth.get("h", 0))),
        "vb": [{"slot": int(slot), "stride": v["stride"], "origin": (cap.resource(v["res"]) or {}).get("origin"),
                "bytes": (cap.resource(v["res"]) or {}).get("size")} for slot, v in sorted((d.get("vb") or {}).items(), key=lambda kv: int(kv[0]))],
        "layout": [{"semantic": "%s%s" % (e["semantic"], e["index"] or ""), "format": e["format"], "slot": e["slot"],
                    "offset": e["offset"]} for e in layout],
        "textures": textures, "buffers": buffers, "windows": windows,
    }


def instance_window(cap, d: dict) -> dict:
    """The per-instance windows of a draw: which slot, whose buffer, and constants 3 and 5 of entry 0."""
    found = {}
    for slot, cb in (d.get("cb") or {}).items():
        if cb["num_constants"] != INSTANCE_WINDOW_CONSTANTS:
            continue
        data = cap.snap_bytes(cb["snap"])
        entry = {"origin": (cap.resource(cb["res"]) or {}).get("origin"), "first_constant": cb["first_constant"],
                 "bytes_kept": None if data is None else len(data)}
        if data and len(data) >= 256:
            entry["constant3_float"] = [round(x, 4) for x in struct.unpack_from("<4f", data, 48)]
            x, y, z = struct.unpack_from("<3I", data, 80)
            entry["constant5_uint"] = [x, y, z]
            entry["constant5_w_float"] = struct.unpack_from("<f", data, 92)[0]
            entry["entry0"] = data[:256].hex()
        found[slot] = entry
    return found


def differing_constants(a_hex: str, b_hex: str) -> list[int]:
    a, b = bytes.fromhex(a_hex), bytes.fromhex(b_hex)
    return [i for i in range(16) if a[i * 16:i * 16 + 16] != b[i * 16:i * 16 + 16]]


def compute_steps(cap, draws: list[dict]) -> list[dict]:
    steps: "OrderedDict[str, dict]" = OrderedDict()
    for d in draws:
        if d["type"] != 8 or d["origin"] != "xxmi":
            continue
        s = steps.setdefault(d["shaders"].get("cs"), {"cs": d["shaders"].get("cs"), "count": 0, "thread_groups": [],
                                                    "constant_windows": [], "inputs": [], "outputs": []})
        s["count"] += 1
        for name, key, group in (("constant_windows", "cb", None), ("inputs", "tex", None), ("outputs", "out", None)):
            for slot, v in (d.get(key) or {}).items():
                if key == "tex" and int(slot.split("-t")[1]) >= 100:
                    continue
                res = cap.resource(v["res"]) or {}
                text = "%s: %s %s" % (slot, res.get("origin"), ("%d constants" % v["num_constants"]) if key == "cb"
                                      else "%s bytes" % res.get("size") if res.get("kind") == "buffer" else res.get("kind"))
                if text not in s[name]:
                    s[name].append(text)
        tg = d["args"].get("thread_groups")
        if tg not in s["thread_groups"]:
            s["thread_groups"].append(tg)
    return list(steps.values())


# --------------------------------------------------------------------------- pairing
def pair_draw(dx: dict, vk_draw: dict, pipelines: dict) -> dict:
    items = dx11_pair.vk_set_items(vk_draw.get("sets"))
    for it in items["images"]:
        it["key"] = it["sig"]
    for it in items["uniform"]:
        it["key"] = dx11_pair.round_to_window(it["range"] or 0)
    for it in items["storage"]:
        it["key"] = it["range"]

    def brief(match: dict, dx_name, vk_name) -> dict:
        return {
            "pairs": [{"dx": dx_name(p["dx"]), "vk": vk_name(p["vk"]), "what": p["key"], "dx_origin": p["dx"].get("origin")}
                      for p in match["pairs"]],
            "ambiguous": [{"dx": [dx_name(i) for i in a["dx"]], "vk": [vk_name(i) for i in a["vk"]], "what": a["key"]}
                          for a in match["ambiguous"]],
            "dx_only": [{"dx": dx_name(i), "what": i["key"], "origin": i.get("origin")} for i in match["dx_only"]],
            "vk_only": [{"vk": vk_name(i), "what": i["key"]} for i in match["vk_only"]],
        }

    where = lambda i: [i["set"], i["binding"]]          # noqa: E731
    pipeline = pipelines.get(vk_draw.get("pipeline"))
    return {
        "dx_draw": dx["index"], "vk_draw": vk_draw["n"], "vk_render_pass": vk_draw.get("rp"), "vk_pipeline": vk_draw.get("pipeline"),
        "dx_vs": dx["vs"], "dx_ps": dx["ps"], "dx_targets": dx["target_text"],
        "textures": brief(dx11_pair.match_by_key(dx["textures"], items["images"]), lambda i: i["slot"], where),
        "constant_buffers": brief(dx11_pair.match_by_key(dx["windows"], items["uniform"]), lambda i: i["slots"], where),
        "buffers": brief(dx11_pair.match_by_key(dx["buffers"], items["storage"]), lambda i: i["slot"], where),
        "dx_vertex_input": {"strides": {v["slot"]: v["stride"] for v in dx["vb"]}, "elements": dx["layout"]},
        "vk_vertex_input": None if not pipeline else {"strides": {b["binding"]: b["stride"] for b in pipeline.get("bindings", [])},
                                                      "attributes": pipeline.get("attributes", [])},
    }


def cmd_summarize(args) -> int:
    model, _client = sibling(args.efmi_mcp)
    base = Path(args.out) / args.label
    cap = model.Capture(json.loads(base.with_suffix(".json").read_text(encoding="utf-8")), base.with_suffix(".bin").read_bytes())
    raw = list(cap.draws())
    records = {d["index"]: dx_draw_record(cap, d) for d in raw if d["type"] < 8}
    by_index = {d["index"]: d for d in raw}

    # The mod: which index ranges each component draws, and which Vulkan hashes stand for it.
    sections = dx11_pair.parse_draw_sections(args.mod) if args.mod else {}
    range_owner = {}
    for number, sec in sections.items():
        for rng in sec["ranges"]:
            range_owner.setdefault(rng, number)
    component_hashes: dict[int, list[str]] = {}
    for o in parse_overrides(args.mod) if args.mod else []:
        m = _COMPONENT.search(o["section"])
        if m:
            component_hashes.setdefault(int(m.group(1)), []).append(o["hash"])

    vk_frame, pipelines = None, {}
    if args.vulkan:
        bundle = Path(args.vulkan)
        vk_frame = json.loads((bundle / "frame.json").read_text(encoding="utf-8"))
        if (bundle / "pipelines.json").exists():
            pipelines = {p["id"]: p for p in json.loads((bundle / "pipelines.json").read_text(encoding="utf-8"))}
    vk_by_hash: dict[str, list[dict]] = {}
    for d in (vk_frame or {}).get("draws", []):
        h = (d.get("ib") or {}).get("hash")
        if h:
            vk_by_hash.setdefault(h, []).append(d)

    # Objects: parts of the mod (drawn by XXMI) and the game's own draws that both frames contain.
    objects: "OrderedDict[str, dict]" = OrderedDict()
    for r in records.values():
        if r["origin"] == "xxmi":
            number = range_owner.get((r["count"], r["first"]))
            name = "component %d" % number if number is not None else "xxmi %s+%s" % (r["first"], r["count"])
            obj = objects.setdefault(name, {"kind": "mod", "component": number, "dx": [],
                                            "vk_hashes": component_hashes.get(number, []) if number is not None else []})
        elif r["ib_hash"] and (r["count"] or 0) >= args.min_index_count and r["ib_hash"] in vk_by_hash:
            obj = objects.setdefault("game %s" % r["ib_hash"], {"kind": "game", "dx": [], "vk_hashes": [r["ib_hash"]]})
        else:
            continue
        obj["dx"].append(r)

    result_objects, mapping = [], {}
    for name, obj in objects.items():
        uses = dx11_pair.group_uses(obj["dx"])
        vk_draws = sorted((d for h in obj["vk_hashes"] for d in vk_by_hash.get(h, [])), key=lambda d: d["n"])
        entry = {"name": name, "kind": obj["kind"], "dx_draws": [r["index"] for r in obj["dx"]],
                 "dx_ranges": sorted({(r["count"], r["first"]) for r in obj["dx"]}), "dx_uses": len(uses),
                 "vk_hashes": obj["vk_hashes"], "vk_draws": [d["n"] for d in vk_draws], "pairs": []}
        if obj["kind"] == "mod":
            entry["instance_windows"] = {r["index"]: instance_window(cap, by_index[r["index"]]) for r in [u[0] for u in uses]}
        if vk_draws and len(uses) == len(vk_draws):
            for use, vk_draw in zip(uses, vk_draws):
                pair = pair_draw(use[0], vk_draw, pipelines)
                entry["pairs"].append(pair)
                for p in pair["textures"]["pairs"]:
                    if p["dx_origin"] == "game" and p["dx"].startswith("ps-"):
                        mapping.setdefault(pair["dx_ps"], {}).setdefault(p["dx"], set()).add(tuple(p["vk"]))
        elif vk_draws:
            entry["not_paired"] = "%d uses under DX11, %d draws under Vulkan" % (len(uses), len(vk_draws))
        result_objects.append(entry)

    summary = {
        "dx11": {"label": args.label, "frame": cap.frame, "draws": len(raw),
                 "origins": dict(Counter(d["origin"] for d in raw)), "calls": dict(Counter(d["call"] for d in raw))},
        "vulkan": None if not vk_frame else {"bundle": str(args.vulkan), "frame": vk_frame.get("frame"), "stats": vk_frame.get("stats")},
        "mod": None if not args.mod else {"ini": str(args.mod), "components": {
            n: {"ranges": s["ranges"], "slots": s["slots"], "hashes": component_hashes.get(n, [])} for n, s in sorted(sections.items())}},
        "compute_steps": compute_steps(cap, raw),
        "objects": result_objects,
        "texture_slot_map": {ps: {slot: sorted(where) for slot, where in sorted(slots.items(), key=lambda kv: int(kv[0].split("-t")[1]))}
                             for ps, slots in mapping.items()},
        "draws": [{k: v for k, v in r.items() if k != "targets"} for r in records.values()
                  if any(r["index"] in o["dx_draws"] for o in result_objects)],
    }
    target = base.with_suffix(".summary.json")
    target.write_text(json.dumps(summary, indent=1), encoding="utf-8")

    print("DX11 frame %s: %d calls %s" % (cap.frame, len(raw), summary["dx11"]["origins"]))
    for s in summary["compute_steps"]:
        print("compute %s x%d groups %s | %s | in %s | out %s" % (s["cs"], s["count"], s["thread_groups"], s["constant_windows"], s["inputs"], s["outputs"]))
    for o in result_objects:
        print("%-16s DX11 draws %d in %d uses, ranges %s | Vulkan draws %s%s" % (
            o["name"], len(o["dx_draws"]), o["dx_uses"], o["dx_ranges"][:4], o["vk_draws"],
            "" if "not_paired" not in o else " | NOT PAIRED: " + o["not_paired"]))
    print("texture slots of the game's own textures, per DX11 pixel shader -> Vulkan [set, binding]:")
    for ps, slots in summary["texture_slot_map"].items():
        print("  %s: %s" % (ps, "  ".join("%s->%s" % (slot.replace("ps-", ""), "/".join("%d.%d" % tuple(w) for w in where)) for slot, where in slots.items())))
    print("written:", target)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--efmi-mcp", default=DEFAULT_SIBLING, help="the server folder of the EFMI MCP project (read only)")
    ap.add_argument("--out", default=str(ROOT / "captures" / "dx11-baseline"), help="folder inside this project")
    sub = ap.add_subparsers(dest="command", required=True)
    c = sub.add_parser("capture")
    c.add_argument("--label", required=True)
    c.add_argument("--pid", type=int, default=None)
    c.add_argument("--timeout-ms", type=int, default=8000)
    c.set_defaults(fn=cmd_capture)
    s = sub.add_parser("summarize")
    s.add_argument("--label", required=True)
    s.add_argument("--mod", help="the INI of the mod that is on screen")
    s.add_argument("--vulkan", help="a bundle written by collect_probe.py for the same scene")
    s.add_argument("--min-index-count", type=int, default=100)
    s.set_defaults(fn=cmd_summarize)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    raise SystemExit(main())
