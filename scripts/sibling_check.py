"""Prepare the game's real variant fragment shaders with their derived rules through the production code.

Usage: sibling_check.py DIR (after sibling_replay.py); uses build/next/efmi_vk_character_test.exe.

For each derivation of derive_replay.py: the native preparation must accept the module with the
rules, every draw the sibling supports must be supported, the image counts must be ones the arenas
exist for, and the native list of image variables must equal the one the replay used.
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(sys.argv[1])
EXE = ROOT / "build" / "next" / "efmi_vk_character_test.exe"
PACKAGE = ROOT / "build" / "character" / "arcane-materials.bin"
manifest = json.loads(PACKAGE.with_suffix(".json").read_text(encoding="utf-8"))
known = json.loads(subprocess.run([str(EXE), "--materials", str(PACKAGE)], capture_output=True, text=True, check=True).stdout)
order = [p["fragment"] for p in manifest["materials"]["profiles"]]
by_fragment = {name: known["profiles"][i]["draws"] for i, name in enumerate(order)}

derived = json.loads((OUT / "derived-rules.json").read_text(encoding="utf-8"))
good = 0
seen = set()
for d in derived:
    key = (d["fragment"], json.dumps(d["rules"]))
    args = [str(EXE), "--derived", str(PACKAGE), str(OUT / f"fs-{d['fragment']}.spv")] + [":".join(str(v) for v in r) for r in d["rules"]]
    proc = subprocess.run(args, capture_output=True, text=True)
    if proc.returncode:
        print(d["component"], d["fragment"], "FAILED TO RUN", proc.stderr.strip()[:200])
        continue
    got = json.loads(proc.stdout)
    sibling = by_fragment[d["sibling"]]
    same_support = [x["ok"] for x in got["draws"]] == [x["ok"] for x in sibling]
    same_assets = [[i[0] for i in x["images"]] for x in got["draws"]] == [[i[0] for i in x["images"]] for x in sibling]
    same_nulls = [len(x["nulls"]) for x in got["draws"]] == [len(x["nulls"]) for x in sibling]
    # The replay listed every UniformConstant variable, plain samplers too; the native list has images only.
    subset = all(i in d["images"] for i in got["images"])
    rule_places = all([r[2], r[3]] in got["images"] for r in d["rules"] if r[1] != 1)
    ok = got["readable"] and got["prepared"] and got["counts_covered"] and subset and rule_places and same_support and same_assets and same_nulls
    good += ok
    if key not in seen or not ok:
        moved = sorted({(i[1], i[2]) for x in got["draws"] for i in x["images"]})
        print(("ok  " if ok else "BAD ") + f"{d['component']:16s} fs {d['fragment']} <- {d['sibling']} | prepared {got['prepared']} {got['reason']} | images {len(got['images'])}"
              f" | draws supported {sum(x['ok'] for x in got['draws'])}/{len(got['draws'])} like the sibling {same_support}, same textures {same_assets}, nulls {same_nulls}"
              f" | file bindings {moved}")
    seen.add(key)
print("derivations:", len(derived), "| accepted by the production preparation and equal to the sibling's plans:", good)
