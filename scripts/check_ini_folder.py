"""Parse the full enabled Mods/Core INI corpus with the pinned XXMI frontend.

No game, graphics device, shader compilation, input dispatch or installed writes.
Resources and render states are parsed as declarations. Renderer compatibility is
reported separately from language acceptance, never implied by a parse success.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from ini_catalog import discover

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = Path("D:/XXMI-Launcher-Portable-v2.1.6/EFMI")


def check(mods: Path, core: Path, out: Path) -> dict:
    out = out.resolve()
    if not out.is_relative_to(ROOT / "build"):
        raise ValueError("Inspection output must stay in the project build directory")
    out.mkdir(parents=True, exist_ok=True)
    inventory = discover(mods, core)
    installation = mods.resolve().parent
    (out / "inventory.json").write_text(json.dumps(inventory, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
    host = ROOT / "build/xxmi-host/host_probe.exe"
    compiled = json.loads(host.with_name("report.json").read_text())
    if not compiled.get("passed") or hashlib.sha256(host.read_bytes()).hexdigest() != compiled["executable_sha256"]:
        raise ValueError("First build and pass scripts/probe_xxmi_host.py")
    for filename, expected in compiled["source_sha256"].items():
        if hashlib.sha256(Path(filename).read_bytes()).hexdigest() != expected:
            raise ValueError("Rebuild changed original-engine host: " + filename)
    result = subprocess.run([str(host), "--parse-folder", str(installation), str(core.resolve()), str(mods.resolve())],
                            cwd=out, capture_output=True, encoding="utf-8", errors="replace", timeout=90)
    (out / "diagnostics.txt").write_text(result.stderr.encode("ascii", "backslashreplace").decode(), encoding="ascii")
    (out / "parser-output.txt").write_text(result.stdout.encode("ascii", "backslashreplace").decode(), encoding="ascii")
    try:
        parsed = json.loads(result.stdout)
    except json.JSONDecodeError:
        parsed = None
    unchanged = all(hashlib.sha256(Path(row["path"]).read_bytes()).hexdigest() == row["sha256"]
                    for row in inventory["files"])
    report = dict(scope="Original XXMI syntax, references, declarations and command parsing; no Vulkan execution",
                  passed=result.returncode == 0 and parsed is not None and unchanged,
                  exit_code=result.returncode, source_unchanged=unchanged,
                  executable_sha256=hashlib.sha256(host.read_bytes()).hexdigest(),
                  inputs=len(inventory["files"]), parsed=parsed, inventory=str(out / "inventory.json"),
                  diagnostics=str(out / "diagnostics.txt"),
                  renderer_gaps=["ShaderRegex assembly patches", "General CustomShader draw/dispatch",
                                 "Panel and Core GUI rendering", "Mutable resource copy and GPU readback",
                                 "Multi-Mod runtime scheduling and complete folder execution"])
    (out / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mods", type=Path, default=DEFAULT / "Mods")
    parser.add_argument("--core", type=Path, default=DEFAULT / "Core/EFMI")
    parser.add_argument("--out", type=Path, default=ROOT / "build/ini-folder")
    args = parser.parse_args()
    report = check(args.mods, args.core, args.out)
    concise = dict(report)
    if concise["parsed"]:
        concise["parsed"] = {k: v for k, v in concise["parsed"].items() if k not in ("resource_declarations", "command_types")}
    print(json.dumps(concise, indent=2, ensure_ascii=True))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
