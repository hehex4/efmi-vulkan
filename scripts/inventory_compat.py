"""Read-only, lexical compatibility inventory and SHA-256 manifest of local inputs.

This preserves duplicate sections and command order. It is not an INI interpreter
or a claim that every scanned Core section is reachable by the acceptance mod.
Generated reports contain metadata only, never mesh/texture/shader payloads.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import ctypes
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SECTION_KINDS = ("TextureOverride", "ShaderOverride", "ShaderRegex", "CustomShader", "CommandList",
                 "Resource", "Constants", "Present", "Include", "Key", "Pool", "Preset")
CONTROL = re.compile(r"^(else if|if|elif|else|endif|while|endwhile|break|continue)(?:\s|$)", re.I)
API = re.compile(r"(?:\$|Resource|CommandList|Pool|CustomShader)\\EFMIv1\\[\w.]+", re.I)


def fingerprint(path: Path, base: Path) -> dict:
    with path.open("rb") as f:
        digest = hashlib.file_digest(f, "sha256").hexdigest()
    return {"path": path.relative_to(base).as_posix(), "bytes": path.stat().st_size, "sha256": digest}


def lexical_ini(path: Path, base: Path) -> dict:
    # Read bytes without executing includes, commands or resource loads.
    text = path.read_text(encoding="utf-8-sig", errors="strict")
    sections, statements, api = [], [], defaultdict(list)
    current, kind = "", "preamble"
    for line_number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith((";", "#")):
            continue
        if line.startswith("[") and "]" in line:
            current = line[1:line.index("]")]
            kind = next((k for k in SECTION_KINDS if current.lower().startswith(k.lower())), "other")
            sections.append({"name": current, "kind": kind, "line": line_number})
            continue
        # Semicolon comments are stripped only outside quoted strings.
        quoted, body = False, []
        for char in line:
            if char == '"':
                quoted = not quoted
            if char == ";" and not quoted:
                break
            body.append(char)
        line = "".join(body).strip()
        if not line:
            continue
        post = bool(re.match(r"^(pre|post)\s+", line, re.I))
        expr = re.sub(r"^(pre|post)\s+", "", line, flags=re.I)
        control = CONTROL.match(expr)
        if control:
            command = control.group(1).lower()
        elif "=" in expr:
            lhs = expr.split("=", 1)[0].strip()
            if lhs.lower().startswith(("global ", "local ")):
                command = "variable declaration"
            elif lhs.startswith("$"):
                command = "variable assignment"
            elif "->" in lhs:
                command = "method assignment"
            elif kind == "Pool" and lhs.lower().startswith("pool_"):
                command = lhs.lower()
            elif re.match(r"^(Resource|CommandList|Pool)", lhs, re.I):
                command = re.match(r"^(Resource|CommandList|Pool)", lhs, re.I).group(1).lower() + " assignment"
            else:
                command = lhs.lower()
        elif expr.lower().startswith(("global ", "local ")):
            command = "variable declaration"
        elif "->" in expr:
            command = "method call"
        else:
            command = "raw section body"
        statements.append({"line": line_number, "section": current, "kind": kind,
                           "command": command, "phase_prefix": post, "text": line})
        for ref in set(API.findall(line)):
            api[ref].append(line_number)
    return dict(fingerprint(path, base), sections=sections, statements=statements, efmiv1=dict(sorted(api.items())))


def tree_manifest(base: Path) -> dict:
    files = [fingerprint(p, base) for p in sorted(base.rglob("*")) if p.is_file()]
    canonical = json.dumps(files, sort_keys=True, ensure_ascii=True, separators=(",", ":")).encode("ascii")
    return {"files": files, "tree_sha256": hashlib.sha256(canonical).hexdigest()}


def file_version(path: Path) -> str:
    api = ctypes.windll.version
    size = api.GetFileVersionInfoSizeW(str(path), None)
    if not size:
        return "unavailable"
    data = ctypes.create_string_buffer(size)
    if not api.GetFileVersionInfoW(str(path), 0, size, data):
        raise OSError("cannot read file version")
    pointer, length = ctypes.c_void_p(), ctypes.c_uint()
    if not api.VerQueryValueW(data, "\\", ctypes.byref(pointer), ctypes.byref(length)):
        raise OSError("cannot query file version")
    words = ctypes.cast(pointer, ctypes.POINTER(ctypes.c_uint32))
    return ".".join(str(n) for n in (words[2] >> 16, words[2] & 65535, words[3] >> 16, words[3] & 65535))


def compact_snapshot(data: dict) -> dict:
    snapshot = {k: v for k, v in data.items() if k != "ini"}
    snapshot["ini"] = []
    for item in data["ini"]:
        commands = defaultdict(list)
        for s in item["statements"]:
            commands[s["command"]].append(s["line"])
        texts = "\n".join(s["text"] for s in item["statements"])
        snapshot["ini"].append({k: v for k, v in item.items() if k != "statements"} | {
            "commands": dict(sorted(commands.items())),
            "methods": sorted(set(re.findall(r"->([A-Za-z_][A-Za-z0-9_]*)", texts))),
            "statement_count": len(item["statements"]),
        })
    return snapshot


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--efmi", type=Path, default=Path("D:/XXMI-Launcher-Portable-v2.1.6/EFMI"))
    ap.add_argument("--mod", default="Mods/arcane fes 1.2")
    ap.add_argument("--game", type=Path, default=Path("D:/Hypergryph Launcher/games/EndField Game"))
    ap.add_argument("--output", type=Path, default=ROOT / "build/inventory/local-inputs.json")
    snapshots = ap.add_mutually_exclusive_group()
    snapshots.add_argument("--snapshot", type=Path, help="write a compact metadata-only manifest inside the project")
    snapshots.add_argument("--check-snapshot", type=Path, help="fail if current inputs differ from a saved manifest")
    args = ap.parse_args()
    args.output.resolve().relative_to(ROOT)
    core, mod = args.efmi / "Core/EFMI", args.efmi / args.mod
    data = {"schema": 1, "efmi_root": str(args.efmi), "scope": "lexical, not evaluated",
            "core": tree_manifest(core), "mod": tree_manifest(mod),
            "d3d11": fingerprint(args.efmi / "d3d11.dll", args.efmi),
            "d3dx_ini": fingerprint(args.efmi / "d3dx.ini", args.efmi), "ini": []}
    data["d3d11"]["file_version"] = file_version(args.efmi / "d3d11.dll")
    data["game"] = {"root": str(args.game), "identity": "selected binary and content-index fingerprints; not a marketing version",
                    "files": []}
    for name in ("Endfield.exe", "UnityPlayer.dll", "GameAssembly.dll", "config.ini", "game_files", "package_files",
                 "Endfield_Data/boot.config", "Endfield_Data/StreamingAssets/index_initial.json",
                 "Endfield_Data/StreamingAssets/index_main.json"):
        path = args.game / name
        info = fingerprint(path, args.game)
        if path.suffix.lower() in (".dll", ".exe"):
            info["file_version"] = file_version(path)
        data["game"]["files"].append(info)
    data["reference_sources"] = {}
    for name in ("XXMI-Libs-Package", "wwmi-dx12"):
        path = Path("F:/vulkan") / name
        commit = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
        data["reference_sources"][name] = commit
    package = Path("F:/vulkan/XXMI-PACKAGE-v1.2.2.zip")
    with zipfile.ZipFile(package) as archive:
        release_hash = hashlib.sha256(archive.read("d3d11.dll")).hexdigest()
    data["xxmi_release"] = dict(fingerprint(package, package.parent), tag="v1.2.2", dll_sha256=release_hash,
                                local_dll_matches=release_hash == data["d3d11"]["sha256"])
    for base in (core, mod):
        for path in sorted(base.rglob("*.ini")):
            item = lexical_ini(path, args.efmi)
            data["ini"].append(item)
            commands = Counter(s["command"] for s in item["statements"])
            print(f"{item['path']}: {len(item['sections'])} sections, {len(item['statements'])} statements")
            print("  " + ", ".join(f"{k} ({v})" for k, v in sorted(commands.items())))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
    if args.snapshot:
        args.snapshot.resolve().relative_to(ROOT)
        args.snapshot.parent.mkdir(parents=True, exist_ok=True)
        args.snapshot.write_text(json.dumps(compact_snapshot(data), indent=2, ensure_ascii=True) + "\n", encoding="ascii")
    if args.check_snapshot:
        expected = json.loads(args.check_snapshot.read_text(encoding="ascii"))
        if compact_snapshot(data) != expected:
            print("Input snapshot differs; review the generated inventory before updating compatibility claims")
            return 1
        print("Input snapshot matches all fingerprints and lexical metadata")
    print(f"Inventory: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
