"""Run original Core spatial identification against synthetic tracked CPU windows."""
from __future__ import annotations
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from check_mod_draw_plans import ROOT, SAMPLE, sha


def main() -> int:
    folder = ROOT / "build/xxmi-host"
    report = {"scope": "original Core spatial state machine, synthetic CPU windows only", "passed": False}
    try:
        compiled = json.loads((folder / "report.json").read_text())
        exe = folder / "host_probe.exe"
        if not compiled.get("passed") or sha(exe) != compiled["executable_sha256"]:
            raise ValueError("First build and pass scripts/probe_xxmi_host.py")
        for filename, expected in compiled["source_sha256"].items():
            if sha(Path(filename)) != expected:
                raise ValueError(f"Rebuild after changed host input: {filename}")
        core = SAMPLE.parents[2] / "Core/EFMI"
        hashes = {str(core / name): sha(core / name) for name in
                  ("API.ini", "MergedSkeleton.ini", "SpatialIdentification.ini", "Helpers.ini")}
        result = subprocess.run([str(exe), "--spatial-core", str(core)], cwd=folder,
                                env=dict(os.environ, TEMP=str(folder / "build/tmp"), TMP=str(folder / "build/tmp")),
                                capture_output=True, encoding="utf-8", errors="replace", timeout=30)
        output = result.stdout + result.stderr
        (folder / "core-spatial-native.txt").write_text(output.encode("ascii", "backslashreplace").decode("ascii"), encoding="ascii")
        counts = re.search(r"XXMI original spatial Core: (\d+) checks passed, (\d+) failed", output)
        if result.returncode or not counts or int(counts[2]) or int(counts[1]) < 12:
            raise ValueError(f"Original spatial Core refused: {result.returncode}; see core-spatial-native.txt")
        if any(sha(Path(p)) != expected for p, expected in hashes.items()):
            raise ValueError("Installed Core changed during verification")
        report.update(passed=True, source_sha256=hashes, executable_sha256=sha(exe),
                      checks_passed=int(counts[1]), checks_failed=int(counts[2]))
        print(counts[0])
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        report["error"] = str(exc)
        print(str(exc).encode("ascii", "backslashreplace").decode("ascii"), file=sys.stderr)
        return 1
    finally:
        (folder / "core-spatial-report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="ascii")


if __name__ == "__main__":
    raise SystemExit(main())
