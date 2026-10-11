"""Build the pinned Khronos validation layer entirely under F:/vulkan.

Uses the installed MSVC/CMake/Ninja tools and upstream known_good dependencies.
No SDK installer, registry writes, global environment changes or package registry.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys

from build import find_vcvars

TAG = "vulkan-sdk-1.4.321.0"
URL = "https://github.com/KhronosGroup/Vulkan-ValidationLayers.git"
ROOT = Path("F:/vulkan")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--proxy", help="optional proxy for child git processes only")
    args = ap.parse_args()
    src = ROOT / "Vulkan-ValidationLayers"
    dest = ROOT / "validation-1.4.321.0"
    scratch = ROOT / "validation-tmp"
    scratch.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TEMP=str(scratch), TMP=str(scratch), PYTHONDONTWRITEBYTECODE="1",
               CMAKE_BUILD_PARALLEL_LEVEL="4", VSLANG="1033")
    if args.proxy:
        # Git does not automatically use the Windows desktop proxy.
        slot = int(env.get("GIT_CONFIG_COUNT", "0"))
        env.update({"GIT_CONFIG_COUNT": str(slot + 1), f"GIT_CONFIG_KEY_{slot}": "http.proxy",
                    f"GIT_CONFIG_VALUE_{slot}": args.proxy})
    if not src.exists():
        subprocess.run(["git", "clone", "--depth", "1", "--branch", TAG, URL, str(src)], env=env, check=True)
    head = subprocess.check_output(["git", "-C", str(src), "rev-parse", "HEAD"], text=True).strip()
    tagged = subprocess.check_output(["git", "-C", str(src), "rev-parse", TAG + "^{commit}"], text=True).strip()
    if head != tagged or subprocess.check_output(["git", "-C", str(src), "status", "--porcelain"]):
        raise RuntimeError("validation source must be clean at the pinned tag")
    vcvars = Path(find_vcvars())
    cmake_root = vcvars.parents[3] / "Common7/IDE/CommonExtensions/Microsoft/CMake"
    cmake_bin, ninja_bin = cmake_root / "CMake/bin", cmake_root / "Ninja"
    env["PATH"] = os.pathsep.join([str(cmake_bin), str(ninja_bin), str(Path(sys.executable).parent), env["PATH"]])
    build_dir = src / "build-local"
    # All resolved build/install/dependency targets must stay under the owned root.
    for p in (src, dest, scratch, build_dir, src / "external"):
        p.resolve().relative_to(ROOT.resolve())
    configure = ["cmake", "-S", str(src), "-B", str(build_dir), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release", "-DUPDATE_DEPS=ON", "-DBUILD_TESTS=OFF",
                 "-DUPDATE_DEPS_HASH=initial", "-DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON",
                 "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF", f"-DPython3_EXECUTABLE={sys.executable}",
                 f"-DCMAKE_INSTALL_PREFIX={dest}"]
    commands = [configure, ["cmake", "--build", str(build_dir), "--parallel", "4"],
                ["cmake", "--install", str(build_dir)]]
    script = scratch / "build-validation.cmd"
    lines = ["@echo off", f'call "{vcvars}" >nul 2>&1', "if errorlevel 1 exit /b 1"]
    for command in commands:
        lines += [subprocess.list2cmdline(command), "if errorlevel 1 exit /b 1"]
    script.write_text("\n".join(lines) + "\n", encoding="ascii")
    subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=src, env=env, check=True)
    print(f"Validation layer {TAG} ({head}) installed at {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
