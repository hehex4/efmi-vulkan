"""Which stage of the pipelines of a mod's draws reads the bone buffer and the instance window?

    stage_usage.py <bundle> [--repoint] [--spirv-val PATH]

Reads a bundle of collect_probe.py that was taken with a mod (matches.json) and has the shaders of
the matched draws. For every program (vertex and fragment shader) it lists where the draw has the
bone buffer and the instance window bound and how often each stage refers to them.

With --repoint every distinct vertex shader is rewritten the way a pipeline variant would use it:
its storage buffer and its instance window move to bindings 0 and 1 of the first set the pipeline
does not use. The result is checked for being the same module apart from four words and, when
spirv-val is found, for being valid SPIR-V. Nothing is written.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from collections import Counter, OrderedDict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))

from efmi_vk.spirv import buffer_variables, repoint  # noqa: E402

SPIRV_VAL = r"F:\vulkan\Vulkan-ValidationLayers\external\Release\64\SPIRV-Tools\build\install\bin\spirv-val.exe"
BONE_BUFFER_BYTES = 8413184     # the game's bone buffer, the same size under DX11 and Vulkan
WINDOW = (2, 0)                 # set and binding of the instance window
STORAGE_TYPES, DYNAMIC_UNIFORM = (7, 9), 8


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bundle", type=Path)
    ap.add_argument("--repoint", action="store_true")
    ap.add_argument("--spirv-val", default=SPIRV_VAL)
    args = ap.parse_args()
    bundle = args.bundle
    frame = json.loads((bundle / "frame.json").read_text(encoding="utf-8"))
    pipelines = {p["id"]: p for p in json.loads((bundle / "pipelines.json").read_text(encoding="utf-8"))}
    shaders = {s["id"]: s for s in json.loads((bundle / "shaders.json").read_text(encoding="utf-8"))}
    matched = {n for m in json.loads((bundle / "matches.json").read_text(encoding="utf-8")) for s in m["sections"] for n in s["draws"]}

    code, variables = {}, {}

    def stage(shader_id: int) -> list[dict]:
        if shader_id not in variables:
            code[shader_id] = (bundle / shaders[shader_id]["file"]).read_bytes()
            variables[shader_id] = buffer_variables(code[shader_id])
        return variables[shader_id]

    def uses(stage_variables: list[dict], place) -> int | None:
        hit = [v for v in stage_variables if (v["set"], v["binding"]) == place]
        return hit[0]["uses"] if hit else None

    programs: "OrderedDict[tuple, dict]" = OrderedDict()
    skipped = 0
    for d in frame["draws"]:
        if d["n"] not in matched:
            continue
        pipe = pipelines.get(d["pipeline"])
        stages = {s["stage"]: s for s in (pipe or {}).get("shaders", []) if "id" in s and s["id"] in shaders and "file" in shaders[s["id"]]}
        if 1 not in stages:
            skipped += 1
            continue
        bone = None
        for index, bound in enumerate(d["sets"]):
            for binding in (bound.get("contents") or {}).get("bindings") or []:
                if binding["type"] in STORAGE_TYPES and any(e.get("range") == BONE_BUFFER_BYTES for e in binding.get("elements") or []):
                    bone = (index, binding["binding"])
        vs = stage(stages[1]["id"])
        fs = stage(stages[16]["id"]) if 16 in stages else []
        key = (stages[1]["hash"], stages[16]["hash"] if 16 in stages else None)
        row = programs.setdefault(key, {"draws": 0, "passes": set(), "vs_id": stages[1]["id"]})
        row["draws"] += 1
        row["passes"].add(d["rp"])
        row.update(bone=bone, sets=sum(1 for s in d["sets"] if s["set"] != "0x0"),
                   vs_storage=[(v["set"], v["binding"]) for v in vs if v["kind"] == "storage"],
                   vs_bone=uses(vs, bone), fs_bone=uses(fs, bone), vs_window=uses(vs, WINDOW), fs_window=uses(fs, WINDOW))

    tally = Counter()
    show = lambda n: "-" if n is None else str(n)      # noqa: E731
    print("vertex / fragment shader | draws, render passes | bone buffer at | references VS, FS | instance window VS, FS | sets bound")
    for (vs_hash, fs_hash), r in programs.items():
        print("%s / %s | %2d, %s | %s | %s, %s | %s, %s | %d" % (
            vs_hash, fs_hash, r["draws"], sorted(r["passes"]), r["bone"], show(r["vs_bone"]), show(r["fs_bone"]),
            show(r["vs_window"]), show(r["fs_window"]), r["sets"]))
        tally["draws"] += r["draws"]
        tally["vertex stage has one storage buffer and it is the bone buffer"] += r["draws"] * (r["vs_storage"] == [r["bone"]])
        tally["vertex stage reads the instance window"] += r["draws"] * (r["vs_window"] is not None)
        tally["fragment stage reads the bone buffer"] += r["draws"] * (r["fs_bone"] is not None)
        tally["fragment stage reads the instance window"] += r["draws"] * (r["fs_window"] is not None)
    print("programs: %d, vertex shaders: %d; matched draws without exported shaders: %d" % (
        len(programs), len({k[0] for k in programs}), skipped))
    for name, count in tally.items():
        print("  %-62s %d" % (name, count))

    if not args.repoint:
        return 0
    validator = args.spirv_val if Path(args.spirv_val).is_file() else None
    done = {}
    failures = 0
    for (vs_hash, _fs), r in programs.items():
        if vs_hash in done:
            continue
        original = code[r["vs_id"]]
        try:
            if r["vs_storage"] != [r["bone"]]:
                raise ValueError("the vertex stage does not have exactly the bone buffer as storage buffer")
            patched = repoint(original, {r["bone"]: (r["sets"], 0), WINDOW: (r["sets"], 1)})
            changed = sum(1 for i in range(0, len(original), 4) if original[i:i + 4] != patched[i:i + 4])
            after = {(v["set"], v["binding"]): v for v in buffer_variables(patched)}
            if len(patched) != len(original) or changed != 4:
                raise ValueError("%d words changed" % changed)
            if (r["sets"], 0) not in after or after[(r["sets"], 0)]["kind"] != "storage" or after[(r["sets"], 1)]["kind"] != "uniform":
                raise ValueError("the moved variables are not where they should be")
            verdict = "four words changed"
            if validator:
                with tempfile.TemporaryDirectory(dir=str(ROOT / "build")) as tmp:
                    for label, data in (("original", original), ("patched", patched)):
                        path = Path(tmp) / (label + ".spv")
                        path.write_bytes(data)
                        run = subprocess.run([validator, "--target-env", "vulkan1.1", str(path)], capture_output=True, text=True)
                        if run.returncode != 0:
                            raise ValueError("spirv-val rejects the %s module: %s" % (label, (run.stdout + run.stderr).strip()[:200]))
                verdict += ", spirv-val accepts both"
            done[vs_hash] = verdict
        except ValueError as exc:
            done[vs_hash] = "FAILED: %s" % exc
            failures += 1
    print("re-pointing the vertex stage to set %s:" % sorted({r["sets"] for r in programs.values()}))
    for verdict, count in Counter(done.values()).items():
        print("  %d vertex shaders: %s" % (count, verdict))
    if not validator:
        print("  spirv-val not found at %s; validity was not checked" % args.spirv_val)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
