"""Live packages for every character Mod of the installed directory, without captures (D-075, D-076).

    package_directory.py [--corpus build/mod-corpus/report.json] [--out build/characters]

For each character INI whose skeleton initializer and draw callbacks the corpus planned
(scripts/check_mod_corpus.py), this builds the mesh and material package from those plans with
static pixel bindings and no fragment profiles (the layer derives every shader's rules from its
module), prepares the live sidecar (the INI and Core are read again at run time), and checks the
result with the production decoder. A Mod the corpus could not plan, or that fails here, is
listed with its reason; nothing is skipped silently and no installed file is written.

This is a step toward the directory generation: the packages still come from plans of each Mod
in isolation, so Mods that need files of other Mods or libraries at planning time are refused
by the corpus until the generation covers the whole directory.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
DECODER = ROOT / "build" / "next" / "efmi_vk_character_test.exe"


def safe_name(namespace: str) -> str:
    return re.sub(r"[^A-Za-z0-9_]", "_", namespace).strip("_").lower() or "mod"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--corpus", type=Path, default=ROOT / "build/mod-corpus/report.json")
    ap.add_argument("--out", type=Path, default=ROOT / "build/characters")
    ap.add_argument("--decoder", type=Path, default=DECODER)
    args = ap.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to(ROOT / "build"):
        raise ValueError("Packages belong inside this project's build directory")
    out.mkdir(parents=True, exist_ok=True)
    corpus = json.loads(args.corpus.read_text(encoding="utf-8"))
    rows, names = [], set()
    for row in corpus["rows"]:
        ini = Path(row["ini"])
        entry = {"ini": str(ini), "namespace": row["namespace"]}
        rows.append(entry)
        if not row.get("skeleton_passed") or not row.get("callbacks_passed"):
            entry["result"] = "not planned"
            entry["reason"] = row.get("callback_error") or ("skeleton initializer failed" if not row.get("skeleton_passed") else "unknown")
            continue
        name = safe_name(row["namespace"])
        while name in names:
            name += "_"
        names.add(name)
        folder = Path(row["output"])
        package, live = out / (name + ".bin"), out / (name + "-live.bin")
        core = ini.parents[2] / "Core" / "EFMI"
        build = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/package_character.py"), "--mod", str(ini),
                                "--plans", str(folder / "mod-plans.json"), "--skeleton", str(folder / "core-skeleton-plan.json"),
                                "--textures", "--no-profiles", "--out", str(package)], capture_output=True, text=True)
        if build.returncode:
            entry["result"], entry["reason"] = "package refused", build.stderr.strip().splitlines()[-1][:300]
            continue
        # The sidecar's namespace names the settings; the INI keeps its own (implicit or explicit).
        settings = name if name.replace("_", "").isalnum() else "mod"
        prepare = subprocess.run([sys.executable, "-B", str(ROOT / "scripts/prepare_runtime.py"), "--package", str(package),
                                  "--report", str(package.with_suffix(".json")), "--ini", str(ini), "--core", str(core),
                                  "--namespace", settings, "--out", str(live)], capture_output=True, text=True)
        if prepare.returncode:
            entry["result"], entry["reason"] = "live profile refused", prepare.stderr.strip().splitlines()[-1][:300]
            continue
        inspect = subprocess.run([str(args.decoder), "--inspect", str(live)], capture_output=True, text=True)
        if inspect.returncode:
            entry["result"], entry["reason"] = "decoder refused", (inspect.stdout + inspect.stderr).strip()[:300]
            continue
        facts = json.loads(inspect.stdout)
        entry.update(result="packaged", package=str(live), sha256=hashlib.sha256(live.read_bytes()).hexdigest(),
                     materials=facts.get("materials"), images=facts.get("images"), profiles=facts.get("profiles"))
    report = {"schema": 1,
              "scope": "Live packages built from isolated corpus plans without captures; not loaded into any process by this script.",
              "corpus": str(args.corpus.resolve()), "rows": rows,
              "packaged": sum(1 for r in rows if r.get("result") == "packaged"), "characters": len(rows)}
    (out / "directory.json").write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    for r in rows:
        print("%-24s %-20s %s" % (r["namespace"][:24], r["result"], r.get("reason", "")[:90]))
    print("packaged %d of %d character Mods -> %s" % (report["packaged"], report["characters"], out / "directory.json"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
