"""Build the unchanged pinned vkd3d shader library using owned portable tools.

All dependencies and generated files stay under F:/vulkan. No registration,
system environment changes, upstream edits or runtime shader caches are used.
"""
from __future__ import annotations
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tarfile
import urllib.request

ROOT = Path("F:/vulkan")
SOURCE = ROOT / "vkd3d-2.1"
OUT = ROOT / "vkd3d-2.1-build"
LIBRARY = OUT / "efmi-vkd3d-shader.dll"
PIN = "7510146aff2adfb4ae07ab890701a607e5ff7c66e57100cfc9f630ec92eeda6a"
URL = "https://dl.winehq.org/vkd3d/source/vkd3d-2.1.tar.xz"
HEADERS_PIN = "5bbea925663d4cd2bab23efad53874f2718248a73dcaf9dd21dff8cb48e602fc"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_source() -> dict[str, str]:
    archive = ROOT / "vkd3d-2.1.tar.xz"
    if not archive.exists():
        data = urllib.request.urlopen(URL, timeout=60).read(32 << 20)
        if hashlib.sha256(data).hexdigest() != PIN:
            raise RuntimeError("Bytecode compiler archive differs from the pinned release")
        archive.write_bytes(data)
    result = {}
    for path, expected, directory in [(archive, PIN, SOURCE),
            (ROOT / "SPIRV-Headers-1.4.321.0.tar.gz", HEADERS_PIN, ROOT / "SPIRV-Headers-1.4.321.0")]:
        if digest(path) != expected:
            raise RuntimeError("Compiler dependency archive differs: " + str(path))
        with tarfile.open(path) as tar:
            for member in tar.getmembers():
                relative = Path(*Path(member.name).parts[1:])
                target = (directory / relative).resolve()
                if not target.is_relative_to(directory.resolve()) or member.issym() or member.islnk():
                    raise RuntimeError("Unexpected compiler archive member")
                if member.isfile():
                    original = hashlib.sha256(tar.extractfile(member).read()).hexdigest()
                    if digest(target) != original:
                        raise RuntimeError("Compiler source is modified: " + str(target))
                    result[str(target)] = original
    return result


def build_dependency() -> Path:
    sources = verify_source()
    kit = ROOT / "w64devkit-1.23.0/w64devkit/bin"
    parsers = ROOT / "winflexbison-2.5.25"
    OUT.mkdir(exist_ok=True)
    (OUT / "tmp").mkdir(exist_ok=True)
    env = os.environ.copy()
    env.update(PATH=str(kit) + os.pathsep + env.get("PATH", ""), TMP=str(OUT / "tmp"), TEMP=str(OUT / "tmp"))
    config = '#define PACKAGE_VERSION "2.1"\n#define PACKAGE_STRING "vkd3d 2.1"\n#define PACKAGE_NAME "vkd3d"\n'
    config += "".join("#define " + name + " 1\n" for name in (
        "HAVE_ATOMIC_EXCHANGE_N", "HAVE_BUILTIN_ADD_OVERFLOW", "HAVE_BUILTIN_CLZ", "HAVE_BUILTIN_CTZ",
        "HAVE_BUILTIN_POPCOUNT", "HAVE_SYNC_ADD_AND_FETCH", "HAVE_SYNC_BOOL_COMPARE_AND_SWAP",
        "HAVE_SPIRV_UNIFIED1_SPIRV_H", "HAVE_SPIRV_UNIFIED1_GLSL_STD_450_H", "HAVE__STRTOD_L"))
    tools = [kit / "gcc.exe", kit.parent / "libexec/gcc/x86_64-w64-mingw32/14.1.0/cc1.exe",
             kit / "as.exe", kit / "ld.exe", parsers / "win_bison.exe", parsers / "win_flex.exe"]
    tool_hashes = {str(p): digest(p) for p in tools}
    flags = ["-std=gnu11", "-O2", "-DLIBVKD3D_SHADER_SOURCE", "-DVKD3D_DEBUG_ENV=VKD3D_SHADER_DEBUG"]
    flags += ["-I" + str(p) for p in (OUT, SOURCE / "include", SOURCE / "include/private", SOURCE / "libs/vkd3d-shader",
                                     ROOT / "SPIRV-Headers-1.4.321.0/include", ROOT / "Vulkan-Headers/include")]
    key = hashlib.sha256(json.dumps([sources, tool_hashes, flags, config, digest(Path(__file__))], sort_keys=True).encode()).hexdigest()
    report_path = OUT / "project-build-report.json"
    if report_path.exists() and LIBRARY.exists():
        previous = json.loads(report_path.read_text())
        if previous.get("key") == key and previous.get("dll_sha256") == digest(LIBRARY):
            return LIBRARY
    (OUT / "config.h").write_text(config, encoding="ascii")
    (OUT / "vkd3d_version.h").write_text('#define VKD3D_VCS_ID " (release 2.1)"\n', encoding="ascii")
    commands = []

    def run(args: list, label: str) -> None:
        args = [str(x) for x in args]
        commands.append(args)
        p = subprocess.run(args, cwd=OUT, env=env, capture_output=True, timeout=180)
        (OUT / (label + ".log")).write_bytes(p.stdout + p.stderr)
        if p.returncode:
            raise RuntimeError(label + " failed: " + (p.stdout + p.stderr).decode("utf-8", "replace")[-4000:])

    for name in ("hlsl", "preproc"):
        run([parsers / "win_bison.exe", "-d", "-o", OUT / (name + ".tab.c"), SOURCE / "libs/vkd3d-shader" / (name + ".y")], name + "-bison")
        run([parsers / "win_flex.exe", "-o", OUT / (name + ".yy.c"), SOURCE / "libs/vkd3d-shader" / (name + ".l")], name + "-flex")
    units = [SOURCE / "libs/vkd3d-common" / (n + ".c") for n in ("blob", "debug", "error", "memory", "utf8")]
    units += [SOURCE / "libs/vkd3d-shader" / (n + ".c") for n in (
        "checksum", "d3d_asm", "d3dbc", "dxbc", "dxil", "fx", "glsl", "hlsl", "hlsl_codegen", "hlsl_constant_ops",
        "ir", "msl", "spirv", "tpf", "vkd3d_shader_main")]
    units += [OUT / (n + suffix + ".c") for n in ("hlsl", "preproc") for suffix in (".tab", ".yy")]

    def compile_unit(path: Path) -> Path:
        output = OUT / (path.stem + ".o")
        run([kit / "gcc.exe", *flags, "-c", path, "-o", output], path.stem)
        return output

    with ThreadPoolExecutor(max_workers=3) as pool:
        objects = list(pool.map(compile_unit, units))
    run([kit / "gcc.exe", "-shared", "-static-libgcc", "-o", LIBRARY, *objects, "-lole32", "-luuid", "-lm"], "link")
    report = dict(key=key, commands=commands, source_sha256=sources, tools_sha256=tool_hashes, dll_sha256=digest(LIBRARY))
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
    return LIBRARY


if __name__ == "__main__":
    print("Verified bytecode compiler:", build_dependency())
