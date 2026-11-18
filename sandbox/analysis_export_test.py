"""Completed GPU data exports preserve native bytes and declared encodings."""
from pathlib import Path
import json
import struct
import sys
import time
from PIL import Image
from harness import Checks, Sandbox, test_dir
from native_shader_test import compile_shader

OPTIONS={'dds':'dump_rt dds','jpg':'dump_rt jpg','both':'dump_rt jpg dds desc',
         'auto':'dump_rt','uint_dds':'dump_rt dds','uint_auto':'dump_rt',
         'uint_jpg':'dump_rt jpg','text':'dump_rt buf txt desc','constant':'dump_cb',
         'layers':'dump_rt dds desc'}

def scene(checks,case):
    root=Path(test_dir('analysis-export-'+case+'-'));mods=root/'Mods';mods.mkdir();settings=root/'settings';settings.mkdir()
    (root/'d3dx.ini').write_text('[Include]\ninclude_recursive = Mods\n[Rendering]\nrecursive_include = 1\n'
        '[Hunting]\nhunting = 1\nanalyse_frame = no_modifiers VK_F9\nanalyse_options = '+OPTIONS[case]+'\n',encoding='ascii')
    keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii')
    env=dict(EFMI_VK_CONFIGURATION=str(root),EFMI_VK_M2_SETTINGS=str(settings),
             EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_KEYS=str(keys),VK_LAYER_VALIDATE_SYNC='1')
    integer=case.startswith('uint');buffer=case in ('text','constant')
    if integer or buffer:
        body='[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;\n'
        if integer:body+='[[vk::binding(2,0)]] RWTexture2D<uint> target;\n[numthreads(1,1,1)] void main(){output[0]+=19;target[int2(0,0)]+=19;}'
        elif case=='constant':body+='[[vk::binding(2,0)]] cbuffer Values {uint4 values;}\n[numthreads(1,1,1)] void main(){output[0]+=values.x;}'
        else:body+='[numthreads(1,1,1)] void main(){output[0]+=19;}'
        source=root/'scene.hlsl';source.write_text(body,encoding='ascii');shader=compile_shader(source,root/'compiled','12345678','cs')
        args=['--compute-shader',str(shader),'--compute-repeat','2','--compute-capture']
        if integer:args+=['--compute-storage-image']
        elif case=='constant':args+=['--compute-constant-buffer']
    else:args=['--capture-target']+(['--capture-target-layers'] if case=='layers' else [])
    with Sandbox(seconds=4,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(.6);keys.write_text('1 120\n',encoding='ascii');time.sleep(.2);keys.write_text('1\n',encoding='ascii')
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+': original application exits normally')
        (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
    records=[(p.parent,json.loads(line)) for p in settings.glob('FrameAnalysis-*/resources.jsonl') for line in p.read_text().splitlines()]
    completed=[(p,r) for p,r in records if r.get('state')=='complete' and (r['file'].startswith('buffer-') if buffer else r['file'].startswith('image-'))]
    checks.equal(len(completed),4 if case=='layers' else 2,case+': selected completed GPU captures')
    for folder,item in completed:
        raw=(folder/item['file']).read_bytes()
        exports={e['format']:e for e in item.get('exports',[])}
        wanted={'buf','txt','desc'} if case=='text' else {'txt'} if case=='constant' else {'jpg','dds','desc'} if case=='both' else {'dds','desc'} if case=='layers' else {'jpg'} if case in ('jpg','auto','uint_jpg') else {'dds'}
        checks.equal(set(exports),wanted,case+': requested output formats')
        for kind,entry in exports.items():
            if case=='uint_jpg':checks.equal(entry.get('state'),'refused',case+': integer image is not silently reinterpreted as JPEG');continue
            checks.equal(entry.get('state'),'complete',case+': '+kind+' file completed')
            if entry.get('state')!='complete':continue
            path=folder/entry['file'];data=path.read_bytes()
            if kind=='dds':
                header=struct.unpack('<37I',data[:148]);checks.equal((header[0],header[1],header[19],header[20],header[21]),(0x20534444,124,32,4,0x30315844),case+': standard DDS/DX10 header')
                checks.equal((header[3],header[4],header[32],header[33],header[35]),(item['height'],item['width'],42 if integer else 28,3,1),case+': DDS type and cell dimensions')
                checks.equal(data[148:],raw,case+': DDS payload exactly equals completed GPU bytes')
            elif kind=='jpg':
                im=Image.open(path).convert('RGB');checks.equal(im.size,(item['width'],item['height']),case+': JPEG dimensions')
                checks.check(all(abs(a-b)<=3 for a,b in zip(im.getpixel((0,0)),raw[:3])),case+': JPEG contains actual target colors')
            elif kind=='buf':checks.equal(data,raw,case+': binary format is lossless')
            elif kind=='txt':
                lines=data.decode('ascii').splitlines();first=next((line for line in lines if line.startswith('0: ')),'')
                checks.equal(first.split()[2] if len(first.split())>2 else '',str(struct.unpack_from('<I',raw)[0]),case+': text reads actual GPU words')
            elif kind=='desc':checks.equal(json.loads(data)['resource'],item['resource'],case+': description names retained source resource')
    checks.check('Validation Error' not in sb.output() and 'VUID-' not in sb.output(),case+': Vulkan validation clean')
    print('artifacts:',root)

def main():
    checks=Checks('analysis export formats');selected=set(sys.argv[1:])
    for case in OPTIONS:
        if not selected or case in selected:scene(checks,case)
    return checks.finish()
if __name__=='__main__':raise SystemExit(main())
