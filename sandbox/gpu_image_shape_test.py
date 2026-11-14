"""Completed per-cell captures preserve image view ranges and GPU clear values."""
from pathlib import Path
import json
import sys
import time
from harness import Checks, Sandbox, test_dir
from runtime_fixture import CORE
from native_shader_test import compile_shader


def scene(checks,case):
    root=Path(test_dir('gpu-image-shape-'+case+'-'))
    cube=case.startswith('cube');layers=12 if case.startswith('cube_array') else 6 if cube else 3
    partial=case in ('partial','remaining');count=2 if partial else layers;mips=2 if partial else 3
    kind='TextureCubeArray' if layers==12 else 'TextureCube' if cube else 'Texture2DArray'
    common=['[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;',f'[[vk::binding(3,0)]] {kind}<float4> destination;']
    if cube:common += ['[[vk::binding(4,0)]] SamplerState samp;']
    def sample(name):
        return f'{name}.SampleLevel(samp,float4(1,0,0,0),0)' if layers==12 else f'{name}.SampleLevel(samp,float3(1,0,0),0)' if cube else f'{name}.Load(int4(0,0,0,0))'
    first=root/'first.hlsl';follow=root/'follow.hlsl'
    first.write_text('\n'.join(common+[f'[[vk::binding(2,0)]] {kind}<float4> source;',
        '[numthreads(1,1,1)] void main() { output[0] += uint(round(255*('+sample('source')+'.r+'+sample('destination')+'.r))); }']),encoding='ascii')
    follow.write_text('\n'.join(common+['[numthreads(1,1,1)] void main() { output[0] += uint(round(255*'+sample('destination')+'.r)); }']),encoding='ascii')
    shader=compile_shader(first,root/'compiled','12345678','cs');later=compile_shader(follow,root/'compiled','87654321','cs')
    mods=root/'Mods';mods.mkdir();settings=root/'settings';settings.mkdir()
    (root/'d3dx.ini').write_text('[Include]\ninclude = Core\\EFMI\\main.ini\ninclude_recursive = Mods\n[Rendering]\nrecursive_include = 1\n[Hunting]\nhunting = 1\nanalyse_frame = no_modifiers VK_F9\nanalyse_options = dump_rt dump_tex dump_cb dump_vb dump_ib buf\n',encoding='ascii')
    keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii')
    env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(settings),
             EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_KEYS=str(keys),VK_LAYER_VALIDATE_SYNC='1')
    args=['--compute-shader',str(shader),'--compute-image-reference',str(later),
          '--texture-shape','3' if layers==12 else '2' if cube else '1']
    if case!='usage':args+=['--compute-capture']
    if case.endswith('_push'):args += ['--descriptors','push']
    if case.endswith('_secondary'):args += ['--compute-secondary']
    if case=='two_queues':args+=['--two-queues']
    if case=='budget':args+=['--reference-layers','128']
    if case=='remaining':args+=['--reference-remaining']
    if partial:args += ['--reference-partial']
    with Sandbox(seconds=4,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(.6);keys.write_text('1 120\n',encoding='ascii');time.sleep(.2);keys.write_text('1\n',encoding='ascii')
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+': normal exit')
        (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
    records=[(path.parent,json.loads(line)) for path in settings.glob('FrameAnalysis-*/resources.jsonl') for line in path.read_text().splitlines()]
    images=[(folder,item) for folder,item in records if item.get('state')=='complete' and item.get('file','').startswith('image-')]
    if case in ('usage','two_queues','budget'):
        checks.check(not images,case+': unsupported capture exports no image bytes')
        checks.check(any(item.get('state')=='refused' and (case!='budget' or 'subresource budget' in item.get('reason','')) for _,item in records),case+': capture refusal is explicit')
    else:
        checks.equal(len(images),count*mips*5,case+': each source cell once and destination cell after all four dispatches')
        expected={(binding,layer,mip):(1 if binding==2 else 4) for binding in (2,3)
                  for layer in range(int(partial),int(partial)+count) for mip in range(int(partial),int(partial)+mips)}
        actual={};correct=True
        for folder,item in images:
            key=(item['binding'],item['layer'],item['mip']);actual[key]=actual.get(key,0)+1
            extent=max(1,4>>item['mip']);value=(1 if item['binding']==2 else 37)+item['layer']*3+item['mip']
            data=(folder/item['file']).read_bytes()
            correct=correct and item['width']==extent and item['height']==extent and item['format']==37 and item['bytes']==extent*extent*4
            correct=correct and data==bytes((value,0,0,255))*(extent*extent)
        checks.equal(actual,expected,case+': only the original view mip/layer cells are exported')
        checks.check(bool(images) and correct,case+': every exported GPU pixel and extent matches its subresource')
    checks.check('Validation Error' not in sb.output() and 'VUID-' not in sb.output(),case+': core and synchronization validation clean')
    print('artifacts:',root)


def main():
    checks=Checks('GPU image shapes');selected=set(sys.argv[1:])
    for case in ('array','cube','cube_array','partial','array_push','cube_secondary','usage','two_queues','remaining','budget'):
        if not selected or case in selected:scene(checks,case)
    return checks.finish()


if __name__=='__main__':raise SystemExit(main())
