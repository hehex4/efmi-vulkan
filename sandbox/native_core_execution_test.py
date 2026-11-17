"""Execute installed Core command lists/shaders through one original configuration."""
from pathlib import Path
import json
import hashlib
import struct
import subprocess
import sys
import time
from harness import Checks, Sandbox, ROOT
from native_compute_test import compile_shader, read_compute_result
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient
sys.path.insert(0,str(Path(ROOT)/'scripts'))
from build_shader_model_index import encode

CORE = Path("D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Core/EFMI")


def main():
    case = sys.argv[1] if len(sys.argv) > 1 else "constant"
    if case not in ("constant", "rolling", "gate_enabled", "gate_disabled"):
        raise ValueError("Unknown Core execution case")
    checks = Checks("native original Core execution")
    root = Path(ROOT) / "build/native-core-execution"
    mods = root / "Commands"
    mods.mkdir(parents=True, exist_ok=True)
    if not (CORE / "main.ini").is_file():
        raise RuntimeError("Installed original Core is required for this acceptance fixture")
    # Original Include paths are installation-relative. A project-owned junction
    # reads the installed Core in place; runtime writes go only to fixture settings.
    link = root / "Core"
    if not link.exists():
        quote = lambda path: "'" + str(path).replace("'", "''") + "'"
        subprocess.run(["powershell", "-NoProfile", "-Command",
                        "New-Item -ItemType Junction -Path " + quote(link) + " -Target " + quote(CORE) + " | Out-Null"], check=True)
    if link.resolve() != CORE.resolve():
        raise RuntimeError("Core fixture junction points to an unexpected directory")
    (root / "d3dx.ini").write_text("[Rendering]\ntrack_region_hashes = 1\n[Include]\ninclude = Core\\main.ini\n"
                                  "include_recursive = Commands\n", encoding="ascii")
    source = root / "application.hlsl"
    source.write_text("[[vk::binding(0,0)]] RWStructuredBuffer<uint4> output;\n"
                      "[numthreads(1,1,1)] void main() { output[0] = uint4(7,0,0,0); }\n", encoding="ascii")
    shader = compile_shader(source, root / "compiled", "12345678", "cs")
    # The sandbox's generated vertex program has an explicit synthetic source
    # model for this fixture. Real-game correspondence comes from paired assets.
    vertex=root/'sandbox-vertex.spv'
    reader=Path(ROOT)/'build/shader-input-test/shader_input_test.exe'
    if not reader.is_file():raise RuntimeError('Build the shader model reader fixture first')
    subprocess.run([str(reader),'--sandbox-vertex',str(vertex)],check=True)
    vertex_code=vertex.read_bytes();models=root/'shader-models.bin'
    models.write_bytes(encode([(hashlib.sha256(vertex_code).digest(),len(vertex_code),0x10050)]))
    gate=case.startswith('gate_')
    values = [0.0] * 4096
    values[1], values[2] = 0, 12
    for row in range(6):
        values[(4 + row) * 4:(5 + row) * 4] = [float(10 + row), 2, 3, 4]
        values[(16 + row) * 4:(17 + row) * 4] = [float(30 + row), 5, 6, 7]
    if case == "rolling":
        config = [0] * 64
        config[20], config[21] = 1536, 1548
        (mods / "config.buf").write_bytes(struct.pack("<64I", *config))
        bones = [0.0] * (1560 * 4)
        for row in range(6):
            bones[(1539 + row) * 4:(1540 + row) * 4] = values[(4 + row) * 4:(5 + row) * 4]
            bones[(1551 + row) * 4:(1552 + row) * 4] = values[(16 + row) * 4:(17 + row) * 4]
        (mods / "bones.buf").write_bytes(struct.pack("<%df" % len(bones), *bones))
    else:
        (mods / "config.buf").write_bytes(struct.pack("<4096f", *values))
    (mods / "verify.hlsl").write_text("StructuredBuffer<float4> merged : register(t7);\n"
        "Texture1D<float4> IniParams : register(t120);\nRWStructuredBuffer<uint4> output : register(u0);\n"
        "[numthreads(1,1,1)] void main() { uint ok = 1;\n"
        "for(uint bone=0; bone<512; ++bone) { uint p=bone*3;\n"
        "ok &= all(merged[p]==float4(1,0,0,0)) && all(merged[p+1]==float4(0,1,0,0)) && all(merged[p+2]==float4(0,0,1,0)); }\n"
        "for(uint r=0;r<6;++r) { ok &= all(merged[1536+r]==float4((10+r)*2,4,6,8));\n"
        "ok &= all(merged[1542+r]==float4((30+r)*2,10,12,14)); }\n"
        "output[0]=uint4(ok, (uint)merged[1536].x, (uint)merged[1542].x, (uint)IniParams.Load(int2("+
        ("200" if gate else "0")+",0)).x); }\n", encoding="ascii")
    importer = "RollingBuffer" if case == "rolling" else "ConstantBuffer"
    bone_binding = ("Resource\\EFMIv1\\Input_ComponentBoneMatrices = ref ResourceBones\n" if case == "rolling" else "")
    bone_declaration = ("[ResourceBones]\ntype = StructuredBuffer\nstride = 16\nfilename = bones.buf\n" if case == "rolling" else "")
    (mods / "execution.ini").write_text("[ResourceCaller]\n[ResourceMerged]\ntype = StructuredBuffer\n"
        "stride = 16\narray = 1548\nbind_flags = shader_resource unordered_access\n"
        "[ResourceConfig]\ntype = Buffer\nformat = R32G32B32A32_FLOAT\nbind_flags = constant_buffer\nfilename = config.buf\n"
        "[ShaderOverrideCore]\nhash = %08x\n" % crc32c(shader.read_bytes()) +
        "handling = skip\nResourceCaller = ref cs-u0\n"
        "Resource\\EFMIv1\\OutputMergedSkeleton_Template = ref ResourceMerged\n"
        "run = CommandList\\EFMIv1\\InitializeMergedSkeleton\n"
        "Resource\\EFMIv1\\Output_MergedSkeleton = ref ResourceMerged\n"
        "$\\EFMIv1\\component_vg_offset = 0\n$\\EFMIv1\\component_vg_count = 2\n"
        "$\\EFMIv1\\component_id = 0\n$\\EFMIv1\\lod_level = 0\n$\\EFMIv1\\custom_mesh_scale = 2\n"
        "$\\EFMIv1\\merged_skeleton_offset = 1536\n$\\EFMIv1\\bones_count = 2\n"
        "x0 = 41\ncs-cb0 = ref ResourceConfig\n" + bone_binding +
        "run = CustomShader\\EFMIv1\\_MergedSkeleton_BonesImporter_" + importer + "\n" +
        "cs-u0 = ref ResourceCaller\ncs-t7 = ref ResourceMerged\nrun = CustomShaderVerify\n"
        "[CustomShaderVerify]\ncs = verify.hlsl\ndispatch = 1,1,1\n" + bone_declaration, encoding="ascii")
    if gate:
        with (mods/'execution.ini').open('a',encoding='ascii') as file:
            file.write('[Constants]\n$\\EFMIv1\\enable_mods = '+('1' if case=='gate_enabled' else '0')+'\nx200 = 0\n'
                       '[TextureOverrideGate]\nmatch_type = Buffer\nmatch_bind_flags = +index_buffer\n'
                       'x200 = 1\nhandling = skip\n')
    settings = root / "settings"
    settings.mkdir(exist_ok=True)
    result = root / "result.raw"
    env = dict(EFMI_VK_CONFIGURATION=str(root), EFMI_VK_M2_SETTINGS=str(settings), EFMI_VK_M2_ENABLED="1",
               EFMI_VK_SHADER_MODELS=str(models))
    with Sandbox(threads=0, seconds=6, validation=True, env_extra=env,
                 extra_args=["--compute-shader", str(shader), "--compute-output", str(result), "--compute-capture"]) as sb:
        time.sleep(2)
        with ProbeClient(sb.pid) as probe:
            status = probe.status().get("replacement", {})
        sb.proc.wait(timeout=30)
        (root / (case + "-stdout.txt")).write_text(sb.output(), encoding="utf-8")
        (root / (case + "-layer.log")).write_bytes(Path(sb.log_path).read_bytes())
        checks.equal(sb.proc.returncode, 0, "original Core sandbox exits normally")
    data = read_compute_result(result)
    words = struct.unpack_from("<4I", data) if len(data) == 16 else None
    last=(1 if case=='gate_enabled' else 0) if gate else 41
    checks.equal(words, (1, 20, 60, last), "original Core initializes/imports bones and preserves the expected parameter or conditional IB result")
    hooks = status.get("shader_overrides", {})
    checks.check(hooks.get("executed", 0) > 0 and not hooks.get("last_reason"), "original Core execution reaches native admission")
    if gate:checks.equal(hooks.get('skipped',0)>0,case=='gate_enabled','original Core condition controls actual indexed call suppression')
    layer_log = (root / (case + "-layer.log")).read_text(encoding="utf-8")
    checks.check("does nothing:" not in layer_log and "Unrecognised entry:" not in layer_log,
                 "Core commands and cached shaders execute without unsupported-command fallbacks")
    checks.check("VUID-" not in sb.output() and "Validation Error" not in sb.output(), "Vulkan and synchronization validation clean")
    (root / (case + "-status.json")).write_text(json.dumps(status, indent=2), encoding="ascii")
    print("artifacts:", root)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
