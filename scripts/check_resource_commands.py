"""Original INI GPU resource commands, with headless child-only validation."""
from __future__ import annotations
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from build import find_vcvars, run_cl
from build_xxmi_runtime import build_runtime, runtime_flags
from probe_xxmi_host import ROOT, write_ascii
from generate_image_formats import verify as verify_image_formats


def main() -> int:
    out = ROOT / "build/resource-commands"
    out.mkdir(parents=True, exist_ok=True)
    report = {"scope": "owned original INI resources/graphics, Core HLSL and exact DXBC translation with native DX11 references; no application import or game integration", "passed": False}
    try:
        from build_bytecode_compiler import build_dependency
        bytecode_library = build_dependency()
        verify_image_formats()
        core = Path("D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Core/EFMI")
        original_inputs = {core / "Shaders/TextPrinter.hlsl": "text.hlsl", core / "Fonts/LiberationSans-Bold.dds": "font.dds",
                           core / "main.ini": "main.ini"}
        report["installed_source_sha256"] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in original_inputs}
        copied = out / "fixture/core-text/source"
        copied.mkdir(parents=True, exist_ok=True)
        for original, name in original_inputs.items():
            shutil.copyfile(original, copied / name)
        # The image panel's pictures from the first enabled installed Mod that ships them.
        mods = core.parents[1] / "Mods"
        panel = next((m / "k" for m in sorted(mods.iterdir()) if m.is_dir() and not m.name.upper().startswith("DISABLED")
                      and (m / "k/k.png").is_file() and (m / "k/panel_icon.png").is_file()), None)
        panel_copy = out / "fixture/png-panel"
        shutil.rmtree(panel_copy, ignore_errors=True)
        panel_copy.mkdir(parents=True)
        if panel:
            for name in ("k.png", "panel_icon.png"):
                report["installed_source_sha256"][str(panel / name)] = hashlib.sha256((panel / name).read_bytes()).hexdigest()
                shutil.copyfile(panel / name, panel_copy / name)
        # The whole panel of the first enabled Mod that ships it with the original shader caches,
        # copied with modification times (the cache rule compares them), plus a stub declaring the
        # main INI variables it reads and the hash of the character mark that activates it.
        needed = ("1.ini", "draw_2d.hlsl", "Draw_2D.vs_5_0.8000.bin", "Draw_2D.ps_5_0.8000.bin", "k.png", "panel_icon.png")
        panel_mod = next((m for m in sorted(mods.iterdir()) if m.is_dir() and not m.name.upper().startswith("DISABLED")
                          and all((m / "k" / n).is_file() for n in needed)), None)
        panel_root = out / "fixture/panel"
        shutil.rmtree(panel_root, ignore_errors=True)
        if panel_mod:
            target = panel_root / "Mods/panel/k"
            target.mkdir(parents=True)
            for item in sorted((panel_mod / "k").iterdir()):
                if item.is_file():
                    report["installed_source_sha256"][str(item)] = hashlib.sha256(item.read_bytes()).hexdigest()
                    shutil.copy2(item, target / item.name)
            text = (panel_mod / "k/1.ini").read_text(encoding="utf-8", errors="replace")
            names = {}
            for ns, var in re.findall(r"\$\\([A-Za-z0-9_.]+)\\([A-Za-z0-9_]+)", text):
                names.setdefault(ns, set()).add(var)
            for ns, variables in sorted(names.items()):
                (panel_root / f"Mods/panel/main-{ns}.ini").write_text(
                    f"namespace = {ns}\n[Constants]\n" + "".join(f"global ${v} = 0\n" for v in sorted(variables)), encoding="ascii")
            mark = None
            for header, body in re.findall(r"^\[(TextureOverride[^\]]*)\]\s*\n(.*?)(?=^\[|\Z)", text, re.M | re.S):
                keys = dict((k.strip().lower(), v.strip()) for k, v in re.findall(r"^([^=;\n]+)=(.*)$", body, re.M))
                if keys.get("$active") == "1" and "hash" in keys:
                    mark = (int(keys["hash"], 16), int(keys.get("match_first_index", "0")), int(keys.get("match_index_count", "3")))
                    break
            if not mark:
                raise RuntimeError("Installed panel has no recognisable character mark")
            (panel_root / "mark.txt").write_text("%d %d %d\n" % mark, encoding="ascii")
            report["panel_mod"] = panel_mod.name
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
                            ROOT / "sandbox/resource_image_conversion_cases.h", ROOT / "sandbox/resource_png_cases.h", ROOT / "sandbox/resource_backbuffer_cases.h", ROOT / "sandbox/resource_panel_cases.h", ROOT / "sandbox/resource_owned_generation_cases.h", ROOT / "sandbox/resource_operand_cases.h",
                            ROOT / "sandbox/resource_parameter_cases.h",
                            ROOT / "sandbox/resource_parameter_consumer.h", ROOT / "sandbox/resource_shader_cases.h",
                            ROOT / "sandbox/resource_compute_cases.h", ROOT / "sandbox/resource_usage_cases.h",
                            ROOT / "sandbox/resource_binding_cases.h",
                            ROOT / "sandbox/resource_graphics_link_cases.h",
                            ROOT / "sandbox/resource_graphics_state_cases.h",
                            ROOT / "sandbox/resource_graphics_cases.h",
                            ROOT / "sandbox/resource_ini_graphics_cases.h",
                            ROOT / "sandbox/resource_geometry_cases.h",
                            ROOT / "sandbox/resource_text_cases.h",
                            ROOT / "sandbox/resource_layout_cases.h",
                            ROOT / "sandbox/resource_divisor_cases.h",
                            ROOT / "sandbox/resource_vertex_input_cases.h",
                            ROOT / "sandbox/resource_match_cases.h",
                            ROOT / "sandbox/resource_shader_hook_cases.h",
                            ROOT / "sandbox/resource_identity_cases.h",
                            ROOT / "sandbox/resource_regex_cases.h",
                            ROOT / "sandbox/resource_bytecode_cases.h",
                            ROOT / "sandbox/resource_bytecode_views.h",
                            shader, ROOT / "scripts/check_panel_hlsl.py"]
        inputs += list((ROOT / "frontend/xxmi").glob("*.cpp")) + list((ROOT / "frontend/xxmi").glob("*.h"))
        inputs += list((ROOT / "frontend/gpu").glob("*.cpp")) + list((ROOT / "frontend/gpu").glob("*.h"))
        inputs += [ROOT / "frontend/assets/png.cpp", ROOT / "frontend/assets/png.h"]
        inputs += [Path(__file__), ROOT / "scripts/build_xxmi_runtime.py", ROOT / "scripts/generate_image_formats.py", ROOT / "scripts/build_bytecode_compiler.py"]
        report["source_sha256"] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
        report["executable_sha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()
        dependencies = [DXC.parent / "dxcompiler.dll", SPIRV.parent / "source/SPIRV-Tools-shared.dll", bytecode_library]
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
        report["installed_sources_unchanged"] = all(hashlib.sha256(Path(p).read_bytes()).hexdigest() == digest
                                                   for p, digest in report["installed_source_sha256"].items())
        report["passed"] = report["passed"] and report["installed_sources_unchanged"]
        print(match[0] if report["passed"] else output[-6000:])
    except Exception as e:
        report["error"] = str(e)
        print(str(e).encode("ascii", "backslashreplace").decode("ascii"))
    finally:
        write_ascii(out / "report.json", json.dumps(report, indent=2, ensure_ascii=True) + "\n")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
