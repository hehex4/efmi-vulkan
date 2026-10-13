"""Check actual LOD1 DX11 material evidence against both saved Vulkan frames.

Reads captured bytes only. Does not connect to a probe or claim rendered output.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys
from collections import Counter
import dx11_baseline as baseline
from material_profiles import profile_shaders
from vertex_profiles import DX_TO_VK

ROOT = baseline.ROOT
FOLDER = ROOT / "captures/dx11-lod1-20261013"
TARGETS = {"0aeef59b", "310c25c3", "a6d7088a"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verified(folder, manifest, name):
    path = folder / name
    if sha(path) != manifest["files"][name]["sha256"]:
        raise ValueError(f"Captured evidence changed: {path}")
    return json.loads(path.read_text())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-table", action="store_true")
    args = parser.parse_args()
    out = ROOT / "build/lod1-material-capture"
    out.mkdir(parents=True, exist_ok=True)
    report = dict(passed=False, scope="Captured LOD1 source correspondence only; no material rendering")
    try:
        manifest = json.loads((FOLDER / "manifest.json").read_text())
        for name in manifest["files"]:
            if sha(FOLDER / name) != manifest["files"][name]["sha256"]:
                raise ValueError("DX11 capture archive changed")
        model, _ = baseline.sibling(baseline.DEFAULT_SIBLING)
        from efmi_mcp import hashes, shader as lister
        if sha(Path(model.__file__)) != manifest["source_decoder"]["sha256"]:
            raise ValueError("Capture decoder changed")
        doc = verified(FOLDER, manifest, "lod1-off.json")
        cap = model.Capture(doc, (FOLDER / "lod1-off.bin").read_bytes())
        declarations = verified(FOLDER, manifest, "lod1-off.shaders.json")
        textures = verified(FOLDER, manifest, "lod1-off.textures.json")
        archive = verified(FOLDER, manifest, "lod1-off.dxbc/index.json")
        if archive["errors"] or set(archive["requested"]) != set(archive["shaders"]):
            raise ValueError("Missing captured source shaders")
        for key, item in archive["shaders"].items():
            code = (FOLDER / "lod1-off.dxbc" / item["file"]).read_bytes()
            if hashlib.sha256(code).hexdigest() != item["sha256"] or f"{hashes.fnv64(code):016x}" != key:
                raise ValueError("Archived DXBC does not match its captured identity")
        raw = {d["index"]: d for d in cap.draws()}
        # The disabled capture retains the EFMI overlay, never character indices.
        if any(d["origin"] == "xxmi" and "Indexed" in d["call"] for d in raw.values()):
            raise ValueError("Mod mesh draws remain in the original-material capture")
        records = {n: baseline.dx_draw_record(cap, d, declarations, textures)
                   for n, d in raw.items() if d["type"] < 8}
        evidence, rules, sources, sequence_count = [], {}, {}, 0
        def source(name, path):
            sources[name] = dict(path=path.relative_to(ROOT).as_posix(), sha256=sha(path))
        source("lod1_dx11_manifest", FOLDER / "manifest.json")
        for tag in ("a", "b"):
            summary_path = FOLDER / f"lod1-off-vulkan-{tag}.summary.json"
            summary = verified(FOLDER, manifest, summary_path.name)
            bundle = ROOT / "captures/m2-0.8.1-20261012" / ("lod1-" + tag) / "attempt-01"
            vk_manifest = json.loads((bundle / "manifest.json").read_text())
            source("lod1_pairing_" + tag, summary_path)
            source("lod1_vulkan_" + tag + "_manifest", bundle / "manifest.json")
            draws = {d["n"]: d for d in verified(bundle, vk_manifest, "frame.json")["draws"]}
            pipelines = {p["id"]: p for p in verified(bundle, vk_manifest, "pipelines.json")}
            shaders = verified(bundle, vk_manifest, "shaders.json")
            checked_objects = set()
            for row in summary["mod_slots"]:
                key = row["fragment"]
                if key not in TARGETS:
                    continue
                obj, = [o for o in summary["objects"] if row["dx_draw"] in o["dx_draws"]]
                if obj["name"] not in checked_objects:
                    if obj["kind"] != "game" or len(obj["dx_draws"]) != len(obj["vk_draws"]):
                        raise ValueError("Original mesh pass sequences cannot be paired")
                    for dn, vn in zip(obj["dx_draws"], obj["vk_draws"]):
                        dx, vk = records[dn], draws[vn]
                        if dx["origin"] != "game" or dx["ib_hash"] != vk["ib"]["hash"]:
                            raise ValueError("Corresponding passes refer to different original meshes")
                        if any(dx[name] != vk[name] for name in ("count", "first", "instances")):
                            raise ValueError("Corresponding indexed arguments differ")
                    checked_objects.add(obj["name"])
                    sequence_count += 1
                dx, vk = records[row["dx_draw"]], draws[row["vk_draw"]]
                pair = baseline.pair_draw(dx, vk, pipelines)
                cached, = [p for p in obj["pairs"] if p["dx_draw"] == row["dx_draw"]]
                if json.loads(json.dumps(pair)) != cached or pair["vk_fs"] != key:
                    raise ValueError("Saved pairing does not reproduce from raw capture")
                elements, attrs = pair["dx_vertex_input"]["elements"], pair["vk_vertex_input"]["attributes"]
                if len(elements) != len(attrs) or any(
                    (a["slot"], a["offset"], DX_TO_VK.get(a["format"])) != (b["binding"], b["offset"], b["format"])
                    for a, b in zip(elements, attrs)):
                    raise ValueError("Corresponding source vertex declarations differ")
                ps = dx["ps"]
                bytecode = FOLDER / "lod1-off.dxbc" / archive["shaders"][ps]["file"]
                declaration = baseline.dx11_pair.parse_declarations(lister.disassemble(bytecode.read_bytes()))
                if json.loads(json.dumps(declaration)) != declarations[ps]:
                    raise ValueError("Source declaration does not reproduce from captured DXBC")
                slot = int(row["slot"].split("-t")[1])
                rule = dict(component=row["component"], fragment=key, slot=row["slot"])
                if slot not in declaration["t"]:
                    if row["result"] != "undeclared":
                        raise ValueError("Absent source slot was mapped")
                    rule["result"] = "undeclared"
                else:
                    match, = [m for m in pair["textures"]["pairs"] if m["dx"] == row["slot"]]
                    if row["result"] != "placed" or row["where"] != match["vk"] or match["dx_origin"] != "game":
                        raise ValueError("Source texture match is missing or ambiguous")
                    rule.update(result="placed", where=match["vk"], how=match["how"])
                identity = (key, row["slot"])
                if identity in rules and rules[identity] != rule:
                    raise ValueError("Repeated LOD1 material correspondence disagrees")
                rules[identity] = rule
                codes = [s for s in shaders if s["hash"] == key]
                digests = {sha(bundle / s["file"]) for s in codes}
                if len(digests) != 1 or any(sha(bundle / s["file"]) != vk_manifest["files"][s["file"]]["sha256"] for s in codes):
                    raise ValueError("Vulkan fragment identity is missing, changed or colliding")
                evidence.append(dict(bundle=tag, dx_draw=row["dx_draw"], vk_draw=row["vk_draw"],
                                     dx_ps=ps, dx_sha256=sha(bytecode), vk_sha256=digests.pop(), **rule))
        if {key for key, _ in rules} != TARGETS:
            raise ValueError("A requested LOD1 fragment is missing")
        if args.check_table:
            table = json.loads((ROOT / "docs/arcane-texture-slots.json").read_text())
            profiles = profile_shaders(table)
            for row in rules.values():
                if row["result"] == "undeclared":
                    expected = {k: row[k] for k in ("component", "fragment", "slot")}
                    if expected not in table["undeclared"]:
                        raise ValueError("Captured absent slot is missing from the table")
                else:
                    item = table["slots"][row["fragment"]][row["slot"]]
                    if [item["set"], item["binding"]] != row["where"] or item["evidence"] != row["how"]:
                        raise ValueError("Table mapping differs from captured correspondence")
            for row in evidence:
                if profiles[row["fragment"]]["sha256"] != row["vk_sha256"]:
                    raise ValueError("Profile bytes differ from repeated Vulkan evidence")
        report.update(passed=True, frame=cap.frame, source_shaders=len(archive["shaders"]),
                      textures=len(textures), origins=dict(Counter(d["origin"] for d in raw.values())),
                      matched_pass_sequences=sequence_count, rules=list(rules.values()), evidence=evidence,
                      sources=sources, table_checked=args.check_table)
        print(json.dumps({k: report[k] for k in ("frame", "source_shaders", "textures", "matched_pass_sequences", "rules", "table_checked")}, indent=2))
        return 0
    except (OSError, ValueError, KeyError, AssertionError) as error:
        report["error"] = str(error)
        print(str(error).encode("ascii", "backslashreplace").decode("ascii"), file=sys.stderr)
        return 1
    finally:
        (out / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="ascii")


if __name__ == "__main__":
    raise SystemExit(main())
