"""Pair what a draw binds under DX11 with what the same draw binds under Vulkan.

The DX11 side comes from a capture of the sibling project's probe (EFMI MCP), the Vulkan side
from a frame of this project's probe. Hashes of resources cannot be compared between the two
APIs, but a region hash of index data can (same bytes, same rule), and so can what a texture or
a buffer looks like: shape, size, mip count, format. Everything here works on plain
dictionaries, so it runs without either probe.
"""

from __future__ import annotations

import re
from collections import OrderedDict

# VkFormat -> the DXGI name of the same layout. A depth format is given the name its DX11 view has.
VK_FORMAT_NAMES = {
    9: "R8_UNORM", 16: "R8G8_UNORM", 37: "R8G8B8A8_UNORM", 38: "R8G8B8A8_SNORM", 41: "R8G8B8A8_UINT",
    43: "R8G8B8A8_UNORM_SRGB", 44: "B8G8R8A8_UNORM", 50: "B8G8R8A8_UNORM_SRGB", 64: "R10G10B10A2_UNORM",
    70: "R16_UNORM", 76: "R16_FLOAT", 77: "R16G16_UNORM", 83: "R16G16_FLOAT", 91: "R16G16B16A16_UNORM",
    95: "R16G16B16A16_UINT", 97: "R16G16B16A16_FLOAT", 100: "R32_FLOAT", 103: "R32G32_FLOAT",
    106: "R32G32B32_FLOAT", 109: "R32G32B32A32_FLOAT", 122: "R11G11B10_FLOAT", 124: "R16_UNORM",
    126: "R32_FLOAT", 129: "R24_UNORM_X8_TYPELESS", 130: "R32_FLOAT_X8X24_TYPELESS",
    131: "BC1_UNORM", 132: "BC1_UNORM_SRGB", 133: "BC1_UNORM", 134: "BC1_UNORM_SRGB", 135: "BC2_UNORM",
    136: "BC2_UNORM_SRGB", 137: "BC3_UNORM", 138: "BC3_UNORM_SRGB", 139: "BC4_UNORM", 140: "BC4_SNORM",
    141: "BC5_UNORM", 142: "BC5_SNORM", 143: "BC6H_UF16", 144: "BC6H_SF16", 145: "BC7_UNORM",
    146: "BC7_UNORM_SRGB",
}

# VkImageViewType -> shape
VK_VIEW_SHAPES = {0: "1d", 1: "2d", 2: "3d", 3: "cube", 4: "1d_array", 5: "2d_array", 6: "cube_array"}

# Descriptor types that carry a sampled image, a uniform buffer, a storage buffer.
VK_IMAGE_TYPES = (1, 2)
VK_UNIFORM_TYPES = (6, 8)
VK_STORAGE_TYPES = (7, 9)

DX_STAGE_BITS = {"vs": 1, "hs": 2, "ds": 4, "gs": 8, "ps": 16, "cs": 32}

_SECTION = re.compile(r"^\s*\[(.+?)\]\s*$")
_ASSIGN = re.compile(r"^\s*([^=;]+?)\s*=\s*(.*?)\s*$")
_DRAW_SECTION = re.compile(r"^commandlist_draw_component(\d+)$", re.IGNORECASE)
_SLOT = re.compile(r"^(vs|ps|hs|ds|gs|cs)-(t|cb)\d+$", re.IGNORECASE)
_DCL_RESOURCE = re.compile(r"^\s*dcl_resource_\w+.*?\bt(\d+)\b", re.MULTILINE)
_DCL_CONSTANTS = re.compile(r"^\s*dcl_constantbuffer\s+CB(\d+)\[(\d+)\]", re.MULTILINE | re.IGNORECASE)


def parse_declarations(listing: str) -> dict:
    """Which slots a DX11 shader declares, from its assembly listing.

    A slot that is bound but not declared is left over from an earlier draw and must not take
    part in a pairing. Returns {"t": [slot, ...], "cb": {slot: constants}}.
    """
    return {"t": sorted({int(m.group(1)) for m in _DCL_RESOURCE.finditer(listing)}),
            "cb": {int(m.group(1)): int(m.group(2)) for m in _DCL_CONSTANTS.finditer(listing)}}


def declared(declarations: dict | None, shaders: dict, slot: str) -> bool:
    """False only when the shader of the slot's stage is known not to declare it."""
    stage, _, rest = slot.partition("-")
    decl = (declarations or {}).get(shaders.get(stage))
    if decl is None:
        return True
    if rest.startswith("cb"):
        return int(rest[2:]) in {int(k) for k in decl["cb"]}
    return int(rest[1:]) in decl["t"]


def dx_texture_sig(resource: dict | None, view: dict | None = None) -> tuple | None:
    """(shape, width, height, depth, layers, mips, format name) of a texture bound under DX11."""
    if not resource or resource.get("kind") not in ("tex1d", "tex2d", "tex3d"):
        return None
    kind = resource["kind"]
    layers = int(resource.get("array", 1) or 1)
    dimension = (view or {}).get("dimension")
    if kind == "tex3d":
        shape = "3d"
    elif dimension == "cube" or (kind == "tex2d" and resource.get("misc", 0) & 4 and dimension is None):
        shape = "cube"
    elif kind == "tex1d":
        shape = "1d" if layers == 1 else "1d_array"
    else:
        shape = "2d" if layers == 1 else "2d_array"
    name = (view or {}).get("format")
    if not name or name == "UNKNOWN" or name.endswith("TYPELESS"):
        name = resource.get("fmt_name") or "dxgi%s" % resource.get("fmt")
    mips = (view or {}).get("mip_levels")
    if not isinstance(mips, int) or mips <= 0:
        mips = int(resource.get("mips", 1) or 1)
    return (shape, int(resource.get("w", 1)), int(resource.get("h", 1)), int(resource.get("d", 1)), layers, mips, name)


def vk_image_sig(element: dict | None) -> tuple | None:
    """The same tuple for a sampled image element of a Vulkan descriptor set."""
    if not element or not element.get("image_known") or "extent" not in element:
        return None
    sub = element.get("subresource") or {}
    fmt = element.get("format", element.get("image_format"))
    extent = element["extent"]
    return (VK_VIEW_SHAPES.get(element.get("view_type"), "view%s" % element.get("view_type")),
            int(extent[0]), int(extent[1]), int(extent[2]), int(sub.get("layers", element.get("array_layers", 1))),
            int(sub.get("levels", element.get("mip_levels", 1))), VK_FORMAT_NAMES.get(fmt, "vk%s" % fmt))


def match_by_key(dx_items: list[dict], vk_items: list[dict]) -> dict:
    """Pairs items whose ``key`` is equal. One on each side is a pair; more is an ambiguous group.

    Nothing is guessed: an item without a counterpart stays in ``dx_only`` or ``vk_only``.
    """
    buckets: "OrderedDict[object, tuple[list, list]]" = OrderedDict()
    for item in dx_items:
        buckets.setdefault(item["key"], ([], []))[0].append(item)
    for item in vk_items:
        buckets.setdefault(item["key"], ([], []))[1].append(item)
    out = {"pairs": [], "ambiguous": [], "dx_only": [], "vk_only": []}
    for key, (dx, vk) in buckets.items():
        if len(dx) == 1 and len(vk) == 1:
            out["pairs"].append({"key": key, "dx": dx[0], "vk": vk[0]})
        elif dx and vk:
            out["ambiguous"].append({"key": key, "dx": dx, "vk": vk})
        elif dx:
            out["dx_only"].extend(dx)
        else:
            out["vk_only"].extend(vk)
    return out


def match_textures(dx_items: list[dict], vk_items: list[dict]) -> dict:
    """Pairs textures by content where both sides know it, and by look otherwise.

    An item may carry ``content``, the CRC-32C of mip 0 of its texture. A content value that occurs
    once on each side is a pair, whatever else looks like it. The rest is matched by ``key`` as in
    :func:`match_by_key`, except that two textures whose known contents differ are never paired.
    Every pair says ``how`` it was found: "content" or "look".
    """
    by_content: "OrderedDict[str, tuple[list, list]]" = OrderedDict()
    for side, items in enumerate((dx_items, vk_items)):
        for item in items:
            if item.get("content"):
                by_content.setdefault(item["content"], ([], []))[side].append(item)
    pairs, taken = [], set()
    for content, (dx, vk) in by_content.items():
        if len(dx) == 1 and len(vk) == 1:
            pairs.append({"key": dx[0]["key"], "dx": dx[0], "vk": vk[0], "how": "content"})
            taken.update((id(dx[0]), id(vk[0])))
    rest = match_by_key([i for i in dx_items if id(i) not in taken], [i for i in vk_items if id(i) not in taken])
    out = {"pairs": pairs, "ambiguous": rest["ambiguous"], "dx_only": rest["dx_only"], "vk_only": rest["vk_only"]}
    for pair in rest["pairs"]:
        a, b = pair["dx"].get("content"), pair["vk"].get("content")
        if a and b and a != b:
            out["dx_only"].append(pair["dx"])
            out["vk_only"].append(pair["vk"])
        else:
            out["pairs"].append(dict(pair, how="look"))
    return out


def dx_windows(constant_buffers: dict) -> list[dict]:
    """Constant buffer bindings of one DX11 draw, merged where two stages see the same window.

    ``constant_buffers`` maps "vs-cb2" to {"res", "first_constant", "num_constants", ...}.
    """
    windows: "OrderedDict[tuple, dict]" = OrderedDict()
    for slot, cb in constant_buffers.items():
        ident = (cb["res"], cb["first_constant"], cb["num_constants"])
        w = windows.setdefault(ident, {"slots": [], "stages": 0, "bytes": cb["num_constants"] * 16,
                                       "res": cb["res"], "first_constant": cb["first_constant"]})
        w["slots"].append(slot)
        w["stages"] |= DX_STAGE_BITS.get(slot.split("-")[0], 0)
    return list(windows.values())


def vk_set_items(sets: list[dict]) -> dict:
    """Sampled images, uniform buffers and storage buffers of the sets one Vulkan draw has bound."""
    out = {"images": [], "uniform": [], "storage": []}
    for index, bound in enumerate(sets or []):
        contents = bound.get("contents") or {}
        for binding in contents.get("bindings") or []:
            for element in binding.get("elements") or []:
                where = {"set": index, "binding": binding["binding"], "element": element.get("element", 0),
                         "stages": binding.get("stages")}
                if binding["type"] in VK_IMAGE_TYPES:
                    sig = vk_image_sig(element)
                    if sig:
                        content = element.get("content") or {}
                        out["images"].append({**where, "sig": sig, "image": element.get("image"),
                                              "content": content.get("crc") if content.get("state") == "hashed" else None})
                elif binding["type"] in VK_UNIFORM_TYPES and element.get("known"):
                    out["uniform"].append({**where, "range": element.get("range"), "buf": element.get("buf"),
                                           "dynamic": binding["type"] == 8, "src": element.get("src")})
                elif binding["type"] in VK_STORAGE_TYPES and element.get("known"):
                    out["storage"].append({**where, "range": element.get("range"), "buf": element.get("buf"),
                                           "src": element.get("src")})
    return out


def round_to_window(size: int) -> int:
    """A DX11.1 constant buffer window is a multiple of 16 constants; a Vulkan range is exact."""
    return (int(size) + 255) // 256 * 256


def group_uses(draws: list[dict]) -> list[list[dict]]:
    """Draws of one object grouped by where and with which shaders they are drawn, in frame order.

    Each draw has ``targets`` (anything hashable naming its outputs) and ``vs`` / ``ps``. A mod that
    draws one part as several ranges yields several draws in one group.
    """
    groups: "OrderedDict[tuple, list[dict]]" = OrderedDict()
    for d in draws:
        groups.setdefault((d.get("targets"), d.get("vs"), d.get("ps")), []).append(d)
    return list(groups.values())


def parse_draw_sections(path: str) -> dict[int, dict]:
    """``[CommandList_Draw_ComponentN]`` sections of an EFMI mod: index ranges drawn and slots set.

    Conditions are ignored: every ``drawindexed`` of a section is listed, whichever key state it
    belongs to. Returns {N: {"ranges": [(count, first), ...], "slots": {"ps-t15": "name", ...}}}.
    """
    found: dict[int, dict] = {}
    current = None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            stripped = line.strip()
            if not stripped or stripped.startswith(";"):
                continue
            m = _SECTION.match(line)
            if m:
                ds = _DRAW_SECTION.match(m.group(1).strip())
                current = found.setdefault(int(ds.group(1)), {"ranges": [], "slots": {}}) if ds else None
                continue
            if current is None:
                continue
            m = _ASSIGN.match(line)
            if not m:
                continue
            key, value = m.group(1).strip().lower(), m.group(2)
            if key == "drawindexed":
                parts = [p.strip() for p in value.split(",")]
                try:
                    rng = (int(parts[0]), int(parts[1]) if len(parts) > 1 else 0)
                except ValueError:
                    continue
                if rng not in current["ranges"]:
                    current["ranges"].append(rng)
            elif _SLOT.match(key):
                current["slots"].setdefault(key, value)
    return found
