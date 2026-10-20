"""DX11 registers of a game shader's resources, derived from its SPIR-V alone (D-076).

The game builds every shader twice from one HLSL source: FXC for DX11, DXC for Vulkan. Both keep
the source's declaration order, and the DX11 build places read-only buffers (StructuredBuffer,
ByteAddressBuffer, typed Buffer) ahead of textures. So the t register of a resource is its rank
among the module's shader resource views, buffers first, each group in declaration order. This
was found and checked against every slot the 2026-10-19 catalogue placed (16 characters) and the
earlier arcane frames: no exception. It needs no DX11 data and no capture of a character, which is
what makes Mods' fixed slots (`ps-t16 = ...`) work on shaders nobody captured.

Constant buffers follow the declaration order of uniform blocks (cb0, cb1, ...); the Vulkan block
is the same size or larger (layout padding) in all but a few cases, so this is a size-checked
rule, not a verified one. Samplers and UAVs are numbered by declaration order too but have no
ground truth yet; their entries say so.
"""

from __future__ import annotations

import struct

SRV_BUFFERS = ("srv-buffer", "srv-texbuf")
SRV_IMAGES = ("srv-image",)


def declarations(code: bytes) -> list[dict]:
    """Resource variables in module order: kind, set, binding, register count and block size.

    Kinds: srv-image, srv-texbuf, srv-buffer (read-only storage buffer), uav-image, uav-texbuf,
    uav-buffer, sampler, cbuffer. Variables without a binding are left out.
    """
    if len(code) < 20 or len(code) % 4:
        raise ValueError("SPIR-V size is not a word multiple")
    words = struct.unpack("<%dI" % (len(code) // 4), code)
    if words[0] != 0x07230203:
        raise ValueError("not a SPIR-V module")
    deco, member_ro, member_off, types, consts, variables = {}, {}, {}, {}, {}, []
    i = 5
    while i < len(words):
        op, n = words[i] & 0xFFFF, words[i] >> 16
        if n == 0 or i + n > len(words):
            raise ValueError("malformed SPIR-V instruction")
        o = words[i + 1:i + n]
        if op == 71:                                  # OpDecorate
            deco.setdefault(o[0], {})[o[1]] = o[2] if len(o) > 2 else True
        elif op == 72:                                # OpMemberDecorate
            if o[2] == 24:                            # NonWritable
                member_ro.setdefault(o[0], set()).add(o[1])
            elif o[2] == 35:                          # Offset
                member_off.setdefault(o[0], {})[o[1]] = o[3]
        elif op == 43:                                # OpConstant
            consts[o[1]] = o[2] if len(o) > 2 else 0
        elif 19 <= op <= 39:                          # type declarations
            types[o[0]] = (op, o[1:])
        elif op == 59:                                # OpVariable
            variables.append((o[1], o[0], o[2]))
        i += n

    def size(tid: int) -> int:
        t = types.get(tid)
        if not t:
            return 0
        op, a = t
        if op in (21, 22):
            return a[0] // 8
        if op in (23, 24):
            return a[1] * size(a[0])
        if op == 28:
            stride = deco.get(tid, {}).get(6) or size(a[0])
            return consts.get(a[1], 0) * stride
        if op == 30:
            offsets = member_off.get(tid, {})
            if not offsets:
                return sum(size(m) for m in a)
            last = max(offsets, key=offsets.get)
            return offsets[last] + size(a[last])
        return 0

    out = []
    for vid, ptr, storage in variables:
        d = deco.get(vid, {})
        if 33 not in d:                               # Binding
            continue
        pointee = types.get(ptr)
        tid = pointee[1][1] if pointee and pointee[0] == 32 else None
        t, count = types.get(tid), 1
        if t and t[0] in (28, 29):                    # array of resources
            count = consts.get(t[1][1], 1) if t[0] == 28 else 0
            tid = t[1][0]
            t = types.get(tid)
        kind, block, stride = "other", None, None
        if t:
            if t[0] == 25:                            # OpTypeImage: dim, depth, arrayed, ms, sampled
                prefix = "uav-" if t[1][5] == 2 else "srv-"
                kind = prefix + ("texbuf" if t[1][1] == 5 else "image")
            elif t[0] == 27:                          # OpTypeSampledImage
                kind = "srv-image"
            elif t[0] == 26:
                kind = "sampler"
            elif t[0] == 30:
                td = deco.get(tid, {})
                if storage == 12 or 3 in td:          # StorageBuffer or BufferBlock
                    readonly = 24 in d or 24 in td or len(member_ro.get(tid, ())) == len(t[1])
                    kind = "srv-buffer" if readonly else "uav-buffer"
                    last = t[1][-1] if t[1] else None   # a structured buffer: its array's element stride
                    if last in types and types[last][0] in (28, 29):
                        stride = deco.get(last, {}).get(6)
                elif storage == 2:                    # Uniform
                    kind = "cbuffer"
                    block = size(tid)
        item = {"kind": kind, "set": d.get(34, 0), "binding": d[33], "count": count, "size": block}
        if stride:
            item["stride"] = stride
        out.append(item)
    return out


def registers(decls: list[dict]) -> dict[tuple[int, int], dict]:
    """DX11 register of every resource: {(set, binding): {"register": "t3", "rule": ...}}."""
    out = {}

    def number(items, letter, rule):
        reg = 0
        for item in items:
            out[(item["set"], item["binding"])] = {"register": "%s%d" % (letter, reg), "rule": rule, "kind": item["kind"]}
            reg += max(item["count"], 1)

    number([x for x in decls if x["kind"] in SRV_BUFFERS] + [x for x in decls if x["kind"] in SRV_IMAGES], "t", "verified")
    number([x for x in decls if x["kind"] == "cbuffer"], "cb", "size-checked")
    number([x for x in decls if x["kind"] == "sampler"], "s", "unverified")
    number([x for x in decls if x["kind"].startswith("uav-")], "u", "unverified")
    return out


def binding_of(decls: list[dict], register: str) -> tuple[int, int] | None:
    """The Vulkan (set, binding) holding a DX11 register such as "t16", or None."""
    for where, item in registers(decls).items():
        if item["register"] == register:
            return where
    return None


def core_facts(decls: list[dict]) -> dict:
    """What the original EFMIv1 Core reads off a vertex shader (D-079), by its own rules on the
    registers above: Component_ReadConfig takes the first of vs-cb2, vs-cb1, vs-cb3 holding 4096
    vectors; MergedSkeleton_DetectBoneDataSource the rolling bones when vs-t0 is a buffer of 16-byte
    elements and such a window exists. {"slot": 1..3 or -1, "rolling": bool, "config": [set, binding]
    or None, "bones": [set, binding] or None}."""
    regs = registers(decls)
    by_register = {r["register"]: (where, r) for where, r in regs.items()}
    sizes = {(d["set"], d["binding"]): d for d in decls}
    facts = {"slot": -1, "rolling": False, "config": None, "bones": None}
    for slot in (2, 1, 3):
        found = by_register.get("cb%d" % slot)
        if found and sizes[found[0]].get("count", 1) == 1 and sizes[found[0]].get("size") == 4096 * 16:
            facts["slot"], facts["config"] = slot, list(found[0])
            break
    t0 = by_register.get("t0")
    if t0 and t0[1]["kind"] == "srv-buffer" and sizes[t0[0]].get("count", 1) == 1 and sizes[t0[0]].get("stride") == 16:
        facts["bones"] = list(t0[0])
    facts["rolling"] = facts["bones"] is not None and facts["slot"] != -1
    return facts

