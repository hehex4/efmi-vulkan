"""Original resource copies, CustomShader work and post bindings through production hooks."""
from pathlib import Path
import json
import struct
import time
from harness import Checks, Sandbox, ROOT, configuration_root
from native_compute_test import compile_shader, read_compute_result
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def main():
    checks = Checks("native configuration transaction")
    root = Path(ROOT) / "build/native-configuration"
    mods = root / "Mods"
    mods.mkdir(parents=True, exist_ok=True)
    source = root / "application.hlsl"
    source.write_text("[[vk::binding(0,0)]] RWStructuredBuffer<uint4> output;\n"
                      "[[vk::binding(2,0)]] StructuredBuffer<uint4> input;\n"
                      "[numthreads(1,1,1)] void main() { output[0].x += input[0].x; }\n", encoding="ascii")
    shader = compile_shader(source, root / "compiled", "12345678", "cs")
    (mods / "increment.hlsl").write_text("RWStructuredBuffer<uint4> value : register(u0);\n"
                                         "Texture1D<float4> IniParams : register(t120);\n"
                                         "StructuredBuffer<uint4> virtualInput : register(t7);\n"
                                         "[numthreads(1,1,1)] void main() { value[0].x += uint(IniParams.Load(0).x) + virtualInput[0].x - 7; }\n", encoding="ascii")
    (mods / "transaction.ini").write_text("[Constants]\nglobal $virtual_ready = 0\n[ShaderOverrideTransaction]\nvk_hash = %08x\n" % crc32c(shader.read_bytes()) +
        "vk_binding = cs-t2, 0, 2\nhandling = skip\nResourceOriginal = ref cs-t2\n"
        "ResourceWork = copy cs-t2\nif $virtual_ready == 0\ncs-t7 = ref ResourceOriginal\n$virtual_ready = 1\nendif\n"
        "x0 = 5\nrun = CustomShaderIncrement\ncs-t2 = ref ResourceWork\ndraw = from_caller\n"
        "post cs-t2 = ref ResourceOriginal\npost draw = from_caller\n"
        "[ResourceOriginal]\n[ResourceSavedUAV]\n[ResourceWork]\nbind_flags = shader_resource unordered_access\n"
        "[CustomShaderIncrement]\ncs = increment.hlsl\nResourceSavedUAV = ref cs-u0\n"
        "cs-u0 = ref ResourceWork\ndispatch = 1,1,1\ncs-u0 = ref ResourceSavedUAV\n", encoding="ascii")
    settings = root / "settings"
    settings.mkdir(exist_ok=True)
    result = root / "result.raw"
    env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(settings), EFMI_VK_M2_ENABLED="1")
    with Sandbox(threads=0, seconds=4, validation=True, env_extra=env,
                 extra_args=["--compute-shader", str(shader), "--compute-output", str(result), "--compute-bindings", "--compute-capture"]) as sb:
        time.sleep(1.2)
        with ProbeClient(sb.pid) as probe:
            status = probe.status().get("replacement", {})
        sb.proc.wait(timeout=20)
        (root / "layer.log").write_bytes(Path(sb.log_path).read_bytes())
        (root / "stdout.txt").write_text(sb.output(), encoding="utf-8")
        checks.equal(sb.proc.returncode, 0, "production sandbox exits normally")
    data = read_compute_result(result)
    checks.equal(struct.unpack_from("<I", data)[0] if data else None, 19,
                 "original copy plus CustomShader reaches caller as 12; post restore reaches it as 7")
    hooks = status.get("shader_overrides", {})
    checks.check(hooks.get("executed", 0) > 0, "production hook admits and records the original transaction")
    checks.check(not hooks.get("last_reason"), "source mapping and binding application have no refusal")
    checks.check("VUID-" not in sb.output() and "Validation Error" not in sb.output(), "core and synchronization validation clean")
    (root / "status.json").write_text(json.dumps(status, indent=2), encoding="ascii")
    print("artifacts:", root)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
