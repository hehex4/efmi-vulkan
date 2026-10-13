"""Build a whole-character package for the layer from one scenario of the hosted frontend's plans.

Inputs, all read-only:
- the mod's INI: entry sections (index hash, match range, component, LOD assignment) and the
  one kind of layout edit this tool understands, ``vbN->ElementFormat(BLENDINDICES, 0) = ...``;
- ``build/xxmi-host/mod-plans.json``: what the original XXMI engine binds and draws in each
  component callback (scripts/check_mod_draw_plans.py), for one scenario of the mod's variables;
- ``build/xxmi-host/core-skeleton-plan.json``: bone ranges and LOD remaps as the original core
  initialization evaluated them (scripts/check_core_skeleton.py);
- the mesh and remap files those name.

The default package holds meshes and bones. --textures adds static pixel bindings and exact
fragment profiles. It is a fixed snapshot of one scenario, not an INI interpreter: keys,
conditions and reloads do not exist at run time. Output goes to this project's build folder.

With ``--check`` the saved game bundles given are replayed against the package: every matched
draw must find the section the Python matcher found, and every pipeline's vertex declaration
must accept the mod's streams under the layer's rule.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))
from efmi_vk.hashes import crc32c

MAGIC = b"EFMICH01"
NONE = 0xFFFFFFFF
KIND_VERTEX, KIND_INDEX, KIND_REMAP = 0, 1, 2
VK_R8G8B8A8_UINT, VK_R16G16B16A16_UINT = 41, 95
FORMATS = {"R16G16B16A16_UINT": (VK_R8G8B8A8_UINT, VK_R16G16B16A16_UINT)}
OP_VERTEX, OP_INDEX, OP_PIXEL, OP_DRAW, OP_EXTERNAL = 0, 1, 2, 3, 4


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ini_sections(text: str) -> list[tuple[str, list[str]]]:
    sections, current = [], None
    for raw in text.splitlines():
        line = raw.strip()
        head = re.fullmatch(r"\[(.+)\]", line)
        if head:
            current = (head.group(1), [])
            sections.append(current)
        elif current is not None and line and not line.startswith(";"):
            current[1].append(line)
    return sections


def entries(text: str, namespace: str) -> list[dict]:
    """Entry sections: an index hash that sets a component and runs a draw callback."""
    result = []
    for name, lines in ini_sections(text):
        if not name.lower().startswith("textureoverride"):
            continue
        values = {}
        for line in lines:
            key, _, value = line.partition("=")
            values.setdefault(key.strip().lower(), value.strip())
        component = values.get("$\\efmiv1\\component_id")
        callback = values.get("commandlist\\efmiv1\\callback_component_drawcustom")
        if component is None or callback is None or "hash" not in values:
            continue
        target = re.fullmatch(r"ref\s+CommandList(\w+)", callback)
        if not target or "match_index_count" not in values:
            raise ValueError(f"entry section {name} is not in the expected form")
        lod = values.get("$lod_level")
        result.append({
            "section": name,
            "hash": int(values["hash"], 16),
            "match_count": int(values["match_index_count"]),
            "match_first": int(values["match_first_index"]) if "match_first_index" in values else None,
            "component": int(component),
            "lod": int(lod) if lod is not None else None,
            "callback": f"commandlist\\{namespace}\\{target.group(1).lower()}",
        })
    return result


def layout_edits(text: str) -> list[dict]:
    edits = []
    for match in re.finditer(r"^\s*vb(\d+)->ElementFormat\(\s*(\w+)\s*,\s*(\d+)\s*\)\s*=\s*(\w+)\s*$", text, re.M | re.I):
        slot, semantic, index, fmt = int(match[1]), match[2].upper(), int(match[3]), match[4].upper()
        if semantic != "BLENDINDICES" or index != 0 or fmt not in FORMATS:
            raise ValueError(f"layout edit outside the supported rule: {match[0].strip()}")
        source, target = FORMATS[fmt]
        edits.append({"binding": slot, "source_format": source, "format": target})
    if re.search(r"ElementFormat|ElementOffset", text, re.I) and not edits:
        raise ValueError("the INI has layout edits this tool does not understand")
    return edits


class Streams:
    def __init__(self):
        self.items, self.index = [], {}

    def add(self, path: Path, kind: int, stride: int, bone_index_offset=NONE, bone_weight_offset=NONE) -> int:
        key = (str(path).lower(), kind, stride)
        if key in self.index:
            item = self.items[self.index[key]]
            if (item["bone_index_offset"], item["bone_weight_offset"]) != (bone_index_offset, bone_weight_offset):
                raise ValueError(f"one file is used with two meanings: {path}")
            return self.index[key]
        data = path.read_bytes()
        if not data or len(data) % stride:
            raise ValueError(f"{path} is empty or not a multiple of its stride {stride}")
        self.index[key] = len(self.items)
        self.items.append({"path": str(path), "kind": kind, "stride": stride, "data": data, "sha256": hashlib.sha256(data).hexdigest(),
                           "bone_index_offset": bone_index_offset, "bone_weight_offset": bone_weight_offset})
        return self.index[key]


def build(args) -> dict:
    ini = args.mod.resolve()
    text = ini.read_text(encoding="utf-8", errors="strict")
    ini_hash = sha(ini)
    plans = json.loads(args.plans.read_text(encoding="utf-8"))
    plan_report = json.loads(args.plans.with_name("mod-plan-report.json").read_text(encoding="utf-8"))
    skeleton = json.loads(args.skeleton.read_text(encoding="utf-8"))
    if plan_report.get("ini_sha256") != ini_hash or not plan_report.get("passed"):
        raise ValueError("the callback plans were not made from this INI; rerun scripts/check_mod_draw_plans.py")
    namespace = re.search(r"^\s*namespace\s*=\s*(\S+)", text, re.M | re.I)
    if not namespace:
        raise ValueError("the INI declares no namespace")
    namespace = namespace.group(1).lower()
    edits = layout_edits(text)
    edit_slots = {e["binding"] for e in edits}

    active = sorted((c for c in skeleton["components"] if c["count"]), key=lambda c: c["first"])
    if len(active) > 32:
        raise ValueError("more than 32 active components")
    cursor = 0
    for c in active:
        if c["first"] != cursor:
            raise ValueError("components do not partition the skeleton")
        cursor += c["count"]
    if cursor != skeleton["bones"]:
        raise ValueError("components do not cover the skeleton")
    dense = {c["id"]: i for i, c in enumerate(active)}

    streams = Streams()
    lists, draws, components = [], [], []
    images, pixels = [], []
    runs = {(r["section"], r["lod"]): r for r in plans["runs"] if r["scenario"] == args.scenario}
    found = entries(text, namespace)
    callbacks = {}
    for entry in found:
        callbacks.setdefault(entry["component"], entry["callback"])
        if callbacks[entry["component"]] != entry["callback"]:
            raise ValueError("one component with two callbacks")
    for c in active:
        remap = c["remaps"][1]
        if remap["state"] == 2:
            path = Path(remap["path"])
            raw = path.read_bytes()
            if list(struct.unpack(f"<{len(raw) // 2}H", raw)) != remap["indices"] or len(remap["indices"]) != c["count"]:
                raise ValueError(f"remap file differs from the evaluated plan: {path}")
            remap_stream = streams.add(path, KIND_REMAP, 2)
        elif remap["state"] == 1:
            remap_stream = NONE
        else:
            raise ValueError(f"component {c['id']} has no LOD1 assignment")
        if c["remaps"][0]["state"] != 1:
            raise ValueError(f"component {c['id']} has a LOD0 remap, which this tool does not expect")
        callback = callbacks.get(c["id"])
        if callback is None:
            raise ValueError(f"component {c['id']} has no entry section")
        pair = []
        for lod in (0, 1):
            run = runs.get((callback, lod))
            if run is None:
                raise ValueError(f"no plan for {callback} at LOD {lod} in scenario {args.scenario}")
            bound_vb, bound_ib, first = {}, None, len(draws)
            bound_pixel = {}
            for item in run["items"]:
                op = item["op"]
                if op == OP_EXTERNAL:
                    if args.textures and item.get("external") != "commandlist\\efmiv1\\overridetextures":
                        raise ValueError("Static material package does not implement this external callback")
                    continue  # Core source-identity queries remain outside the static scenario.
                if op == OP_PIXEL:
                    if args.textures:
                        slot, binding = item["slot"], item.get("file_binding")
                        if not 0 <= slot < 128:
                            raise ValueError("Pixel slot outside the material limit")
                        if binding:
                            if binding["usage"]:
                                raise ValueError("Pixel binding must be an image")
                            path = str(Path(binding["path"]).resolve())
                            if path not in images:
                                images.append(path)
                            bound_pixel[slot] = images.index(path)
                        elif item.get("resource") is None:
                            bound_pixel[slot] = NONE
                        else:
                            raise ValueError("Unresolved static pixel binding")
                    continue
                if op == OP_VERTEX or op == OP_INDEX:
                    binding = item.get("file_binding")
                    if not binding:
                        raise ValueError(f"{callback}: a vertex or index binding without a file")
                    path = Path(binding["path"])
                    if op == OP_INDEX:
                        stride = {0: 2, 1: 4}[binding["index_type"]]
                        if binding["stride"] != stride:
                            raise ValueError("index stride and type disagree")
                        bound_ib = streams.add(path, KIND_INDEX, stride)
                    else:
                        slot, stride = item["slot"], binding["stride"]
                        if not 0 <= slot < 4:
                            raise ValueError(f"{callback}: vertex slot {slot} is outside the layer's four tracked slots")
                        if slot in edit_slots:
                            # The widened element is the last one of the stream; 16-bit weights precede it.
                            if stride not in (8, 16):
                                raise ValueError(f"unexpected blend stride {stride}")
                            bound_vb[slot] = streams.add(path, KIND_VERTEX, stride, stride - 8, 0 if stride == 16 else NONE)
                        else:
                            bound_vb[slot] = streams.add(path, KIND_VERTEX, stride)
                elif op == OP_DRAW:
                    if item["instances"] != 1 or item["first_instance"] != 0 or bound_ib is None or not bound_vb:
                        raise ValueError(f"{callback}: unsupported draw item")
                    draws.append({"index": bound_ib, "count": item["count"], "first": item["first"], "base": item["base"],
                                  "vb": [bound_vb.get(s, NONE) for s in range(4)]})
                    pixels.append(dict(bound_pixel))
                else:
                    raise ValueError(f"{callback}: unknown plan operation {op}")
            pair.append(len(lists))
            lists.append({"first": first, "count": len(draws) - first})
        components.append({"first": c["first"], "count": c["count"], "remap": remap_stream, "lists": pair, "id": c["id"]})

    sections = []
    for entry in found:
        if entry["component"] not in dense:
            raise ValueError(f"{entry['section']} names an inactive component")
        if entry["lod"] not in (None, 0, 1):
            raise ValueError(f"{entry['section']}: only LOD 0 and 1 are supported")
        sections.append({"hash": entry["hash"], "match_count": entry["match_count"],
                         "match_first": NONE if entry["match_first"] is None else entry["match_first"],
                         "component": dense[entry["component"]], "lod": NONE if entry["lod"] is None else entry["lod"],
                         "name": entry["section"]})

    # Checks the layer repeats at load: bounds of every draw and bone indices of every blend stream.
    for d in draws:
        index = streams.items[d["index"]]
        total = len(index["data"]) // index["stride"]
        if d["count"] <= 0 or d["first"] + d["count"] > total:
            raise ValueError("a draw reads past its index file")
        code = "<%d%s" % (d["count"], "H" if index["stride"] == 2 else "I")
        values = struct.unpack_from(code, index["data"], d["first"] * index["stride"])
        low, high = min(values) + d["base"], max(values) + d["base"]
        for stream in d["vb"]:
            if stream != NONE:
                item = streams.items[stream]
                if low < 0 or high >= len(item["data"]) // item["stride"]:
                    raise ValueError("a draw reads past a vertex file")
    for item in streams.items:
        if item["bone_index_offset"] == NONE:
            continue
        count, stride = len(item["data"]) // item["stride"], item["stride"]
        worst, bad_weights = 0, 0
        for v in range(count):
            worst = max(worst, *struct.unpack_from("<4H", item["data"], v * stride + item["bone_index_offset"]))
            if item["bone_weight_offset"] != NONE:
                total = sum(struct.unpack_from("<4H", item["data"], v * stride + item["bone_weight_offset"]))
                bad_weights += not 65533 <= total <= 65537
        if worst >= skeleton["bones"] or bad_weights:
            raise ValueError(f"{item['path']}: bone index {worst} of {skeleton['bones']} or {bad_weights} unnormalized weights")

    offset, table = 0, []
    payload = bytearray()
    for item in streams.items:
        payload += b"\0" * (-len(payload) % 16)
        table.append((item["kind"], item["stride"], len(payload), len(item["data"]), item["bone_index_offset"], item["bone_weight_offset"]))
        payload += item["data"]
    body = bytearray()
    for c in components:
        body += struct.pack("<6I", c["first"], c["count"], c["remap"], c["lists"][0], c["lists"][1], 0)
    for kind, stride, start, size, index_offset, weight_offset in table:
        body += struct.pack("<2I2Q2I", kind, stride, start, size, index_offset, weight_offset)
    for s in sections:
        body += struct.pack("<5I", s["hash"], s["match_count"], s["match_first"], s["component"], s["lod"])
    for entry in lists:
        body += struct.pack("<2I", entry["first"], entry["count"])
    for d in draws:
        body += struct.pack("<3Ii4I", d["index"], d["count"], d["first"], d["base"], *d["vb"])
    for e in edits:
        body += struct.pack("<3I", e["binding"], e["source_format"], e["format"])
    header = MAGIC + struct.pack("<8IQ", skeleton["bones"], len(components), len(table), len(sections), len(lists),
                                 len(draws), len(edits), crc32c(bytes(body) + bytes(payload)), len(payload))
    data = header + bytes(body) + bytes(payload)
    materials = None
    if args.textures:
        from character_materials import build_materials
        data, materials = build_materials(data, images, pixels, args.material_slots, args.dx11_dimension_zero)
    elif args.dx11_dimension_zero:
        raise ValueError("Dimension compatibility requires --textures")
    out = args.out.resolve()
    if not out.is_relative_to(ROOT):
        raise ValueError("package output must stay in this project")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    report = {
        "scope": "meshes, bones and optional static materials of one scenario; no keys, conditions or reload",
        "materials": materials,
        "ini": {"path": str(ini), "sha256": ini_hash},
        "plans": {"path": str(args.plans.resolve()), "sha256": sha(args.plans), "scenario": args.scenario,
                  "host_executable_sha256": plan_report.get("executable_sha256")},
        "skeleton": {"path": str(args.skeleton.resolve()), "sha256": sha(args.skeleton)},
        "bones": skeleton["bones"],
        "components": [{"id": c["id"], "first": c["first"], "count": c["count"], "lod1_remap": c["remap"] != NONE,
                        "draws": [lists[i]["count"] for i in c["lists"]]} for c in components],
        "sections": sections,
        "edits": edits,
        "files": [{"path": i["path"], "kind": i["kind"], "stride": i["stride"], "bytes": len(i["data"]), "sha256": i["sha256"]}
                  for i in streams.items],
        "draws": len(draws),
        "payload_bytes": len(payload),
        "package_bytes": len(data),
        "package_sha256": hashlib.sha256(data).hexdigest(),
    }
    out.with_suffix(".json").write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    report["_model"] = {"components": components, "sections": sections, "lists": lists, "draws": draws,
                        "streams": streams.items, "edits": edits}
    return report


def patched_declaration(pipeline: dict, strides: dict, edits: list[dict]):
    """The layer's rule: a replaced stream takes the file's stride; an edit widens matching elements in place."""
    bindings = {b["binding"]: b for b in pipeline["bindings"]}
    result_attributes = []
    for slot, stride in strides.items():
        if slot not in bindings or bindings[slot].get("per_instance"):
            return None, f"slot {slot} is not a per-vertex stream of the pipeline"
    for a in pipeline["attributes"]:
        fmt = a["format"]
        for e in edits:
            if a["binding"] == e["binding"] and a["binding"] in strides and fmt == e["source_format"]:
                fmt = e["format"]
        result_attributes.append([a["location"], a["binding"], fmt, a["offset"]])
    sizes = {41: 4, 95: 8, 91: 8, 106: 12, 103: 8, 100: 4, 109: 16, 37: 4, 38: 4, 97: 8}
    for location, binding, fmt, offset in result_attributes:
        if binding in strides:
            if fmt not in sizes:
                return None, f"format {fmt} has no known size"
            if offset + sizes[fmt] > strides[binding]:
                return None, f"element at {offset} of format {fmt} does not fit stride {strides[binding]} of slot {binding}"
    result_bindings = [[b["binding"], strides.get(b["binding"], b["stride"]), int(b.get("per_instance", 0))] for b in pipeline["bindings"]]
    return (result_bindings, result_attributes), ""


def check(report: dict, bundles: list[Path]) -> dict:
    sys.path.insert(0, str(ROOT / "scripts"))
    from vertex_profiles import historical_vertex_profiles, reference_vertex
    model = report["_model"]
    profiles = historical_vertex_profiles()
    by_count = {}
    for index, s in enumerate(model["sections"]):
        by_count.setdefault(s["match_count"], []).append(index)
    outcome = {"bundles": [], "draws": 0, "replaced_draw_items": 0, "skips": 0, "declarations_checked_against_semantics": 0}
    for folder in bundles:
        frame = json.loads((folder / "frame.json").read_text(encoding="utf-8"))
        pipelines = {p["id"]: p for p in json.loads((folder / "pipelines.json").read_text(encoding="utf-8"))}
        matches = json.loads((folder / "matches.json").read_text(encoding="utf-8"))
        expected = {n: s["section"] for mod in matches for s in mod["sections"] for n in (s.get("draws") or [])}
        lod, seen, per_bundle = 0, 0, {"draws": 0, "items": 0, "skips": 0, "max_items_in_one_command_buffer": 0}
        per_cb = {}
        for d in frame["draws"]:
            ib = d.get("ib") or {}
            if d.get("kind") not in ("indexed", "draw_indexed", 1) and "hash" not in ib:
                continue
            candidates = [i for i in by_count.get(d["count"], [])
                          if model["sections"][i]["match_first"] in (NONE, d["first"]) and ib.get("hash")
                          and int(ib["hash"], 16) == model["sections"][i]["hash"]]
            if len(candidates) > 1:
                raise ValueError("two sections match one draw")
            name = model["sections"][candidates[0]]["name"] if candidates else None
            if name != expected.get(d["n"]):
                raise ValueError(f"{folder.name} draw {d['n']}: package finds {name}, matcher found {expected.get(d['n'])}")
            if not candidates:
                continue
            seen += 1
            section = model["sections"][candidates[0]]
            if section["lod"] != NONE:
                lod = section["lod"]
            component = model["components"][section["component"]]
            entry = model["lists"][component["lists"][lod]]
            per_bundle["draws"] += 1
            if not entry["count"]:
                per_bundle["skips"] += 1
                continue
            pipeline = pipelines.get(d["pipeline"])
            if pipeline is None:
                raise ValueError(f"{folder.name} draw {d['n']}: pipeline not saved")
            for item in model["draws"][entry["first"]:entry["first"] + entry["count"]]:
                strides = {slot: model["streams"][stream]["stride"] for slot, stream in enumerate(item["vb"]) if stream != NONE}
                result, why = patched_declaration(pipeline, strides, model["edits"])
                if result is None:
                    raise ValueError(f"{folder.name} draw {d['n']} pipeline {d['pipeline']}: {why}")
                stage = [s for s in pipeline["shaders"] if s["stage"] == 1][0]
                profile = profiles.get(stage["hash"])
                if profile:
                    source = dict(semantics=profile["semantics"], bindings=pipeline["bindings"], attributes=pipeline["attributes"])
                    files = {slot: {"stride": stride} for slot, stride in strides.items()}
                    want_bindings, want_attributes = reference_vertex(source, files)
                    if sorted(result[0]) != sorted(want_bindings) or sorted(result[1]) != sorted(want_attributes):
                        raise ValueError(f"{folder.name} draw {d['n']}: declaration differs from the semantic reference")
                    outcome["declarations_checked_against_semantics"] += 1
                per_bundle["items"] += 1
                per_cb[d["cb"]] = per_cb.get(d["cb"], 0) + 1
        if seen != len(expected):
            raise ValueError(f"{folder.name}: {seen} matched draws, matcher has {len(expected)}")
        per_bundle["max_items_in_one_command_buffer"] = max(per_cb.values(), default=0)
        outcome["bundles"].append({"bundle": str(folder), **per_bundle})
        outcome["draws"] += per_bundle["draws"]
        outcome["replaced_draw_items"] += per_bundle["items"]
        outcome["skips"] += per_bundle["skips"]
    return outcome


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mod", type=Path, required=True, help="the mod's INI file")
    ap.add_argument("--plans", type=Path, default=ROOT / "build/xxmi-host/mod-plans.json")
    ap.add_argument("--skeleton", type=Path, default=ROOT / "build/xxmi-host/core-skeleton-plan.json")
    ap.add_argument("--scenario", type=int, default=0, help="scenario of the plans (0: declared defaults)")
    ap.add_argument("--out", type=Path, default=ROOT / "build/character/character.bin")
    ap.add_argument("--textures", action="store_true", help="include static hosted pixel bindings and verified fragment profiles")
    ap.add_argument("--material-slots", type=Path, default=ROOT / "docs/arcane-texture-slots.json")
    ap.add_argument("--dx11-dimension-zero", type=Path, help="explicit invalid-dimension compatibility, bound to the measured hardware report")
    ap.add_argument("--check", type=Path, action="append", default=[], help="saved game bundle to replay against the package")
    args = ap.parse_args()
    report = build(args)
    model = report.pop("_model")
    summary = {k: report[k] for k in ("bones", "draws", "payload_bytes", "package_bytes", "package_sha256")}
    summary["components"] = report["components"]
    summary["sections"] = len(report["sections"])
    print(json.dumps(summary, indent=1))
    if args.check:
        report["_model"] = model
        outcome = check(report, [p.resolve() for p in args.check])
        print(json.dumps(outcome, indent=1))
        args.out.resolve().with_suffix(".check.json").write_text(json.dumps(outcome, indent=1) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
