"""Persistent hunting counts are rendered after transient notices expire."""
from pathlib import Path
import json
import shutil
import sys
import time
from harness import Checks,Sandbox,test_dir
from runtime_fixture import CORE
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
    root=Path(test_dir('hunting-overlay-'+case+'-'));mods=root/'Mods';mods.mkdir()
    (mods/'test.ini').write_text('namespace = steady_hunting\n[Constants]\nglobal $touch = 0\n[KeyTouch]\nkey = VK_F8\n$touch = $touch + 1\n',encoding='ascii')
    (root/'d3dx.ini').write_text('[Include]\ninclude = Core\\EFMI\\main.ini\ninclude_recursive = Mods\n[Rendering]\nrecursive_include = 1\n'
        '[Hunting]\nhunting = 1\nmarking_mode = original\nnext_pixelshader = VK_NUMPAD2\ntoggle_hunting = VK_DECIMAL\n',encoding='ascii')
    keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii');pixels=root/'pixels.raw';trigger=root/'capture.trigger'
    env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(root/'settings'),EFMI_VK_M2_ENABLED='1',
        EFMI_VK_TEST_KEYS=str(keys),EFMI_VK_TEST_NOTICES_READBACK=str(pixels),EFMI_VK_TEST_NOTICES_READBACK_TRIGGER=str(trigger),VK_LAYER_VALIDATE_SYNC='1')
    with Sandbox(seconds=12,validation=True,env_extra=env,extra_args=['--readable-swapchain']) as sb:
        with ProbeClient(sb.pid) as probe:
            if case=='swap':
                time.sleep(.5)
                module=root/'native-backend.dll';shutil.copy2(Path(__file__).resolve().parents[1]/'dist/efmi_vk_backend.dll',module)
                probe.swap_module(str(module));deadline=time.monotonic()+4
                while time.monotonic()<deadline:
                    swapped=probe.status()
                    if swapped.get('replacement',{}).get('module',{}).get('generation',0)>0:break
                    time.sleep(.1)
                checks.check(swapped.get('replacement',{}).get('module',{}).get('generation',0)>0,'exchanged code module becomes active')
            time.sleep(3.2)
            before=probe.status();trigger.touch();deadline=time.monotonic()+4
            while not pixels.exists() and time.monotonic()<deadline:time.sleep(.1)
            status=probe.status()
            def press(key):
                keys.write_text('1 %d\n'%key,encoding='ascii');time.sleep(.2);keys.write_text('1\n',encoding='ascii');time.sleep(.2)
            def wait_counts(wanted):
                until=time.monotonic()+3
                while time.monotonic()<until:
                    current=probe.status()
                    if bool(current.get('notices',{}).get('hunting'))==wanted:return current
                    time.sleep(.1)
                return current
            if case=='steady':
                events=status.get('replacement',{}).get('hunting',{}).get('input_events',0)
                press(119)
                after=probe.status().get('replacement',{}).get('hunting',{})
                checks.check(after.get('input_events',0)>events,'registered Mod key refreshes hunting activity')
                events=after.get('input_events',0);press(118)
                checks.equal(probe.status().get('replacement',{}).get('hunting',{}).get('input_events',0),events,'unregistered key does not impersonate an original input event')
                press(110);off=wait_counts(False)
                checks.check(not off.get('notices',{}).get('hunting'),'toggle off clears persistent counts')
                press(110);on=wait_counts(True)
                checks.check(bool(on.get('notices',{}).get('hunting')),'toggle on restores persistent counts')
            elif case=='reload':
                ini=root/'d3dx.ini';ini.write_text(ini.read_text().replace('hunting = 1','hunting = 0'),encoding='ascii')
                press(121);off=wait_counts(False)
                checks.check(not off.get('notices',{}).get('hunting') and off.get('replacement',{}).get('hunting',{}).get('mode')==0,'F10 disabled configuration clears host-owned counts')
        sb.proc.wait(timeout=20)
        checks.equal(sb.proc.returncode,0,'sandbox exits normally')
        (root/'stdout.txt').write_text(sb.output(),encoding='utf-8');(root/'layer.log').write_bytes(Path(sb.log_path).read_bytes())
    counts=status.get('notices',{}).get('hunting','')
    checks.check('VS:' in counts and 'PS:' in counts,'persistent shader counts remain after transient notices expire')
    white=0
    if pixels.exists():
        meta=json.loads(Path(str(pixels)+'.json').read_text());data=pixels.read_bytes();width=meta['width']
        white=sum(all(data[4*(y*width+x)+c]>140 for c in range(3)) for y in range(min(100,meta['height'])) for x in range(min(700,width)))
    checks.check(white>20,'delayed fenced readback contains the persistent white counter text')
    checks.check('Validation Error' not in sb.output() and 'VUID-' not in sb.output(),'core and synchronization validation clean')
    (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root)

def main():
    checks=Checks('persistent hunting');selected=set(sys.argv[1:])
    for case in ('steady','swap','reload'):
        if not selected or case in selected:scene(checks,case)
    return checks.finish()
if __name__=='__main__':raise SystemExit(main())
