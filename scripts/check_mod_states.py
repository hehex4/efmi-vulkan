"""Every installed Mod in every state its keys reach, planned as the layer plans its draws (D-137).

Runs `--plan-folder-states` on the installed EFMI Mods folder (read-only), one generation for the
folder as the original loads it: the default state, then each key action's keys (as the original
parsed them) held for a presentation and released, up to eight presses or until the variables repeat a
state; after each, every Mod's sections are planned. The original's own dispatch decides what a key
changes (cycle, toggle, hold, conditions). A section the layer refuses in any state is a place where a
player sees the game's model or a broken one: the check fails and lists it with the first state it
appeared in. The offline stand-ins' own limits (no game pipeline) are not counted.
No GPU. Writes build/mod-states/states.jsonl.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODS = Path(os.environ.get("EFMI_VK_CHECK_MODS", r"D:\XXMI-Launcher-Portable-v2.1.6\EFMI\Mods"))
CORE = Path(os.environ.get("EFMI_VK_CHECK_CORE", r"D:\XXMI-Launcher-Portable-v2.1.6\EFMI\Core\EFMI"))


def main() -> int:
    dist = Path(os.environ.get("EFMI_VK_DIST", ROOT / "dist"))
    exe = dist / "efmi_vk_character_test.exe"
    out = ROOT / "build" / "mod-states"
    out.mkdir(parents=True, exist_ok=True)
    if not MODS.is_dir() or not exe.is_file():
        print("mod states: the installed Mods folder or %s is missing" % exe)
        return 1
    result = subprocess.run([str(exe), "--plan-folder-states", str(MODS), str(CORE), str(out / "settings")], cwd=out,
                            capture_output=True, text=True, errors="replace", timeout=1800)
    (out / "states.jsonl").write_text(result.stdout, encoding="utf-8")
    if result.stderr.strip():
        (out / "stderr.txt").write_text(result.stderr, encoding="utf-8")
    lines = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
    if result.returncode or not lines:
        print("mod states: the dry run failed: %s" % result.stderr[-1500:])
        return 1
    summary, mods = lines[0], lines[1:]
    print("mod states: %d key actions, %d states, %d Mods" % (summary["key_actions"], summary["states"], len(mods)))
    for error in summary.get("present_errors", []):
        print("  PRESENT REFUSED: %s" % error)
    failed = bool(summary.get("present_errors"))
    reasons = Counter()
    for mod in mods:
        refusals = mod.get("refusals", [])
        if not mod.get("loaded"):
            print("  NOT LOADED: %s: %s" % (mod["ini"], mod.get("reason", "")))
            failed = True
            continue
        if not refusals:
            continue
        failed = True
        print("  %s: %d sections refused in some state (%d plans)" % (mod["ini"], len(refusals), mod.get("plans", 0)))
        for r in refusals[:12]:
            where = "%s c%s lod%s" % (r["hash"], r.get("component", "-"), r.get("lod", "-"))
            print("    %s in %s: %s" % (where, r["state"], r["reason"]))
            reasons[r["reason"]] += 1
        if len(refusals) > 12:
            print("    ... %d more" % (len(refusals) - 12))
            for r in refusals[12:]:
                reasons[r["reason"]] += 1
    if reasons:
        print("refusal reasons:")
        for reason, n in reasons.most_common():
            print("  %4d  %s" % (n, reason))
    print("mod states: %s" % ("FAILED" if failed else "every section plans in every state"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
