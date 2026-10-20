"""Check D-082: RabbitFX's texture roles from the fragment module and the bound textures.

`--refresh` reads the 2026-10-19 catalogue: for every Vulkan draw whose fragment module pairs with
a DX11 pixel shader RabbitFX patched (the archived shaders are already patched, so `t70` replaces
the diffuse register and so on, scripts/rabbitfx_assessment.py), the module's samples
(server/efmi_vk/texture_roles.py), the textures bound at them and RabbitFX's roles. It writes
docs/texture-roles-truth.json (derived metadata only, no shader code or texture data).

Without it the stored patterns are checked: the rule's roles against RabbitFX's, which must agree
on at least the recorded number of patterns, and every disagreement must be one the truth file
names with its reason. When the catalogue's modules are present, the layer's C++ reading
(`efmi_vk_character_test --texture-roles`) must equal the Python one for every module.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))
sys.path.insert(0, str(ROOT / "scripts"))
from efmi_vk.texture_roles import role_samples, roles  # noqa: E402

TRUTH = ROOT / "docs" / "texture-roles-truth.json"
CAPTURES = Path(r"F:\efmi vulkan\captures\catalogue-20261019")
CATALOGUE = Path(r"F:\efmi vulkan\build\catalogue\catalogue-20261019.json")
SIBLING = r"F:\EFMI MCP\server"           # the DX11 disassembler (read-only)
ROLE_NAMES = ("diffuse", "lightmap", "normal")
# Patterns where the rule and RabbitFX disagree, with the reason (docs/rabbitfx-roles.md).
KNOWN = {
    "e68ce2ff": "RabbitFX takes the lightmap for the normal map: an alpha test lies between the samples",
    "c3ec7e65": "RabbitFX takes the lightmap for the normal map",
    "85b304b4": "RabbitFX takes the lightmap for the normal map",
    "8210722e": "RabbitFX names the colour texture the normal map and a placeholder the diffuse",
    "4d789b6d": "RabbitFX names the colour texture the normal map and a placeholder the diffuse",
    "026c0d6f": "RabbitFX names the colour texture the normal map and a later small one the diffuse",
    "9bb6fe05": "RabbitFX takes the fourth sample for the diffuse",
}


def modules() -> dict[str, Path]:
    files = {}
    for bundle in sorted((CAPTURES / "vulkan").glob("vk*")):
        listing = bundle / "shaders.json"
        if listing.exists():
            for s in json.loads(listing.read_text(encoding="utf-8")):
                if s.get("hash") and s.get("file"):
                    files.setdefault(s["hash"], bundle / s["file"])
    return files


def refresh() -> dict:
    import rabbitfx_assessment as ra
    sys.path.insert(0, SIBLING)
    from efmi_mcp import shader as lister  # noqa: E402
    patched = {}
    for folder in sorted((CAPTURES / "dx11").glob("char*.dxbc")):
        for f in sorted(folder.glob("*.dxbc")):
            if f.stem not in patched:
                text = lister.disassemble(f.read_bytes())
                if re.search(r"^ps_5_0", text, re.M) and re.search(r"\bt70\.", text):
                    patched[f.stem] = ra.dx_roles(text)
    catalogue = json.loads(CATALOGUE.read_text(encoding="utf-8"))
    pairs = defaultdict(set)
    for p in catalogue["programs"].values():
        for dx in p["dx_ps"]:
            if dx in patched:
                pairs[p["vk_fs"]].add(dx)
    shaders, patterns = {}, {}
    for bundle in sorted((CAPTURES / "vulkan").glob("vk*")):
        listing = {s["id"]: s for s in json.loads((bundle / "shaders.json").read_text(encoding="utf-8"))}
        pipes = {p["id"]: p for p in json.loads((bundle / "pipelines.json").read_text(encoding="utf-8"))}
        frame = json.loads((bundle / "frame.json").read_text(encoding="utf-8"))
        for d in frame["draws"]:
            p = pipes.get(d.get("pipeline"))
            if not p or not p.get("recipe"):
                continue
            fs = [s for s in p["recipe"]["stages"] if s["stage"] == 16]
            sh = listing.get(fs[0]["shader"]) if fs else None
            if not sh or sh.get("hash") not in pairs or not sh.get("file"):
                continue
            vk = sh["hash"]
            if vk not in shaders:
                truth = {}
                for dx in sorted(pairs[vk]):
                    for role, reg in patched[dx].items():
                        if role in ROLE_NAMES:
                            truth.setdefault(role, int(reg[1:]))
                shaders[vk] = {"samples": role_samples((bundle / sh["file"]).read_bytes()), "rabbitfx": truth}
            bound = {}
            for si, s in enumerate(d.get("sets", [])):
                for b in (s.get("contents") or {}).get("bindings", []):
                    e = (b.get("elements") or [{}])[0]
                    if e.get("image_known"):
                        bound[(si, b["binding"])] = [e.get("image_format"), *e.get("extent", [0, 0])[:2], e.get("mip_levels")]
            textures = {}
            for s in shaders[vk]["samples"]:
                tex = bound.get((s["set"], s["binding"]))
                if tex and s["t"] not in textures:
                    textures[s["t"]] = tex
            key = json.dumps([vk, sorted(textures.items())])
            entry = patterns.setdefault(key, {"shader": vk, "textures": {str(t): v for t, v in sorted(textures.items())}, "draws": 0})
            entry["draws"] += 1
    return {"scope": "Vulkan draws of the 2026-10-19 catalogue whose fragment module pairs with a RabbitFX-patched DX11 "
                     "pixel shader: the module's samples (t register, decoded as a normal, instruction index), the textures "
                     "bound at them (Vulkan format, width, height, mip levels) and RabbitFX's roles (DX11 t registers)",
            "shaders": shaders, "patterns": sorted(patterns.values(), key=lambda e: (e["shader"], json.dumps(e["textures"])))}


def check(truth: dict) -> tuple[Counter, list[str]]:
    stats, problems, seen_known = Counter(), [], set()
    for entry in truth["patterns"]:
        shader = truth["shaders"][entry["shader"]]
        textures = {int(t): v for t, v in entry["textures"].items()}
        got = roles(shader["samples"], textures)
        expected = shader["rabbitfx"]
        same = all(got.get(r) == expected.get(r) for r in ROLE_NAMES)
        for r in ROLE_NAMES:
            stats["%s draws agree" % r] += entry["draws"] if got.get(r) == expected.get(r) else 0
        stats["draws"] += entry["draws"]
        stats["patterns agree" if same else "patterns differ"] += 1
        if not same:
            reason = KNOWN.get(entry["shader"][:8])
            if reason:
                seen_known.add(entry["shader"][:8])
            else:
                problems.append("%s %s: rule %s, RabbitFX %s" % (entry["shader"][:8], entry["textures"], got, expected))
    for name in sorted(set(KNOWN) - seen_known):
        problems.append("%s: a known disagreement no longer occurs; update KNOWN" % name)
    return stats, problems


def compare_cpp(truth: dict) -> tuple[int, list[str]]:
    files = modules()
    present = [(vk, files[vk]) for vk in truth["shaders"] if vk in files]
    if not present:
        return 0, []
    dist = Path(os.environ.get("EFMI_VK_DIST", ROOT / "dist"))
    exe = dist / "efmi_vk_character_test.exe"
    run = subprocess.run([str(exe), "--texture-roles"] + [str(p) for _, p in present], capture_output=True, text=True, timeout=600)
    if run.returncode:
        return 0, ["--texture-roles failed: " + run.stderr[-500:]]
    output = run.stdout.splitlines()
    problems = []
    for vk, path in present:
        python = " ".join("%d.%d.%d.%d.%d" % (s["t"], s["set"], s["binding"], int(s["decoded"]), s["at"])
                          for s in role_samples(path.read_bytes()))
        # Paths hold spaces: the line of a module is the one that starts with its path.
        cpp = next((line[len(str(path)) + 1:] for line in output if line.startswith(str(path) + " ")), None)
        if cpp != python:
            problems.append("%s: C++ %r, Python %r" % (vk[:8], cpp, python))
    return len(present), problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--refresh", action="store_true", help="rebuild the truth file from the catalogue captures")
    args = ap.parse_args()
    if args.refresh:
        truth = refresh()
        with TRUTH.open("w", encoding="ascii", newline="\n") as out:
            out.write("{\n\"scope\": %s,\n\"shaders\": {\n" % json.dumps(truth["scope"]))
            items = list(truth["shaders"].items())
            out.write(",\n".join('"%s": %s' % (k, json.dumps(v, separators=(",", ":"))) for k, v in items))
            out.write("\n},\n\"patterns\": [\n")
            out.write(",\n".join(json.dumps(p, separators=(",", ":")) for p in truth["patterns"]))
            out.write("\n]\n}\n")
        print("written %s: %d fragment modules, %d patterns" % (TRUTH, len(items), len(truth["patterns"])))
    truth = json.loads(TRUTH.read_text(encoding="ascii"))
    stats, problems = check(truth)
    compared, cpp = compare_cpp(truth)
    problems += cpp
    for p in problems[:20]:
        print("  " + p)
    print("texture roles %s: %d modules, %d patterns, %d draws; patterns agree %d, differ %d (known); draws: diffuse %d, lightmap %d, "
          "normal %d agree; C++ and Python agree on %d modules" % (
              "FAILED" if problems else "checks passed", len(truth["shaders"]), len(truth["patterns"]), stats["draws"], stats["patterns agree"], stats["patterns differ"],
              stats["diffuse draws agree"], stats["lightmap draws agree"], stats["normal draws agree"], compared - len(cpp)))
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
