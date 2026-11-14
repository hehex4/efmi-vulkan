"""Dynamic depth filtering follows actual bound views, with depth testing disabled."""
from pathlib import Path
import json,sys,time,shutil
from harness import Checks,Sandbox,test_dir,DIST
from native_shader_test import compile_pixel,colored_pixels
from runtime_fixture import CORE
from runtime_test import sandbox_shader_hashes,band_pixels
from efmi_vk.probe_client import ProbeClient

def main():
    checks=Checks('native dynamic depth');selected=set(sys.argv[1:])
    root=Path(test_dir('dynamic-depth-compiler-'));shader=compile_pixel(root,'1,0,1');_,fragment=sandbox_shader_hashes()
    for case in ('active','inactive','null_active','null_inactive','switch_active','switch_inactive','lists_active','lists_inactive','lists_null','secondary','secondary_inactive','reverse_active','reverse_inactive','inject','swap','reload'):
        if selected and case not in selected:continue
        root=Path(test_dir('dynamic-depth-'+case+'-'));mods=root/'Mods';mods.mkdir();settings=root/'settings';fixes=settings/'ShaderFixes';fixes.mkdir(parents=True)
        lists=case.startswith('lists');inactive=case.endswith('inactive') or case=='lists_null'
        mode=4 if case.startswith('reverse') else 2 if case.startswith('null') or case=='lists_null' else 3 if case.startswith('switch') else 1
        ini=mods/'test.ini'
        def config(inactive):return 'namespace = dynamic_depth\n[ShaderOverrideDepth]\nhash = %08x\ndepth_filter = depth_%s\n'%(fragment,'inactive' if inactive else 'active')+('handling = skip\n' if lists else '')
        ini.write_text(config(inactive),encoding='ascii')
        if not lists:(fixes/('%08x-ps_replace.spv'%fragment)).write_bytes(shader)
        keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii');pixels=root/'pixels.raw';trigger=root/'capture.trigger'
        env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',
            EFMI_VK_TEST_KEYS=str(keys),EFMI_VK_TEST_NOTICE='> dynamic depth',EFMI_VK_TEST_NOTICES_READBACK=str(pixels),EFMI_VK_TEST_NOTICES_READBACK_TRIGGER=str(trigger),VK_LAYER_VALIDATE_SYNC='1')
        args=['--readable-swapchain','--dynamic-rendering','2' if case=='inject' else '1','--dynamic-depth',str(mode),'--api','1.3']
        with Sandbox(seconds=25,threads=2 if case.startswith('secondary') else 0,entry='inject' if case=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
            time.sleep(1)
            with ProbeClient(sb.pid) as probe:
                if case=='swap':
                    module=root/'backend.dll';shutil.copy2(Path(DIST)/'efmi_vk_backend.dll',module);probe.swap_module(str(module));time.sleep(.5)
                if case=='reload':
                    ini.write_text(config(True),encoding='ascii');keys.write_text('1 121\n',encoding='ascii');time.sleep(.2);keys.write_text('1\n',encoding='ascii');time.sleep(.7)
                trigger.touch();deadline=time.monotonic()+6
                while not pixels.exists() and time.monotonic()<deadline:time.sleep(.05)
                status=probe.status().get('replacement',{})
            checks.check(pixels.exists() and sb.proc.poll() is None,case+': completed live GPU readback')
            matching=(mode not in (2,3)) != inactive and not case.startswith('secondary') and case!='reload'
            if lists:checks.check((band_pixels(pixels) or 0)<30,case+': original command list executes independently of shader depth filtering')
            else:checks.check(colored_pixels(pixels,(255,0,255))>200 if matching else colored_pixels(pixels,(255,0,255))==0 and (band_pixels(pixels) or 0)>200,case+': native replacement follows actual DSV presence')
            sb.proc.terminate();sb.proc.wait(timeout=20)
            checks.check('VALIDATION ERROR' not in sb.output() and 'VUID-' not in sb.output(),case+': core and synchronization validation clean')
            (root/'stdout.txt').write_text(sb.output(),encoding='utf-8');(root/'layer.log').write_bytes(Path(sb.log_path).read_bytes())
        (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root)
    return checks.finish()
if __name__=='__main__':raise SystemExit(main())
