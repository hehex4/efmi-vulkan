"""Retained array/cube views across GPU writes, persistent refresh and application rebinding."""
from pathlib import Path
import json
import struct
import sys
import time
from harness import Checks, Sandbox, test_dir, configuration_root
from native_shader_test import compile_shader
from native_compute_test import read_compute_result
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def scene(checks,case):
    root=Path(test_dir('application-image-shape-'+case+'-'))
    cube=case.startswith('cube');layers=12 if case.startswith('cube_array') else 6 if cube else 3
    partial=case=='partial';mismatch=case=='mismatch';count=2 if partial else layers;mips=2 if partial else 3
    kind='TextureCubeArray' if layers==12 else 'TextureCube' if cube else 'Texture2DArray'
    common=['[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;',f'[[vk::binding(3,0)]] {"Texture2D" if mismatch else kind}<float4> destination;']
    if cube:common += ['[[vk::binding(4,0)]] SamplerState samp;']
    directions=('1,0,0','-1,0,0','0,1,0','0,-1,0','0,0,1','0,0,-1')
    def lines(texture):
        result=[]
        for layer in range(1 if mismatch and texture=="destination" else count):
            for mip in range(mips):
                sample=(f'{texture}.SampleLevel(samp,float4({directions[layer%6]},{layer//6}),{mip})' if layers==12 else
                        f'{texture}.SampleLevel(samp,float3({directions[layer]}),{mip})') if cube else f'{texture}.Load(int4(0,0,{layer},{mip}))'
                if mismatch and texture=='destination':sample=f'{texture}.Load(int3(0,0,{mip}))'
                result += [f'value += uint(round({sample}.r*255))*{1+layer*3+mip};']
        return result
    first=root/'first.hlsl';follow=root/'follow.hlsl'
    first.write_text('\n'.join(common+[f'[[vk::binding(2,0)]] {kind}<float4> source;','[numthreads(1,1,1)] void main() { uint value=0;']+
                              lines('source')+['uint original=value;value=0;']+lines('destination')+['output[0] += original+100*value; }']),encoding='ascii')
    follow.write_text('\n'.join(common+['[numthreads(1,1,1)] void main() { uint value=0;']+lines('destination')+['output[0] += 100*value; }']),encoding='ascii')
    shader=compile_shader(first,root/'compiled','12345678','cs');later=compile_shader(follow,root/'compiled','87654321','cs')
    mods=root/'Mods';mods.mkdir()
    (mods/'test.ini').write_text('namespace = application_shapes\n[Constants]\nglobal $loaded = 1\n[ResourceHeld]\n'
        '[ShaderOverrideCompute]\nhash = %08x\nvk_binding = cs-t0, 0, 2\nvk_binding = cs-t1, 0, 3\n'
        'ResourceHeld = ref cs-t0\ncs-t1 = ref ResourceHeld\n'%crc32c(shader.read_bytes()),encoding='ascii')
    def total(base,destination=False):
        return sum((base+(layer+partial)*3+mip+partial)*(1+layer*3+mip) for layer in range(1 if mismatch and destination else count) for mip in range(mips))
    expected=total(1)+400*total(37,True) if mismatch else 101*total(1)+200*total(9)+100*total(37)
    output=root/'result.bin'
    env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_ENABLED='1',VK_LAYER_VALIDATE_SYNC='1')
    args=['--compute-shader',str(shader),'--compute-output',str(output),'--compute-image-reference',str(later),
          '--texture-shape','3' if layers==12 else '2' if cube else '1']
    if case.endswith('_push'):args += ['--descriptors','push']
    if case.endswith('_secondary'):args += ['--compute-secondary']
    if partial:args += ['--reference-partial']
    if mismatch:args += ['--reference-narrow']
    with Sandbox(seconds=3,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:status=probe.status()
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+': normal exit')
        (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
    checks.equal(struct.unpack_from('<I',read_compute_result(output))[0],expected,case+': all view cells read latest GPU data and application rebind wins')
    images=status.get('device',{}).get('native_images',{})
    if mismatch:
        calls=status.get('replacement',{}).get('shader_overrides',{})
        checks.check(calls.get('executed',0)==0 and 'shape' in calls.get('last_reason',''),case+': mismatched native view refuses without executing')
    else:
        checks.check(images.get('transitions',0)>0,case+': temporary per-cell transitions used')
        checks.check(images.get('refreshes',0)>0,case+': persistent descriptors refreshed')
    checks.equal(images.get('refused',0),0,case+': supported image scopes have no refusals')
    checks.check('Validation Error' not in sb.output() and 'VUID-' not in sb.output(),case+': core and synchronization validation clean')
    (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root)


def main():
    checks=Checks('application image shapes');selected=set(sys.argv[1:])
    for case in ('array','cube','cube_array','partial','array_push','cube_push','array_secondary','cube_array_secondary','mismatch'):
        if not selected or case in selected:scene(checks,case)
    return checks.finish()


if __name__=='__main__':raise SystemExit(main())
