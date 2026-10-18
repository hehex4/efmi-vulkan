"""Compile the original read-only image panel HLSL for Vulkan; no rendering claim."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DXC = Path("F:/vulkan/dxc-1.9.2609/bin/x64/dxc.exe")
SPIRV = Path("F:/vulkan/Vulkan-ValidationLayers/external/Release/64/SPIRV-Tools/build/tools")


MODS = Path("D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Mods")


def installed_panel() -> Path:
    """The first enabled installed copy of the panel shader. The Mods folder is the user's and
    changes; the panel ships with many character Mods under k/ and is the same program."""
    found = sorted((p for p in MODS.glob("*/k/*") if p.name.lower() == "draw_2d.hlsl"
                    and not p.parts[-3].lower().startswith("disabled")), key=lambda p: str(p).lower())
    if not found:
        raise SystemExit("No enabled installed Mod carries k/Draw_2D.hlsl; pass --source")
    return found[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="panel HLSL (default: the first enabled installed copy)")
    parser.add_argument("--out", type=Path, default=ROOT / "build/panel-hlsl")
    args = parser.parse_args()
    args.source = args.source or installed_panel()
    args.out.mkdir(parents=True, exist_ok=True)
    result = dict(scope="Original panel HLSL compiles and validates for Vulkan 1.1; no pipeline, backbuffer, INI parameter upload or pixels tested",
                  compiler=str(DXC), source=str(args.source), source_sha256=hashlib.sha256(args.source.read_bytes()).hexdigest(), stages=[])
    for name, profile, define in (("vertex", "vs_6_0", "VERTEX_SHADER"), ("fragment", "ps_6_0", "PIXEL_SHADER")):
        output = args.out / (name + ".spv")
        command = [str(DXC), "-spirv", "-fspv-target-env=vulkan1.1", "-fvk-use-dx-layout", "-T", profile, "-E", "main", "-D", define,
                   "-Fo", str(output), str(args.source)]
        if name == "vertex":
            command += ["-fvk-invert-y"]
        run = subprocess.run(command, capture_output=True, text=True, errors="replace", timeout=30)
        stage = dict(stage=name, command=command, exit=run.returncode, diagnostics=run.stdout + run.stderr)
        if not run.returncode:
            validate = subprocess.run([str(SPIRV / "spirv-val.exe"), "--target-env", "vulkan1.1", str(output)], capture_output=True, text=True, timeout=30)
            stage.update(validation_exit=validate.returncode, validation=validate.stdout + validate.stderr, bytes=output.stat().st_size,
                         sha256=hashlib.sha256(output.read_bytes()).hexdigest())
            dis = subprocess.run([str(SPIRV / "spirv-dis.exe"), str(output)], capture_output=True, text=True, timeout=30, check=True).stdout
            stage["interface"] = [line.strip() for line in dis.splitlines() if any(word in line for word in ("OpCapability", "DescriptorSet", " Binding ", " Location "))]
        result["stages"].append(stage)
    (args.out / "report.json").write_text(json.dumps(result, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
    good = all(not stage["exit"] and not stage.get("validation_exit", 1) for stage in result["stages"])
    print("Original panel HLSL: " + ("both stages compile and validate" if good else "FAILED; see report.json"))
    return 0 if good else 1


if __name__ == "__main__":
    raise SystemExit(main())
