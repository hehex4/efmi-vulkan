"""Original analyse_options select native resource classes and survive F10."""
from pathlib import Path
import json
import struct
import sys
import time
from harness import Checks, Sandbox, test_dir
from runtime_fixture import CORE
from native_shader_test import compile_shader

OPTIONS={'missing':None,'log':'log','rt':'dump_rt buf','tex':'dump_tex buf','cb':'dump_cb buf',
         'all':'dump_rt dump_tex dump_cb buf','unsupported':'dump_rt buf hold dds','reload':'dump_rt buf',
         'image_rt':'dump_rt buf','image_tex':'dump_tex buf','sampled_rt':'dump_rt buf','sampled_tex':'dump_tex buf',
         'vertex':'dump_vb buf','index':'dump_ib buf','target':'dump_rt buf'}


def scene(checks,case):
    root=Path(test_dir('analysis-selection-'+case+'-'));mods=root/'Mods';mods.mkdir();settings=root/'settings';settings.mkdir()
    config=root/'d3dx.ini'
    def configure(options):
        config.write_text('[Include]\ninclude = Core\\EFMI\\main.ini\ninclude_recursive = Mods\n[Rendering]\nrecursive_include = 1\n'
            '[Hunting]\nhunting = 1\nanalyse_frame = no_modifiers VK_F9\n'+('analyse_options = '+options+'\n' if options is not None else ''),encoding='ascii')
    configure(OPTIONS[case]);keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii')
    env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(settings),
             EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_KEYS=str(keys),VK_LAYER_VALIDATE_SYNC='1')
    graphics=case in ('vertex','index','target');image=case.startswith('image_');sampled=case.startswith('sampled_')
    if graphics:args=['--geometry-capture','--capture-target']
    else:
        source=root/'scene.hlsl';common='[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;\n'
        if image:
            body='[[vk::binding(2,0)]] RWTexture2D<uint> target;\n[numthreads(1,1,1)] void main(){output[0]+=19;target[int2(0,0)]+=19;}'
        elif sampled:
            body='[[vk::binding(2,0)]] Texture2D<float4> source;\n[[vk::binding(3,0)]] Texture2D<float4> destination;\n'
            body+='[numthreads(1,1,1)] void main(){output[0]+=uint(round(255*(source.Load(int3(0,0,0)).r+destination.Load(int3(0,0,0)).r)));}'
        else:
            body='[[vk::binding(1,0)]] StructuredBuffer<uint> source;\n[[vk::binding(2,0)]] cbuffer Values {uint4 values;}\n'
            body+='[numthreads(1,1,1)] void main(){output[0]+=19+source[0]+values.x;}'
        source.write_text(common+body,encoding='ascii');shader=compile_shader(source,root/'compiled','12345678','cs')
        args=['--compute-shader',str(shader),'--compute-repeat','2','--compute-capture']
        if image:args += ['--compute-storage-image']
        elif sampled:
            follow=root/'follow.hlsl';follow.write_text(common+'[[vk::binding(3,0)]] Texture2D<float4> destination;\n'
                '[numthreads(1,1,1)] void main(){output[0]+=uint(round(255*destination.Load(int3(0,0,0)).r));}',encoding='ascii')
            later=compile_shader(follow,root/'compiled','87654321','cs');args+=['--compute-image-reference',str(later)]
        else:args+=['--compute-constant-buffer']
    def key(value):
        keys.write_text('1 '+str(value)+'\n',encoding='ascii');time.sleep(.2);keys.write_text('1\n',encoding='ascii')
    with Sandbox(seconds=7 if case=='reload' else 4,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(.6);key(120)
        if case=='reload':
            time.sleep(.8);configure('dump_cb buf');key(121);time.sleep(1.1);key(120)
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+': original application exits normally')
        (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
    folders=sorted(settings.glob('FrameAnalysis-*'));checks.equal(len(folders),2 if case=='reload' else 1,case+': requested analyses completed')
    for index,folder in enumerate(folders):
        checks.check((folder/'log.txt').is_file(),case+': draw log remains available')
        records=[json.loads(line) for line in (folder/'resources.jsonl').read_text().splitlines()] if (folder/'resources.jsonl').exists() else []
        completed=[item for item in records if item.get('state')=='complete']
        if graphics:
            expected={'vertex'} if case=='vertex' else {'index'} if case=='index' else {'image'}
            checks.equal({item['file'].split('-')[0] for item in completed},expected,case+': only selected geometry or targets exported')
            if case=='target':checks.equal(len(completed),2,case+': both owned stored targets exported')
            else:checks.check(bool(completed) and all((folder/item['file']).stat().st_size==item['bytes'] for item in completed),case+': GPU allocation bytes match metadata')
        elif sampled or image:
            images=[item for item in completed if item['file'].startswith('image-')]
            buffers=[item for item in completed if item['file'].startswith('buffer-')]
            checks.equal((len(images),len(buffers)),(5,0) if case=='sampled_tex' else (0,4) if sampled else (2,2) if case=='image_rt' else (0,0),case+': sampled and storage image classes are selected independently')
            checks.check(all((folder/item['file']).stat().st_size==item['bytes'] for item in completed),case+': exported cell sizes are exact')
        else:
            expected=[] if case in ('missing','log') else [1,1] if case=='tex' else [2,2] if case=='cb' or (case=='reload' and index==1) else [0,0,1,1,2,2] if case=='all' else [0,0]
            checks.equal(sorted(item['binding'] for item in completed),expected,case+': only requested buffer classes captured')
            values={binding:sorted(struct.unpack_from('<I',(folder/item['file']).read_bytes())[0] for item in completed if item['binding']==binding) for binding in set(expected)}
            checks.equal(values,{binding:[26,52] if binding==0 else [0,0] if binding==1 else [7,7] for binding in set(expected)},case+': selected files hold completed GPU values')
        if case=='unsupported':checks.check(any(item.get('state')=='refused' and item.get('point')=='options' for item in records),case+': unsupported controls and encodings are reported')
    checks.check('Validation Error' not in sb.output() and 'VUID-' not in sb.output(),case+': Vulkan validation clean')
    print('artifacts:',root)


def main():
    checks=Checks('analysis resource selection');selected=set(sys.argv[1:])
    for case in OPTIONS:
        if not selected or case in selected:scene(checks,case)
    return checks.finish()


if __name__=='__main__':raise SystemExit(main())
