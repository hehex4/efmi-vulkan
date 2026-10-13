"""Read exact historical vertex semantic correspondences, without guessing.

The paired capture records declaration order on both APIs. Every corresponding
element must agree in stream/offset/format, and repeated shader locations must
agree in semantic name/index. This is observed profile data, not a general
DXBC-to-SPIR-V semantic reconstruction algorithm.
"""
from __future__ import annotations
import json
import re
import struct
from check_mod_draw_plans import ROOT, sha

DX_TO_VK = {2:109, 6:106, 16:103, 41:100, 11:91, 12:95, 10:97, 28:37, 30:41, 31:38}


def historical_vertex_profiles() -> dict:
    table = json.loads((ROOT / "docs/arcane-texture-slots.json").read_text())
    for source in table["sources"].values():
        if sha(ROOT / source["path"]) != source["sha256"]:
            raise ValueError("Vertex pairing evidence fingerprint changed")
    pairing = json.loads((ROOT / table["sources"]["pairing"]["path"]).read_text())
    manifest_path = ROOT / table["sources"]["vulkan_manifest"]["path"]
    manifest = json.loads(manifest_path.read_text())
    folder = manifest_path.parent
    if sha(folder / "shaders.json") != manifest["files"]["shaders.json"]["sha256"]:
        raise ValueError("Original vertex shader inventory changed")
    shaders = json.loads((folder / "shaders.json").read_text())
    result = {}
    for obj in pairing["objects"]:
        for pair in obj.get("pairs", []):
            dx, vk = pair["dx_vertex_input"], pair["vk_vertex_input"]
            if not vk or not dx["elements"] or len(dx["elements"]) != len(vk["attributes"]):
                continue
            # Only retain complete same-order correspondences, not partial matches.
            elements = list(zip(dx["elements"], vk["attributes"]))
            if any((a["slot"], a["offset"], DX_TO_VK.get(a["format"])) !=
                   (b["binding"], b["offset"], b["format"]) for a, b in elements):
                continue
            key = pair["vk_vs"]
            matching = [s for s in shaders if s["hash"] == key]
            if not matching:
                raise ValueError("Original paired vertex bytecode is missing")
            digests = {sha(folder / s["file"]) for s in matching}
            if len(digests) != 1 or any(sha(folder / s["file"]) != manifest["files"][s["file"]]["sha256"] for s in matching):
                raise ValueError("Changed or colliding original vertex shader")
            semantics = []
            for a, b in elements:
                match = re.fullmatch(r"([A-Z_]+)(\d*)", a["semantic"])
                if not match:
                    raise ValueError("Ambiguous displayed source semantic name")
                semantics.append(dict(location=b["location"], index=int(match[2] or 0), name=match[1]))
            semantics.sort(key=lambda s: s["location"])
            entry = dict(sha256=digests.pop(), semantics=semantics)
            if key in result and result[key] != entry:
                raise ValueError("Repeated vertex semantic correspondence disagrees")
            result[key] = entry
    return result


def encode_vertex(vertex: dict) -> bytes:
    code = vertex["code"]
    data = bytearray(struct.pack("<I", len(code)//4)) + code
    data += struct.pack("<I", len(vertex["semantics"]))
    for s in vertex["semantics"]:
        name = s["name"].encode("ascii")
        data += struct.pack("<3I", s["location"], s["index"], len(name)) + name
    data += struct.pack("<I", len(vertex["bindings"]))
    for b in vertex["bindings"]:
        data += struct.pack("<3I", b["binding"], b["stride"], b["per_instance"])
    data += struct.pack("<I", len(vertex["attributes"]))
    for a in vertex["attributes"]:
        data += struct.pack("<4I", a["location"], a["binding"], a["format"], a["offset"])
    return bytes(data)


def reference_vertex(source: dict, files: dict) -> tuple[list, list]:
    semantics = {s["location"]: s for s in source["semantics"]}
    bindings = [[b["binding"], files[b["binding"]]["stride"] if b["binding"] in files else b["stride"], int(b["per_instance"])]
                for b in source["bindings"]]
    attributes = []
    for a in source["attributes"]:
        s = semantics[a["location"]]
        # Independent known sample rule, not a second general layout evaluator.
        # The original initializer uses ElementFormat(BLENDINDICES,0) on vb2.
        fmt = 95 if s["name"] == "BLENDINDICES" and s["index"] == 0 and a["binding"] == 2 else a["format"]
        attributes.append([a["location"], a["binding"], fmt, a["offset"]])
    return bindings, attributes
