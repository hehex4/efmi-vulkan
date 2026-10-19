"""Static material envelope for a verified v1 character mesh package."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))
from efmi_vk.hashes import crc32c
from material_profiles import profile_shaders

NONE = 0xFFFFFFFF


def envelope(mesh: bytes, paths: list[str], profiles: list[dict], pixels: list[dict], device=(0, 0)) -> bytes:
    """No embedded DDS bytes: ResourceCache reads each immutable asset once."""
    body = bytearray(struct.pack("<6I", len(paths), len(profiles), len(pixels), *device, int(device != (0, 0))))
    for path in paths:
        raw = path.encode("utf-8")
        body += struct.pack("<I", len(raw)) + raw
    for profile in profiles:
        code = profile["code"]
        body += struct.pack("<2I", len(code) // 4, len(profile["rules"])) + code
        for rule in profile["rules"]:
            body += struct.pack("<4I", *rule)
    for draw in pixels:
        body += struct.pack("<I", len(draw))
        for slot, asset in sorted(draw.items()):
            body += struct.pack("<2I", slot, asset)
    if len(body) > 4 << 20:
        raise ValueError("Material table exceeds the decoder budget")
    return b"EFMICH02" + struct.pack("<2Q2I", len(mesh), len(body), crc32c(mesh + body), 0) + mesh + body


def build_materials(mesh: bytes, paths: list[str], pixels: list[dict], table_path: Path,
                    zero_report: Path | None) -> tuple[bytes, dict]:
    # Without a table the package carries no profiles: the layer derives every fragment shader's
    # rules from its module by the DX11 register rule (D-076).
    table = json.loads(table_path.read_text()) if table_path else {"slots": {}, "undeclared": []}
    shaders = profile_shaders(table) if table_path else {}
    device, policy = (0, 0), None
    if zero_report:
        # The measured NVIDIA result is a compatibility choice, not portable D3D11 semantics.
        evidence = json.loads(zero_report.read_text())
        hardware, = [r for r in evidence["runs"] if r["device"] == "hardware-normal"]
        if not evidence["passed"] or hardware["returncode"] or not hardware["controls_passed"] or any(
                hardware["pixels"][name] != [0, 0, 0, 0]
                for name in ("3d_shader_2d_view_z0", "3d_shader_2d_view_z05", "3d_shader_2d_view_z2")):
            raise ValueError("Dimension compatibility needs passing hardware controls")
        device = tuple(hardware["adapter"][:2])
        if device != (0x10DE, 11545):
            raise ValueError("Dimension-zero policy has no evidence for this device")
        policy = dict(name="dx11-invalid-2d-to-3d-zero", vendor=device[0], device=device[1],
                      evidence=str(zero_report.resolve()), sha256=hashlib.sha256(zero_report.read_bytes()).hexdigest(),
                      scope="Headless hardware result; actual game pixels not established")
    profiles, records = [], []
    for key, shader in sorted(shaders.items()):
        rules = {}
        for row in table["undeclared"]:
            if row["fragment"] == key:
                rules[int(row["slot"].split("-t")[1])] = (1, 0, 0)
        for slot, row in table["slots"].get(key, {}).items():
            logical = int(slot.split("-t")[1])
            if logical in rules:
                raise ValueError("A material slot is both absent and mapped")
            kind = 3 if policy and row.get("shader_image_dimension") == "3d" else 2
            rules[logical] = (kind, row["set"], row["binding"])
        profiles.append(dict(code=shader["path"].read_bytes(), rules=[(slot, *r) for slot, r in sorted(rules.items())]))
        records.append(dict(fragment=key, sha256=shader["sha256"], rules=profiles[-1]["rules"]))
    files = [dict(path=path, bytes=Path(path).stat().st_size,
                  sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest()) for path in paths]
    return envelope(mesh, paths, profiles, pixels, device), dict(
        table=str(table_path.resolve()) if table_path else None,
        table_sha256=hashlib.sha256(table_path.read_bytes()).hexdigest() if table_path else None,
        files=files, profiles=records, pixels=pixels, compatibility=policy,
        limits=["Fixed hosted scenario; no runtime keys or Core texture-identity query execution",
                "Unknown exact fragment code or logical slot retains the original draw"])
