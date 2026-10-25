"""Check EFMITTL's hook rules on Vulkan (D-113) against the paired catalogue (ground truth, D-075).

The library recognises the game's lit character vertex shader and its copy shader by DX11 patterns.
The layer reads the same facts off SPIR-V (layer/src/shader_hooks.cpp). In the paired catalogue
(captures/catalogue-20261019: charNN DX11, vkNN Vulkan of the same scenes), a character draw of one
API pairs with the other's by index region hash, colour target count and order. Every Vulkan vertex
shader that pairs with the library's DX11 lit vertex shader must be recognised as lit, and none that
pairs only with other DX11 vertex shaders; the copy rule must name the game's copy shader and no
shader of a character draw. Read-only on the catalogue.

    python scripts/check_shader_hooks.py [--catalogue DIR] [--dist DIR]
"""
import argparse
import collections
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIT_DX11 = "1479b2b594b9c91a"  # the library's lit vertex shader on game 1.6.3 (translucency.ini)


def colours(pipe: dict) -> int:
    recipe = pipe.get("recipe") or {}
    subpasses = (recipe.get("render_pass") or {}).get("subpasses") or []
    sub = subpasses[recipe.get("subpass", 0)] if subpasses else {}
    return len(sub.get("colors", []))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--catalogue", type=Path, default=Path("F:/efmi vulkan/captures/catalogue-20261019"))
    ap.add_argument("--dist", type=Path, default=Path(os.environ.get("EFMI_VK_DIST", ROOT / "dist")))
    args = ap.parse_args()
    paired = collections.defaultdict(set)   # Vulkan VS hash -> DX11 VS hashes it pairs with
    files = {}                              # Vulkan shader hash -> SPIR-V file
    character_ps, fullscreen_ps = set(), set()
    for n in range(1, 17):
        dx_file, vk_dir = args.catalogue / "dx11" / ("char%02d.json" % n), args.catalogue / "vulkan" / ("vk%02d" % n)
        if not dx_file.is_file() or not vk_dir.is_dir():
            continue
        dx = json.loads(dx_file.read_text())
        shaders = {s["id"]: s["hash"] for s in dx["shaders"]}
        regions = dx.get("regions", [])
        dx_seq = collections.defaultdict(list)
        for d in dx["draws"]:
            ib = d.get("ib")
            if not ib or len(ib) < 4 or ib[3] is None or not 0 <= ib[3] < len(regions) or d["sh"][0] < 0:
                continue
            rts = len([o for o in d.get("out", []) if o[0] == 10])
            dx_seq[(regions[ib[3]][3], rts)].append(shaders.get(d["sh"][0], ""))
        frame = json.loads((vk_dir / "frame.json").read_text())
        pipes = {p["id"]: p for p in json.loads((vk_dir / "pipelines.json").read_text())}
        for s in json.loads((vk_dir / "shaders.json").read_text()):
            if s.get("file"):
                files.setdefault(s["hash"], vk_dir / s["file"])
        vk_seq = collections.defaultdict(list)
        for d in frame["draws"]:
            p = pipes.get(d["pipeline"], {})
            stages = {s["stage"]: s["hash"] for s in p.get("shaders", [])}
            if d["kind"] == "draw" and 16 in stages:
                fullscreen_ps.add(stages[16])
            if d["kind"] != "indexed" or not (d.get("ib") or {}).get("hash") or 1 not in stages:
                continue
            vk_seq[(d["ib"]["hash"], colours(p))].append(stages[1])
            if 16 in stages:
                character_ps.add(stages[16])
        for key, vk in vk_seq.items():
            dxs = dx_seq.get(key, [])
            if len(dxs) == len(vk):
                for a, b in zip(vk, dxs):
                    paired[a].add(b)
    names = sorted(set(paired) | character_ps | fullscreen_ps)
    names = [h for h in names if h in files]
    tool = args.dist / "efmi_vk_character_test.exe"
    facts = {}
    for chunk in range(0, len(names), 64):
        part = names[chunk:chunk + 64]
        out = subprocess.run([str(tool), "--shader-hooks"] + [str(files[h]) for h in part], capture_output=True, text=True, check=True).stdout
        for h, line in zip(part, out.splitlines()):
            _, lit, copy = line.rsplit(" ", 2)
            facts[h] = (lit == "1", copy == "1")
    problems = []
    lit_ok = [h for h in paired if LIT_DX11 in paired[h]]
    for h in sorted(paired):
        if h not in facts:
            continue
        expect = LIT_DX11 in paired[h]
        if facts[h][0] != expect:
            problems.append("vertex %s pairs with %s: lit %s, expected %s" % (h, sorted(paired[h]), facts[h][0], expect))
    copies = sorted(h for h in names if facts.get(h, (0, 0))[1])
    for h in copies:
        if h in character_ps:
            problems.append("fragment %s of a character draw is taken for the copy shader" % h)
    if not copies:
        problems.append("no copy shader recognised among the full-screen passes")
    print("shader hooks: %d paired vertex shaders (%d with the lit DX11 one), recognised lit %s; copy shaders %s" % (
        len(paired), len(lit_ok), sorted(h for h in paired if facts.get(h, (0, 0))[0]), copies))
    for line in problems:
        print("CHECK FAILED: " + line)
    if not problems:
        print("shader hook checks passed")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
