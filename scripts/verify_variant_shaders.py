"""Cross-check the production C++ VS rewrite against the Python reference on a saved mod capture.

No process attachment or game execution. The inputs are immutable historical SPIR-V exports;
this establishes offline bytecode agreement and validity, not rendering on the character.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))
from efmi_vk.spirv import buffer_variables, repoint

DEFAULT_VAL = Path("F:/vulkan/Vulkan-ValidationLayers/external/Release/64/SPIRV-Tools/build/install/bin/spirv-val.exe")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("bundle", type=Path)
    ap.add_argument("--dist", type=Path, default=Path(os.environ.get("EFMI_VK_DIST", ROOT / "dist")))
    ap.add_argument("--spirv-val", type=Path, default=DEFAULT_VAL)
    args = ap.parse_args()
    executable = args.dist.resolve() / "efmi_vk_recipe_test.exe"
    bundle = args.bundle.resolve()
    frame = json.loads((bundle / "frame.json").read_text())
    matches = json.loads((bundle / "matches.json").read_text())
    matched = {n for mod in matches for section in mod["sections"] for n in section["draws"]}
    wanted = {draw["pipeline"] for draw in frame["draws"] if draw["n"] in matched}
    pipelines = json.loads((bundle / "pipelines.json").read_text())
    shader_ids = {s["id"] for p in pipelines if p["id"] in wanted for s in p["shaders"] if s["stage"] == 1}
    shaders = {s["id"]: s for s in json.loads((bundle / "shaders.json").read_text())}
    if not executable.is_file() or not args.spirv_val.is_file() or not shader_ids:
        raise RuntimeError("built rewriter, spirv-val and matched vertex exports are required")
    scratch = Path(tempfile.mkdtemp(prefix="variant-shaders-", dir=ROOT / "build"))
    rows = []
    seen = set()
    for sid in sorted(shader_ids):
        original = bundle / shaders[sid]["file"]
        code = original.read_bytes()
        digest = hashlib.sha256(code).hexdigest()
        if digest in seen:
            continue
        seen.add(digest)
        variables = buffer_variables(code)
        bone, = [v for v in variables if v["kind"] == "storage"]
        instance, = [v for v in variables if v["kind"] == "uniform" and (v["set"], v["binding"]) == (2, 0)]
        expected = repoint(code, {(bone["set"], bone["binding"]): (3, 0), (instance["set"], instance["binding"]): (3, 1)})
        output = scratch / f"{sid}.spv"
        subprocess.run([str(executable), "--repoint-file", str(original), str(output)], check=True, cwd=scratch)
        actual = output.read_bytes()
        if actual != expected:
            raise RuntimeError(f"shader {sid}: native and reference rewrites differ")
        subprocess.run([str(args.spirv_val), "--target-env", "vulkan1.1", str(output)], check=True, cwd=scratch)
        rows.append({"shader": sid, "sha256": digest, "rewritten_sha256": hashlib.sha256(actual).hexdigest(),
                     "bytes": len(code), "equal_to_reference": True, "spirv_val": True})
    report = {"scope": "historical shaders, offline validation only", "bundle": str(bundle), "shaders": rows}
    (scratch / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"variant shaders: {len(rows)}/{len(rows)} checks passed; {scratch}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
