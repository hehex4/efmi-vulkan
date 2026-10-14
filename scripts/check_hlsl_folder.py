"""Compile every enabled source declaration from the original Mods/Core parser.

Original HLSL/INI files remain read-only. This is source/IR evidence, not device
feature enablement, complete shader execution or application/game integration.
"""
from __future__ import annotations
import argparse
import contextlib
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tempfile

from build import find_vcvars, run_cl
from build_xxmi_runtime import build_runtime, runtime_flags
from check_ini_folder import check as inspect_ini, DEFAULT
from check_panel_hlsl import DXC, SPIRV
from probe_xxmi_host import ROOT, write_ascii


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compile_tool(out: Path) -> Path:
    objects = build_runtime(ROOT / "build/xxmi-runtime")
    objects = [p for p in objects if p.endswith(".lib") or Path(p).stem in ("hlsl_compiler", "shader_reflection")]
    executable = out / "shader_inspect.exe"
    log = io.StringIO()
    try:
        with contextlib.redirect_stdout(log):
            run_cl(find_vcvars(), str(out), runtime_flags(out) + ["/DNOMINMAX", str(ROOT / "sandbox/shader_inspect.cpp"),
                *objects, "/Fe:" + str(executable), "/link", "/NOLOGO", "/OPT:REF", "/OPT:ICF"])
    finally:
        write_ascii(out / "build.txt", log.getvalue())
    return executable


def source_path(installation: Path, declaration: dict, filename: str) -> Path:
    # Pinned CustomShader::compile concatenates the module directory, original
    # section path and filename before falling back to the module directory.
    prefix = str(installation.resolve()) + "\\"
    scoped = Path(prefix + declaration["namespace_path"] + filename)
    return scoped if declaration["namespace_path"] and scoped.exists() else Path(prefix + filename)


def inspect_source(executable: Path, source: Path, working: Path, flags: int, stage: str,
                   recursive: int, target: str, out: Path) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    before = digest(source) if source.is_file() else None
    result = subprocess.run([str(executable), str(source), str(working), str(flags), stage,
                             str(recursive), target, str(DXC.parent / "dxcompiler.dll"), str(out)],
                            cwd=out, capture_output=True, text=True, errors="replace", timeout=60)
    write_ascii(out / "runtime.txt", result.stdout + result.stderr)
    report_file = out / "compiler.json"
    report = json.loads(report_file.read_text(encoding="ascii")) if report_file.exists() else dict(compiled=False, diagnostics=result.stderr)
    report.update(source=str(source), flags=flags, stage=stage, recursive_include=recursive,
                  target=target, returncode=result.returncode, source_sha256=before, evidence=str(out))
    unchanged = before is not None and source.is_file() and digest(source) == before
    for dependency in report.get("dependencies", []):
        original = Path(dependency["path"])
        snapshot = out / dependency["snapshot"]
        dependency["sha256"] = digest(snapshot)
        dependency["unchanged"] = original.is_file() and digest(original) == dependency["sha256"]
        unchanged &= dependency["unchanged"]
    report["source_unchanged"] = bool(unchanged)
    if report.get("compiled") and result.returncode == 0:
        report["module_sha256"] = digest(out / "module.spv")
        report["reference_sha256"] = digest(out / "reference.dxbc")
        validations = []
        for options in ([], ["--scalar-block-layout", "--uniform-buffer-standard-layout"]):
            validation = subprocess.run([str(SPIRV / "spirv-val.exe"), "--target-env", "vulkan" + target,
                                         *options, str(out / "module.spv")], capture_output=True, text=True, timeout=30)
            validations.append(dict(options=options, exit_code=validation.returncode, diagnostics=validation.stdout + validation.stderr))
            if not validation.returncode:
                break
        report["validations"] = validations
        report["valid"] = validations[-1]["exit_code"] == 0
    report["passed"] = bool(report.get("compiled") and report.get("valid") and report["source_unchanged"] and result.returncode == 0)
    return report


def check(mods: Path, core: Path, out: Path, target: str = "1.2") -> dict:
    out = out.resolve()
    if not out.is_relative_to(ROOT / "build"):
        raise ValueError("Compiler evidence must stay inside the project build directory")
    out.mkdir(parents=True, exist_ok=True)
    report = dict(scope="Original enabled INI shader declarations, native SM5 validation and Vulkan compilation; no GPU or game execution",
                  passed=False, target=target, modules=[], declarations=[])
    try:
        syntax = inspect_ini(mods, core, out / "ini")
        if not syntax["passed"]:
            raise ValueError("Original full-folder INI parsing failed; see ini/report.json")
        declarations = syntax["parsed"]["shader_declarations"]
        if len(declarations) > 4096:
            raise ValueError("Shader declaration budget exceeded")
        executable = compile_tool(out)
        report["executable_sha256"] = digest(executable)
        run = Path(tempfile.mkdtemp(prefix="run-", dir=out))
        report["evidence"] = str(run)
        installation = mods.resolve().parent
        report["working_directory"] = str(installation)
        seen = {}
        nulls = inherited = 0
        for declaration in declarations:
            row = dict(declaration, modules=[])
            for stage, filename in zip("vhdgpc", declaration["sources"], strict=True):
                if not filename:
                    inherited += 1
                    continue
                if filename.lower() == "null":
                    nulls += 1
                    continue
                source = source_path(installation, declaration, filename)
                key = (str(source).lower(), declaration["flags"], stage, declaration["recursive_include"])
                if key not in seen:
                    if len(seen) >= 1024:
                        raise ValueError("Distinct source program budget exceeded")
                    index = len(seen)
                    seen[key] = index
                    module = inspect_source(executable, source, installation, declaration["flags"], stage,
                                            declaration["recursive_include"], target, run / str(index))
                    report["modules"].append(module)
                row["modules"].append(dict(stage=stage, index=seen[key]))
            report["declarations"].append(row)
        report.update(ini_inputs=syntax["inputs"], source_declarations=len(declarations), inherited_stages=inherited,
                      null_stages=nulls, distinct_programs=len(seen), compiled=sum(m.get("compiled", False) for m in report["modules"]),
                      validated=sum(m.get("valid", False) for m in report["modules"]))
        inventory = json.loads(Path(syntax["inventory"]).read_text())
        report["ini_unchanged"] = all(Path(r["path"]).is_file() and digest(Path(r["path"])) == r["sha256"] for r in inventory["files"])
        report["passed"] = report["ini_unchanged"] and all(m["passed"] for m in report["modules"])
        paths = [ROOT / "sandbox/shader_inspect.cpp", Path(__file__), ROOT / "scripts/check_ini_folder.py",
                 ROOT / "scripts/build_xxmi_runtime.py", ROOT / "sandbox/xxmi_host/folder_parse.cpp", ROOT / "frontend/xxmi/ini_module.cpp",
                 ROOT / "sandbox/hlsl_folder_test.py", ROOT / "sandbox/ini_folder_test.py", ROOT / "sandbox/run_all.py"]
        paths += [ROOT / "frontend/gpu" / name for name in ("hlsl_compiler.cpp", "hlsl_compiler.h", "shader_reflection.cpp")]
        report["source_sha256"] = {str(p.relative_to(ROOT)): digest(p) for p in paths}
        report["dependency_sha256"] = {str(p): digest(p) for p in (DXC.parent / "dxcompiler.dll", SPIRV / "spirv-val.exe")}
    except Exception as error:
        report["error"] = str(error)
    write_ascii(out / "report.json", json.dumps(report, indent=2, ensure_ascii=True) + "\n")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mods", type=Path, default=DEFAULT / "Mods")
    parser.add_argument("--core", type=Path, default=DEFAULT / "Core/EFMI")
    parser.add_argument("--out", type=Path, default=ROOT / "build/hlsl-folder")
    parser.add_argument("--target", choices=("1.1", "1.2"), default="1.2")
    args = parser.parse_args()
    report = check(args.mods, args.core, args.out, args.target)
    concise = {k: report[k] for k in ("passed", "source_declarations", "distinct_programs", "compiled", "validated", "null_stages", "inherited_stages", "error") if k in report}
    concise["failures"] = [dict(source=m["source"], stage=m["stage"], diagnostics=m.get("diagnostics", "")) for m in report["modules"] if not m["passed"]]
    print(json.dumps(concise, ensure_ascii=True, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
