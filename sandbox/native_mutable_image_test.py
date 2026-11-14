"""Private writable file images preserve data, independent declarations and publication."""
from pathlib import Path
import json
import struct
import sys
import time
import zlib
from harness import Checks,Sandbox,test_dir
from runtime_fixture import CORE
from native_shader_test import compile_shader
from native_compute_test import read_compute_result
from native_image_shape_test import dds
from native_storage_image_test import unformatted
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
    root=Path(test_dir('mutable-image-'+case+'-'));mods=root/'Mods';mods.mkdir()
    layers=12 if case in ('cube_array','cube_array_disabled') else 6 if case=='cube' else 3 if case.startswith('array') else 1
    sampled=case.startswith('sampled');formatless=case.startswith('formatless') or case=='wic'
    kind='RWTexture2DArray' if layers>1 else 'RWTexture2D';at='int3(0,0,layer)' if layers>1 else 'int2(0,0)'
    common='[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;\n[[vk::binding(3,0)]] [[vk::image_format("rgba8")]] '+kind+'<float4> destination;\n'
    first=root/'first.hlsl';follow=root/'follow.hlsl'
    first.write_text(common+'[[vk::binding(2,0)]] '+(kind.replace('RW','')+'<float4>' if sampled else '[[vk::image_format("rgba8")]] '+kind+'<float4>')+' source;\n[numthreads(1,1,1)] void main(){for(uint layer=0;layer<'+str(layers)+';++layer){destination['+at+']=float4(99/255.0,0,0,1);output[0]+=uint(round(255*source['+at+'].x))+100*uint(round(255*destination['+at+'].x));}}',encoding='ascii')
    follow.write_text(common+'[numthreads(1,1,1)] void main(){for(uint layer=0;layer<'+str(layers)+';++layer){output[0]+=uint(round(255*destination['+at+'].x));}}',encoding='ascii')
    if sampled:first.write_text(first.read_text().replace('source['+at+']','source.Load(int3(0,0,0))'),encoding='ascii')
    shader=compile_shader(first,root/'compiled','12345678','cs');later=compile_shader(follow,root/'compiled','87654321','cs')
    if formatless:unformatted(shader);unformatted(later)
    for compiled in (shader,later):
        words=struct.unpack('<%dI'%(compiled.stat().st_size//4),compiled.read_bytes());at_word=5
        while at_word<len(words):
            count,op=words[at_word]>>16,words[at_word]&65535
            if op==25 and words[at_word+7]==2:assert words[at_word+8]==(0 if formatless else 4),'fixture storage format must match RGBA8'
            at_word+=count
    dds(mods/'input.dds',layers,case in ('cube','cube_array','cube_array_disabled'),17)
    if case=='srgb':
        data=bytearray((mods/'input.dds').read_bytes());struct.pack_into('<I',data,128,29);(mods/'input.dds').write_bytes(data)
    if case=='wic':
        def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
        (mods/'input.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',4,4,8,6,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes((17,0,0,255))*4)*4))+chunk(b'IEND',b''))
    flags='' if case=='implicit' else 'bind_flags = shader_resource unordered_access\n'
    if case=='readonly':flags='bind_flags = shader_resource\n'
    if case=='attachment':flags='bind_flags = shader_resource render_target\n'
    slot='cs-t0' if sampled else 'cs-u0'
    header='namespace = mutable_image\n[ShaderOverrideFirst]\nhash = %08x\nvk_binding = cs-u0, 0, 2\nvk_binding = cs-u1, 0, 3\n'%crc32c(shader.read_bytes())
    if sampled:header=header.replace('cs-u0','cs-t0')
    def content(reverse=False):
        a,b=('B','A') if reverse else ('A','B')
        if case=='alias':b=a
        return header+slot+' = Resource'+a+'\ncs-u1 = Resource'+b+'\n'+''.join('[Resource'+name+']\nfilename = '+('input.png' if case=='wic' else 'input.dds')+'\n'+flags for name in ('A','B'))
    ini=mods/'test.ini';ini.write_text(content(),encoding='ascii')
    keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii');output=root/'result.bin'
    env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(root/'settings'),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_KEYS=str(keys),VK_LAYER_VALIDATE_SYNC='1')
    args=['--compute-shader',str(shader),'--compute-output',str(output),'--compute-image-reference',str(later),'--reference-storage','--reference-rgba']
    if layers>1:args+=['--texture-shape','1','--reference-layers',str(layers)]
    if case=='cube_array':args+=['--reference-cube-arrays']
    if sampled:args+=['--reference-sampled-source']
    if formatless:args+=['--storage-formatless']
    if case.endswith('push'):args+=['--descriptors','push']
    if case=='secondary':args+=['--compute-secondary']
    if case=='two_queues':args+=['--two-queues']
    if case=='sync2':args+=['--sync2']
    refused=case in ('readonly','attachment','two_queues','srgb','cube_array_disabled')
    expected=(10198 if refused else 10234 if case=='alias' else 10152)*layers+(3 if refused else 10)*layers*(layers-1)//2
    def value():
        data=read_compute_result(output)
        return struct.unpack_from('<I',data)[0] if data else None
    with Sandbox(seconds=6 if case=='reload' else 3,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:
            if case=='reload':
                checks.equal(value(),expected,'reload: independent original declarations retain GPU mutations')
                ini.write_text(content(True),encoding='ascii');keys.write_text('1 121\n',encoding='ascii');time.sleep(.2);keys.write_text('1\n',encoding='ascii');time.sleep(1)
            status=probe.status()
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+': normal exit')
        (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
    checks.equal(value(),expected,case+': completed GPU bytes retain writes and declaration identity')
    calls=status.get('replacement',{}).get('shader_overrides',{})
    checks.check(bool(calls.get('last_reason')) if refused else calls.get('executed',0)>0 and not calls.get('last_reason'),case+': explicit refusal' if refused else case+': native image plan executes')
    checks.check('Validation Error' not in sb.output() and 'VUID-' not in sb.output(),case+': core and synchronization validation clean')
    (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root)

def main():
    checks=Checks('mutable images');selected=set(sys.argv[1:])
    for case in ('independent','implicit','alias','push','secondary','array','array_push','cube','cube_array','cube_array_disabled','formatless','formatless_push','wic','sampled','sampled_push','srgb','reload','readonly','attachment','two_queues','sync2'):
        if not selected or case in selected:scene(checks,case)
    return checks.finish()
if __name__=='__main__':raise SystemExit(main())
