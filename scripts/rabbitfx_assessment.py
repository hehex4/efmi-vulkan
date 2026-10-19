"""Assessment of RabbitFX under Vulkan (docs/rabbitfx-assessment.md), from the 2026-10-19 catalogue.

The DX11 pixel shaders the probe archived are the ones the game had bound, i.e. already patched by
RabbitFX's [ShaderRegexMain]: each replaced sample reads `if_nz <t70 bound> sample t70 else sample
tN endif`, so tN is the role's original register (t70 diffuse, t71 lightmap, t72 normal map, t74
rain map). This script reads those roles, pairs each shader with its Vulkan fragment module through
the catalogue, and measures candidate SPIR-V rules: the role's register (by D-076) at a position of
the module's image sample sequence. Read-only: captures, catalogue, the installed RabbitFX.ini.

    py -B scripts/rabbitfx_assessment.py [--captures DIR] [--catalogue FILE]
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from collections import Counter
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))
from efmi_vk.source_slots import declarations, registers  # noqa: E402

DEFAULT_CAPTURES = Path(r"F:\efmi vulkan\captures\catalogue-20261019")
DEFAULT_CATALOGUE = Path(r"F:\efmi vulkan\build\catalogue\catalogue-20261019.json")
SIBLING = r"F:\EFMI MCP\server"           # the DX11 disassembler the baseline tool uses (read-only)
ROLES = {70: "diffuse", 71: "lightmap", 72: "normal", 74: "rain"}
DX_SAMPLE = re.compile(r"^\s*(?:sample\w*|ld\w*|gather\w*)\([^)]*\)\([^)]*\)\s+[^,]+,\s*[^,]+,\s*t(\d+)", re.M)
SAMPLE_OPS = set(range(87, 98)) | set(range(305, 316))   # image sample, fetch, gather and sparse forms
TRIVIAL = {61, 65, 66, 79, 80, 81, 86, 100, 124}         # load, access chain, shuffle, construct, extract, image, bitcast
FMUL = 133


def dx_roles(text: str) -> dict[str, str]:
    seq = [int(t) for t in DX_SAMPLE.findall(text)]
    roles = {}
    for k, t in enumerate(seq):
        if t in ROLES and k + 1 < len(seq) and seq[k + 1] < 60:
            roles.setdefault(ROLES[t], "t%d" % seq[k + 1])
    return roles


def spirv_samples(code: bytes) -> list[tuple[str, frozenset]]:
    """(DX11 register by D-076, non-trivial opcodes since the previous sample) per image sample."""
    words = struct.unpack("<%dI" % (len(code) // 4), code)
    regs = registers(declarations(code))
    deco: dict[int, dict] = {}
    origin, out, between = {}, [], set()
    i = 5
    while i < len(words):
        op, n = words[i] & 0xFFFF, words[i] >> 16
        o = words[i + 1:i + n]
        if op == 71 and len(o) > 2 and o[1] in (33, 34):        # Binding, DescriptorSet
            deco.setdefault(o[0], {})[o[1]] = o[2]
        elif op == 59:
            origin[o[1]] = o[1]
        elif op in (61, 65, 66, 86, 100) and len(o) > 2 and o[2] in origin:
            origin[o[1]] = origin[o[2]]
        if op in SAMPLE_OPS and len(o) > 2 and o[2] in origin:
            d = deco.get(origin[o[2]], {})
            reg = regs.get((d.get(34, 0), d.get(33)), {}).get("register", "?")
            out.append((reg, frozenset(between)))
            between = set()
        elif out and op not in TRIVIAL:
            between.add(op)
        i += n
    return out


def rule(samples: list[tuple[str, frozenset]]) -> dict[str, str]:
    """Candidate: diffuse = first sample; lightmap = the second when only multiplications lie
    between them (DX11's `(?:mul.*\\n)*?`); normal = the next sample after those."""
    got = {}
    if not samples:
        return got
    got["diffuse"] = samples[0][0]
    rest = samples[1:]
    if rest and rest[0][1] <= {FMUL}:
        got["lightmap"] = rest[0][0]
        rest = rest[1:]
    if rest:
        got["normal"] = rest[0][0]
    return got


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--captures", type=Path, default=DEFAULT_CAPTURES)
    ap.add_argument("--catalogue", type=Path, default=DEFAULT_CATALOGUE)
    args = ap.parse_args()
    sys.path.insert(0, SIBLING)
    from efmi_mcp import shader as lister  # noqa: E402
    dx_files = {}
    for folder in sorted((args.captures / "dx11").glob("char*.dxbc")):
        for f in sorted(folder.glob("*.dxbc")):
            dx_files.setdefault(f.stem, f)
    pixel, patched = 0, {}
    for h, f in dx_files.items():
        text = lister.disassemble(f.read_bytes())
        if not re.search(r"^ps_5_0", text, re.M):
            continue
        pixel += 1
        if re.search(r"\bt70\.", text):
            patched[h] = dx_roles(text)
    print("DX11 pixel shaders %d, patched by RabbitFX's main pattern %d" % (pixel, len(patched)))
    print("role sets:", ", ".join("%s %d" % ("+".join(k), v) for k, v in
                                  Counter(tuple(sorted(r)) for r in patched.values()).most_common()))
    files = {}
    for bundle in sorted((args.captures / "vulkan").glob("vk*")):
        listing = bundle / "shaders.json"
        if listing.exists():
            for s in json.loads(listing.read_text(encoding="utf-8")):
                if s.get("hash") and s.get("file"):
                    files.setdefault(s["hash"], bundle / s["file"])
    catalogue = json.loads(args.catalogue.read_text(encoding="utf-8"))
    pairs = sorted({(dx, p["vk_fs"]) for p in catalogue["programs"].values() for dx in p["dx_ps"]
                    if dx in patched and p["vk_fs"] in files})
    print("pairs with a Vulkan fragment module: %d (%d DX11 shaders)" % (len(pairs), len({d for d, _ in pairs})))
    position, score, whole = Counter(), Counter(), 0
    for dx, vk in pairs:
        samples = spirv_samples(files[vk].read_bytes())
        regs = [r for r, _ in samples]
        roles = patched[dx]
        for role, reg in roles.items():
            position[role, regs.index(reg) if reg in regs else None] += 1
        got = rule(samples)
        exact = True
        for role in ("diffuse", "lightmap", "normal"):
            ok = roles.get(role) == got.get(role)
            score[role, ok] += 1
            exact &= ok
        whole += exact
    for role in ("diffuse", "lightmap", "normal", "rain"):
        print("%-8s at SPIR-V sample index: %s" % (role, ", ".join(
            "%s: %d" % (k[1], v) for k, v in sorted(position.items(), key=lambda kv: str(kv[0][1])) if k[0] == role)))
    for role in ("diffuse", "lightmap", "normal"):
        print("candidate rule, %-8s right %d, wrong %d (absent counts as a value)" % (role, score[role, True], score[role, False]))
    print("candidate rule, all three roles right: %d of %d pairs" % (whole, len(pairs)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
