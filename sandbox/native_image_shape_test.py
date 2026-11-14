"""Native file layers and cube faces, measured through completed compute output."""
from pathlib import Path
import json
import struct
import sys
import time
from harness import Checks, Sandbox, test_dir
from runtime_fixture import CORE
from native_shader_test import compile_shader
from native_compute_test import read_compute_result
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def dds(path, layers, cube, seed=10):
    header=[124,0x2100F,4,4,16,0,3]+[0]*11
    header += [32,4,int.from_bytes(b'DX10','little'),0,0,0,0,0]
    header += [0x401008,0,0,0,0]
    data=b'DDS '+struct.pack('<31I',*header)+struct.pack('<5I',28,3,4 if cube else 0,layers//6 if cube else layers,0)
    total=0
    for layer in range(layers):
        for mip,size in enumerate((4,2,1)):
            value=seed+layer*7+mip*3
            data += bytes((value,0,255-value,255))*(size*size)
            total += value*(1+layer*3+mip)
    path.write_bytes(data)
    return total*2


def scene(checks,case):
    root=Path(test_dir('native-image-shape-'+case+'-'))
    cube=case.startswith('cube');layers=12 if case.startswith('cube_array') else 6 if cube else 3
    kind='TextureCubeArray' if layers==12 else 'TextureCube' if cube else 'Texture2DArray'
    lines=['[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;',f'[[vk::binding(2,0)]] {kind}<float4> input;']
    if cube:lines += ['[[vk::binding(3,0)]] SamplerState samp;']
    lines += ['[numthreads(1,1,1)] void main() { uint value=0;']
    directions=('1,0,0','-1,0,0','0,1,0','0,-1,0','0,0,1','0,0,-1')
    for layer in range(layers):
        for mip in range(3):
            sample=(f'input.SampleLevel(samp,float4({directions[layer%6]},{layer//6}),{mip})' if layers==12 else
                    f'input.SampleLevel(samp,float3({directions[layer]}),{mip})') if cube else f'input.Load(int4(0,0,{layer},{mip}))'
            lines += [f'value += uint(round({sample}.r*255))*{1+layer*3+mip};']
    lines += ['output[0] += value; }']
    source=root/'scene.hlsl';source.write_text('\n'.join(lines),encoding='ascii')
    shader=compile_shader(source,root/'compiled','12345678','cs')
    mods=root/'Mods';mods.mkdir();file=mods/'input.dds';expected=dds(file,layers,cube)
    (mods/'test.ini').write_text('namespace = native_image_shapes\n[Constants]\nglobal $loaded = 1\n'
        '[ShaderOverrideCompute]\nhash = %08x\nvk_binding = cs-t0, 0, 2\ncs-t0 = ResourceTexture\n'
        '[ResourceTexture]\nfilename = input.dds\n'%crc32c(shader.read_bytes()),encoding='ascii')
    output=root/'result.bin';keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii')
    env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_KEYS=str(keys),VK_LAYER_VALIDATE_SYNC='1')
    args=['--compute-shader',str(shader),'--compute-output',str(output),'--compute-image','--compute-repeat','2',
          '--texture-shape','3' if layers==12 else '2' if cube else '1']
    if case.endswith('_push'):args += ['--descriptors','push']
    if case.endswith('_secondary'):args += ['--compute-secondary']
    with Sandbox(seconds=6 if case=='array_reload' else 3,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:
            if case=='array_reload':
                checks.check(probe.status().get('replacement',{}).get('shader_overrides',{}).get('executed',0)>0,'reload: initial layered file bound')
                expected=dds(file,layers,cube,31);keys.write_text('1 121\n',encoding='ascii');time.sleep(.2);keys.write_text('1\n',encoding='ascii');time.sleep(1)
            status=probe.status().get('replacement',{})
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+': normal exit')
        (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes())
        (root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
    checks.equal(struct.unpack_from('<I',read_compute_result(output))[0],expected,case+': every file layer and mip reaches the shader')
    checks.check(status.get('shader_overrides',{}).get('executed',0)>0,case+': native file plan executed')
    checks.check('Validation Error' not in sb.output() and 'VUID-' not in sb.output(),case+': core and synchronization validation clean')
    (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root)


def main():
    checks=Checks('native image shapes');selected=set(sys.argv[1:])
    for case in ('array','cube','cube_array','array_push','array_secondary','cube_push','cube_array_secondary','array_reload'):
        if not selected or case in selected:scene(checks,case)
    return checks.finish()


if __name__=='__main__':raise SystemExit(main())
