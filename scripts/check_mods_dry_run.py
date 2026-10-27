"""Offline dry run of the installed character Mods through the production planner (D-078, D-079).

Runs `efmi_vk_character_test --plan-mods` on the installed EFMI Mods folder: every character INI is
read as the layer reads it and the live plan of each of its sections is made, with the source facts
the game's common vertex shaders give (rolling vs-t0 bones, vs-cb2). No GPU and no owned runtime,
so a Mod's other INIs and CustomShader sections are not part of these generations. Read-only.

Checked, as invariants that hold whatever Mods are installed: every Mod that loads draws its mesh in
at least one section, every refused section is refused for a known limit, and every Mod that loads
states how many components identify it (its own identification_min_components, else the Core's).
The numbers are printed for the record.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODS = Path(r"D:\XXMI-Launcher-Portable-v2.1.6\EFMI\Mods")
CORE = Path(r"D:\XXMI-Launcher-Portable-v2.1.6\EFMI\Core\EFMI")
# Limits recorded in docs/directory-generation.md; any other refusal is a regression.
KNOWN = ("validated blend-index contract",          # layout edits beyond the blend-index widening and restorable offsets
         "Shader resource fetch requires a GPU resource adapter",  # GPU work inside a matched draw (P2)
         "offline the game's vertex buffers are not known")       # `vbN->stride`, which the game's pipeline answers


def main() -> int:
    dist = Path(os.environ.get("EFMI_VK_DIST", ROOT / "dist"))
    exe = dist / "efmi_vk_character_test.exe"
    out = ROOT / "build" / "plan-mods"
    out.mkdir(parents=True, exist_ok=True)
    if not MODS.is_dir() or not exe.is_file():
        print("mods dry run: the installed Mods folder or %s is missing" % exe)
        return 1
    run = subprocess.run([str(exe), "--plan-mods", str(MODS), str(CORE), str(out / "settings")], cwd=out,
                         capture_output=True, text=True, errors="replace", timeout=600)
    (out / "plans.jsonl").write_text(run.stdout, encoding="utf-8")
    if run.returncode:
        print(run.stderr[-2000:]); return 1
    totals, problems, loaded, mods, thresholds = Counter(), [], 0, 0, Counter()
    for line in run.stdout.splitlines():
        mod = json.loads(line)
        mods += 1
        if not mod["loaded"]:
            totals["not loaded"] += 1
            continue
        loaded += 1
        thresholds[mod.get("identify_components", 0)] += 1
        if not 1 <= mod.get("identify_components", 0) <= 16:
            problems.append("%s: no identification threshold" % mod["ini"])
        drawn = 0
        for s in mod["sections"]:
            if not s["planned"]:
                totals["refused"] += 1
                if not any(k in s["reason"] for k in KNOWN):
                    problems.append("%s %s: %s" % (mod["ini"], s["hash"], s["reason"]))
                # Refused offline for the game's vertex buffer: the answer reaches the list, whose branches
                # then plan with some strides and not with others (the dry run probes eleven).
                if "vertex buffers are not known" in s["reason"] and not 0 < len(s.get("planned_with_vertex_strides", [])) < 11:
                    problems.append("%s %s: the vertex stride answered does not reach the list" % (mod["ini"], s["hash"]))
            elif s.get("textures"):
                totals["texture-only"] += 1
            elif "component" not in s:
                totals["hide applied" if s["skip"] else "hide not applied"] += 1
            elif not s["skip"]:
                totals["left to the game"] += 1
            elif s["draws"]:
                drawn += 1; totals["drawn"] += 1; totals["drawn with the Mod's images"] += s["images_bound"] > 0
            else:
                totals["hidden"] += 1
        if not drawn and not all(not s["planned"] for s in mod["sections"] if "component" in s):
            problems.append("%s: no section draws the Mod's mesh" % mod["ini"])
    for p in problems[:20]:
        print("  " + p)
    print("mods dry run: %d character INIs, %d load; sections: %s; identification thresholds: %s" % (
        mods, loaded, ", ".join("%s %d" % kv for kv in sorted(totals.items())),
        ", ".join("%d components %d Mods" % kv for kv in sorted(thresholds.items()))))
    return 1 if problems or not loaded else 0


if __name__ == "__main__":
    raise SystemExit(main())
