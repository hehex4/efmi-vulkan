"""Original compute sampler inheritance from an exact source pair and immutable set layout."""
from pathlib import Path
import hashlib
import struct
import subprocess
import sys
import time

from harness import Checks, ROOT, Sandbox, configuration_root
from native_compute_test import compile_shader, read_compute_result
from native_image_test import dds
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient

sys.path.insert(0, str(Path(ROOT) / "scripts"))
from build_shader_model_index import encode_sources
from check_shader_inputs import build


def main(case):
    checks = Checks("original sampler " + case)
    accepted = case in ("immutable", "push", "push_then_caller")
    root = Path(ROOT) / "build/original-sampler"
    mods = root / "Mods"
    mods.mkdir(parents=True, exist_ok=True)
    native = root / "application.hlsl"
    declarations = ("RWStructuredBuffer<uint4> output : register(u0);\n"
                    "Texture2DArray<float4> image : register(t0);\n"
                    "SamplerState pointSource : register(s5);\n")
    body = ("[numthreads(1,1,1)] void main(){float r=image.SampleLevel(pointSource,float3(.49,.5,0),0).r;"
            "output[0]=uint4(7,round(r*255),0,0);}\n")
    if case == "push_then_caller":
        body = body.replace("uint4(7,", "uint4(output[0].x+7,")
    native.write_text("".join(f"[[vk::binding({binding},0)]] " + line for binding, line in
                              zip((0, 2, 3), declarations.splitlines(True))) + body, encoding="ascii")
    shader = compile_shader(native, root / "compiled", "12345678", "cs")
    original = root / "source.hlsl"
    original.write_text(declarations + body, encoding="ascii")
    helper = build(Path(ROOT) / "build/shader-input-test")
    bytecode = root / "source.dxbc"
    subprocess.run([str(helper), "--dxbc", str(original), str(bytecode)], check=True)
    code = shader.read_bytes()
    metadata = root / "sources.bin"
    metadata.write_bytes(encode_sources([(hashlib.sha256(code).digest(), len(code), bytecode.read_bytes())]))
    texture = mods / "stripes.dds"
    dds(texture, (0, 0, 0, 255))
    stripe = bytes((0, 0, 0, 255)) * 2 + bytes((200, 0, 0, 255)) * 2
    texture.write_bytes(texture.read_bytes()[:-64] + stripe * 4)
    slot = 0 if case == "other_slot" else 5
    (mods / "replace.hlsl").write_text("Texture2D<float4> stripes : register(t12);\n"
        f"SamplerState fromContext : register(s{slot});\nRWStructuredBuffer<uint4> output : register(u0);\n"
        "[numthreads(1,1,1)] void main(){float r=stripes.SampleLevel(fromContext,float2(.49,.5),0).r;"
        "output[0]=uint4(23+round(r*255),0,0,0);}\n", encoding="ascii")
    (mods / "test.ini").write_text("[ShaderOverrideSampler]\nvk_hash = %08x\nhandling = skip\nrun = CustomShaderSampler\n"
        "[ResourceStripes]\nfilename = stripes.dds\n[CustomShaderSampler]\ncs = replace.hlsl\n"
        "cs-t12 = ref ResourceStripes\ndispatch = 1,1,1\n" % crc32c(code), encoding="ascii")
    if case == "push_then_caller":
        ini = mods / "test.ini"
        ini.write_text(ini.read_text().replace("handling = skip\n", ""), encoding="ascii")
    output = root / "result.raw"
    output.unlink(missing_ok=True)
    env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(root / "settings"),
               EFMI_VK_M2_ENABLED="1", EFMI_VK_SHADER_SOURCES=str(metadata))
    args = ["--compute-shader", str(shader), "--compute-output", str(output), "--compute-image",
            "--texture-shape", "1", "--compute-capture"]
    if case in ("mutable", "push", "push_then_caller"):
        args.append("--mutable-sampler")
    if case in ("push", "push_then_caller"):
        args += ["--descriptors", "push"]
    with Sandbox(threads=0, seconds=4, validation=True, env_extra=env, extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:
            status = probe.status().get("replacement", {}).get("shader_overrides", {})
        sb.proc.wait(timeout=20)
        checks.equal(sb.proc.returncode, 0, "Sandbox exited normally")
        log = sb.output()
        (root / (case + "-stdout.txt")).write_text(log, encoding="utf8")
    data = read_compute_result(output)
    checks.equal(struct.unpack_from("<I", data)[0] if len(data) >= 4 else None,
                 30 if case == "push_then_caller" else 23 if accepted else 7, "Sample uses the bound nearest filter at sparse s5")
    why = status.get("last_reason", "")
    checks.check(status.get("executed", 0) > 0 and not why if accepted else
                 "unknown application sampler" in why, "Only the proven original register executes")
    print("source sampler admission", repr(why))
    checks.check("VUID-" not in log and "Validation Error" not in log, "Vulkan and synchronization validation clean")
    return checks.finish()


if __name__ == "__main__":
    mode = sys.argv[1]
    if mode not in ("immutable", "push", "push_then_caller", "mutable", "other_slot"):
        raise ValueError(mode)
    raise SystemExit(main(mode))
