"""Count the original INI operations of the enabled Mods and Core by family, variant and Mod.

Reads the original parser's own command objects (`host_probe.exe --parse-folder`, the
`operation_census` it prints) and the installed file inventory. Nothing is executed and no
installed file is written. The result says what real Mods use; it does not say what runs.
"""
from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path
import re
import subprocess

from ini_catalog import discover

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = Path("D:/XXMI-Launcher-Portable-v2.1.6/EFMI")
HOST = ROOT / "build/xxmi-host/host_probe.exe"


def owner_of(section: str, namespaces: dict[str, str], mods: str) -> str:
    """The Mod folder (or Core) a section came from. Unnamespaced sections carry their file
    path; namespaced ones carry the namespace, which the inventory maps to its file."""
    lowered = section.lower().replace("/", "\\")
    match = re.search(r"\\mods\\([^\\]+)\\", lowered)
    if match:
        return match.group(1)
    for namespace, folder in sorted(namespaces.items(), key=lambda kv: -len(kv[0])):
        if "\\" + namespace + "\\" in lowered or lowered.startswith(namespace + "\\") or lowered.endswith("\\" + namespace):
            return folder
    return "Core or shared" if section else "(unnamed)"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mods", type=Path, default=DEFAULT / "Mods")
    ap.add_argument("--core", type=Path, default=DEFAULT / "Core/EFMI")
    ap.add_argument("--out", type=Path, default=ROOT / "build/ini-operations")
    args = ap.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to(ROOT / "build"):
        raise SystemExit("Output must stay in the project build directory")
    out.mkdir(parents=True, exist_ok=True)
    inventory = discover(args.mods, args.core)
    mods_root = args.mods.resolve()
    namespaces = {}
    for row in inventory["files"]:
        path = Path(row["path"])
        if row.get("namespace") and path.is_relative_to(mods_root):
            namespaces[row["namespace"].lower()] = path.relative_to(mods_root).parts[0].lower()
        elif row.get("namespace"):
            namespaces[row["namespace"].lower()] = "Core or shared"
    run = subprocess.run([str(HOST), "--parse-folder", str(mods_root.parent), str(args.core.resolve()), str(mods_root)],
                         cwd=out, capture_output=True, encoding="utf-8", errors="replace", timeout=120)
    parsed = json.loads(run.stdout)
    by_key = collections.Counter()
    by_family = collections.Counter()
    by_owner = collections.defaultdict(collections.Counter)
    owners_of_key = collections.defaultdict(set)
    for row in parsed["operation_census"]:
        owner = owner_of(row["section"], namespaces, str(mods_root))
        family = row["key"].split(" ", 1)[0]
        by_key[row["key"]] += row["count"]
        by_family[family] += row["count"]
        by_owner[owner][family] += row["count"]
        owners_of_key[row["key"]].add(owner)
    report = dict(scope=__doc__.strip(), parser_exit=run.returncode, inputs=len(inventory["files"]),
                  families=dict(by_family.most_common()),
                  variants=[dict(key=k, count=c, owners=len(owners_of_key[k])) for k, c in by_key.most_common()],
                  owners={o: dict(c.most_common()) for o, c in sorted(by_owner.items())})
    (out / "report.json").write_text(json.dumps(report, indent=1, ensure_ascii=True) + "\n", encoding="ascii")
    print(f"{len(inventory['files'])} INI inputs, {sum(by_family.values())} commands, {len(by_key)} variants, {len(by_owner)} owners")
    for key, count in by_key.most_common():
        if key.split(" ", 1)[0] in ("IfCommand", "VariableAssignment", "ElseIfCommand"):
            continue
        print(f"{count:6d} {len(owners_of_key[key]):3d} owners  {key}")
    return 0 if run.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
