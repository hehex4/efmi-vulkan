"""Execute original sample initialization and compare its pools with saved bone reference data.

Read-only installed INI/Core/assets. No native GPU Core operation or game execution.
The older literal extractor is used only as an independent test oracle.
"""
from __future__ import annotations
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
from check_mod_draw_plans import ROOT, SAMPLE, sha
from verify_bone_import import sample_layout


def main() -> int:
    folder = ROOT / "build/xxmi-host"
    report = {"scope": "original CPU initialization and owned metadata, no GPU/Core runtime", "passed": False}
    try:
        compiled = json.loads((folder / "report.json").read_text())
        exe = folder / "host_probe.exe"
        if not compiled.get("passed") or sha(exe) != compiled["executable_sha256"]:
            raise ValueError("First build and pass scripts/probe_xxmi_host.py")
        for filename, expected in compiled["source_sha256"].items():
            if sha(Path(filename)) != expected:
                raise ValueError(f"Rebuild after changed host input: {filename}")
        core = SAMPLE.parents[2] / "Core/EFMI"
        inputs = [SAMPLE, *(core / name for name in ("API.ini", "MergedSkeleton.ini", "SpatialIdentification.ini"))]
        hashes = {str(path): sha(path) for path in inputs}
        reference = sample_layout(SAMPLE)
        for asset in reference["assets"]:
            hashes[asset["path"]] = asset["sha256"]
        result = subprocess.run([str(exe), "--skeleton-mod", str(SAMPLE), str(core), "arcane_fes"],
                                cwd=folder, env=dict(os.environ, TEMP=str(folder / "build/tmp"), TMP=str(folder / "build/tmp")),
                                capture_output=True, encoding="utf-8", errors="replace", timeout=30)
        (folder / "core-skeleton-errors.txt").write_text(
            result.stderr.encode("ascii", "backslashreplace").decode("ascii"), encoding="ascii")
        if result.returncode:
            raise ValueError(f"Original skeleton initialization failed: {result.returncode}; see core-skeleton-errors.txt")
        plan = json.loads(result.stdout)
        if plan["bones"] != reference["bones"] or plan["instances"] != reference["instances"] or plan["lods"] != 4:
            raise ValueError("Executed dimensions differ from the independent reference")
        remap_files = []
        active = [c for c in plan["components"] if c["count"]]
        if len(active) != len(reference["components"]):
            raise ValueError("Executed component coverage differs")
        for component, expected in zip(active, reference["components"]):
            if (component["first"], component["count"]) != (expected["first"], expected["count"]):
                raise ValueError("Executed bone partition differs")
            for lod, remap in enumerate(component["remaps"]):
                if lod not in expected["remaps"]:
                    if remap != {"state": 0, "path": ""}:
                        raise ValueError("Unassigned LOD must stay distinct from explicit identity")
                    continue
                if remap["state"] == 2:
                    path = Path(remap["path"])
                    data = path.read_bytes()
                    if len(data) != component["count"] * 2 or str(path) not in hashes:
                        # Path separators/casing may differ after native normalization.
                        matched = [p for p in hashes if Path(p).resolve() == path.resolve()]
                        if len(data) != component["count"] * 2 or len(matched) != 1:
                            raise ValueError("Unexpected remap file or size")
                    actual = list(struct.unpack("<" + "H" * component["count"], data))
                    remap_files.append(str(path))
                elif remap["state"] == 1 and not remap["path"]:
                    actual = []
                else:
                    raise ValueError("Initialized LOD has no explicit remap")
                if actual != expected["remaps"][lod]:
                    raise ValueError("Executed remap bytes differ from independent source")
        if any(c["count"] or c["first"] for c in plan["components"][len(active):]):
            raise ValueError("Unused declared component slot was not preserved")
        if any(sha(Path(path)) != expected for path, expected in hashes.items()):
            raise ValueError("Installed source changed during verification")
        (folder / "core-skeleton-plan.json").write_text(json.dumps(plan, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
        report.update(passed=True, source_sha256=hashes, executable_sha256=sha(exe), bones=plan["bones"],
                      instances=plan["instances"], declared_components=len(plan["components"]),
                      active_components=len(active), remap_files=remap_files, unknown_lod_slots=sum(
                          r["state"] == 0 for c in plan["components"] for r in c["remaps"]))
        print(json.dumps({k: v for k, v in report.items() if k not in ("source_sha256", "remap_files")}, indent=2))
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        report["error"] = str(exc)
        print(str(exc).encode("ascii", "backslashreplace").decode("ascii"), file=sys.stderr)
        return 1
    finally:
        (folder / "core-skeleton-report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="ascii")


if __name__ == "__main__":
    raise SystemExit(main())
