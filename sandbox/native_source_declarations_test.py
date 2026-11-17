"""Current shader declarations reach original INI conditions and actual GPU output."""
from pathlib import Path
import json
import struct
import sys
import time
import hashlib
import subprocess
from harness import Checks, Sandbox, ROOT, configuration_root
from native_compute_test import compile_shader, read_compute_result
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def main():
    case = sys.argv[1] if len(sys.argv) > 1 else "known"
    if case not in ("known", "missing", "words", "exact", "exact_unknown", "exact_slot",
                    "exact_inherit", "exact_inherit_unknown", "exact_inherit_nested",
                    "exact_inherit_source_fnv", "exact_inherit_source_embedded", "exact_inherit_source_bytecode", "exact_inherit_source_unknown", "exact_inherit_source_slots", "exact_inherit_source_regex",
                    "exact_inherit_source_regex_skip", "exact_inherit_source_regex_prepost", "exact_inherit_source_regex_nomatch", "exact_inherit_source_regex_indirect",
                    "exact_inherit_source_regex_indirect_skip", "exact_inherit_source_regex_indirect_prepost", "exact_inherit_source_regex_indirect_nomatch"):
        raise ValueError("Unknown declaration fixture case")
    checks = Checks("native source declarations")
    regex_case='_regex' in case
    root = Path(ROOT) / "build/native-source-declarations"
    mods = root / "Mods"
    mods.mkdir(parents=True, exist_ok=True)
    application = root / "application.hlsl"
    source_type = "uint" if case == "words" or (case.startswith("exact") and not case.startswith("exact_inherit")) else "float4"
    application.write_text("[[vk::binding(0,0)]] RWStructuredBuffer<uint4> output;\n" +
                           "[[vk::binding(1,0)]] StructuredBuffer<" + source_type + "> source;\n" +
                           "[[vk::binding(2,0)]] cbuffer Input { uint4 inputValue; };\n"
                           "[numthreads(1,1,1)] void main() { output[0] += inputValue + asuint(source[0]); }\n",
                           encoding="ascii")
    if regex_case:
        application.write_text(application.read_text().replace('asuint(source[0]);','asuint(source[0]) + uint4(1,0,0,0);'),encoding='ascii')
    shader = compile_shader(application, root / "compiled", "12345678", "cs")
    (mods / "query.hlsl").write_text("RWStructuredBuffer<uint4> output : register(u0);\n"
                                     "Texture1D<float4> IniParams : register(t120);\n"
                                     "[numthreads(1,1,1)] void main() { float4 p = IniParams.Load(0);\n"
                                     "output[0] = uint4(p.x, 0, p.y, p.z); }\n", encoding="ascii")
    slot, stride = (0 if case == "missing" else 5), (4 if case == "words" or case.startswith("exact") else 16)
    (mods / "query.ini").write_text("[ShaderOverrideQuery]\nvk_hash = %08x\n" % crc32c(shader.read_bytes()) +
        "vk_binding = cs-cb3, 0, 2\nvk_binding = cs-t5, 0, 1\n"
        f"if cs->cb_size(3) == 1 && cs->srv_stride({slot}) == {stride} && cs->srv_type(5) == 2 && cs->srv_dimension(5) == 1\n"
        "handling = skip\nx0 = cs->cb_size(3)\ny0 = cs->srv_stride(5)\nz0 = cs->srv_type(5)\n"
        "run = CustomShaderQuery\nendif\n[CustomShaderQuery]\ncs = query.hlsl\ndispatch = 1,1,1\n",
        encoding="ascii")
    settings = root / "settings"
    settings.mkdir(exist_ok=True)
    result = root / "result.raw"
    env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(settings), EFMI_VK_M2_ENABLED="1")
    source_entry=case.startswith('exact_inherit_source_')
    hash_mode=case.rsplit('_',1)[1] if source_entry and case.rsplit('_',1)[1] in ('fnv','embedded','bytecode') else 'fnv'
    (root/'d3dx.ini').write_text('[Rendering]\nshader_hash = '+('3dmigoto' if hash_mode=='fnv' else hash_mode)+'\n[Include]\ninclude_recursive = Mods\n',encoding='ascii')
    if case.startswith("exact"):
        sys.path.insert(0,str(Path(ROOT)/'scripts'))
        from check_shader_inputs import build
        from build_shader_model_index import encode_sources
        helper=build(Path(ROOT)/'build/shader-input-test')
        original=root/'original.hlsl';bytecode=root/'original.dxbc'
        original.write_text('RWStructuredBuffer<uint4> output : register(u0);\nStructuredBuffer<'+source_type+'> source : register(t5);\n'
                            'cbuffer Input : register(b3) { uint4 inputValue; };\n'
                            '[numthreads(1,1,1)] void main() { output[0] += inputValue + asuint(source[0]); }\n',encoding='ascii')
        if source_entry:
            original.write_text(original.read_text().replace('register(t5)','register(t1)' if case.endswith('_slots') else 'register(t0)').replace('register(b3)','register(b2)' if case.endswith('_slots') else 'register(b0)'),encoding='ascii')
        if regex_case:
            original.write_text(original.read_text().replace('asuint(source[0]);','asuint(source[0]) + uint4(1,0,0,0);'),encoding='ascii')
        subprocess.run([str(helper),'--dxbc',str(original),str(bytecode)],check=True)
        metadata=root/'shader-sources.bin';code=shader.read_bytes()
        identity=hashlib.sha256(code+b'changed' if case.endswith('_unknown') else code).digest()
        metadata.write_bytes(encode_sources([(identity,len(code),bytecode.read_bytes())]))
        env['EFMI_VK_SHADER_SOURCES']=str(metadata)
        ini=mods/'query.ini'
        ini.write_text(ini.read_text().replace('if cs->cb_size(3) == 1',
            'if cs->cb_type(3) == 1 && cs->srv_stride(0) == -1 && cs->cb_size(%d) == 1' %
            (999 if case=='exact_slot' else 3)),encoding='ascii')
        if case.startswith('exact_inherit'):
            (mods/'zero.hlsl').write_text('RWStructuredBuffer<uint4> output : register(u0);\n'
                '[numthreads(1,1,1)] void main() { output[0]=0; }\n',encoding='ascii')
            ini.write_text('[ShaderOverrideInherited]\nvk_hash = %08x\n'%crc32c(code)+
                'vk_binding = cs-cb3, 0, 2\nvk_binding = cs-t5, 0, 1\nhandling = skip\n'
                'cs-cb3 = ResourceInput\nrun = CustomShaderInherited\n'
                '[ResourceInput]\ntype = Buffer\nbind_flags = constant_buffer\nstride = 16\ndata = R32_UINT 23 0 0 0\n'
                '[CustomShaderInherited]\n'+('run = CustomShaderZero\n' if case.endswith('_nested') else '')+
                'dispatch = 1,1,1\n[CustomShaderZero]\ncs = zero.hlsl\ndispatch = 1,1,1\n',encoding='ascii')
            if source_entry:
                from check_shader_hashes import expected as source_hashes
                digest=source_hashes(bytecode.read_bytes())[('fnv','embedded','bytecode').index(hash_mode)]
                text=ini.read_text().replace('vk_hash = %08x'%crc32c(code),'hash = %016x\nfilter_index = 42\nif cs == 42'%digest)
                text=text.replace('vk_binding = cs-cb3, 0, 2\nvk_binding = cs-t5, 0, 1\n','').replace('cs-cb3 =','cs-cb2 =' if case.endswith('_slots') else 'cs-cb0 =')
                text=text.replace('run = CustomShaderInherited\n','run = CustomShaderInherited\nendif\n')
                ini.write_text(text,encoding='ascii')
            if regex_case:
                commands='handling = skip\n' if case.endswith('_skip') else ''
                tail=''
                if case.endswith('_prepost'):
                    commands='cs-cb0 = ResourceInput\npost run = CustomShaderPost\n'
                    tail='[ResourceInput]\ntype = Buffer\nbind_flags = constant_buffer\nstride = 16\ndata = R32_UINT 23 0 0 0\n[CustomShaderPost]\ncs = post.hlsl\ndispatch = 1,1,1\n'
                    (mods/'post.hlsl').write_text('RWStructuredBuffer<uint4> output:register(u0);[numthreads(1,1,1)] void main(){output[0]+=uint4(10,0,0,0);}',encoding='ascii')
                pattern='UNMATCHED_OPCODE' if case.endswith('_nomatch') else 'l\\(1, 0, 0, 0\\)'
                ini.write_text('[ShaderRegexCompute]\nshader_model = cs_5_0\n'+commands+'[ShaderRegexCompute.Pattern]\n'+pattern+'\n[ShaderRegexCompute.Pattern.Replace]\nl(9, 0, 0, 0)\n'+tail,encoding='ascii')
    with Sandbox(threads=0, seconds=4, validation=True, env_extra=env,
                 extra_args=["--compute-shader", str(shader), "--compute-output", str(result),
                             "--compute-constant-buffer", "--compute-capture"]+(['--compute-indirect'] if '_indirect' in case else [])) as sb:
        time.sleep(1.2)
        with ProbeClient(sb.pid) as probe:
            status = probe.status().get("replacement", {})
        sb.proc.wait(timeout=20)
        (root / (case + "-stdout.txt")).write_text(sb.output(), encoding="utf-8")
        (root / (case + "-layer.log")).write_bytes(Path(sb.log_path).read_bytes())
        checks.equal(sb.proc.returncode, 0, "production declaration sandbox exits normally")
    data = read_compute_result(result)
    words = struct.unpack_from("<4I", data) if len(data) == 16 else None
    expected = (23, 0, 0, 0) if case in ('exact_inherit','exact_inherit_nested') or (source_entry and not case.endswith(('_unknown','_slots'))) else (1, 0, 4, 2) if case == "exact" else (1, 0, 16, 2) if case == "known" else (7, 0, 0, 0)
    if regex_case:expected=(0 if case.endswith('_skip') else 42 if case.endswith('_prepost') else 8 if case.endswith('_nomatch') else 16,0,0,0)
    checks.equal(words, expected, "verified aliases drive replacement; missing and ambiguous declarations preserve the caller")
    hooks = status.get("shader_overrides", {})
    if case=='exact_inherit_unknown':
        checks.check('unknown application compute stage' in hooks.get('last_reason',''),'unknown inherited source preserves the caller with an explicit reason')
    elif case.endswith('_slots'):
        checks.check('source slot unavailable' in hooks.get('last_reason',''),'incompatible source addresses preserve the caller instead of binding unrelated data')
    else:
        checks.check(hooks.get("executed", 0) > 0 and not hooks.get("last_reason"), "declaration-dependent original commands execute without fallback")
    checks.check("VUID-" not in sb.output() and "Validation Error" not in sb.output(), "Vulkan and synchronization validation clean")
    (root / (case + "-status.json")).write_text(json.dumps(status, indent=2), encoding="ascii")
    print("artifacts:", root)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
