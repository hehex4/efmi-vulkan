"""Resolve observed material profiles to fingerprinted source shader bytes."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def profile_shaders(table: dict) -> dict:
    for source in table["sources"].values():
        if digest(ROOT / source["path"]) != source["sha256"]:
            raise ValueError("Material profile evidence fingerprint changed")
    known = set(table["slots"]) | {row["fragment"] for row in table["undeclared"]}
    assignments = table.get("fragment_sources", {})
    if set(assignments) - known:
        raise ValueError("Shader source assigned without a material profile")
    inventories, result = {}, {}
    for key in sorted(known):
        name = assignments.get(key, "vulkan_manifest")
        if name not in inventories:
            source = table["sources"][name]
            folder = (ROOT / source["path"]).parent
            manifest = json.loads((ROOT / source["path"]).read_text())
            inventory = folder / "shaders.json"
            if digest(inventory) != manifest["files"]["shaders.json"]["sha256"]:
                raise ValueError("Material shader inventory changed")
            inventories[name] = folder, manifest, json.loads(inventory.read_text())
        folder, manifest, inventory = inventories[name]
        candidates = [shader for shader in inventory if shader["hash"] == key]
        if not candidates:
            raise ValueError(f"Mapped shader {key} is missing from {name}")
        fingerprints = set()
        for shader in candidates:
            path = folder / shader["file"]
            fingerprint = digest(path)
            if fingerprint != manifest["files"][shader["file"]]["sha256"]:
                raise ValueError("Material shader bytes changed")
            fingerprints.add(fingerprint)
        if len(fingerprints) != 1:
            raise ValueError("Material CRC key refers to different shader bytes")
        result[key] = dict(path=folder / candidates[0]["file"], id=candidates[0]["id"],
                           sha256=fingerprints.pop(), manifest=name)
    return result
