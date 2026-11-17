"""Original graphics CustomShader -> sampled image -> compute -> application call."""
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
    mode = sys.argv[1] if len(sys.argv) > 1 else "enabled"
    if mode not in ("enabled", "sampler", "missing_stage", "missing_state", "missing_sampler"):
        raise ValueError("unknown graphics configuration mode")
    accepted = mode in ("enabled", "sampler")
    sampling = mode in ("sampler", "missing_sampler")
    checks = Checks("native configuration graphics " + mode)
    root = Path(ROOT) / "build/native-configuration-graphics"
    mods = root / "Mods"
    mods.mkdir(parents=True, exist_ok=True)
    source = root / "application.hlsl"
    source.write_text("[[vk::binding(0,0)]] RWStructuredBuffer<uint4> output;\n"
                      "[[vk::binding(2,0)]] StructuredBuffer<uint4> input;\n"
                      "[numthreads(1,1,1)] void main() { output[0].x += input[0].x; }\n", encoding="ascii")
    shader = compile_shader(source, root / "compiled", "12345678", "cs")
    pixel = ("Texture2D<float4> input:register(t0); SamplerState filter:register(s0);\n"
             "float4 main():SV_Target { return input.SampleLevel(filter,float2(.5,.5),0); }\n") if sampling else (
             "float4 main():SV_Target { return float4(23,0,0,1); }\n")
    (mods / "graphics.hlsl").write_text(
        "#ifdef VERTEX_SHADER\nfloat4 main(uint id:SV_VertexID):SV_Position {\n"
        "return float4(id==2?3:-1,id==1?3:-1,0,1); }\n#else\n"
        + pixel + "#endif\n", encoding="ascii")
    (mods / "sample.hlsl").write_text(
        "Texture2D<float4> image:register(t0); RWStructuredBuffer<uint4> value:register(u0);\n"
        "[numthreads(1,1,1)] void main() { value[0].x=uint(image.Load(int3(0,0,0)).x); }\n", encoding="ascii")
    stages = "vs = graphics.hlsl\nps = graphics.hlsl\nhs = null\nds = null\ngs = null\n"
    state = "topology = triangle_list\ncull = none\ndepth_enable = false\nsample_mask = 0xffffffff\n"
    if mode == "missing_stage":
        stages = stages.replace("gs = null\n", "")
    if mode == "missing_state":
        state = state.replace("sample_mask = 0xffffffff\n", "")
    if mode == "sampler":
        state += "sampler = point_filter\n"
    sampled_resource = ("[ResourceSampling]\ntype = Texture2D\nwidth = 1\nheight = 1\narray = 1\nmips = 1\nmsaa = 1\n"
                        "format = R32G32B32A32_FLOAT\nbind_flags = render_target shader_resource\n") if sampling else ""
    sampled_binding = "o0 = ref ResourceSampling\nclear = o0 23.0 0.0 0.0 1.0\n" if sampling else ""
    (mods / "graphics.ini").write_text(
        "[ShaderOverrideTransaction]\nvk_hash = %08x\nvk_binding = cs-t2, 0, 2\n" % crc32c(shader.read_bytes()) +
        "handling = skip\nResourceOriginal = ref cs-t2\nResourceWork = copy cs-t2\n"
        "run = CustomShaderGraphics\nrun = CustomShaderSample\ncs-t2 = ref ResourceWork\n"
        "draw = from_caller\npost cs-t2 = ref ResourceOriginal\n"
        "[ResourceOriginal]\n[ResourceSavedUAV]\n[ResourceWork]\nbind_flags = shader_resource unordered_access\n"
        "[ResourcePixels]\ntype = Texture2D\nwidth = 4\nheight = 4\narray = 1\nmips = 1\nmsaa = 1\n"
        "format = R32G32B32A32_FLOAT\nbind_flags = render_target shader_resource\n" +
        sampled_resource + "[CustomShaderGraphics]\n" + stages + state + sampled_binding +
        "o0 = ref set_viewport ResourcePixels\n" + ("ps-t0 = ref ResourceSampling\n" if sampling else "") +
        "".join("o%d = null\n" % i for i in range(1, 8)) +
        "oD = null\nclear = o0 0 0 0 0\ndraw = 3,0\n"
        "[CustomShaderSample]\ncs = sample.hlsl\nResourceSavedUAV = ref cs-u0\n"
        "cs-t0 = ref ResourcePixels\ncs-u0 = ref ResourceWork\ndispatch = 1,1,1\n"
        "cs-u0 = ref ResourceSavedUAV\ncs-t0 = null\n", encoding="ascii")
    settings = root / "settings"
    settings.mkdir(exist_ok=True)
    result = root / "result.raw"
    env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(settings), EFMI_VK_M2_ENABLED="1")
    with Sandbox(threads=0, seconds=4, validation=True, env_extra=env,
                 extra_args=["--compute-shader", str(shader), "--compute-output", str(result), "--compute-bindings", "--compute-capture"]) as sb:
        time.sleep(1.5)
        with ProbeClient(sb.pid) as probe:
            status = probe.status().get("replacement", {})
        sb.proc.wait(timeout=20)
        (root / (mode + "-layer.log")).write_bytes(Path(sb.log_path).read_bytes())
        (root / (mode + "-stdout.txt")).write_text(sb.output(), encoding="utf-8")
        checks.equal(sb.proc.returncode, 0, "production graphics sandbox exits normally")
    data = read_compute_result(result)
    checks.equal(struct.unpack_from("<I", data)[0] if data else None, 23 if accepted else 7,
                 "original graphics pixels reach the application only after complete admission")
    hooks = status.get("shader_overrides", {})
    if accepted:
        checks.check(hooks.get("executed", 0) > 0 and not hooks.get("last_reason"), "production records the complete original graphics transaction")
    else:
        expected = "unknown application sampler" if sampling else "unknown application graphics"
        checks.check(expected in hooks.get("last_reason", ""), "unknown inherited graphics state refuses explicitly")
    checks.check("VUID-" not in sb.output() and "Validation Error" not in sb.output(), "core and synchronization validation clean")
    (root / (mode + "-status.json")).write_text(json.dumps(status, indent=2), encoding="ascii")
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
