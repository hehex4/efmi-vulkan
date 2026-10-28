"""The original as the judge (D-137 step 2, D-150).

Runs small INIs under the pinned XXMI 3DMigoto (d3d11.dll from F:\\vulkan\\XXMI-PACKAGE-v1.2.2.zip, the
version the user's EFMI installation carries) in a D3D11 program of the project's own, and the same INIs
under the layer's owned GPU runtime (resource_gpu.exe --present-ini), and compares what each case leaves in
its persistent variables. A case's [Present] runs commands and puts their results in `global persist`
variables; the original saves them to d3dx_user.ini, the layer's runtime prints them.

The user's installation is never touched: the DLL is extracted into build/dx11-judge/xxmi with a d3dx.ini of
the judge's own whose [Loader] names the judge program.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

from build import COMMON, find_vcvars, find_vulkan_include, run_cl

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = Path(r"F:\vulkan\XXMI-PACKAGE-v1.2.2.zip")
OUT = ROOT / "build" / "dx11-judge"
CASES = ROOT / "sandbox" / "judge_cases"

D3DX = """[Loader]
target = efmi_vk_dx11_judge.exe
[Logging]
calls = 0
input = 0
debug = 0
[Include]
include_recursive = Mods
[System]
allow_check_interface = 1
allow_create_device = 1
allow_platform_update = 1
skip_early_includes_load = 1
config_initialization_delay = 0
settings_auto_save_interval = 1
check_foreground_window = 0
[Device]
[Stereo]
automatic_mode = 0
[Rendering]
[Hunting]
hunting = 0
"""


def build_host() -> Path:
    exe = OUT / "efmi_vk_dx11_judge.exe"
    source = ROOT / "sandbox" / "dx11_judge.cpp"
    if not exe.is_file() or exe.stat().st_mtime < source.stat().st_mtime:
        run_cl(find_vcvars(), str(OUT), COMMON + ["/Fo" + str(OUT) + "/", "/Fd" + str(OUT / "judge.pdb"), str(source),
                                                  "/Fe:" + str(exe), "/link", "/NOLOGO", "d3d11.lib", "dxgi.lib", "user32.lib"])
    return exe


def prepare_original() -> Path:
    folder = OUT / "xxmi"
    folder.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(PACKAGE) as z:
        for name in ("d3d11.dll", "d3dcompiler_47.dll"):
            target = folder / name
            if not target.is_file():
                target.write_bytes(z.read(name))
    (folder / "d3dx.ini").write_text(D3DX, encoding="utf-8")
    return folder


def persisted(text: str) -> dict[str, float]:
    """`$\\namespace\\name = value` lines of a d3dx_user.ini."""
    out = {}
    for line in text.splitlines():
        m = re.match(r"\s*(\$[^=\s]+)\s*=\s*([-+0-9.eE]+)\s*$", line)
        if m:
            out[m.group(1).lower()] = float(m.group(2))
    return out


def run_original(exe: Path, folder: Path, case: Path, frames: int) -> dict[str, float]:
    mods = folder / "Mods"
    shutil.rmtree(mods, ignore_errors=True)
    mods.mkdir()
    shutil.copy(case, mods / case.name)
    for extra in case.parent.glob(case.stem + ".*"):
        if extra.suffix != ".ini":
            shutil.copy(extra, mods / extra.name)
    user = folder / "d3dx_user.ini"
    user.unlink(missing_ok=True)
    run = subprocess.run([str(exe), str(folder), str(frames)], cwd=folder, capture_output=True, text=True, timeout=300)
    if run.returncode:
        raise RuntimeError("the original's program failed: " + (run.stdout + run.stderr)[-800:])
    return persisted(user.read_text(encoding="utf-8", errors="replace")) if user.is_file() else {}


def run_layer(case: Path, frames: int) -> dict[str, float]:
    exe = ROOT / "build" / "resource-commands" / "resource_gpu.exe"
    if not exe.is_file():
        raise RuntimeError("build the layer's GPU resource runtime first (scripts/check_resource_commands.py)")
    work = OUT / "layer" / case.stem
    shutil.rmtree(work, ignore_errors=True)
    (work / "Mods").mkdir(parents=True)
    shutil.copy(case, work / "Mods" / case.name)
    for extra in case.parent.glob(case.stem + ".*"):
        if extra.suffix != ".ini":
            shutil.copy(extra, work / "Mods" / extra.name)
    # The test's own child process, with the validation layer as check_resource_commands.py runs it.
    sys.path.insert(0, str(ROOT / "sandbox"))
    from harness import inject_env
    env = inject_env(str(work / "unexpected-injection.log"))
    validation = Path(os.environ.get("EFMI_VK_VALIDATION_DIR", "F:/vulkan/validation-1.4.321.0/bin"))
    env.update(VK_LAYER_PATH=str(validation), VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0",
               XDG_CACHE_HOME=str(work), TMPDIR=str(work), TEMP=str(work), TMP=str(work))
    run = subprocess.run([str(exe), "--present-ini", str(work), str(frames)], cwd=work, env=env, capture_output=True, text=True,
                         timeout=300)
    values = {}
    for line in run.stdout.splitlines():
        if line.startswith("{"):
            values = {k.lower(): float(v) for k, v in json.loads(line).items()}
    if run.returncode or not values:
        raise RuntimeError("the layer's runtime failed: " + (run.stdout + run.stderr)[-800:])
    return values


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=120)
    ap.add_argument("--only", help="a case name")
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    exe = build_host()
    folder = prepare_original()
    shutil.copy(exe, folder / exe.name)
    failed = 0
    for case in sorted(CASES.glob("*.ini")):
        if args.only and case.stem != args.only:
            continue
        original = run_original(folder / exe.name, folder, case, args.frames)
        try:
            layer = run_layer(case, args.frames)
        except RuntimeError as e:
            print("FAIL %s: %s" % (case.stem, e)); failed += 1; continue
        expected = {k: v for k, v in original.items()}
        differ = {k: (v, layer.get(k)) for k, v in expected.items() if layer.get(k) is None or abs(layer[k] - v) > 1e-4 * max(1, abs(v))}
        if not expected:
            print("FAIL %s: the original saved no variable" % case.stem); failed += 1
        elif differ:
            print("FAIL %s: original vs layer %s" % (case.stem, differ)); failed += 1
        else:
            print("ok   %s: %d values as the original's" % (case.stem, len(expected)))
    print("dx11 judge: %s" % ("FAILED" if failed else "passed"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
