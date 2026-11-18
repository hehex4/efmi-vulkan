"""Exact inherited vertex bytecode and input semantics through the loaded layer."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import time
from harness import ROOT, Checks, Sandbox, configuration_root
from native_shader_test import colored_pixels
from native_compute_test import compile_shader
from runtime_test import sandbox_shader_hashes
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient
sys.path.insert(0, str(Path(ROOT) / 'scripts'))
from check_shader_inputs import build
from build_shader_input_index import encode
from build_shader_model_index import encode_sources


def main():
    case = sys.argv[1] if len(sys.argv) > 1 else 'enabled'
    if case not in ('enabled', 'unknown_source', 'unknown_layout', 'direct_root_this', 'nested', 'reference', 'state', 'state_nested',
                    'indirect', 'indirect_nonindexed', 'indirect_count', 'regex', 'regex_nested', 'regex_nomatch', 'regex_caller_unknown', 'regex_caller', 'regex_caller_indirect', 'regex_caller_skip', 'regex_caller_pixel', 'regex_caller_self_ref', 'regex_caller_indirect_self_ref'):
        raise ValueError('Unknown inherited graphics fixture')
    original_caller = case in ('regex_caller', 'regex_caller_indirect', 'regex_caller_skip', 'regex_caller_pixel', 'regex_caller_self_ref', 'regex_caller_indirect_self_ref')
    checks = Checks('native inherited graphics ' + case)
    root = Path(ROOT) / 'build/native-inherited-graphics'
    mods, settings = root / 'Mods', root / 'settings'
    mods.mkdir(parents=True, exist_ok=True)
    settings.mkdir(exist_ok=True)
    helper = build(Path(ROOT) / 'build/shader-input-test')
    module, source, bytecode = root / 'vertex.spv', root / 'vertex.hlsl', root / 'vertex.dxbc'
    subprocess.run([str(helper), '--sandbox-tagged-vertex' if case.startswith('regex') else '--sandbox-vertex', str(module)], check=True)
    source.write_text('cbuffer Config:register(b0){float4 offset;float4 color;};\n'
                      'float4 main(float2 position:POSITION):SV_Position{return float4(position,0,1)+offset;}\n', encoding='ascii')
    subprocess.run([str(helper), '--dxbc', str(source), str(bytecode), 'vs_5_0'], check=True)
    code = module.read_bytes()
    identity = hashlib.sha256(code).digest()
    pairs = [(identity if case != 'unknown_source' else bytes(32), len(code), bytecode.read_bytes())]
    if case.startswith('regex'):
        ordinary = root / 'ordinary.spv'
        ordinary_bytecode = root / 'ordinary.dxbc'
        subprocess.run([str(helper), '--sandbox-vertex', str(ordinary)], check=True)
        subprocess.run([str(helper), '--dxbc', str(source), str(ordinary_bytecode), 'vs_4_0'], check=True)
        ordinary_code = ordinary.read_bytes()
        pairs.append((hashlib.sha256(ordinary_code).digest(), len(ordinary_code), ordinary_bytecode.read_bytes()))
    if original_caller:
        solid, solid_source, solid_bytecode = root / 'solid.spv', root / 'solid.hlsl', root / 'solid.dxbc'
        subprocess.run([str(helper), '--sandbox-solid-fragment', str(solid)], check=True)
        solid_source.write_text('float4 main():SV_Target{return float4(0,1,1,1);}', encoding='ascii')
        subprocess.run([str(helper), '--dxbc', str(solid_source), str(solid_bytecode), 'ps_5_0'], check=True)
        solid_code = solid.read_bytes()
        pairs.append((hashlib.sha256(solid_code).digest(), len(solid_code), solid_bytecode.read_bytes()))
    if case == 'regex_caller_pixel':
        display, display_source, display_bytecode = root / 'display.spv', root / 'display.hlsl', root / 'display.dxbc'
        subprocess.run([str(helper), '--sandbox-fragment', str(display)], check=True)
        display_source.write_text('cbuffer Config:register(b0){float4 offset;float4 color;};\n'
                                  'StructuredBuffer<float4> materials:register(t0);\n'
                                  'Texture2D<float4> albedo:register(t2);SamplerState samp:register(s0);\n'
                                  'float4 main():SV_Target{return color*materials[0]*albedo.Sample(samp,float2(0,0));}', encoding='ascii')
        subprocess.run([str(helper), '--dxbc', str(display_source), str(display_bytecode), 'ps_5_0'], check=True)
        display_code = display.read_bytes()
        pairs.append((hashlib.sha256(display_code).digest(), len(display_code), display_bytecode.read_bytes()))
    (root / 'sources.bin').write_bytes(encode_sources(pairs))
    (root / 'inputs.bin').write_bytes(encode([(identity if case != 'unknown_layout' else bytes(32), len(code), [(0, 'POSITION', 0)])]))
    (mods / 'pixel.hlsl').write_text('float4 main():SV_Target{return float4(0,1,1,1);}', encoding='ascii')
    (mods / 'offscreen.hlsl').write_text('float4 main(uint id:SV_VertexID):SV_Position{return float4(10,10,0,1);}', encoding='ascii')
    _, fragment = sandbox_shader_hashes()
    (mods / 'graphics.ini').write_text('[Constants]\nglobal $small = 0\nglobal $width = 0\n[ResourceSaved]\nbind_flags = shader_resource\n[ShaderOverrideInherited]\nvk_hash = %08x\n$width = rt_width\nif rt_width == 4\n$small = $small + 1\nhandling = skip\nrun = CustomShaderInherited\nResourceSaved = copy o0\nendif\n' % crc32c(code) +
        '[ShaderOverrideSample]\nvk_hash = %08x\nvk_binding = ps-t0, 1, 5\nif rt_width != 4 && ResourceSaved->width == 4\nps-t0 = ref ResourceSaved\nendif\n' % fragment +
        '[ResourceOriginalVB]\n[ResourceTriangle]\ntype = Buffer\nstride = 8\nformat = R32G32_FLOAT\nbind_flags = vertex_buffer\ndata = -1.0 -1.0 -1.0 3.0 3.0 -1.0\n'
        '[CustomShaderInherited]\nps = pixel.hlsl\ntopology = triangle_list\ncull = none\ndepth_enable = false\nsample_mask = 0xffffffff\n'
        'o0 = ref set_viewport o0\noD = null\n' + ''.join('o%d = null\n' % i for i in range(1, 8)) +
        ('run = CustomShaderNested\n' if case in ('nested', 'state_nested', 'regex_nested') else '') +
        'ResourceOriginalVB = ref vb0\nvb0 = ref ResourceTriangle\ndraw = from_caller\nvb0 = ref ResourceOriginalVB\n[CustomShaderNested]\nvs = offscreen.hlsl\ndraw = 3,0\n', encoding='ascii')
    if case == 'direct_root_this':
        ini = mods / 'graphics.ini'
        ini.write_text(ini.read_text().replace('if rt_width == 4', 'if rt_width == 4 && ib->size > 0 && this->size == -2'), encoding='ascii')
    if case == 'reference':
        ini = mods / 'graphics.ini'
        ini.write_text(ini.read_text().replace('ResourceSaved = copy o0', 'ResourceSaved = ref o0'), encoding='ascii')
    if case in ('state', 'state_nested'):
        ini = mods / 'graphics.ini'
        text = ini.read_text().replace('topology = triangle_list\ncull = none\ndepth_enable = false\nsample_mask = 0xffffffff\n', '')
        text = text.replace('o0 = ref set_viewport o0', 'o0 = ref o0')
        text = text.replace('vs = offscreen.hlsl', 'vs = offscreen.hlsl\ncull = front\nsample_mask = 0\no0 = ref set_viewport o0')
        ini.write_text(text, encoding='ascii')
    if case.startswith('regex'):
        ini = mods / 'graphics.ini'
        text = ini.read_text().replace('data = -1.0 -1.0 -1.0 3.0 3.0 -1.0', 'data = 9.0 9.0 9.0 13.0 13.0 9.0')
        if original_caller:
            text = text.replace('handling = skip\nrun = CustomShaderInherited\nResourceSaved = copy o0',
                                ('handling = skip\n' if case == 'regex_caller_skip' else '') +
                                'ResourceOriginalVB = ref vb0\nvb0 = ref ResourceTriangle\n'
                                'post ResourceSaved = copy o0\npost vb0 = ref ResourceOriginalVB')
        if case in ('regex_caller_self_ref', 'regex_caller_indirect_self_ref'):
            text = text.replace('vb0 = ref ResourceTriangle\npost ResourceSaved', 'vb0 = ref ResourceTriangle\no0 = ref set_viewport o0\npost ResourceSaved')
        if case == 'regex_caller_unknown':
            text = text.replace('handling = skip\nrun = CustomShaderInherited', '')
        pattern = r'add o0\.xyzw, r0\.xyzw, cb0\[0\]\.xyzw' if case != 'regex_nomatch' else 'instruction_that_does_not_exist'
        text += ('[ShaderRegexVertex]\nshader_model = vs_5_0\n'
                 '[ShaderRegexVertex.Pattern]\n' + pattern + '\n'
                 '[ShaderRegexVertex.Pattern.Replace]\nadd o0.xyzw, r0.xyzw, l(-10.000000, -10.000000, 0, 0)\n')
        if case == 'regex_caller_pixel':
            text += ('[ShaderRegexPixel]\nshader_model = ps_5_0\n[ShaderRegexPixel.Pattern]\n'
                     r'l\(0,\s*1\.000000,\s*1\.000000,\s*1\.000000\)' + '\n'
                     '[ShaderRegexPixel.Pattern.Replace]\nl(1.000000, 0, 0, 1.000000)\n')
        ini.write_text(text, encoding='ascii')
    sample = root / 'sample.hlsl'
    sample.write_text('[[vk::binding(5,1)]] [[vk::combinedImageSampler]] Texture2D<float4> tex;\n'
                      '[[vk::binding(5,1)]] [[vk::combinedImageSampler]] SamplerState samp;\n'
                      'float4 main():SV_Target{return tex.SampleLevel(samp,float2(.5,.5),0);}', encoding='ascii')
    fixes = settings / 'ShaderFixes'
    fixes.mkdir(exist_ok=True)
    compile_shader(sample, fixes, '%08x' % fragment, 'ps')
    pixels, trigger = root / (case + '.raw'), root / (case + '.trigger')
    trigger.unlink(missing_ok=True)
    pixels.unlink(missing_ok=True)
    Path(str(pixels) + '.json').unlink(missing_ok=True)
    env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(settings), EFMI_VK_M2_ENABLED='1',
               EFMI_VK_SHADER_SOURCES=str(root / 'sources.bin'), EFMI_VK_SHADER_INPUTS=str(root / 'inputs.bin'),
               EFMI_VK_TEST_NOTICE='> inherited graphics',
               EFMI_VK_TEST_NOTICES_READBACK=str(pixels), EFMI_VK_TEST_NOTICES_READBACK_TRIGGER=str(trigger))
    args = ['--readable-swapchain', '--native-target']
    if case.startswith('regex'):
        args.append('--native-target-unique-vertex')
    if original_caller:
        args.append('--native-target-solid')
    if case in ('regex_caller_indirect', 'regex_caller_indirect_self_ref'):
        args.append('--indexed-indirect')
    if case.startswith('indirect'):
        args.append('--non-indexed-indirect' if case == 'indirect_nonindexed' else
                    '--indexed-indirect-count' if case == 'indirect_count' else '--indexed-indirect')
    with Sandbox(seconds=6, threads=0, validation=True, env_extra=env, extra_args=args) as sb:
        time.sleep(.75)
        with ProbeClient(sb.pid) as probe:
            status = probe.status().get('replacement', {})
        trigger.touch()
        sb.proc.wait(timeout=20)
        checks.equal(sb.proc.returncode, 0, 'Inherited graphics sandbox exits normally')
        (root / (case + '-stdout.txt')).write_text(sb.output(), encoding='utf-8')
        (root / (case + '-layer.log')).write_text(sb.log(), encoding='utf-8')
    accepted = case in ('enabled', 'direct_root_this', 'nested', 'reference', 'state', 'state_nested', 'indirect', 'indirect_nonindexed', 'regex', 'regex_nested', 'regex_nomatch', 'regex_caller', 'regex_caller_indirect', 'regex_caller_skip', 'regex_caller_pixel', 'regex_caller_self_ref', 'regex_caller_indirect_self_ref')
    checks.check(pixels.exists(), 'This process produced a fresh completed image')
    checks.equal(colored_pixels(pixels, (0, 0, 255) if case == 'regex_caller_pixel' else (255, 255, 0)) > 200, accepted and case not in ('regex_nomatch', 'regex_caller_skip'), 'Inherited VS/IA produce actual replacement pixels; unknown proof preserves the caller')
    reason = status.get('shader_overrides', {}).get('last_reason', '')
    expected = 'unknown application graphics' if case in ('unknown_source', 'regex_caller_unknown') else 'argument' if case == 'indirect_count' else 'input layout'
    checks.check(not reason if accepted else expected in reason, 'Admission reports the precise source limitation')
    checks.check('VUID-' not in sb.output() and 'Validation Error' not in sb.output(), 'Vulkan and synchronization validation clean')
    (root / (case + '-status.json')).write_text(json.dumps(status, indent=2), encoding='ascii')
    return checks.finish()


if __name__ == '__main__':
    raise SystemExit(main())
