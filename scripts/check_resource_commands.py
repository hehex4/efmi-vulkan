"""Original INI GPU resource commands, with headless child-only validation."""
from __future__ import annotations
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from build import find_vcvars, run_cl
from build_xxmi_runtime import build_runtime, runtime_flags
from probe_xxmi_host import ROOT, write_ascii
from generate_image_formats import verify as verify_image_formats


def main() -> int:
    out = ROOT / "build/resource-commands"
    out.mkdir(parents=True, exist_ok=True)
    report = {"scope": "owned INI resources/compute and isolated graphics-link pixels, native DX11 references; no INI graphics or game integration", "passed": False}
    try:
        verify_image_formats()
        from check_panel_hlsl import DXC, SPIRV
        shader = ROOT / "sandbox/shaders/parameters.hlsl"
        spirv = out / "parameters.spv"
        compile_shader = subprocess.run([str(DXC), "-spirv", "-fspv-target-env=vulkan1.1", "-fvk-use-dx-layout",
                                        "-T", "cs_6_0", "-E", "main", "-Fo", str(spirv), str(shader)],
                                       capture_output=True, text=True, errors="replace", timeout=30)
        write_ascii(out / "parameters-build.txt", compile_shader.stdout + compile_shader.stderr)
        compile_shader.check_returncode()
        validate_shader = subprocess.run([str(SPIRV / "spirv-val.exe"), "--target-env", "vulkan1.1", str(spirv)],
                                        capture_output=True, text=True, errors="replace", timeout=30)
        write_ascii(out / "parameters-validation.txt", validate_shader.stdout + validate_shader.stderr)
        validate_shader.check_returncode()
        report["parameter_consumer_sha256"] = hashlib.sha256(spirv.read_bytes()).hexdigest()
        objects = build_runtime(ROOT / "build/xxmi-runtime")
        executable = out / "resource_gpu.exe"
        sources = [ROOT / "sandbox/resource_gpu.cpp", ROOT / "layer/src/merged_skeleton.cpp"]
        log = io.StringIO()
        try:
            with contextlib.redirect_stdout(log):
                run_cl(find_vcvars(), str(out), runtime_flags(out) + ["/DNOMINMAX"] + [str(p) for p in sources] +
                       objects + ["/Fe:" + str(executable), "/link", "/NOLOGO", "/OPT:REF", "/OPT:ICF"])
        except Exception as e:
            log.write(str(e))
            raise
        finally:
            write_ascii(out / "build.txt", log.getvalue())
        inputs = sources + [ROOT / "sandbox/resource_image_cases.h", ROOT / "sandbox/resource_image_ini_cases.h",
                            ROOT / "sandbox/resource_image_conversion_cases.h", ROOT / "sandbox/resource_parameter_cases.h",
                            ROOT / "sandbox/resource_parameter_consumer.h", ROOT / "sandbox/resource_shader_cases.h",
                            ROOT / "sandbox/resource_compute_cases.h", ROOT / "sandbox/resource_usage_cases.h",
                            ROOT / "sandbox/resource_binding_cases.h",
                            ROOT / "sandbox/resource_graphics_link_cases.h",
                            shader, ROOT / "scripts/check_panel_hlsl.py"]
        inputs += list((ROOT / "frontend/xxmi").glob("*.cpp")) + list((ROOT / "frontend/xxmi").glob("*.h"))
        inputs += list((ROOT / "frontend/gpu").glob("*.cpp")) + list((ROOT / "frontend/gpu").glob("*.h"))
        inputs += [Path(__file__), ROOT / "scripts/build_xxmi_runtime.py", ROOT / "scripts/generate_image_formats.py"]
        report["source_sha256"] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
        report["executable_sha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()
        dependencies = [DXC.parent / "dxcompiler.dll", SPIRV.parent / "source/SPIRV-Tools-shared.dll"]
        report["dependency_sha256"] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in dependencies}
        sys.path.insert(0, str(ROOT / "sandbox"))
        from harness import inject_env
        env = inject_env(str(out / "unexpected-injection.log"))
        validation = Path(os.environ.get("EFMI_VK_VALIDATION_DIR", "F:/vulkan/validation-1.4.321.0/bin"))
        if not (validation / "VkLayer_khronos_validation.json").is_file():
            raise RuntimeError("Required validation layer is unavailable")
        env.update(VK_LAYER_PATH=str(validation), VK_LAYER_VALIDATE_SYNC="1", VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0",
                   XDG_CACHE_HOME=str(out), TMPDIR=str(out), TEMP=str(out), TMP=str(out))
        result = subprocess.run([str(executable), str(out / "fixture")], cwd=out, env=env, capture_output=True,
                                encoding="utf-8", errors="replace", timeout=60)
        output = result.stdout + result.stderr
        write_ascii(out / "runtime.txt", output)
        match = re.search(r"resource_gpu: (\d+) checks passed; validation errors=(\d+) warnings=(\d+)", output)
        report.update(returncode=result.returncode, checks=int(match[1]) if match else 0)
        report["passed"] = result.returncode == 0 and bool(match) and match[2] == match[3] == "0"
        print(match[0] if report["passed"] else output[-6000:])
    except Exception as e:
        report["error"] = str(e)
        print(str(e).encode("ascii", "backslashreplace").decode("ascii"))
    finally:
        write_ascii(out / "report.json", json.dumps(report, indent=2, ensure_ascii=True) + "\n")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
