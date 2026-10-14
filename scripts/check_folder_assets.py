"""Inspect all original-engine file declarations, independent of selected outfits.

This checks storage, buffer alignment and native DDS decoding. It does not infer
vertex semantics, reachable draw bounds, GPU readiness or visual compatibility.
Missing/dormant declarations and unsupported loaders remain explicit findings.
"""
from __future__ import annotations
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def inspect(report_path: Path, out: Path) -> dict:
    out = out.resolve()
    if not out.is_relative_to(ROOT / "build"):
        raise ValueError("Asset reports belong in the project build directory")
    out.mkdir(parents=True, exist_ok=True)
    report = json.loads(report_path.read_text())
    if not report["passed"]:
        raise ValueError("Full original-engine syntax must pass first")
    inventory = json.loads(Path(report["inventory"]).read_text())
    if any(sha(Path(row["path"])) != row["sha256"] for row in inventory["files"]):
        raise ValueError("INI inputs changed since the original-engine parse")
    host = ROOT / "build/xxmi-host/host_probe.exe"
    if sha(host) != report["executable_sha256"]:
        raise ValueError("Native inspector executable changed since INI parse")
    # DXGI typed buffer widths; stride overrides retain original byte layout.
    sizes = {**dict.fromkeys(range(1, 5), 16), **dict.fromkeys(range(5, 9), 12),
             **dict.fromkeys(range(9, 23), 8), **dict.fromkeys(range(23, 48), 4),
             **dict.fromkeys(range(48, 60), 2), **dict.fromkeys(range(60, 66), 1)}
    assets = {}
    declarations = report["parsed"]["resource_declarations"]
    for resource in declarations:
        if not resource["filename"]:
            continue
        path = Path(resource["filename"]).resolve()
        key = str(path).casefold()
        if key not in assets:
            row = assets[key] = dict(path=str(path), declarations=[], state="missing")
            if path.is_file():
                row.update(bytes=path.stat().st_size, sha256=sha(path), state="present")
                if path.suffix.casefold() == ".dds":
                    run = subprocess.run([str(host), "--dds", str(path)], cwd=out,
                                         capture_output=True, text=True, errors="replace", timeout=30)
                    row["state"] = "dds_decoded" if not run.returncode else "dds_refused"
                    row["dds"] = json.loads(run.stdout) if not run.returncode else None
                    if run.returncode:
                        row["reason"] = run.stderr.strip()
                elif path.suffix.casefold() == ".png":
                    with path.open("rb") as source:
                        header = source.read(24)
                    row["state"] = "image_loader_unimplemented"
                    if header[:8] == b"\x89PNG\r\n\x1a\n" and len(header) == 24:
                        row["width"], row["height"] = struct.unpack_from(">II", header, 16)
                    row["reason"] = "Vulkan file resource loader has no WIC/PNG path; header dimensions only"
        row = assets[key]
        declaration = dict(name=resource["name"], type=resource["type"], format=resource["format"], stride=resource["stride"])
        if row["state"] != "missing" and resource["type"] in (1, 2, 3):
            width = resource["stride"] if resource["stride"] > 0 else sizes.get(resource["format"])
            if resource["type"] == 3 and not width:
                width = 4
            declaration["element_bytes"] = width
            declaration["aligned"] = bool(width and row["bytes"] and row["bytes"] % width == 0)
            declaration["check"] = "buffer_storage_only"
        row["declarations"].append(declaration)
    counts = dict(collections.Counter(row["state"] for row in assets.values()))
    buffers = [d for row in assets.values() for d in row["declarations"] if "aligned" in d]
    unchanged = all(not row.get("sha256") or sha(Path(row["path"])) == row["sha256"] for row in assets.values())
    result = dict(scope=__doc__.strip(), ini_report=str(report_path.resolve()), source_unchanged=unchanged,
                  declarations=len(declarations), file_declarations=sum(len(row["declarations"]) for row in assets.values()),
                  unique_files=len(assets), states=counts, buffer_declarations=len(buffers),
                  buffer_alignment_failures=[d for d in buffers if not d["aligned"]],
                  bytes=sum(row.get("bytes", 0) for row in assets.values()),
                  assets=list(assets.values()), all_mods_renderable=False)
    (out / "report.json").write_text(json.dumps(result, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ini-report", type=Path, default=ROOT / "build/ini-folder/report.json")
    parser.add_argument("--out", type=Path, default=ROOT / "build/folder-assets")
    args = parser.parse_args()
    result = inspect(args.ini_report, args.out)
    print(json.dumps({k: v for k, v in result.items() if k != "assets"}, indent=2, ensure_ascii=True))
    # An audit succeeds at reporting gaps; never label missing files compatible.
    return 0 if result["source_unchanged"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
