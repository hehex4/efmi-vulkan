"""Native application storage images retain writes, layouts and binding state."""
from pathlib import Path
import json
import struct
import subprocess
import sys
import time
from harness import Checks, Sandbox, test_dir
from runtime_fixture import CORE
from native_shader_test import compile_shader
from native_compute_test import read_compute_result, VALIDATOR
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient

def unformatted(path):
    words=list(struct.unpack('<%dI'%(path.stat().st_size//4),path.read_bytes()))
    at=5
    while at<len(words):
        count,op=words[at]>>16,words[at]&65535
        assert count and count<=len(words)-at
        if op==25 and words[at+7]==2:words[at+8]=0
        at+=count
    words[5:5]=[(2<<16)|17,55,(2<<16)|17,56]
    path.write_bytes(struct.pack('<%dI'%len(words),*words))
    subprocess.run([str(VALIDATOR),'--target-env','vulkan1.1',str(path)],check=True,capture_output=True,timeout=30)

def scene(checks,case):
    formatless=case.startswith('formatless');mode=case.removeprefix('formatless_') if formatless else case
    if mode in ('formatless','core'):mode='reference'
    numeric=case=='numeric'

    root=Path(test_dir('native-storage-image-'+case+'-'));array=mode.startswith('array');layers=3 if array else 1
    kind='RWTexture2DArray' if array else 'RWTexture2D';at='int3(0,0,layer)' if array else 'int2(0,0)'
    common='[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;\n[[vk::binding(3,0)]] '+kind+'<uint> destination;\n'
    first=root/'first.hlsl';follow=root/'follow.hlsl'
    first.write_text(common+'[[vk::binding(2,0)]] '+kind+('<float>' if numeric else '<uint>')+' source;\n[numthreads(1,1,1)] void main(){for(uint layer=0;layer<'+str(layers)+';++layer){destination['+at+']+=5;output[0]+=uint(source['+at+'])+100*destination['+at+'];}}',encoding='ascii')
    follow.write_text(common+'[numthreads(1,1,1)] void main(){for(uint layer=0;layer<'+str(layers)+';++layer){destination['+at+']+=5;output[0]+=100*destination['+at+'];}}',encoding='ascii')
    shader=compile_shader(first,root/'compiled','12345678','cs');later=compile_shader(follow,root/'compiled','87654321','cs')
    if formatless:unformatted(shader);unformatted(later)
    mods=root/'Mods';mods.mkdir();body='ResourceHeld = ref cs-u0\ncs-u1 = ref ResourceHeld\n'
    if mode=='post':body='ResourceOld = ref cs-u1\n'+body+'post cs-u1 = ref ResourceOld\n'
    if mode=='dispatch':body+='dispatch = 1, 1, 1\n'
    (mods/'test.ini').write_text('namespace = storage_image\n[Constants]\nglobal $loaded = 1\n[ResourceHeld]\n[ResourceOld]\n'
        '[ShaderOverrideFirst]\nhash = %08x\nvk_binding = cs-u0, 0, 2\nvk_binding = cs-u1, 0, 3\n'%crc32c(shader.read_bytes())+body,encoding='ascii')
    output=root/'result.bin';env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_ENABLED='1',VK_LAYER_VALIDATE_SYNC='1')
    args=['--compute-shader',str(shader),'--compute-output',str(output),'--compute-image-reference',str(later),'--reference-storage']
    if formatless:args+=['--storage-formatless']
    if case=='formatless_core':args+=['--api','1.3']
    if numeric:args+=['--reference-float-source']
    if array:args+=['--texture-shape','1']
    if mode.endswith('push'):args+=['--descriptors','push']
    if mode=='secondary':args+=['--compute-secondary']
    if case=='two_queues':args+=['--two-queues']
    if case=='sync2':args+=['--sync2']
    with Sandbox(seconds=3,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:status=probe.status()
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+': original application exits normally')
        (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
    blocked=case in ('two_queues','sync2','numeric');base=19801 if blocked else 14706 if mode=='post' else 9217 if mode=='dispatch' else 8106
    expected=base*layers+1203*layers*(layers-1)//2
    checks.equal(struct.unpack_from('<I',read_compute_result(output))[0],expected,case+': GPU writes and original rebind produce expected values')
    calls=status.get('replacement',{}).get('shader_overrides',{})
    if blocked:checks.check(('queue' if case=='two_queues' else 'numeric' if numeric else 'synchronization2') in calls.get('last_reason',''),case+': unsupported access graph explicitly refused')
    else:
        checks.check(calls.get('executed',0)>0 and not calls.get('last_reason'),case+': native storage-image plan executes')
        if mode!='post':checks.check(status.get('device',{}).get('native_images',{}).get('transitions',0)>0,case+': temporary storage layout transitions actually used')
    checks.check('Validation Error' not in sb.output() and 'VUID-' not in sb.output(),case+': core and synchronization validation clean')
    (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root)

def main():
    checks=Checks('native storage images');selected=set(sys.argv[1:])
    for case in ('reference','push','secondary','post','dispatch','array','array_push','two_queues','sync2','numeric',
                 'formatless','formatless_push','formatless_secondary','formatless_post','formatless_array','formatless_core'):
        if not selected or case in selected:scene(checks,case)
    return checks.finish()
if __name__=='__main__':raise SystemExit(main())
