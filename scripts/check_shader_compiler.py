"""Original HLSL semantics, SPIR-V descriptor mapping and independent validation."""
from __future__ import annotations
import contextlib
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
from build import find_vcvars, run_cl
from build_xxmi_runtime import build_runtime, runtime_flags
from probe_xxmi_host import ROOT, DEPS, write_ascii
from check_panel_hlsl import DXC, SPIRV


def main() -> int:
    out = ROOT / "build/shader-compiler"
    out.mkdir(parents=True, exist_ok=True)
    report = {"scope": "original HLSL signatures, Vulkan compilation and graphics linking; no INI graphics or game", "passed": False}
    try:
        objects = build_runtime(ROOT / "build/xxmi-runtime")
        objects = [p for p in objects if p.endswith(".lib") or Path(p).stem in ("hlsl_compiler", "shader_cache", "graphics_link", "shader_reflection")]
        executable = out / "shader_compile.exe"
        log = io.StringIO()
        try:
            with contextlib.redirect_stdout(log):
                run_cl(find_vcvars(), str(out), runtime_flags(out) + ["/DNOMINMAX", str(ROOT / "sandbox/shader_compile.cpp"),
                    *objects, "/Fe:" + str(executable), "/link", "/NOLOGO", "/OPT:REF", "/OPT:ICF"])
        finally:
            write_ascii(out / "build.txt", log.getvalue())
        # A new output directory avoids counting stale shaders as current results.
        import tempfile
        fixtures = Path(tempfile.mkdtemp(prefix="run-", dir=out))
        result = subprocess.run([str(executable), str(fixtures), str(DXC.parent / "dxcompiler.dll")],
                                cwd=out, capture_output=True, text=True, errors="replace", timeout=60)
        output = result.stdout + result.stderr
        write_ascii(out / "runtime.txt", output)
        report.update(returncode=result.returncode, fixtures=str(fixtures))
        match = re.search(r"shader_compile: (\d+) checks passed; (\d+) modules emitted", output)
        if result.returncode or not match:
            raise RuntimeError(output[-6000:])
        modules = sorted(fixtures.glob("*.spv"))
        if len(modules) != int(match[2]):
            raise RuntimeError("Missing generated shader modules")
        report["modules"] = []
        for module in modules:
            target = "vulkan1.2" if module.name.endswith("-vk12.spv") else "vulkan1.1"
            validation = subprocess.run([str(SPIRV / "spirv-val.exe"), "--target-env", target, str(module)],
                                        capture_output=True, text=True, timeout=30)
            report["modules"].append({"path": str(module), "sha256": hashlib.sha256(module.read_bytes()).hexdigest(),
                                      "validation_exit": validation.returncode, "diagnostics": validation.stdout + validation.stderr})
            if validation.returncode:
                raise RuntimeError("SPIR-V validation failed: " + validation.stderr)
        inputs = [ROOT / "frontend/gpu/hlsl_compiler.cpp", ROOT / "frontend/gpu/hlsl_compiler.h", ROOT / "frontend/gpu/shader_reflection.cpp",
                  ROOT / "frontend/gpu/shader_cache.cpp", ROOT / "frontend/gpu/shader_cache.h",
                  ROOT / "frontend/gpu/graphics_link.cpp", ROOT / "frontend/gpu/graphics_link.h", ROOT / "sandbox/shader_link_cases.h",
                  ROOT / "sandbox/shader_compile.cpp", ROOT / "scripts/build_xxmi_runtime.py", Path(__file__)]
        report["source_sha256"] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
        dependencies = [DXC.parent / "dxcompiler.dll", DEPS / "dxc-1.9.2609/inc/dxcapi.h",
                        DEPS / "SPIRV-Reflect-1.4.321.0/spirv_reflect.c", DEPS / "SPIRV-Reflect-1.4.321.0/spirv_reflect.h",
                        DEPS / "SPIRV-Reflect-1.4.321.0/include/spirv/unified1/spirv.h", SPIRV / "spirv-val.exe"]
        report["dependency_sha256"] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in dependencies}
        report["executable_sha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()
        report.update(passed=True, checks=int(match[1]))
        print(match[0] + "; all modules validate")
    except Exception as error:
        report["error"] = str(error)
        print(str(error).encode("ascii", "backslashreplace").decode("ascii"))
    finally:
        write_ascii(out / "report.json", json.dumps(report, indent=2, ensure_ascii=True) + "\n")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
