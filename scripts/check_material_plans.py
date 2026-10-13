"""Audit shader-specific material contracts on saved LOD0/LOD1 callbacks.

This tests only material correspondence and bytecode. Native texture-override
queries, GPU pipeline creation and character rendering are separate gates.
Unknown shader slots and known 2D-for-3D mismatches must explicitly refuse.
"""
from __future__ import annotations
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
from check_mod_draw_plans import ROOT, SAMPLE, sha
from check_shared_palettes import BUNDLES
from verify_bone_import import sample_layout, saved_records
from verify_fragment_images import VAL
from material_profiles import profile_shaders


def verified_json(folder: Path, manifest: dict, name: str):
    path = folder / name
    if sha(path) != manifest["files"][name]["sha256"]:
        raise ValueError(f"Capture changed: {path}")
    return json.loads(path.read_text())


def rules_for(table: dict, shader: str) -> dict:
    rules = {}
    for name, entry in table["slots"].get(shader, {}).items():
        rules[int(name[4:])] = dict(kind=2, set=entry["set"], binding=entry["binding"],
                                   incompatible=bool(entry.get("sample_2d_replacement")))
    for entry in table["undeclared"]:
        if entry["fragment"] != shader:
            continue
        slot = int(entry["slot"][4:])
        if slot in rules and rules[slot]["kind"] != 1:
            raise ValueError("Contradictory absent/image material correspondence")
        rules[slot] = dict(kind=1, set=0, binding=0)
    return rules


def material_requests(run: dict) -> list[bytes]:
    state = bytearray(128)
    output = []
    for item in run["items"]:
        if item["op"] == 2:
            state[item["slot"]] = 2 if item["resource"] else 1
        elif item["op"] == 3:
            output.append(bytes(state))
    return output


def expected_contract(request: bytes, rules: dict) -> dict:
    images, nulls = [], []
    for slot, value in enumerate(request):
        if not value:
            continue
        rule = rules.get(slot)
        if not rule:
            return dict(ok=False, refusal="unknown_correspondence")
        if rule["kind"] == 1:
            continue
        if value == 2 and rule.get("incompatible"):
            return dict(ok=False, refusal="dimension_mismatch")
        if value == 2:
            images.append([slot, rule["set"], rule["binding"]])
        else:
            nulls.append([rule["set"], rule["binding"]])
    return dict(ok=True, images=images, nulls=nulls)


def run_checked(args: list[str], folder: Path):
    result = subprocess.run(args, cwd=folder, capture_output=True, text=True, errors="replace", timeout=30)
    if result.returncode:
        raise ValueError(f"Material fixture refused: {result.returncode}: {result.stderr[-1600:]}")
    return result


def main() -> int:
    folder = ROOT / "build/material-plans"
    folder.mkdir(parents=True, exist_ok=True)
    report = dict(scope="historical shader/callback material contracts only; no full Core/GPU/game execution", passed=False)
    try:
        asset = json.loads((ROOT / "build/assets-gpu/report.json").read_text())
        exe = ROOT / "build/assets-gpu/asset_gpu.exe"
        if not asset.get("passed") or sha(exe) != asset["executable_sha256"]:
            raise ValueError("Rebuild and pass the GPU asset fixture first")
        for path, digest in asset["source_sha256"].items():
            if sha(Path(path)) != digest:
                raise ValueError(f"Asset fixture source changed: {path}")
        run_checked([sys.executable, "-B", str(ROOT / "scripts/check_mod_draw_plans.py")], ROOT)
        callbacks_file = ROOT / "build/xxmi-host/mod-plans.json"
        callback_report = json.loads((ROOT / "build/xxmi-host/mod-plan-report.json").read_text())
        if not callback_report.get("passed") or callback_report["ini_sha256"] != sha(SAMPLE):
            raise ValueError("Recheck original sample callbacks first")
        callbacks = json.loads(callbacks_file.read_text())
        plans = {(int(re.search(r"component(\d+)$", row["section"])[1]), row["lod"]): row
                 for row in callbacks["runs"] if row["scenario"] == 0}
        table_path = ROOT / "docs/arcane-texture-slots.json"
        table = json.loads(table_path.read_text())
        profiles = profile_shaders(table)
        known = set(profiles)
        known_hashes = {key: item["sha256"] for key, item in profiles.items()}
        reference = sample_layout(SAMPLE)
        requests = defaultdict(list)
        codes, sources = {}, {}
        for serial, bundle in enumerate(BUNDLES, 1):
            path = ROOT / "captures" / bundle
            manifest = json.loads((path / "manifest.json").read_text())
            sources[str(path / "manifest.json")] = sha(path / "manifest.json")
            pipes = {p["id"]: p for p in verified_json(path, manifest, "pipelines.json")}
            shaders = {s["id"]: s for s in verified_json(path, manifest, "shaders.json")}
            draws = {d["n"]: d for d in verified_json(path, manifest, "frame.json")["draws"]}
            for record in saved_records(path, serial, reference):
                selected = material_requests(plans[record["component"], record["lod"]])
                if not selected:
                    continue
                pipeline = pipes[draws[record["draw"]]["pipeline"]]
                stage, = [s for s in pipeline["shaders"] if s["stage"] == 16]
                shader = shaders[stage["id"]]
                code = path / shader["file"]
                fingerprint = sha(code)
                if fingerprint != manifest["files"][shader["file"]]["sha256"]:
                    raise ValueError("Historical shader fingerprint changed")
                key = shader["hash"]
                if key in known and fingerprint != known_hashes[key]:
                    raise ValueError("Material profile CRC collision or changed exact bytecode")
                if key in codes and fingerprint != sha(codes[key]):
                    raise ValueError("Historical CRC collision")
                codes[key] = code
                for request in selected:
                    requests[key].append(dict(bundle=bundle, draw=record["draw"], component=record["component"],
                                              lod=record["lod"], bindings=request))
        totals, shader_results = Counter(), []
        for key, cases in sorted(requests.items()):
            rules = rules_for(table, key)
            data = bytearray(b"EFMIMP01") + struct.pack("<I", len(rules))
            for slot, rule in sorted(rules.items()):
                data += struct.pack("<4I", slot, rule["kind"], rule["set"], rule["binding"])
            data += struct.pack("<I", len(cases)) + b"".join(c["bindings"] for c in cases)
            source, result = folder / (key + ".bin"), folder / (key + ".jsonl")
            source.write_bytes(data)
            run_checked([str(VAL), "--target-env", "vulkan1.1", str(codes[key])], folder)
            run_checked([str(exe), "--material-profile", str(codes[key]), str(source), str(result)], folder)
            rows = [json.loads(line) for line in result.read_text().splitlines()]
            if len(rows) != len(cases):
                raise ValueError("Missing native material contracts")
            counts, masks = Counter(), set()
            for row, case in zip(rows, cases):
                expected = expected_contract(case["bindings"], rules)
                if row["ok"] != expected["ok"]:
                    raise ValueError(f"Material acceptance differs for {key}: {case['draw']}")
                if not expected["ok"]:
                    keyword = "correspondence" if expected["refusal"] == "unknown_correspondence" else "incompatible"
                    if keyword not in row["reason"] or row["images"] or row["nulls"]:
                        raise ValueError("Native material refusal published partial output or wrong reason")
                    counts[expected["refusal"]] += 1
                    continue
                if row["images"] != expected["images"] or row["nulls"] != expected["nulls"] or row["reason"]:
                    raise ValueError("Native material bindings differ from explicit correspondence")
                counts["accepted"] += 1
                masks.add((tuple(tuple(i) for i in row["images"]), tuple(tuple(i) for i in row["nulls"])))
            for number, (images, nulls) in enumerate(sorted(masks)):
                code = codes[key]
                if nulls:
                    output = folder / f"{key}-{number}-null.spv"
                    run_checked([str(exe), "--null-fragment", str(code), str(output), *[str(n) for pair in nulls for n in pair]], folder)
                    code = output
                if images:
                    output = folder / f"{key}-{number}-material.spv"
                    moves = [(s, b, 3, i+2) for i, (_, s, b) in enumerate(images)]
                    run_checked([str(exe), "--repoint-fragment", str(code), str(output), *[str(n) for move in moves for n in move]], folder)
                    code = output
                run_checked([str(VAL), "--target-env", "vulkan1.1", str(code)], folder)
            totals.update(counts)
            shader_results.append(dict(fragment=key, sha256=sha(codes[key]), source=str(codes[key]), counts=dict(counts),
                                       logical_slots=sorted(rules), validated_contracts=len(masks)))
        report.update(passed=True, counts=dict(totals), shaders=shader_results, source_sha256=sources,
                      slot_table_sha256=sha(table_path), callback_plans_sha256=sha(callbacks_file),
                      executable_sha256=sha(exe), validator_sha256=sha(VAL))
        print(json.dumps(dict(shaders=len(shader_results), **dict(totals)), indent=2))
        return 0
    except (OSError, ValueError, KeyError, IndexError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
        print(str(error).encode("ascii", "backslashreplace").decode("ascii"), file=sys.stderr)
        return 1
    finally:
        (folder / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True)+"\n", encoding="ascii")


if __name__ == "__main__":
    raise SystemExit(main())
