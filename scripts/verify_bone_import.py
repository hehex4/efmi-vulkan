"""Replay saved sample palettes through production C++ and an independent Core-derived reference.

Reads the installed INI/remap buffers without changing them. This narrowly extracts the sample's
literal skeleton declarations, not INI command execution. Game assets and replay outputs stay
uncommitted. A byte match is offline evidence, not a rendered character or a captured DX11 output.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sample_layout(ini: Path) -> dict:
    raw = ini.read_bytes()
    text = raw.decode("utf-8-sig")
    sections = dict(re.findall(r"^\[([^\]]+)\][ \t]*\r?\n(.*?)(?=^\[|\Z)", text, re.M | re.S))
    body = sections["CommandListInitializeMergedSkeleton"]
    components = []
    assets = [{"path": str(ini), "sha256": digest(raw), "bytes": len(raw)}]
    for component, block in re.findall(r"^\$component_id = (\d+)\s*\n(.*?)(?=^\$component_id =|\Z)", body, re.M | re.S):
        if int(component) != len(components):
            raise ValueError("noncontiguous component declarations")
        first = int(re.search(r"VertexGroupOffsets\[\$component_id\] = (\d+)", block)[1])
        count = int(re.search(r"VertexGroupCounts\[\$component_id\] = (\d+)", block)[1])
        remaps = {}
        for lod, value in re.findall(r"LodRemaps\[\$component_id\*\$lod_level_count\+(\d+)\] = ([^\r\n]+)", block):
            value = value.strip()
            if value == "null":
                remaps[int(lod)] = []
                continue
            resource = sections[value.removeprefix("ref ")]
            if not re.search(r"^format = R16_UINT\s*$", resource, re.M):
                raise ValueError("unsupported remap format")
            path = ini.parent / re.search(r"^filename = (.+)$", resource, re.M)[1].strip()
            data = path.read_bytes()
            if len(data) != count * 2:
                raise ValueError("remap size differs from component")
            remaps[int(lod)] = list(struct.unpack("<" + "H" * count, data))
            assets.append({"path": str(path), "sha256": digest(data), "bytes": len(data)})
        components.append({"first": first, "count": count, "remaps": remaps})
    bones = int(re.search(r"^global \$bones_count = (\d+)", text, re.M)[1])
    instances = int(re.search(r"^global \$max_instance_count = (\d+)", text, re.M)[1])
    if not components or sum(c["count"] for c in components) != bones:
        raise ValueError("component ranges do not cover the sample")
    return {"bones": bones, "instances": instances, "components": components, "assets": assets}


def saved_records(bundle: Path, serial: int, layout: dict) -> list[dict]:
    manifest = json.loads((bundle / "manifest.json").read_text())
    for name, meta in manifest["files"].items():
        data = (bundle / name).read_bytes()
        if len(data) != meta["bytes"] or digest(data) != meta["sha256"]:
            raise ValueError(f"bundle fingerprint differs: {name}")
    frame = json.loads((bundle / "frame.json").read_text())
    pipelines = {p["id"]: p for p in json.loads((bundle / "pipelines.json").read_text())}
    matches = json.loads((bundle / "matches.json").read_text())
    if len(matches) != 1 or matches[0]["sha256"] != layout["assets"][0]["sha256"]:
        raise ValueError("capture's mod fingerprint differs from the supplied sample")
    selection = {}
    for section in matches[0]["sections"]:
        if not section["draws"]:
            continue
        match = re.fullmatch(r"TextureOverride_EntryPoint_Component(\d+)(_LOD1)?", section["section"])
        if not match:
            raise ValueError("unsupported sample entry point")
        for number in section["draws"]:
            if number in selection:
                raise ValueError("multiple sections match one draw")
            selection[number] = (int(match[1]), int(bool(match[2])))
    blob_info = json.loads((bundle / "snapshots.json").read_text())["blobs"]
    raw = (bundle / "snapshots.bin").read_bytes()
    blobs = {b["id"]: raw[b["file_offset"]:b["file_offset"]+b["size"]] for b in blob_info}
    records, positions = [], set()
    for draw in frame["draws"]:
        if draw["n"] not in selection:
            continue
        if draw["instances"] != 1:
            raise ValueError("replay currently requires one instance per draw")
        component, lod = selection[draw["n"]]
        pre = pipelines[draw["pipeline"]]["variant_preflight"]
        if not pre["supported"]:
            raise ValueError("matched VS bone binding is not identified")
        snapshots = draw["snapshot"]
        entries = [s for s in snapshots if s["kind"] == "entry" and s["set"] == 2 and s["binding"] == 0]
        follows = [s for s in snapshots if s["kind"] == "follow" and
                   (s["set"], s["binding"]) == (pre["bone_set"], pre["bone_binding"])]
        if len(entries) != 1 or sorted(s["word"] for s in follows) != [0, 1]:
            raise ValueError("missing or ambiguous actual bone snapshots")
        entry = entries[0]
        follows.sort(key=lambda s: s["word"])
        if any(s.get("id") is None or s["record"]["state"] != "equal" for s in [entry, *follows]):
            raise ValueError("record-time equality not established for a relevant snapshot")
        entry_data = blobs[entry["id"]]
        if len(entry_data) != 256:
            raise ValueError("instance entry is not 256 bytes")
        positions.add(entry_data[48:60])
        offsets = struct.unpack_from("<2I", entry_data, 80)
        for pose, follow in enumerate(follows):
            if follow["value"] != offsets[pose]:
                raise ValueError("follow does not use the entry's offset")
        remap = layout["components"][component]["remaps"][lod]
        records.append({"serial": serial, "instance": 0, "component": component, "lod": lod,
                        "scale": 1.0, "entry": entry_data, "remap": remap,
                        "current": blobs[follows[0]["id"]], "previous": blobs[follows[1]["id"]],
                        "draw": draw["n"], "bundle": str(bundle), "frame": frame["frame"]})
    if len(positions) != 1 or not records:
        raise ValueError("replay requires exactly one observed character position per bundle")
    return records


def encode(layout: dict, records: list[dict]) -> bytes:
    out = bytearray(b"EFMIBN01")
    out += struct.pack("<4I", layout["bones"], layout["instances"], len(layout["components"]), len(records))
    for c in layout["components"]:
        out += struct.pack("<2I", c["first"], c["count"])
    for r in records:
        out += struct.pack("<Q3If", r["serial"], r["instance"], r["component"], r["lod"], r["scale"])
        out += r["entry"] + struct.pack("<I", len(r["remap"]))
        out += struct.pack("<" + "H" * len(r["remap"]), *r["remap"])
        for pose in ("current", "previous"):
            out += struct.pack("<I", len(r[pose])) + r[pose]
    return bytes(out)


def verify(layout: dict, records: list[dict], actual: bytes) -> dict:
    # The reference follows Core's row addressing directly, independently of the C++ loops.
    bones = layout["bones"]
    master = bytearray((512 + 2 * bones * layout["instances"]) * 48)
    master[:512*48] = struct.pack("<12f", 1,0,0,0,0,1,0,0,0,0,1,0) * 512
    seen = {}
    if actual[:8] != b"EFMIBO01" or struct.unpack_from("<I", actual, 8)[0] != len(records):
        raise ValueError("bad replay output header")
    at, imported, skipped = 12, 0, 0
    for record in records:
        c = layout["components"][record["component"]]
        key = (record["serial"], record["instance"], record["component"])
        do_import = key not in seen or record["lod"] < seen[key]
        dst_base = (512 + 2 * bones * record["instance"]) * 3
        offsets = struct.unpack_from("<2I", record["entry"], 80)
        if do_import:
            for pose, src_offset in enumerate(offsets):
                if src_offset < 1536:
                    continue
                for i in range(c["count"]):
                    src_bone = record["remap"][i] if record["remap"] else i
                    data = record[("current", "previous")[pose]][(3+src_bone*3)*16:(6+src_bone*3)*16]
                    values = struct.unpack("<12f", data)
                    if not all(math.isfinite(v) for v in values):
                        raise ValueError("nonfinite reference input")
                    values = tuple(v * record["scale"] for v in values)
                    dst_row = dst_base + (c["first"]+i)*3 + pose*bones*3
                    master[dst_row*16:(dst_row+3)*16] = struct.pack("<12f", *values)
            seen[key] = record["lod"]
            imported += 1
        else:
            skipped += 1
        expected_entry = bytearray(record["entry"])
        for pose, src_offset in enumerate(offsets):
            if src_offset >= 1536:
                struct.pack_into("<I", expected_entry, 80+pose*4, dst_base+pose*bones*3)
        result = struct.unpack_from("<I", actual, at)[0]
        entry = actual[at+4:at+260]
        size = struct.unpack_from("<I", actual, at+260)[0]
        output_master = actual[at+264:at+264+size]
        if result != (1 if do_import else 2) or entry != expected_entry or output_master != master:
            raise ValueError(f"C++ differs from reference at {record['bundle']} draw {record['draw']}")
        at += 264+size
    if at != len(actual):
        raise ValueError("extra replay output bytes")
    return {"records": len(records), "imports": imported, "already_current": skipped,
            "master_bytes": len(master), "final_master_sha256": digest(master),
            "entry_and_master_equal_after_every_record": True}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--ini", type=Path, required=True)
    ap.add_argument("--bundle", type=Path, action="append", required=True)
    ap.add_argument("--exe", type=Path, default=ROOT / "build/next/efmi_vk_bone_test.exe")
    args = ap.parse_args()
    layout = sample_layout(args.ini.resolve())
    records = [r for serial, bundle in enumerate(args.bundle, 1)
               for r in saved_records(bundle.resolve(), serial, layout)]
    folder = Path(tempfile.mkdtemp(prefix="bone-replay-", dir=ROOT / "build"))
    encoded = encode(layout, records)
    (folder / "input.bin").write_bytes(encoded)
    subprocess.run([str(args.exe.resolve()), "--replay", str(folder / "input.bin"), str(folder / "output.bin")], check=True)
    actual = (folder / "output.bin").read_bytes()
    report = verify(layout, records, actual)
    report.update({"evidence": "offline replay of saved game inputs; no replacement rendering",
                   "assets": layout["assets"], "bundles": [str(p.resolve()) for p in args.bundle],
                   "input_sha256": digest(encoded), "output_sha256": digest(actual),
                   "exe_sha256": digest(args.exe.read_bytes())})
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k:v for k,v in report.items() if k not in ("assets", "bundles")}, indent=2))
    print(f"report: {folder / 'report.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
