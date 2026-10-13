"""Build bounded CPU palette tests and replay historical LOD0/LOD1 records.

Runtime remaps come from the hosted original INI initialization and file loader.
The independent reference uses the earlier Core-derived oracle. This is offline
byte/completeness evidence, not new game data or rendered character acceptance.
"""
from __future__ import annotations
import contextlib
import io
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
from build import BuildError, find_vcvars, run_cl
from check_mod_draw_plans import ROOT, SAMPLE, sha
from verify_bone_import import sample_layout, saved_records

BUNDLES = ["m2-0.8.0-20261012/display", "m2-0.8.0-20261012/world-rendered-1",
           "m2-0.8.0-20261012/world-rendered-2", "m2-0.8.1-20261012/lod1-a/attempt-01",
           "m2-0.8.1-20261012/lod1-b/attempt-01"]


def encode(plan: dict, records: list[dict]) -> bytes:
    data = bytearray(b"EFMIPB01")
    data += struct.pack("<4I", plan["bones"], plan["instances"], len(plan["components"]), len(records))
    for c in plan["components"]:
        data += struct.pack("<2I", c["first"], c["count"])
        for remap in c["remaps"][:2]:
            indices = remap["indices"]
            data += struct.pack("<2I", int(remap["state"] != 0), len(indices))
            data += struct.pack("<" + "H" * len(indices), *indices)
    for r in records:
        # Each isolated bundle gets a distinct identity; no animation continuity is inferred.
        data += struct.pack("<QQIIf", r["serial"], r["serial"], r["component"], r["lod"], r["scale"])
        data += r["entry"]
        for pose in ("current", "previous"):
            data += struct.pack("<I", len(r[pose])) + r[pose]
    return bytes(data)


def verify(reference: dict, records: list[dict], actual: bytes) -> dict:
    if actual[:8] != b"EFMIPO01" or struct.unpack_from("<I", actual, 8)[0] != len(records):
        raise ValueError("Wrong palette replay output header")
    at, ready_count, waiting, imports = 12, 0, 0, 0
    identities = {}
    bones = reference["bones"]
    for r in records:
        token = r["serial"]
        if token not in identities:
            master = bytearray((512 + 2*bones)*48)
            master[:512*48] = struct.pack("<12f", 1,0,0,0,0,1,0,0,0,0,1,0)*512
            identities[token] = (master, {}, set())
        master, seen, complete = identities[token]
        component = reference["components"][r["component"]]
        key = (r["serial"], r["component"])
        changed = key not in seen or r["lod"] < seen[key]
        offsets = struct.unpack_from("<2I", r["entry"], 80)
        if any(v < 1536 for v in offsets):
            raise ValueError("These historical fixtures require two dynamic poses")
        if changed:
            for pose, field in enumerate(("current", "previous")):
                for i in range(component["count"]):
                    source = r["remap"][i] if r["remap"] else i
                    values = struct.unpack_from("<12f", r[field], (source+1)*48)
                    start = (512 + pose*bones + component["first"]+i)*48
                    master[start:start+48] = struct.pack("<12f", *(v*r["scale"] for v in values))
            seen[key] = r["lod"]
            complete.add(r["component"])
            imports += 1
        imported, ready = struct.unpack_from("<2I", actual, at)
        at += 8
        expected_ready = len(complete) == len(reference["components"])
        if imported != (1 if changed else 2) or ready != int(expected_ready):
            raise ValueError(f"Import or completeness differs at {r['bundle']} draw {r['draw']}")
        if not ready:
            waiting += 1
            continue
        ready_count += 1
        entry = bytearray(r["entry"])
        struct.pack_into("<2I", entry, 80, 1533, 1533+3*bones)
        size = struct.unpack_from("<I", actual, at+256)[0]
        if actual[at:at+256] != entry or actual[at+260:at+260+size] != master:
            raise ValueError(f"Ready snapshot differs at {r['bundle']} draw {r['draw']}")
        at += 260+size
    if at != len(actual) or ready_count == 0 or waiting == 0:
        raise ValueError("Unexpected output tail or missing completeness coverage")
    return dict(records=len(records), identities=len(identities), imports=imports,
                ready_snapshots=ready_count, incomplete_refusals=waiting,
                all_ready_snapshots_equal_reference=True, snapshot_bytes=(512+2*bones)*48)


def main() -> int:
    folder = ROOT / "build/shared-palettes"
    folder.mkdir(parents=True, exist_ok=True)
    report = {"scope": "CPU masters and saved LOD0/LOD1 inputs only; no GPU/game execution", "passed": False}
    try:
        sources = [ROOT / p for p in ("sandbox/shared_palette_test.cpp", "frontend/core/palette_bank.cpp",
                                     "layer/src/merged_skeleton.cpp")]
        inputs = sources + [ROOT / "frontend/core/palette_bank.h", ROOT / "layer/src/merged_skeleton.h", Path(__file__)]
        exe = folder / "palette_test.exe"
        flags = ["/nologo", "/std:c++17", "/EHsc", "/W4", "/O2", "/MT", "/DNOMINMAX",
                 "/Fo" + str(folder) + "/", *map(str, sources), "/Fe:" + str(exe), "/link", "/NOLOGO"]
        vcvars = find_vcvars()
        if any(any(c in arg for c in '\"&|<>^%!\r\n') for arg in [vcvars, *flags]):
            raise ValueError("Unsupported shell characters in compiler input")
        report["source_sha256"] = {str(p): sha(p) for p in inputs}
        os.environ["VSLANG"] = "1033"
        log = io.StringIO()
        try:
            with contextlib.redirect_stdout(log):
                run_cl(vcvars, str(folder), flags)
        finally:
            (folder / "build.txt").write_text(log.getvalue(), encoding="ascii", errors="backslashreplace")
        result = subprocess.run([str(exe)], cwd=folder, capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr
        (folder / "native.txt").write_text(output, encoding="ascii", errors="backslashreplace")
        counts = re.search(r"Shared character palettes: (\d+) checks passed, (\d+) failed", output)
        if result.returncode or not counts or int(counts[2]):
            raise ValueError("Palette native fixture failed; see native.txt")
        print(counts[0], flush=True)
        # Refresh and verify the evaluated host plan; this checks host fingerprints too.
        subprocess.run([sys.executable, "-B", str(ROOT / "scripts/check_core_skeleton.py")], check=True)
        plan = json.loads((ROOT / "build/xxmi-host/core-skeleton-plan.json").read_text())
        reference = sample_layout(SAMPLE)
        records = [r for serial, bundle in enumerate(BUNDLES, 1)
                   for r in saved_records(ROOT / "captures" / bundle, serial, reference)]
        encoded = encode(plan, records)
        (folder / "input.bin").write_bytes(encoded)
        subprocess.run([str(exe), "--replay", str(folder / "input.bin"), str(folder / "output.bin")],
                       cwd=folder, check=True, timeout=30)
        replay = verify(reference, records, (folder / "output.bin").read_bytes())
        if any(sha(Path(a["path"])) != a["sha256"] for a in reference["assets"]):
            raise ValueError("Original source changed during replay")
        report.update(passed=True, native_checks=int(counts[1]), replay=replay, assets=reference["assets"],
                      bundles=BUNDLES, executable_sha256=sha(exe), input_sha256=sha(folder / "input.bin"),
                      output_sha256=sha(folder / "output.bin"),
                      initialization_report_sha256=sha(ROOT / "build/xxmi-host/core-skeleton-report.json"))
        print(json.dumps(replay, indent=2))
        return 0
    except (OSError, ValueError, KeyError, BuildError, subprocess.SubprocessError) as exc:
        report["error"] = str(exc)
        print(str(exc).encode("ascii", "backslashreplace").decode("ascii"), file=sys.stderr)
        return 1
    finally:
        (folder / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True)+"\n", encoding="ascii")


if __name__ == "__main__":
    raise SystemExit(main())
