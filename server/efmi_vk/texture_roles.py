"""RabbitFX's texture roles in a game draw, from its fragment module and the bound textures (D-082).

RabbitFX (a ShaderRegex library for EFMI) rewrites the game's character pixel shaders so that a
Mod's textures at t70 to t72 replace three samples: the diffuse map, the lightmap after it and the
normal map. Under Vulkan the shaders are not rewritten; the layer finds the same three samples and
binds the Mod's textures at their DX11 registers (D-076) instead. The rule, measured against the
RabbitFX-patched DX11 shaders of the 2026-10-19 catalogue (docs/rabbitfx-roles.md):

- The samples of the module in instruction order, each with the t register of its image, whether
  its result is decoded as a tangent-space normal (scaled by 2, then a square root of something
  derived from it, within a few steps of data flow) and its instruction index.
- Each distinct register is classified by the texture bound there: "S" an sRGB asset texture, "N"
  a two-channel one (BC5), "U" another linear block-compressed one, "o" anything else. An asset
  texture is square with a full mip chain; render targets, lookup tables and placeholders are not.
- The diffuse map is the first "S". A "U" right after it, of the same size, is the lightmap. The
  normal map is the first "N" after those, else the first sample decoded as a normal within 100
  instructions of the previous role's sample, else the next "U".

Shared by scripts/check_texture_roles.py; the layer has the same rule in C++
(frontend/gpu/texture_roles.cpp).
"""

from __future__ import annotations

import struct

from .source_slots import declarations, registers

SAMPLE_OPS = set(range(87, 98)) | set(range(305, 316))   # image sample, fetch, gather and sparse forms
ORIGIN_OPS = (61, 65, 66, 86, 100)                       # load, access chains, sampled image, image
GLSL_SQRT, GLSL_INVERSE_SQRT = 31, 32
NEAR = 100                                               # instructions between a role's sample and the normal's
DECODE_DEPTH = 12                                        # data-flow steps searched from a sample's result
SRGB = {43, 50, 57, 132, 134, 136, 138, 146}              # VK_FORMAT_*_SRGB of 8-bit colour and BC1 to BC7
TWO_CHANNEL = {141, 142}                                  # BC5
LINEAR_BC = {131, 133, 135, 137, 139, 145}                # BC1 to BC4 and BC7 UNORM


def _flow_operands(op: int, o: list[int]) -> list[int]:
    """The ids a value-producing instruction reads, for the ones the normal decode passes through."""
    if op in (79, 82, 142, 148):          # VectorShuffle, CompositeInsert, VectorTimesScalar, Dot
        return o[2:4]
    if op in (81, 124):                   # CompositeExtract, Bitcast
        return o[2:3]
    if op in (80, 127, 128, 129, 130, 131, 133, 136):  # CompositeConstruct, FNegate, IAdd, FAdd, ISub, FSub, FMul, FDiv
        return o[2:]
    if op == 12:                          # ExtInst: set, instruction, operands
        return o[4:]
    return []


def role_samples(code: bytes) -> list[dict]:
    """Every image sample of the module in order: {"t", "set", "binding", "decoded", "at"}.
    Samples of something other than a t register (none in the game's shaders) are left out."""
    if len(code) < 20 or len(code) % 4:
        raise ValueError("SPIR-V size is not a word multiple")
    words = struct.unpack("<%dI" % (len(code) // 4), code)
    if words[0] != 0x07230203:
        raise ValueError("not a SPIR-V module")
    regs = registers(declarations(code))
    insts = []
    i = 5
    while i < len(words):
        op, n = words[i] & 0xFFFF, words[i] >> 16
        if n == 0 or i + n > len(words):
            raise ValueError("malformed SPIR-V instruction")
        insts.append((op, list(words[i + 1:i + n])))
        i += n
    float32, twos = set(), set()
    for op, o in insts:
        if op == 22 and len(o) > 1 and o[1] == 32:                   # OpTypeFloat 32
            float32.add(o[0])
        elif op == 43 and len(o) > 2 and o[0] in float32 and o[2] == 0x40000000:  # OpConstant 2.0
            twos.add(o[1])
    deco, origin, users, samples = {}, {}, {}, []
    for at, (op, o) in enumerate(insts):
        if op == 71 and len(o) > 2 and o[1] in (33, 34):             # Binding, DescriptorSet
            deco.setdefault(o[0], {})[o[1]] = o[2]
        elif op == 59 and len(o) > 1:                                # OpVariable
            origin[o[1]] = o[1]
        elif op in ORIGIN_OPS and len(o) > 2 and o[2] in origin:
            origin[o[1]] = origin[o[2]]
        if op in SAMPLE_OPS and len(o) > 2 and o[2] in origin:
            d = deco.get(origin[o[2]], {})
            reg = regs.get((d.get(34, 0), d.get(33)), {}).get("register", "")
            if reg.startswith("t"):
                samples.append({"t": int(reg[1:]), "set": d.get(34, 0), "binding": d.get(33), "result": o[1], "at": at})
        for a in _flow_operands(op, o):
            users.setdefault(a, []).append((op, o))

    def decoded(start: int) -> bool:
        seen, frontier, scaled, rooted = {start}, [start], False, False
        for _ in range(DECODE_DEPTH):
            nxt = []
            for v in frontier:
                for op, o in users.get(v, []):
                    if o[1] in seen:
                        continue
                    seen.add(o[1])
                    nxt.append(o[1])
                    if op in (133, 142) and any(a in twos for a in o[2:4]):
                        scaled = True
                    if op == 12 and len(o) > 4 and o[3] in (GLSL_SQRT, GLSL_INVERSE_SQRT):
                        rooted = True
            frontier = nxt
        return scaled and rooted

    for s in samples:
        s["decoded"] = decoded(s.pop("result"))
    return samples


def texture_class(fmt: int, width: int, height: int, levels: int) -> str:
    if not (width and width == height and levels > 1 and (1 << (levels - 1)) == width):
        return "o"
    if fmt in SRGB:
        return "S"
    if fmt in TWO_CHANNEL:
        return "N"
    if fmt in LINEAR_BC:
        return "U"
    return "o"


def roles(samples: list[dict], textures: dict) -> dict[str, int]:
    """{"diffuse": t, "lightmap": t, "normal": t} for the roles found. `textures` maps a t register
    to (format, width, height, levels) of the texture bound there, or has no entry for an unknown one."""
    seq, seen = [], set()
    for s in samples:
        if s["t"] in seen:
            continue
        seen.add(s["t"])
        tex = textures.get(s["t"])
        seq.append((s["t"], texture_class(*tex) if tex else "o", tuple(tex[1:3]) if tex else None, s["decoded"], s["at"]))
    got = {}
    first = next((i for i, x in enumerate(seq) if x[1] == "S"), None)
    if first is None:
        return got
    diffuse = seq[first]
    got["diffuse"] = diffuse[0]
    rest, previous = seq[first + 1:], diffuse[4]
    if rest and rest[0][1] == "U" and rest[0][2] == diffuse[2]:
        got["lightmap"] = rest[0][0]
        previous = rest[0][4]
        rest = rest[1:]
    normal = next((x for x in rest if x[1] == "N"), None)
    if normal is None:
        normal = next((x for x in rest if x[3] and x[1] in ("S", "U", "N") and x[4] - previous <= NEAR), None)
    if normal is None and rest and rest[0][1] == "U":
        normal = rest[0]
    if normal is not None:
        got["normal"] = normal[0]
    return got
