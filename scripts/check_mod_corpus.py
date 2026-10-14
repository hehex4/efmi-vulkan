"""Exercise merged-skeleton asset plans for every discovered character INI.

The whole folder is parsed independently by check_ini_folder.py. This additional
fixture executes each exported mesh callback and bone initializer in isolation.
It preserves unresolved renderer/library operations as failures, not no-ops.
It is a coverage report, not an all-Mod runtime loader or a rendering claim.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ini-report", type=Path, default=ROOT / "build/ini-folder/report.json")
    parser.add_argument("--out", type=Path, default=ROOT / "build/mod-corpus")
    args = parser.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to(ROOT / "build"):
        raise ValueError("Corpus results belong in project build")
    out.mkdir(parents=True, exist_ok=True)
    parsed = json.loads(args.ini_report.read_text())
    if not parsed["passed"]:
        raise ValueError("Full folder syntax must pass before isolated callback tests")
    inventory = json.loads(Path(parsed["inventory"]).read_text())
    host = ROOT / "build/xxmi-host/host_probe.exe"
    if hashlib.sha256(host.read_bytes()).hexdigest() != parsed["executable_sha256"]:
        raise ValueError("Host changed since folder parsing")
    rows = []
    for source in inventory["files"]:
        path = Path(source["path"])
        if not path.is_relative_to(Path(inventory["mods"])):
            continue
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != source["sha256"]:
            raise ValueError("Source INI changed: " + str(path))
        # Feature-based fixture selection only. All declarations and commands
        # were already parsed by the original engine, including other Mods.
        if not re.search(r"(?im)^\s*\[CommandListInitializeMergedSkeleton\]\s*$", data.decode("utf-8-sig")):
            continue
        name = source["namespace"]
        if not name:
            raise ValueError("Merged-skeleton fixture needs an explicit namespace")
        folder = out / hashlib.sha256(str(path).encode()).hexdigest()[:16]
        folder.mkdir(exist_ok=True)
        row = dict(ini=str(path), namespace=name, output=str(folder))
        skeleton = subprocess.run([str(host), "--skeleton-mod", str(path), inventory["core"], name.lower()],
                                  cwd=folder, capture_output=True, text=True, errors="replace", timeout=45)
        (folder / "skeleton-errors.txt").write_text(skeleton.stderr.encode("ascii", "backslashreplace").decode(), encoding="ascii")
        row["skeleton_passed"] = skeleton.returncode == 0
        if not skeleton.returncode:
            plan = json.loads(skeleton.stdout)
            (folder / "core-skeleton-plan.json").write_text(json.dumps(plan, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
            row.update(bones=plan["bones"], active_components=sum(bool(c["count"]) for c in plan["components"]))
        result = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/check_mod_draw_plans.py"),
                                 "--mod", str(path), "--out", str(folder)],
                                cwd=ROOT, capture_output=True, text=True, errors="replace", timeout=120)
        callback = json.loads((folder / "mod-plan-report.json").read_text())
        row["callbacks_passed"] = result.returncode == 0 and callback["passed"]
        row["callback_error"] = callback.get("error")
        for key in ("plans", "draws", "unique_index_ranges", "texture_files", "rabbit_texture_roles"):
            if key in callback:
                row[key] = callback[key]
        rows.append(row)
        print(json.dumps(row, ensure_ascii=True), flush=True)
    result = dict(scope=__doc__.strip(), characters=len(rows),
                  skeletons_passed=sum(row["skeleton_passed"] for row in rows),
                  callbacks_passed=sum(row["callbacks_passed"] for row in rows),
                  rows=rows, renderer_compatible=False)
    (out / "report.json").write_text(json.dumps(result, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
    print(f"Corpus audit: {result['skeletons_passed']}/{len(rows)} skeletons, {result['callbacks_passed']}/{len(rows)} callback fixtures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
