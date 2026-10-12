"""Plan installed sample draw callbacks with the original XXMI frontend, offline.

Reads the selected INI and file assets only. Validates selected ranges against
actual index/vertex bytes; does not render, execute Core or modify the mod.
"""
from __future__ import annotations

import argparse
from array import array
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SAMPLE = Path("D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Mods/arcane fes 1.2/arcane.ini")


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mod", type=Path, default=SAMPLE)
    parser.add_argument("--prefix", default="commandlist\\arcane_fes\\_draw_component")
    parser.add_argument("--native", default="commandlist\\efmiv1\\overridetextures")
    args = parser.parse_args()
    folder = ROOT / "build/xxmi-host"
    report = {"scope": "CPU callback planning and file bounds only; no GPU/Core execution", "passed": False}
    try:
        compiled = json.loads((folder / "report.json").read_text())
        exe = folder / "host_probe.exe"
        if not compiled.get("passed") or sha(exe) != compiled["executable_sha256"]:
            raise ValueError("First build and pass scripts/probe_xxmi_host.py")
        for filename, expected in compiled["source_sha256"].items():
            if sha(Path(filename)) != expected:
                raise ValueError(f"Rebuild after changed host input: {filename}")
        source_hash = sha(args.mod)
        report.update(ini=str(args.mod.resolve()), ini_sha256=source_hash,
                      executable_sha256=compiled["executable_sha256"])
        result = subprocess.run([str(exe), "--plan-mod", str(args.mod.resolve()), args.prefix, args.native],
                                cwd=folder, env=dict(os.environ, TEMP=str(folder / "build/tmp"),
                                                     TMP=str(folder / "build/tmp")),
                                capture_output=True, encoding="utf-8", errors="replace", timeout=30)
        (folder / "mod-plan-errors.txt").write_text(
            result.stderr.encode("ascii", "backslashreplace").decode("ascii"), encoding="ascii")
        if result.returncode:
            raise ValueError(f"Host refused callback plans: {result.returncode}; see mod-plan-errors.txt")
        plans = json.loads(result.stdout)
        (folder / "mod-plans.json").write_text(json.dumps(plans, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
        resources = {r["name"]: r for r in plans["resources"]}
        assets, indices, ranges, draws, empty = {}, {}, {}, 0, 0
        declared_resources = set()
        for run in plans["runs"]:
            vertex = {}; index = None; run_draws = 0
            for item in run["items"]:
                op = item["op"]
                if op == 4:
                    if item["external"] != args.native or item["phase_mask"] not in (1, 2, 3):
                        raise ValueError("Unknown native boundary in plan")
                    continue  # Preserved unresolved boundary, never counted as Core execution.
                if op in (0, 1, 2):
                    name = item["resource"]
                    if name is not None:
                        resource = resources[name]
                        filename = Path(resource["filename"]).resolve()
                        if filename not in assets:
                            assets[filename] = {"bytes": filename.stat().st_size, "sha256": sha(filename)}
                        declared_resources.add(name)
                    if op == 0:
                        vertex[item["slot"]] = name
                    elif op == 1:
                        index = name
                    continue
                if op != 3:
                    raise ValueError(f"Unknown plan operation: {op}")
                if not index or not all(vertex.get(i) for i in range(4)):
                    raise ValueError("Sample indexed draw lacks a required mesh binding")
                resource = resources[index]
                filename = Path(resource["filename"]).resolve()
                if filename not in indices:
                    if resource["format"] not in (42, 57):
                        raise ValueError("Sample index format is not R32_UINT or R16_UINT")
                    data = filename.read_bytes(); values = array("I" if resource["format"] == 42 else "H")
                    if len(data) % values.itemsize:
                        raise ValueError("Partial index element in input file")
                    values.frombytes(data)
                    if sys.byteorder != "little":
                        values.byteswap()
                    indices[filename] = values
                first, count, base = item["first"], item["count"], item["base"]
                values = indices[filename]
                if count <= 0 or first < 0 or first + count > len(values):
                    raise ValueError("Selected draw exceeds its index file")
                key = (str(filename), first, count, base)
                if key not in ranges:
                    selected = values[first:first + count]
                    ranges[key] = (min(selected) + base, max(selected) + base)
                low, high = ranges[key]
                for name in vertex.values():
                    if name is None:
                        raise ValueError("Sample mesh uses a null vertex binding")
                    res = resources[name]; length = assets[Path(res["filename"]).resolve()]["bytes"]
                    if res["stride"] <= 0 or length % res["stride"] or low < 0 or high >= length // res["stride"]:
                        raise ValueError("Selected draw exceeds a bound vertex stream")
                draws += 1; run_draws += 1
            empty += not bool(run_draws)
        if sha(args.mod) != source_hash:
            raise ValueError("INI changed during planning")
        report.update(passed=True, callbacks=len({r["section"] for r in plans["runs"]}),
                      scenarios=len({r["scenario"] for r in plans["runs"]}),
                      lod_values=sorted({r["lod"] for r in plans["runs"]}),
                      plans=len(plans["runs"]), draws=draws, empty_callbacks=empty,
                      unique_index_ranges=len(ranges), bound_resources=len(declared_resources),
                      assets={str(k): v for k, v in sorted(assets.items())})
        print(json.dumps({k: v for k, v in report.items() if k != "assets"}, indent=2))
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        report["error"] = str(exc)
        print(str(exc).encode("ascii", "backslashreplace").decode("ascii"), file=sys.stderr)
        return 1
    finally:
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "mod-plan-report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="ascii")


if __name__ == "__main__":
    raise SystemExit(main())
