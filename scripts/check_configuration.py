"""Original root configuration and lazy GPU resources, without package discovery."""
from __future__ import annotations
import argparse
import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys

from build import find_vcvars, run_cl
from build_xxmi_runtime import build_runtime, runtime_flags
from probe_xxmi_host import ROOT, write_ascii


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--only", choices=("cpu", "gpu", "prepared", "graphics", "owned-images", "bytecode-resources", "all"), default="all")
    parser.add_argument("--installation", type=Path, help="Read-only root configuration load; settings stay in the project")
    args = parser.parse_args()
    if args.installation and args.only == "cpu":
        parser.error("--installation requires the GPU adapter")
    runtime = ROOT / "build/xxmi-runtime"
    gpu = ROOT / "build/resource-commands"
    gpu.mkdir(parents=True, exist_ok=True)
    if not args.no_build:
        log = io.StringIO()
        try:
            with contextlib.redirect_stdout(log):
                objects = build_runtime(runtime, test=args.only in ("cpu", "all"))
                if args.only != "cpu":
                    sources = [ROOT / "sandbox/resource_gpu.cpp", ROOT / "layer/src/merged_skeleton.cpp",
                               ROOT / "layer/src/pipeline_recipe.cpp"]
                    run_cl(find_vcvars(), str(gpu), runtime_flags(gpu) + ["/DNOMINMAX"] +
                           [str(p) for p in sources] + objects + ["/Fe:" + str(gpu / "resource_gpu.exe"),
                           "/link", "/NOLOGO", "/OPT:REF", "/OPT:ICF"])
        except Exception as exc:
            log.write(str(exc))
            raise
        finally:
            write_ascii(ROOT / "build/configuration-build.out", log.getvalue())
    if args.only in ("cpu", "all"):
        result = subprocess.run([str(runtime / "runtime_test.exe"), "--configuration", str(runtime / "fixture")],
                                capture_output=True, encoding="utf-8", errors="replace", timeout=40)
        write_ascii(ROOT / "build/configuration-cpu.out", result.stdout + result.stderr)
        print(result.stdout + result.stderr, end="")
        if result.returncode:
            return result.returncode
    if args.only != "cpu":
        if args.only == "owned-images":
            from check_panel_hlsl import DXC, SPIRV
            spirv = gpu / "parameters.spv"
            commands = [
                [str(DXC), "-spirv", "-fspv-target-env=vulkan1.1", "-fvk-use-dx-layout", "-T", "cs_6_0", "-E", "main",
                 "-Fo", str(spirv), str(ROOT / "sandbox/shaders/parameters.hlsl")],
                [str(SPIRV / "spirv-val.exe"), "--target-env", "vulkan1.1", str(spirv)],
            ]
            for command in commands:
                result = subprocess.run(command, capture_output=True, encoding="utf-8", errors="replace", timeout=30)
                if result.returncode:
                    print(result.stdout + result.stderr, end="")
                    return result.returncode
        sys.path.insert(0, str(ROOT / "sandbox"))
        from harness import inject_env
        env = inject_env(str(gpu / "unexpected-injection.log"))
        validation = Path(os.environ.get("EFMI_VK_VALIDATION_DIR", "F:/vulkan/validation-1.4.321.0/bin"))
        if not (validation / "VkLayer_khronos_validation.json").is_file():
            raise RuntimeError("Required validation layer is unavailable")
        env.update(VK_LAYER_PATH=str(validation), VK_LAYER_VALIDATE_SYNC="1", VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0",
                   XDG_CACHE_HOME=str(gpu), TMPDIR=str(gpu), TEMP=str(gpu), TMP=str(gpu))
        mode = args.only if args.only in ("prepared", "graphics", "owned-images", "bytecode-resources") else "configuration"
        result = subprocess.run([str(gpu / "resource_gpu.exe"), "--" + mode, str(gpu / "fixture" / mode)],
                                cwd=gpu, env=env, capture_output=True, encoding="utf-8", errors="replace", timeout=45)
        write_ascii(ROOT / ("build/" + mode + "-gpu.out"), result.stdout + result.stderr)
        print(result.stdout + result.stderr, end="")
        if result.returncode:
            return result.returncode
        if args.installation:
            settings = gpu / "configuration-inspection"
            result = subprocess.run([str(gpu / "resource_gpu.exe"), "--load-configuration", str(args.installation.resolve()), str(settings)],
                                    cwd=gpu, env=env, capture_output=True, encoding="utf-8", errors="replace", timeout=300)
            write_ascii(ROOT / "build/configuration-installation.out", result.stdout + result.stderr)
            lines = (result.stdout + result.stderr).splitlines()
            print("\n".join(lines[-24:]))
            if result.returncode:
                return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
