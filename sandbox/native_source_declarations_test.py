"""Current shader declarations reach original INI conditions and actual GPU output."""
from pathlib import Path
import json
import struct
import sys
import time
from harness import Checks, Sandbox, ROOT, configuration_root
from native_compute_test import compile_shader, read_compute_result
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def main():
    case = sys.argv[1] if len(sys.argv) > 1 else "known"
    if case not in ("known", "missing", "words"):
        raise ValueError("Unknown declaration fixture case")
    checks = Checks("native source declarations")
    root = Path(ROOT) / "build/native-source-declarations"
    mods = root / "Mods"
    mods.mkdir(parents=True, exist_ok=True)
    application = root / "application.hlsl"
    source_type = "uint" if case == "words" else "float4"
    application.write_text("[[vk::binding(0,0)]] RWStructuredBuffer<uint4> output;\n" +
                           "[[vk::binding(1,0)]] StructuredBuffer<" + source_type + "> source;\n" +
                           "[[vk::binding(2,0)]] cbuffer Input { uint4 inputValue; };\n"
                           "[numthreads(1,1,1)] void main() { output[0] += inputValue + asuint(source[0]); }\n",
                           encoding="ascii")
    shader = compile_shader(application, root / "compiled", "12345678", "cs")
    (mods / "query.hlsl").write_text("RWStructuredBuffer<uint4> output : register(u0);\n"
                                     "Texture1D<float4> IniParams : register(t120);\n"
                                     "[numthreads(1,1,1)] void main() { float4 p = IniParams.Load(0);\n"
                                     "output[0] = uint4(p.x, 0, p.y, p.z); }\n", encoding="ascii")
    slot, stride = (0 if case == "missing" else 5), (4 if case == "words" else 16)
    (mods / "query.ini").write_text("[ShaderOverrideQuery]\nhash = %08x\n" % crc32c(shader.read_bytes()) +
        "vk_binding = cs-cb3, 0, 2\nvk_binding = cs-t5, 0, 1\n"
        f"if cs->cb_size(3) == 1 && cs->srv_stride({slot}) == {stride} && cs->srv_type(5) == 2 && cs->srv_dimension(5) == 1\n"
        "handling = skip\nx0 = cs->cb_size(3)\ny0 = cs->srv_stride(5)\nz0 = cs->srv_type(5)\n"
        "run = CustomShaderQuery\nendif\n[CustomShaderQuery]\ncs = query.hlsl\ndispatch = 1,1,1\n",
        encoding="ascii")
    settings = root / "settings"
    settings.mkdir(exist_ok=True)
    result = root / "result.raw"
    env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(settings), EFMI_VK_M2_ENABLED="1")
    with Sandbox(threads=0, seconds=4, validation=True, env_extra=env,
                 extra_args=["--compute-shader", str(shader), "--compute-output", str(result),
                             "--compute-constant-buffer", "--compute-capture"]) as sb:
        time.sleep(1.2)
        with ProbeClient(sb.pid) as probe:
            status = probe.status().get("replacement", {})
        sb.proc.wait(timeout=20)
        (root / (case + "-stdout.txt")).write_text(sb.output(), encoding="utf-8")
        (root / (case + "-layer.log")).write_bytes(Path(sb.log_path).read_bytes())
        checks.equal(sb.proc.returncode, 0, "production declaration sandbox exits normally")
    data = read_compute_result(result)
    words = struct.unpack_from("<4I", data) if len(data) == 16 else None
    expected = (1, 0, 16, 2) if case == "known" else (7, 0, 0, 0)
    checks.equal(words, expected, "verified aliases drive replacement; missing and ambiguous declarations preserve the caller")
    hooks = status.get("shader_overrides", {})
    checks.check(hooks.get("executed", 0) > 0 and not hooks.get("last_reason"), "declaration-dependent original commands execute without fallback")
    checks.check("VUID-" not in sb.output() and "Validation Error" not in sb.output(), "Vulkan and synchronization validation clean")
    (root / (case + "-status.json")).write_text(json.dumps(status, indent=2), encoding="ascii")
    print("artifacts:", root)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
