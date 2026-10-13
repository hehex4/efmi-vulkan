"""Build the pinned original XXMI runtime without modifying its dependencies."""
from __future__ import annotations
import argparse
import contextlib
import io
import hashlib
import json
import os
from pathlib import Path
import subprocess

from build import find_vcvars, find_vulkan_include, run_cl
from probe_xxmi_host import DEPS, PINS, ROOT, digest, pinned_checkout, write_ascii


def build_runtime(out: Path, test: bool = False) -> list[str]:
    upstream, reference = DEPS / "XXMI-Libs-Package", DEPS / "wwmi-dx12"
    pinned_checkout(upstream, PINS["xxmi"])
    pinned_checkout(reference, PINS["reference"])
    port = reference / "reshade_addon/migoto"
    pristine = DEPS / "efmi-xxmi-host-prototype/pristine"
    for line in (port / "PRISTINE.txt").read_text().splitlines():
        if (line := line.strip()) and not line.startswith("#"):
            original = upstream / line
            if digest(original) != digest(pristine / original.name):
                raise RuntimeError("Original source staging mismatch; run probe_xxmi_host.py")
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    legacy = [pristine / (name + ".cpp") for name in ("ResourceHash", "profiling", "util")]
    legacy += [ROOT / "frontend/xxmi" / (name + ".cpp") for name in ("command_module", "runtime_override")]
    sources = [ROOT / "frontend/xxmi" / (name + ".cpp") for name in
               ("runtime", "runtime_input", "runtime_parser", "runtime_shim", "draw_plan", "resource_plan")]
    sources += [ROOT / "frontend/xxmi" / (name + ".cpp") for name in ("character_core", "skeleton_plan", "palette_files")]
    sources += [ROOT / "frontend/core/palette_bank.cpp"]
    sources += [upstream / f"DirectXTK/Src/{name}.cpp" for name in ("DDSTextureLoader", "WICTextureLoader")]
    sources += [upstream / "crc32c-hw-1.0.5/src/crc32c.cpp"]
    includes = [ROOT / "frontend/xxmi", Path(find_vulkan_include()), port, pristine, upstream,
                upstream / "HLSLDecompiler", upstream / "D3D_Shaders", upstream / "DirectXTK/Inc",
                upstream / "DirectXTK/Src", upstream / "crc32c-hw-1.0.5/include", upstream / "pcre2"]
    flags = ["/nologo", "/std:c++17", "/permissive", "/W0", "/O2", "/MT", "/EHa", "/Gy", "/Gw",
             "/DUNICODE", "/D_UNICODE", "/D_HAS_STD_BYTE=0", "/DMIGOTO_DX=11",
             "/DPCRE2_CODE_UNIT_WIDTH=8", "/DPCRE2_STATIC", "/DCRC32C_STATIC", "/D_CRT_SECURE_NO_WARNINGS",
             "/D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING", "/D_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1",
             "/D_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT=1", "/FI" + str(port / "port_compat.h")]
    flags += ["/I" + str(path) for path in includes] + ["/Fo" + str(out) + "/"]
    libraries = ["user32.lib", "gdi32.lib", "advapi32.lib", "d3dcompiler.lib", "dxguid.lib", "ole32.lib",
                 "xinput.lib", "dbghelp.lib", str(upstream / "pcre2/pcre2-8-64.lib")]
    objects = [str(out / (path.stem + ".obj")) for path in legacy + sources]
    os.environ["VSLANG"] = "1033"
    vcvars = find_vcvars()
    output = io.StringIO()
    stamp_file = out / "objects.json"
    stamps = json.loads(stamp_file.read_text()) if stamp_file.exists() else {}
    common_files = list(pristine.glob("*")) + list(port.glob("*.h"))
    common_files += list((ROOT / "frontend").rglob("*.h")) + [ROOT / "frontend/xxmi/ini_module.cpp"]
    common_files += list((ROOT / "layer/src").glob("*.h"))
    common_hash = hashlib.sha256("".join(digest(p) for p in sorted(common_files) if p.is_file()).encode()).hexdigest()
    def compile_sources(units, extra):
        pending, next_stamps = [], {}
        for unit in units:
            stamp = hashlib.sha256((common_hash + digest(unit) + repr(flags + extra)).encode()).hexdigest()
            next_stamps[str(unit)] = stamp
            if stamps.get(str(unit)) != stamp or not (out / (unit.stem + ".obj")).exists():
                pending.append(str(unit))
        if pending:
            run_cl(vcvars, str(out), flags + extra + ["/c"] + pending)
        stamps.update(next_stamps)
        stamp_file.write_text(json.dumps(stamps, indent=2), encoding="ascii")
    try:
        with contextlib.redirect_stdout(output):
            compile_sources(legacy, [])
            compile_sources(sources, ["/DNOMINMAX"])
            if test:
                run_cl(vcvars, str(out), flags + ["/DNOMINMAX", str(ROOT / "sandbox/xxmi_runtime.cpp"),
                       str(ROOT / "layer/src/character_package.cpp"), str(ROOT / "layer/src/merged_skeleton.cpp"),
                       str(ROOT / "layer/src/pipeline_recipe.cpp"), str(ROOT / "layer/src/character_program.cpp"),
                       *objects, "/Fe:" + str(out / "runtime_test.exe"), "/link", "/NOLOGO", "/OPT:REF", "/OPT:ICF", *libraries])
    except Exception as exc:
        output.write(str(exc))
        raise
    finally:
        write_ascii(out / "build.txt", output.getvalue())
    return objects + libraries


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/xxmi-runtime")
    args = parser.parse_args()
    build_runtime(args.out, test=True)
    package = ROOT / "build/character/arcane-materials.bin"
    if package.is_file():
        from prepare_runtime import prepare
        mod = Path("D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Mods/arcane fes 1.2")
        prepare(package, package.with_suffix(".json"), mod / "arcane.ini", mod.parents[1] / "Core/EFMI",
                "arcane_fes", args.out / "fixture/actual-live.bin")
    result = subprocess.run([str(args.out.resolve() / "runtime_test.exe"), str(args.out.resolve())],
                            capture_output=True, text=True, timeout=40)
    write_ascii(args.out / "runtime.txt", result.stdout + result.stderr)
    print(result.stdout)
    if result.returncode:
        print(result.stderr[-6000:])
    raise SystemExit(result.returncode)
