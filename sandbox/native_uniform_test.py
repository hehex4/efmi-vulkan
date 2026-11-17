"""Graphics uniform references use recorded pass dependencies; published files are separate."""
from pathlib import Path
import json
import struct
import sys
import time
from harness import Checks,Sandbox,test_dir, configuration_root
from runtime_test import sandbox_shader_hashes
from native_shader_test import compile_shader,colored_pixels
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
    root=Path(test_dir('native-uniform-'+case+'-'));mods=root/'Mods';mods.mkdir()
    vertex,fragment=sandbox_shader_hashes()
    body='if $saved == 0\nResourceSaved = ref vs-cb0\n$saved = 1\nendif\nvs-cb0 = ref ResourceSaved\n'
    declaration='[ResourceSaved]\n'
    if case=='file_secondary':
        body='vs-cb0 = ResourceSaved\n'
        declaration+='type = Buffer\nstride = 16\nbind_flags = constant_buffer\nfilename = input.buf\n'
        (mods/'input.buf').write_bytes(struct.pack('<24f',*( [0,0,0,0,.2,1,.5,1]+[0]*16 )))
    (mods/'test.ini').write_text('namespace = native_uniform\n[Constants]\nglobal $saved = 0\n'
        '[ShaderOverrideVertex]\nvk_hash = %08x\nvk_binding = vs-cb0, 0, 0\n'%vertex+body+declaration,encoding='ascii')
    settings=root/'settings';fixes=settings/'ShaderFixes';fixes.mkdir(parents=True)
    shader=root/'color.hlsl';shader.write_text('[[vk::binding(0,0)]] cbuffer U { float4 offset; float4 color; };\n'
        'float4 main() : SV_Target0 { return color.y > .9 ? float4(0,1,0,1) : float4(1,0,1,1); }\n',encoding='ascii')
    compile_shader(shader,fixes,'%08x'%fragment,'ps')
    pixels=root/'pixels.raw';trigger=root/'capture.trigger'
    env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',
        EFMI_VK_TEST_NOTICE='> native uniform',EFMI_VK_TEST_NOTICES_READBACK=str(pixels),EFMI_VK_TEST_NOTICES_READBACK_TRIGGER=str(trigger))
    secondary=case in ('secondary','file_secondary')
    args=['--readable-swapchain']+(['--descriptors','push'] if case=='push' else [])
    with Sandbox(threads=2 if secondary else 0,seconds=4,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:
            status=probe.status().get('replacement',{});trigger.touch()
        sb.proc.wait(timeout=20)
        checks.equal(sb.proc.returncode,0,case+': normal exit')
        (root/'stdout.txt').write_text(sb.output(),encoding='utf-8');(root/'layer.log').write_bytes(Path(sb.log_path).read_bytes())
    green=colored_pixels(pixels,(0,255,0),(.01,.99));magenta=colored_pixels(pixels,(255,0,255),(.01,.99))
    hooks=status.get('shader_overrides',{})
    if case=='secondary':
        checks.check(magenta>100 and green>100,case+': inherited secondary keeps original uniform colors without parent proof')
        checks.check(hooks.get('executed',0)==0 and bool(hooks.get('last_reason')),case+': missing parent proof is reported')
    else:
        checks.check(green>200 and magenta<20,case+': retained uniform reaches all fragment colors')
        checks.check(hooks.get('executed',0)>0,case+': native descriptor execution observed')
    checks.check('VALIDATION ERROR' not in sb.output() and 'VUID-' not in sb.output(),case+': Vulkan validation clean')
    (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root,'green',green,'magenta',magenta)

def main():
    checks=Checks('native uniforms');selected=set(sys.argv[1:])
    for case in ('primary','push','secondary','file_secondary'):
        if not selected or case in selected:scene(checks,case)
    return checks.finish()
if __name__=='__main__':raise SystemExit(main())
