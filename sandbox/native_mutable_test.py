"""Writable native files retain GPU data and each declaration owns its allocation."""
from pathlib import Path
import json
import struct
import shutil
import sys
import time
from harness import Checks,Sandbox,test_dir,DIST, configuration_root
from native_compute_test import compile_shader,read_compute_result
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
    root=Path(test_dir('mutable-file-'+case+'-'));mods=root/'Mods';mods.mkdir()
    source=root/'scene.hlsl'
    persistent=case in ('persistent','persistent_secondary')
    source.write_text('[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;\n'
        '[[vk::binding(2,0)]] RWStructuredBuffer<uint> state;\n'
        '[numthreads(1,1,1)] void main(){ if(output[0]==0){output[0]=state[0];state[0]=99;}else{output[0]+=1000*state[0];} }\n',encoding='ascii')
    if persistent:source.write_text(source.read_text().replace('if(output[0]==0){output[0]=state[0];state[0]=99;}else{output[0]+=1000*state[0];}', 'output[0]+=state[0];state[0]=19;'),encoding='ascii')
    shader=compile_shader(source,root/'compiled','12345678','cs')
    (mods/'input.buf').write_bytes(struct.pack('<4I',17,0,0,0))
    flags='' if case=='implicit' else 'bind_flags = shader_resource unordered_access\n'
    if case=='readonly':flags='bind_flags = shader_resource\n'
    stride=8 if case=='stride' else 4
    header='namespace = mutable_file\n[ShaderOverrideCompute]\nvk_hash = %08x\nhandling = skip\nvk_binding = cs-u2, 0, 2\n'%crc32c(shader.read_bytes())
    if persistent:header=header.replace('handling = skip\n','').replace('[ShaderOverrideCompute]', '[Constants]\nglobal $bound = 0\n[Present]\n$bound = 0\n[ShaderOverrideCompute]')
    def content(reverse=False):
        first,second=('B','A') if reverse else ('A','B')
        if case=='alias':second=first
        body='cs-u2 = Resource%s\ndispatch = 1, 1, 1\ncs-u2 = Resource%s\ndispatch = 1, 1, 1\n'%(first,second)
        if case=='application_sync2':body='cs-u2 = reference cs-u2\ndispatch = 1,1,1\n'
        if persistent:body='if $bound == 0\ncs-u2 = ResourceA\n$bound = 1\nendif\n'
        return header+body+''.join('[Resource%s]\ntype = Buffer\nstride = %d\nfilename = input.buf\n%s'%(name,stride,flags) for name in ('A','B'))
    ini=mods/'test.ini';ini.write_text(content(),encoding='ascii')
    output=root/'compute.raw';keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii')
    env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(root/'settings'),
        EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_KEYS=str(keys),VK_LAYER_VALIDATE_SYNC='1')
    args=['--compute-shader',str(shader),'--compute-output',str(output),'--compute-bindings']
    if case=='push':args+=['--descriptors','push']
    if case in ('secondary','persistent_secondary'):args+=['--compute-secondary']
    if persistent:args+=['--compute-repeat','3','--compute-buffer-barriers']
    if case=='two_queues':args+=['--two-queues']
    if case in ('sync2','indirect_sync2','application_sync2'):args+=['--sync2']
    if case=='indirect_sync2':args+=['--compute-indirect']
    refused=case in ('readonly','stride','two_queues','indirect_sync2','application_sync2')
    expected=7 if refused else 57 if persistent else 99099 if case=='alias' else 17099
    def value():
        data=read_compute_result(output)
        return struct.unpack_from('<I',data)[0] if data else None
    with Sandbox(seconds=12 if case=='swap' else 6 if case=='reload' else 3,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:
            if case=='swap':
                module=root/'backend.dll';shutil.copy2(Path(DIST)/'efmi_vk_backend.dll',module);probe.swap_module(str(module))
                deadline=time.monotonic()+6
                while time.monotonic()<deadline:
                    active=probe.status().get('replacement',{})
                    if active.get('module',{}).get('generation',0)>0 and active.get('shader_overrides',{}).get('executed',0)>0 and value()==expected:break
                    time.sleep(.05)
                checks.check(active.get('module',{}).get('generation',0)>0,'swap: replacement module exchange completed')
            if case=='reload':
                checks.equal(value(),17099,'reload: first declaration retains writes while its sibling stays independent')
                ini.write_text(content(True),encoding='ascii')
                keys.write_text('1 121\n',encoding='ascii');time.sleep(.2);keys.write_text('1\n',encoding='ascii');time.sleep(1)
            status=probe.status().get('replacement',{})
        sb.proc.wait(timeout=20)
        checks.equal(sb.proc.returncode,0,case+': normal exit')
        (root/'stdout.txt').write_text(sb.output(),encoding='utf-8');(root/'layer.log').write_bytes(Path(sb.log_path).read_bytes())
    checks.equal(value(),expected,case+': completed GPU bytes preserve the declaration contract')
    hooks=status.get('shader_overrides',{})
    checks.check(bool(hooks.get('last_reason')) if refused else hooks.get('executed',0)>0,case+': explicit refusal' if refused else case+': native execution observed')
    checks.check('Validation Error' not in sb.output() and 'VUID-' not in sb.output(),case+': core and synchronization validation clean')
    (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root)

def main():
    checks=Checks('mutable files');selected=set(sys.argv[1:])
    for case in ('independent','implicit','alias','push','secondary','reload','swap','readonly','stride','two_queues','sync2','indirect_sync2','application_sync2','persistent','persistent_secondary'):
        if not selected or case in selected:scene(checks,case)
    return checks.finish()
if __name__=='__main__':raise SystemExit(main())
