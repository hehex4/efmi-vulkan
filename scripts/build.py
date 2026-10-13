"""Builds the layer and the sandbox with the MSVC x64 toolchain.

    python scripts/build.py [layer] [sandbox] [--out DIR]

Without a target both are built. Output goes to dist/ (or --out), objects to
<component>/build/. The Vulkan headers are looked for in this order:
%VULKAN_SDK%\\Include, F:\\vulkan\\Include, F:\\vulkan\\Vulkan-Headers\\include.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VULKAN_ROOT = os.environ.get("EFMI_VK_VULKAN_ROOT", r"F:\vulkan")

LAYER_NAME = "VK_LAYER_EFMI_probe"
LAYER_DLL = "efmi_vk_layer.dll"
INJECT_DLL = "efmi_vk_inject.dll"
LAYER_JSON = "efmi_vk_layer.json"
SANDBOX_EXE = "efmi_vk_sandbox.exe"


class BuildError(RuntimeError):
    pass


def find_vcvars() -> str:
    explicit = os.environ.get("VCVARS")
    if explicit and os.path.isfile(explicit):
        return explicit
    vswhere = os.path.join(
        os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
        "Microsoft Visual Studio", "Installer", "vswhere.exe",
    )
    if os.path.isfile(vswhere):
        out = subprocess.run(
            [vswhere, "-latest", "-products", "*", "-requires",
             "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
            capture_output=True, text=True,
        ).stdout.strip().splitlines()
        if out:
            candidate = os.path.join(out[0], "VC", "Auxiliary", "Build", "vcvars64.bat")
            if os.path.isfile(candidate):
                return candidate
    raise BuildError("vcvars64.bat not found; set VCVARS to its full path")


def find_vulkan_include() -> str:
    candidates = []
    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        candidates.append(os.path.join(sdk, "Include"))
    candidates.append(os.path.join(VULKAN_ROOT, "Include"))
    candidates.append(os.path.join(VULKAN_ROOT, "Vulkan-Headers", "include"))
    for c in candidates:
        if os.path.isfile(os.path.join(c, "vulkan", "vulkan_core.h")):
            return c
    raise BuildError(
        "Vulkan headers not found (looked in: %s); run scripts/fetch_vulkan_headers.ps1" % ", ".join(candidates)
    )


def run_cl(vcvars: str, cwd: str, args: list[str]) -> None:
    # vcvars64.bat has to run in the same cmd instance as cl. The command line is
    # written to a temporary batch file so that no quoting passes through cmd /c.
    quoted = " ".join('"%s"' % a if (" " in a or "(" in a) else a for a in args)
    script = os.path.join(cwd, "build", "_build.bat")
    os.makedirs(os.path.dirname(script), exist_ok=True)
    with open(script, "w", encoding="ascii", newline="\r\n") as f:
        f.write("@echo off\n")
        f.write('call "%s" >nul 2>&1\n' % vcvars)
        f.write("if errorlevel 1 exit /b 1\n")
        f.write("cl %s\n" % quoted)
        f.write("exit /b %errorlevel%\n")
    env = dict(os.environ)
    scratch = os.path.join(cwd, "build", "tmp")
    os.makedirs(scratch, exist_ok=True)
    env.update(TEMP=scratch, TMP=scratch)
    proc = subprocess.run(["cmd.exe", "/d", "/c", script], cwd=cwd, env=env, capture_output=True, text=True, errors="replace")
    output = (proc.stdout or "") + (proc.stderr or "")
    lines = [ln for ln in output.splitlines() if ln.strip()]
    noise = (".cpp", "Generating Code...", "Compiling...", "Creating library")
    shown = [ln for ln in lines if not (ln.strip().endswith(noise[0]) and ":" not in ln) and not ln.strip().startswith(noise[1:])]
    if proc.returncode != 0:
        raise BuildError("cl failed in %s:\n%s" % (cwd, "\n".join(lines)))
    for ln in shown:
        if "warning" in ln.lower():
            print("  " + ln)


def sources(folder: str) -> list[str]:
    src = os.path.join(folder, "src")
    return [os.path.join("src", n) for n in sorted(os.listdir(src)) if n.endswith(".cpp")]


COMMON = [
    "/nologo", "/std:c++17", "/O2", "/MT", "/EHsc", "/W3", "/GS", "/Zi", "/DNDEBUG",
    "/DUNICODE", "/D_UNICODE", "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", "/DVK_USE_PLATFORM_WIN32_KHR", "/D_CRT_SECURE_NO_WARNINGS",
]


# The replacement code as a module of its own (docs/hot-swap.md): what decides and records
# character replacements, without hooks and tracker. The layer DLL has the same code built in.
MODULE_DLL = "efmi_vk_backend.dll"
MODULE_SOURCES = [
    "module/module_main.cpp", "src/character_backend.cpp", "src/character_package.cpp", "src/character_materials.cpp",
    "src/material_sibling.cpp", "src/merged_skeleton.cpp", "src/pipeline_variant.cpp", "src/spirv_repoint.cpp",
    "src/variant_cache.cpp", "src/draw_snapshots.cpp", "src/pipeline_recipe.cpp", "../frontend/gpu/fragment_images.cpp",
]
# Headers whose structures cross between the layer and a module. A module built from other
# contents of these is refused by the layer.
ABI_ROOTS = ["state.h", "character_module.h", "command_owner.h", "json.h"]


def abi_stamp() -> str:
    """64-bit stamp over the shared headers and what they include from layer/src."""
    import hashlib
    import re
    src = os.path.join(ROOT, "layer", "src")
    seen, todo = [], list(ABI_ROOTS)
    while todo:
        name = todo.pop()
        if name in seen:
            continue
        seen.append(name)
        with open(os.path.join(src, name), encoding="utf-8") as f:
            text = f.read()
        todo += [m for m in re.findall(r'^#include "([^"/]+)"', text, re.M) if os.path.isfile(os.path.join(src, m))]
    digest = hashlib.sha256()
    for name in sorted(seen):
        with open(os.path.join(src, name), encoding="utf-8") as f:
            # The version text changes with every build of the replacement code and is no structure.
            lines = [ln for ln in f.read().splitlines() if not ln.startswith("#define EFVK_VERSION ")]
        digest.update(name.encode() + b"\0" + "\n".join(lines).encode() + b"\0")
    digest.update(" ".join(f for f in COMMON if not f.startswith("/DEFVK_ABI=")).encode())
    return "0x" + digest.hexdigest()[:16] + "ull"


def abi_headers() -> list[str]:
    import re
    src = os.path.join(ROOT, "layer", "src")
    seen, todo = [], list(ABI_ROOTS)
    while todo:
        name = todo.pop()
        if name in seen:
            continue
        seen.append(name)
        with open(os.path.join(src, name), encoding="utf-8") as f:
            todo += [m for m in re.findall(r'^#include "([^"/]+)"', f.read(), re.M) if os.path.isfile(os.path.join(src, m))]
    return sorted(seen)


def build_module(vcvars: str, include: str, out: str, mismatch: bool = False) -> str:
    """The module DLL. `mismatch`: a copy with another stamp, for the test that it is refused."""
    folder = os.path.join(ROOT, "layer")
    name = "module_mismatch" if mismatch else "module"
    os.makedirs(os.path.join(folder, "build", name), exist_ok=True)
    dll = os.path.join(out, "efmi_vk_backend_mismatch.dll" if mismatch else MODULE_DLL)
    flags = [f for f in COMMON if not f.startswith("/DEFVK_ABI=")]
    stamp = abi_stamp()
    flags.append("/DEFVK_ABI=" + (stamp[:-4] + ("0" if stamp[-4] != "0" else "1") + "ull" if mismatch else stamp))
    args = flags + ["/I" + include, "/Fobuild\\" + name + "\\", "/Fdbuild\\" + name + "\\module_cl.pdb", "/LD"]
    args += MODULE_SOURCES + MATERIAL_SOURCES + [
        "/Fe:" + dll, "/link", "/NOLOGO", "/DLL", "/OPT:REF", "/OPT:ICF", "/DEBUG",
        "/PDB:build\\" + name + "\\" + name + ".pdb", "/IMPLIB:build\\" + name + "\\" + name + ".lib", "advapi32.lib", "user32.lib",
    ]
    run_cl(vcvars, folder, args)
    return dll


# Shared static material path also linked by tracker/recipe fixtures with the backend.
MATERIAL_SOURCES = ["../frontend/gpu/material_plan.cpp", "../frontend/assets/dds.cpp",
                    "../frontend/assets/image_upload.cpp", "../frontend/assets/buffer_upload.cpp",
                    "../frontend/assets/resource_cache.cpp"]


def build_layer(vcvars: str, include: str, out: str) -> str:
    folder = os.path.join(ROOT, "layer")
    os.makedirs(os.path.join(folder, "build"), exist_ok=True)
    dll = os.path.join(out, LAYER_DLL)
    args = COMMON + [
        "/I" + include, "/Fobuild\\", "/Fdbuild\\efmi_vk_layer_cl.pdb", "/LD",
    ] + sources(folder) + ["../frontend/gpu/fragment_images.cpp"] + MATERIAL_SOURCES + [
        "/Fe:" + dll,
        "/link", "/NOLOGO", "/DLL", "/OPT:REF", "/OPT:ICF", "/DEBUG",
        "/PDB:build\\efmi_vk_layer.pdb", "/IMPLIB:build\\efmi_vk_layer.lib",
        "advapi32.lib", "user32.lib",
    ]
    run_cl(vcvars, folder, args)
    manifest = {
        "file_format_version": "1.2.0",
        "layer": {
            "name": LAYER_NAME,
            "type": "GLOBAL",
            "library_path": ".\\" + LAYER_DLL,
            "api_version": "1.4.321",
            "implementation_version": "1",
            "description": "EFMI Vulkan development probe",
            "functions": {
                "vkNegotiateLoaderLayerInterfaceVersion": "vkNegotiateLoaderLayerInterfaceVersion",
            },
        },
    }
    with open(os.path.join(out, LAYER_JSON), "w", encoding="ascii", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    # The file name selects the entry (see layer/src/util.cpp): the loader is given the layer
    # DLL through the manifest, an injector is given this copy.
    shutil.copyfile(dll, os.path.join(out, INJECT_DLL))
    return dll


def build_sandbox(vcvars: str, include: str, out: str) -> str:
    folder = os.path.join(ROOT, "sandbox")
    os.makedirs(os.path.join(folder, "build"), exist_ok=True)
    exe = os.path.join(out, SANDBOX_EXE)
    args = COMMON + [
        "/I" + include, "/Fobuild\\", "/Fdbuild\\efmi_vk_sandbox_cl.pdb",
    ] + sources(folder) + [
        "/Fe:" + exe,
        "/link", "/NOLOGO", "/OPT:REF", "/OPT:ICF", "/DEBUG", "/SUBSYSTEM:CONSOLE",
        "/PDB:build\\efmi_vk_sandbox.pdb",
        "user32.lib",
    ]
    run_cl(vcvars, folder, args)
    return exe


def build_tracker_test(vcvars: str, include: str, out: str) -> str:
    folder = os.path.join(ROOT, "sandbox")
    scratch = os.path.join(folder, "build", "tracker")
    os.makedirs(scratch, exist_ok=True)
    exe = os.path.join(out, "efmi_vk_tracker_test.exe")
    args = COMMON + ["/I" + include, "/Fobuild\\tracker\\", "/Fdbuild\\tracker\\test_cl.pdb",
                     "tracker_test.cpp", "../layer/src/state.cpp", "../layer/src/resources.cpp", "../layer/src/util.cpp",
                     "../layer/src/pipeline_recipe.cpp",
                     "../layer/src/pipeline_variant.cpp", "../layer/src/spirv_repoint.cpp",
                     "../frontend/gpu/fragment_images.cpp",
                     "../layer/src/variant_cache.cpp", "../layer/src/draw_snapshots.cpp",
                     "../layer/src/merged_skeleton.cpp", "../layer/src/m2_mesh.cpp", "../layer/src/m2_backend.cpp",
                     "../layer/src/character_package.cpp", "../layer/src/character_backend.cpp", "../layer/src/character_materials.cpp", "../layer/src/material_sibling.cpp"] + MATERIAL_SOURCES + [
                     "/Fe:" + exe, "/link", "/NOLOGO", "/DEBUG", "/SUBSYSTEM:CONSOLE",
                     "/PDB:build\\tracker\\test.pdb", "user32.lib"]
    run_cl(vcvars, folder, args)
    return exe


def build_recipe_tests(vcvars: str, include: str, out: str) -> dict:
    folder = os.path.join(ROOT, "sandbox")
    result = {}
    for name, tracker in (("recipe_test", True), ("variant_gpu", False), ("cache_test", False), ("snapshot_test", False),
                          ("mesh_test", False), ("character_test", False)):
        os.makedirs(os.path.join(folder, "build", name), exist_ok=True)
        exe = os.path.join(out, "efmi_vk_" + name + ".exe")
        sources = [name + ".cpp", "../layer/src/pipeline_recipe.cpp", "../layer/src/pipeline_variant.cpp",
                   "../layer/src/spirv_repoint.cpp", "../frontend/gpu/fragment_images.cpp"]
        if name in ("cache_test", "variant_gpu"):
            sources.append("../layer/src/variant_cache.cpp")
        if name in ("variant_gpu", "snapshot_test"):
            sources.append("../layer/src/draw_snapshots.cpp")
        if name == "mesh_test":
            sources += ["../layer/src/m2_mesh.cpp", "../layer/src/util.cpp"]
        if name == "character_test":
            sources += ["../layer/src/character_package.cpp", "../layer/src/character_materials.cpp", "../layer/src/material_sibling.cpp", "../frontend/gpu/material_plan.cpp", "../layer/src/util.cpp"]
        if tracker:
            sources += ["../layer/src/state.cpp", "../layer/src/resources.cpp", "../layer/src/util.cpp",
                        "../layer/src/variant_cache.cpp", "../layer/src/draw_snapshots.cpp",
                        "../layer/src/merged_skeleton.cpp", "../layer/src/m2_mesh.cpp", "../layer/src/m2_backend.cpp",
                        "../layer/src/character_package.cpp", "../layer/src/character_backend.cpp", "../layer/src/character_materials.cpp", "../layer/src/material_sibling.cpp"] + MATERIAL_SOURCES
        args = COMMON + ["/I" + include, "/Fobuild\\" + name + "\\", "/Fdbuild\\" + name + "\\test_cl.pdb"]
        args += sources + ["/Fe:" + exe, "/link", "/NOLOGO", "/DEBUG", "/SUBSYSTEM:CONSOLE",
                           "/PDB:build\\" + name + "\\test.pdb", "user32.lib"]
        run_cl(vcvars, folder, args)
        result[name] = exe
    return result


def build_bone_test(vcvars: str, include: str, out: str) -> str:
    folder = os.path.join(ROOT, "sandbox")
    os.makedirs(os.path.join(folder, "build", "bones"), exist_ok=True)
    exe = os.path.join(out, "efmi_vk_bone_test.exe")
    args = COMMON + ["/I" + include, "/Fobuild\\bones\\", "/Fdbuild\\bones\\test_cl.pdb",
                     "bone_test.cpp", "../layer/src/merged_skeleton.cpp", "/Fe:" + exe,
                     "/link", "/NOLOGO", "/DEBUG", "/SUBSYSTEM:CONSOLE", "/PDB:build\\bones\\test.pdb"]
    run_cl(vcvars, folder, args)
    return exe


def build(targets, out: str | None = None) -> dict:
    out = os.path.abspath(out or os.path.join(ROOT, "dist"))
    os.makedirs(out, exist_ok=True)
    vcvars = find_vcvars()
    include = find_vulkan_include()
    result = {"out": out, "include": include}
    # Every binary of one build carries the stamp of the shared headers it was built from.
    COMMON[:] = [f for f in COMMON if not f.startswith("/DEFVK_ABI=")] + ["/DEFVK_ABI=" + abi_stamp()]
    result["abi"] = abi_stamp()
    if "module" in targets:
        result["module"] = build_module(vcvars, include, out)
    if "layer" in targets:
        result["layer"] = build_layer(vcvars, include, out)
        result["module"] = build_module(vcvars, include, out)
        result["module_mismatch"] = build_module(vcvars, include, out, mismatch=True)
        result["tracker_test"] = build_tracker_test(vcvars, include, out)
        result.update(build_recipe_tests(vcvars, include, out))
        result["bone_test"] = build_bone_test(vcvars, include, out)
    if "sandbox" in targets:
        result["sandbox"] = build_sandbox(vcvars, include, out)
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description="Build the EFMI Vulkan layer and sandbox")
    ap.add_argument("targets", nargs="*", choices=["layer", "sandbox", "module"],
                    help="what to build (default: layer and sandbox; 'module' builds the replacement module alone)")
    ap.add_argument("--out", help="output directory (default: dist)")
    args = ap.parse_args()
    targets = args.targets or ["layer", "sandbox"]
    try:
        result = build(targets, args.out)
    except BuildError as e:
        print(str(e))
        return 1
    print("shared header stamp %s" % result["abi"])
    for key in ("layer", "module", "sandbox", "tracker_test", "recipe_test", "variant_gpu", "bone_test"):
        if key in result:
            print("built %s" % result[key])
    return 0


if __name__ == "__main__":
    sys.exit(main())
