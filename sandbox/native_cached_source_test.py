"""Original cached game-buffer queries reach actual CustomShader GPU outputs."""
from pathlib import Path
import json
import struct
import time
from harness import Checks, Sandbox, ROOT, configuration_root
from native_compute_test import compile_shader, read_compute_result
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def main():
    checks = Checks("native cached source queries")
    root = Path(ROOT) / "build/native-cached-source"
    mods = root / "Mods"
    mods.mkdir(parents=True, exist_ok=True)
    source = root / "application.hlsl"
    source.write_text("[[vk::binding(0,0)]] RWStructuredBuffer<uint4> output;\n"
                      "[[vk::binding(2,0)]] cbuffer Input { uint4 inputValue; };\n"
                      "[numthreads(1,1,1)] void main() { output[0].x += inputValue.x; }\n", encoding="ascii")
    shader = compile_shader(source, root / "compiled", "12345678", "cs")
    (mods / "query.hlsl").write_text("RWStructuredBuffer<uint4> output : register(u0);\n"
                                     "Texture1D<float4> IniParams : register(t120);\n"
                                     "[numthreads(1,1,1)] void main() { float4 p = IniParams.Load(0);\n"
                                     "output[0] = uint4(asuint(p.x), 0, asuint(p.y), 23); }\n", encoding="ascii")
    (mods / "query.ini").write_text("[ShaderOverrideQuery]\nvk_hash = %08x\n" % crc32c(shader.read_bytes()) +
        "vk_binding = cs-cb0, 0, 2\nhandling = skip\nResourceSaved = ref cs-cb0\n"
        "x0 = ResourceSaved->HashRegion(0,16)\ny0 = ResourceSaved->SpatialHash(0,1,2,1)\n"
        "run = CustomShaderQuery\n[ResourceSaved]\n[CustomShaderQuery]\ncs = query.hlsl\ndispatch = 1,1,1\n", encoding="ascii")
    settings = root / "settings"
    settings.mkdir(exist_ok=True)
    result = root / "result.raw"
    env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(settings), EFMI_VK_M2_ENABLED="1")
    with Sandbox(threads=0, seconds=4, validation=True, env_extra=env,
                 extra_args=["--compute-shader", str(shader), "--compute-output", str(result),
                             "--compute-constant-buffer", "--compute-input-word", "0x41800000", "--compute-capture"]) as sb:
        time.sleep(1.2)
        with ProbeClient(sb.pid) as probe:
            status = probe.status().get("replacement", {})
        sb.proc.wait(timeout=20)
        (root / "stdout.txt").write_text(sb.output(), encoding="utf-8")
        (root / "layer.log").write_bytes(Path(sb.log_path).read_bytes())
        checks.equal(sb.proc.returncode, 0, "production cached source sandbox exits normally")
    data = read_compute_result(result)
    words = struct.unpack_from("<4I", data) if len(data) >= 16 else None
    value = crc32c(struct.pack("<4I", 0x41800000, 0, 0, 0))
    value ^= value >> 16
    value = value * 0x85EBCA6B & 0xFFFFFFFF
    value ^= value >> 13
    region = ((1 + ((value & 0x3FFFFFFF) >> 23)) << 23) | (value & 0x7FFFFF)
    # The binding-scene readback reserves word 1 for the alternate output buffer.
    checks.equal(words, (region, 0, 16 << 20, 23), "original region/spatial query bits reach the GPU through IniParams")
    hooks = status.get("shader_overrides", {})
    checks.check(hooks.get("executed", 0) > 0 and not hooks.get("last_reason"), "production cache queries execute without fallback")
    checks.check("VUID-" not in sb.output() and "Validation Error" not in sb.output(), "Vulkan and synchronization validation clean")
    (root / "status.json").write_text(json.dumps(status, indent=2), encoding="ascii")
    print("artifacts:", root)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
