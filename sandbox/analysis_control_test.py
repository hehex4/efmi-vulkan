"""Analysis hold uses original key release and preserves completed partial sessions."""
from pathlib import Path
import json
import struct
import sys
import time
from harness import Checks, Sandbox, test_dir
from native_shader_test import compile_shader
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
    root=Path(test_dir('analysis-control-'+case+'-'));mods=root/'Mods';mods.mkdir();settings=root/'settings';settings.mkdir()
    config=root/'d3dx.ini'
    def configure(options):
        config.write_text('[Include]\ninclude_recursive = Mods\n[Rendering]\nrecursive_include = 1\n'
            '[Hunting]\nhunting = 1\nanalyse_frame = no_modifiers VK_F9\nanalyse_options = '+options+'\n',encoding='ascii')
    configure('log hold' if case=='log' else 'dump_rt buf'+('' if case=='normal' else ' hold'))
    keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii')
    source=root/'scene.hlsl';body='[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;\n'
    body+=('[[vk::binding(2,0)]] RWTexture2D<uint> image;\n[numthreads(1,1,1)] void main(){output[0]+=19;image[int2(0,0)]+=19;}' if case=='image_hold' else '[numthreads(1,1,1)] void main(){output[0]+=19;}')
    source.write_text(body,encoding='ascii')
    shader=compile_shader(source,root/'compiled','12345678','cs')
    args=['--compute-shader',str(shader),'--compute-capture']+(['--compute-storage-image'] if case=='image_hold' else [])
    env=dict(EFMI_VK_CONFIGURATION=str(root),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_KEYS=str(keys),VK_LAYER_VALIDATE_SYNC='1')
    with Sandbox(seconds=7,validation=True,env_extra=env,extra_args=args) as sb:
        with ProbeClient(sb.pid) as probe:
            time.sleep(.5);keys.write_text('1 120\n',encoding='ascii');time.sleep(1.2)
            hunting=lambda:probe.status().get('replacement',{}).get('hunting',{})
            held=hunting();checks.equal(held.get('analysing'),case!='normal',case+': analysis remains active only in hold mode')
            if case=='reconfigure':
                configure('dump_cb txt');keys.write_text('1 120 121\n',encoding='ascii');time.sleep(.2);keys.write_text('1 120\n',encoding='ascii');time.sleep(.8)
            keys.write_text('1\n',encoding='ascii')
            deadline=time.monotonic()+2
            while hunting().get('analysing') and time.monotonic()<deadline:time.sleep(.05)
            checks.check(not hunting().get('analysing'),case+': releasing the key stops analysis')
            time.sleep(.4)
            finished=hunting();checks.check(finished.get('analyses',0)>=1,case+': completed analysis is reported')
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+': application exits normally')
        (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
    logs=list(settings.glob('FrameAnalysis-*/log.txt'));checks.check(bool(logs),case+': partial log is saved')
    text='\n'.join(p.read_text() for p in logs);calls=text.count('Dispatch(')
    checks.check(calls==1 if case=='normal' else calls>=3,case+': expected single-frame or continued draw log')
    if case=='reconfigure':checks.check('configuration changed' in text,case+': F10 closes the old session explicitly')
    records=[(p.parent,json.loads(line)) for p in settings.glob('FrameAnalysis-*/resources.jsonl') for line in p.read_text().splitlines()]
    completed=[(p,r) for p,r in records if r.get('state')=='complete']
    checks.check(len(completed)==0 if case=='log' else len(completed)==1 if case=='normal' else len(completed)>=3,case+': selected GPU frames are exported')
    if completed:
        frames={r.get('frame',0) for _,r in completed};checks.check(frames=={1} if case=='normal' else len(frames)>=3 and min(frames)==1,case+': capture frames have explicit distinct identities')
    checks.check(all(struct.unpack_from('<I',(p/r['file']).read_bytes())[0]==19 for p,r in completed),case+': each file contains completed GPU output')
    checks.check(not any(r.get('point')=='options' for _,r in records),case+': hold is implemented without an options refusal')
    checks.check('Validation Error' not in sb.output() and 'VUID-' not in sb.output(),case+': Vulkan validation clean')
    print('artifacts:',root)

def main():
    checks=Checks('analysis controls');selected=set(sys.argv[1:])
    for case in ('hold','image_hold','normal','log','reconfigure'):
        if not selected or case in selected:scene(checks,case)
    return checks.finish()
if __name__=='__main__':raise SystemExit(main())
