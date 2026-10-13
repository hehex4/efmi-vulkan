"""Replay actual sample entry matching, original Core and native CPU skeleton draws.

Each saved frame is repeated for two synthetic logical updates to exercise the
original first-frame identification delay. This is not new game/animation data.
OverrideTextures remains an explicit unresolved boundary; no GPU draw is issued.
"""
from __future__ import annotations
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
from check_mod_draw_plans import ROOT, SAMPLE, sha
from check_shared_palettes import BUNDLES
from verify_bone_import import sample_layout, saved_records


def inputs(reference: dict) -> list[dict]:
    records = []
    for serial, name in enumerate(BUNDLES, 1):
        bundle = ROOT / "captures" / name
        draws = {d["n"]: d for d in json.loads((bundle / "frame.json").read_text())["draws"]}
        for r in saved_records(bundle, serial, reference):
            d = draws[r["draw"]]
            r.update(hash=int(d["ib"]["hash"], 16), count=d["count"], first=d["first"], base=d["vertex_offset"])
            records.append(r)
    return records


def encode(records: list[dict]) -> bytes:
    data = bytearray(b"EFMICP01") + struct.pack("<I", len(records))
    for r in records:
        data += struct.pack("<4Ii", r["serial"], r["hash"], r["count"], r["first"], r["base"]) + r["entry"]
        for key in ("current", "previous"):
            data += struct.pack("<I", len(r[key])) + r[key]
    return bytes(data)


def verify(reference: dict, records: list[dict], callbacks: dict, actual: bytes) -> dict:
    if actual[:8] != b"EFMICO01" or struct.unpack_from("<I", actual, 8)[0] != 2*len(records):
        raise ValueError("Bad component output header")
    plans = {(int(re.search(r"component(\d+)$", run["section"])[1]), run["lod"]): run
             for run in callbacks["runs"] if run["scenario"] == 0}
    at, draws, warm_empty, mesh_empty, unresolved = 12, 0, 0, 0, 0
    epochs = set()
    bones = reference["bones"]
    for serial in sorted({r["serial"] for r in records}):
        group = [(n, r) for n, r in enumerate(records) if r["serial"] == serial]
        master = bytearray((512+2*bones)*48)
        master[:512*48] = struct.pack("<12f", 1,0,0,0,0,1,0,0,0,0,1,0)*512
        seen, complete = {}, set()
        epoch = None
        for phase in (0, 1):
            for n, r in group:
                index, actual_phase, count, native, component, lod = struct.unpack_from("<6I", actual, at)
                at += 24
                if (index, actual_phase, component) != (n, phase, r["component"]) or lod not in (0, 1):
                    raise ValueError("Original match/component order differs")
                c = reference["components"][component]
                key = (phase, component)
                if key not in seen or lod < seen[key]:
                    remap = c["remaps"][lod]
                    for pose, field in enumerate(("current", "previous")):
                        for b in range(c["count"]):
                            src = remap[b] if remap else b
                            values = struct.unpack_from("<12f", r[field], (src+1)*48)
                            start = (512 + pose*bones + c["first"]+b)*48
                            master[start:start+48] = struct.pack("<12f", *values)
                    seen[key] = lod
                    complete.add(component)
                if phase == 0:
                    if count or native:
                        raise ValueError("Original first-frame delay unexpectedly emitted callback commands")
                    warm_empty += 1
                    continue
                if len(complete) != len(reference["components"]):
                    raise ValueError("Synthetic follow-up frame has incomplete reference bones")
                expected = plans[component, lod]["items"]
                expected_draws = [item for item in expected if item["op"] == 3]
                expected_native = sum(item["op"] == 4 for item in expected)
                if count != len(expected_draws) or native != expected_native:
                    raise ValueError(f"Full original entry differs from isolated callback at record {n}")
                unresolved += native
                if not count:
                    mesh_empty += 1
                for item in expected_draws:
                    index_count, first, base, identity = struct.unpack_from("<IIiQ", actual, at)
                    at += 20
                    entry = bytearray(r["entry"])
                    struct.pack_into("<2I", entry, 80, 1533, 1533+3*bones)
                    size = struct.unpack_from("<I", actual, at+256)[0]
                    if (index_count, first, base) != (item["count"], item["first"], item["base"]) or \
                            actual[at:at+256] != entry or actual[at+260:at+260+size] != master:
                        raise ValueError(f"Native draw, entry or complete bones differ at record {n}")
                    at += 260+size
                    if not identity or (epoch is not None and epoch != identity):
                        raise ValueError("Components in one isolated capture did not share a palette epoch")
                    epoch = identity
                    draws += 1
        if epoch is None or epoch in epochs:
            raise ValueError("Isolated expired groups incorrectly share an identity")
        epochs.add(epoch)
    if at != len(actual):
        raise ValueError("Trailing component output")
    return dict(source_records=len(records), planned_entries=2*len(records), original_first_frame_skips=warm_empty,
                intentional_mesh_empty_callbacks=mesh_empty, custom_draws=draws, unresolved_material_calls=unresolved,
                isolated_identity_epochs=len(epochs), all_draw_arguments_entries_and_palettes_equal=True)


def main() -> int:
    folder = ROOT / "build/core-components"
    folder.mkdir(parents=True, exist_ok=True)
    report = {"scope": "actual INI/Core CPU entry replay; synthetic repeated updates; no material/GPU/game execution", "passed": False}
    try:
        host = ROOT / "build/xxmi-host"
        compiled = json.loads((host / "report.json").read_text())
        exe = host / "host_probe.exe"
        if not compiled.get("passed") or sha(exe) != compiled["executable_sha256"]:
            raise ValueError("Rebuild/pass the XXMI host before component replay")
        for filename, expected in compiled["source_sha256"].items():
            if sha(Path(filename)) != expected:
                raise ValueError(f"Host input changed: {filename}")
        subprocess.run([sys.executable, "-B", str(ROOT / "scripts/check_mod_draw_plans.py")], check=True)
        reference = sample_layout(SAMPLE)
        records = inputs(reference)
        core = SAMPLE.parents[2] / "Core/EFMI"
        hashes = {a["path"]: a["sha256"] for a in reference["assets"]}
        hashes.update({str(core / n): sha(core / n) for n in ("API.ini", "MergedSkeleton.ini", "SpatialIdentification.ini", "Helpers.ini")})
        (folder / "input.bin").write_bytes(encode(records))
        result = subprocess.run([str(exe), "--components-mod", str(SAMPLE), str(core), "arcane_fes",
                                 str(folder / "input.bin"), str(folder / "output.bin")],
                                cwd=host, env=dict(os.environ, TEMP=str(host / "build/tmp"), TMP=str(host / "build/tmp")),
                                capture_output=True, encoding="utf-8", errors="replace", timeout=60)
        output = result.stdout + result.stderr
        (folder / "native.txt").write_text(output.encode("ascii", "backslashreplace").decode("ascii"), encoding="ascii")
        if result.returncode or not re.search(r", 4 refusal/control checks", output):
            raise ValueError(f"Original component/Core replay refused: {result.returncode}; see native.txt")
        outcome = verify(reference, records, json.loads((host / "mod-plans.json").read_text()), (folder / "output.bin").read_bytes())
        if any(sha(Path(p)) != value for p, value in hashes.items()):
            raise ValueError("Original Core/mod/remap inputs changed")
        report.update(passed=True, replay=outcome, source_sha256=hashes, executable_sha256=sha(exe),
                      input_sha256=sha(folder / "input.bin"), output_sha256=sha(folder / "output.bin"))
        print(json.dumps(outcome, indent=2))
        return 0
    except (OSError, ValueError, KeyError, IndexError, struct.error, subprocess.SubprocessError) as exc:
        report["error"] = str(exc)
        print(str(exc).encode("ascii", "backslashreplace").decode("ascii"), file=sys.stderr)
        return 1
    finally:
        (folder / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True)+"\n", encoding="ascii")


if __name__ == "__main__":
    raise SystemExit(main())
