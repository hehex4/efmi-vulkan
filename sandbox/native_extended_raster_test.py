"""Native-only ShaderFixes twins retain the application's dynamic geometry."""
from pathlib import Path
import shutil
import sys
import time
from harness import Checks, Sandbox, test_dir, DIST, configuration_root
from native_shader_test import compile_pixel, colored_pixels
from runtime_test import sandbox_shader_hashes, band_pixels
from efmi_vk.probe_client import ProbeClient


def main():
    checks=Checks('native extended raster');selected=set(sys.argv[1:])
    compiler=Path(test_dir('native-dynamic-compiler-'))
    magenta=compile_pixel(compiler,'1,0,1');green=compile_pixel(compiler,'0,1,0')
    _,fragment=sandbox_shader_hashes()
    for case in ('core','counts','input','stride','secondary','inject','reload','removed','swap','original','pink'):
        if selected and case not in selected:continue
        root=Path(test_dir('native-raster-'+case+'-'));mods=root/'Mods';mods.mkdir()
        (root/'d3dx.ini').write_text('[Include]\ninclude_recursive = Mods\n[Rendering]\nrecursive_include = 1\n[Hunting]\nhunting = 1\nmarking_mode = '+('pink' if case=='pink' else 'original')+'\nnext_pixelshader = no_modifiers VK_NUMPAD2\n',encoding='ascii')
        settings=root/'settings';fixes=settings/'ShaderFixes';fixes.mkdir(parents=True)
        shader=fixes/('%08x-ps_replace.spv'%fragment);shader.write_bytes(green if case=='pink' else magenta)
        readback=root/'pixels.raw';trigger=root/'capture.trigger';keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii')
        env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_KEYS=str(keys),EFMI_VK_TEST_NOTICES_READBACK=str(readback),EFMI_VK_TEST_NOTICES_READBACK_TRIGGER=str(trigger),VK_LAYER_VALIDATE_SYNC='1')
        mode=1 if case=='core' else 2 if case=='ext' else 5 if case=='stride' else 4
        args=['--readable-swapchain','--geometry-dynamic',str(mode),'--geometry-shader-scene','--api','1.1' if case=='ext' else '1.3']
        args+=['--extended-raster','2' if case=='counts' else '1']
        with Sandbox(seconds=30,threads=2 if case=='secondary' else 0,entry='inject' if case=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
            with ProbeClient(sb.pid) as probe:
                time.sleep(1)
                before=probe.status().get('replacement',{}).get('shader_fixes',{})
                checks.check(before.get('loaded')==1 and before.get('binds',0)>0,case+': initial native twin active')
                if case in ('reload','removed'):
                    if case=='reload':shader.write_bytes(green)
                    else:shader.unlink()
                    keys.write_text('1 121\n',encoding='ascii');time.sleep(.25);keys.write_text('1\n',encoding='ascii')
                    deadline=time.monotonic()+6
                    while time.monotonic()<deadline:
                        current=probe.status().get('replacement',{})
                        if current.get('configuration_generation',{}).get('reloads',0)>0:break
                        time.sleep(.05)
                    checks.check(current.get('configuration_generation',{}).get('reloads',0)>0,case+': configuration reload completed')
                elif case=='swap':
                    module=root/'backend.dll';shutil.copy2(Path(DIST)/'efmi_vk_backend.dll',module);probe.swap_module(str(module))
                    deadline=time.monotonic()+6
                    while time.monotonic()<deadline:
                        current=probe.status().get('replacement',{})
                        if current.get('module',{}).get('generation',0)>0 and current.get('shader_fixes',{}).get('binds',0)>0:break
                        time.sleep(.05)
                    checks.check(current.get('module',{}).get('generation',0)>0 and current.get('shader_fixes',{}).get('binds',0)>0,case+': exchanged module binds dynamic native twins')
                elif case in ('original','pink'):
                    keys.write_text('1 98\n',encoding='ascii');deadline=time.monotonic()+6
                    while time.monotonic()<deadline:
                        current=probe.status().get('replacement',{})
                        if current.get('hunting',{}).get('selected',{}).get('pixel'):break
                        time.sleep(.05)
                    keys.write_text('1\n',encoding='ascii')
                    checks.check(bool(current.get('hunting',{}).get('selected',{}).get('pixel')),case+': shader selection active')
                time.sleep(.6);trigger.touch();deadline=time.monotonic()+6
                while not readback.is_file() and time.monotonic()<deadline:time.sleep(.05)
                after=probe.status().get('replacement',{})
            checks.check(readback.is_file() and sb.proc.poll() is None,case+': completed GPU readback while process remains live')
            if case in ('original','removed'):
                checks.check(colored_pixels(readback,(255,0,255))==0 and (band_pixels(readback) or 0)>200,case+': original shader and dynamic geometry restored')
            else:
                checks.check(colored_pixels(readback,(0,255,0) if case=='reload' else (255,0,255))>200,case+': correct native shader pixels with dynamic geometry')
            sb.proc.terminate();sb.proc.wait(timeout=30)
        checks.check('Validation Error' not in sb.output() and 'VUID-' not in sb.output(),case+': core and synchronization validation clean')
        (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes())
        print('artifacts:',root)
    return checks.finish()


if __name__=='__main__':raise SystemExit(main())
