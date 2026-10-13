"""Prepare a project-owned live INI profile from a verified mesh/material package.

The package is a shader/mesh correspondence profile. INI commands, BUF contents
and all referenced DDS files are read again by the runtime and on F10. No files
inside the source mod or Core installation are written.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def prepare(package: Path, report: Path, ini: Path, core: Path, namespace: str, output: Path) -> Path:
    package, report, ini, core, output = (p.resolve() for p in (package, report, ini, core, output))
    if not output.is_relative_to(ROOT / "build"):
        raise ValueError("Live profiles belong inside this project's build directory")
    info = json.loads(report.read_text(encoding="utf-8"))
    if info.get("package_sha256") != hashlib.sha256(package.read_bytes()).hexdigest():
        raise ValueError("Package/report fingerprint differs")
    if not ini.is_file() or not core.is_dir() or not namespace.isascii() or not namespace.replace("_", "").isalnum():
        raise ValueError("Missing INI/Core or invalid namespace")
    streams = info["files"]
    config = {"schema": 1, "ini": str(ini), "core": str(core), "namespace": namespace, "settings": namespace}
    for index, stream in enumerate(streams):
        source = Path(stream["path"]).resolve()
        if not source.is_file():
            raise ValueError("Missing profile stream: " + str(source))
        config[f"stream_{index}"] = str(source)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output != package:
        shutil.copyfile(package, output)
    sidecar = Path(str(output) + ".runtime.json")
    sidecar.write_text(json.dumps(config, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
    if output != package:
        info["scope"] = "Shader and mesh correspondence profile; commands and source assets are evaluated live through the runtime sidecar"
        info["runtime"] = {"sidecar": str(sidecar), "sha256": hashlib.sha256(sidecar.read_bytes()).hexdigest()}
        output.with_suffix(".json").write_text(json.dumps(info, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
    return sidecar


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT / "build/character/arcane-materials.bin")
    parser.add_argument("--report", type=Path, default=ROOT / "build/character/arcane-materials.json")
    mod = Path("D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Mods/arcane fes 1.2")
    parser.add_argument("--ini", type=Path, default=mod / "arcane.ini")
    parser.add_argument("--core", type=Path, default=mod.parents[1] / "Core/EFMI")
    parser.add_argument("--namespace", default="arcane_fes")
    parser.add_argument("--out", type=Path, default=ROOT / "build/character/arcane-live.bin")
    args = parser.parse_args()
    print(prepare(args.package, args.report, args.ini, args.core, args.namespace, args.out))


if __name__ == "__main__":
    main()
