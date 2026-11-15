"""Retained uniforms fall back and recover across passes without another binding list.

The extra uniform slots are shader-unused, so an old implementation remains a
valid baseline. Actual descriptor restoration is checked by CPU driver stubs;
GPU refresh/refusal counters prove the pass adapter runs with real allocations.
"""
from pathlib import Path
import json,sys,time,shutil
from harness import Checks,Sandbox,test_dir,DIST
from runtime_fixture import CORE
from runtime_test import sandbox_shader_hashes,band_pixels
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
 root=Path(test_dir('native-uniform-persistent-'+case+'-'));mods=root/'Mods';mods.mkdir();_,fragment=sandbox_shader_hashes()
 body='vk_binding = ps-cb1, 1, 6\nvk_binding = ps-cb2, 1, 7\nif o0->bindflags == 32\nps-cb1 = ref ps-cb2\nendif\n'
 if case=='control':body=''
 (mods/'mod.ini').write_text('[ShaderOverrideUniform]\nhash = %08x\n'%fragment+body,encoding='ascii')
 settings=root/'settings';settings.mkdir();screen=root/'screen.raw';owned=root/'owned.raw';trigger=root/'capture.trigger'
 env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',
          EFMI_VK_TEST_NOTICE='> persistent native uniform',EFMI_VK_TEST_NOTICES_READBACK=str(screen),VK_LAYER_VALIDATE_SYNC='1')
 args=['--readable-swapchain','--api','1.3','--dynamic-rendering','2' if case=='khr' else '1',
       '--native-output',str(owned),'--native-output-memory','2' if case=='disjoint' else '1','--native-uniform-persistent']
 if case in ('classic','renderpass2'):
  args[args.index('--dynamic-rendering')+1]='0';args+=['--native-output-classic','2' if case=='renderpass2' else '1']
 if case=='push':args+=['--descriptors','push']
 if case=='swap':env['EFMI_VK_TEST_NOTICES_READBACK_TRIGGER']=str(trigger)
 with Sandbox(seconds=12 if case=='swap' else 4,threads=0,entry='inject' if case=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
  time.sleep(1)
  with ProbeClient(sb.pid) as probe:
   if case=='swap':
    module=root/'backend.dll';shutil.copy2(Path(DIST)/'efmi_vk_backend.dll',module);probe.swap_module(str(module))
    deadline=time.monotonic()+7;exchanged=False
    while time.monotonic()<deadline:
     if probe.status().get('replacement',{}).get('module',{}).get('generation',0)>0:exchanged=True;break
     time.sleep(.05)
    checks.check(exchanged,'sandbox module exchange completed');time.sleep(.3);trigger.touch()
   status=probe.status()
  sb.proc.wait(timeout=25);checks.equal(sb.proc.returncode,0,case+': normal exit')
  (root/'stdout.txt').write_text(sb.output(),encoding='utf-8');(root/'layer.log').write_bytes(Path(sb.log_path).read_bytes())
 images=status.get('device',{}).get('native_images',{});calls=status.get('replacement',{}).get('shader_overrides',{})
 if case not in ('control','disjoint'):
  checks.check(images.get('refused',0)>0 and images.get('refreshes',0)>=2,case+': conflicting and independent later passes refresh persistent bindings')
  checks.check('Native input overlaps attachment memory' in (root/'layer.log').read_text(encoding='utf-8'),case+': actual attachment storage suspends the retained request')
 else:checks.equal(images.get('refused',0),0,case+': independent or unchanged application bindings have no refusal')
 if case!='control':checks.check(calls.get('executed',0)>0 and not calls.get('last_reason'),case+': initial independent reference executes without refusal')
 counts=[band_pixels(p) for p in (screen,owned,Path(str(owned)+'.independent.raw'))]
 checks.check(counts[0]==6418 and counts[1]==counts[2]==902,case+': original geometry survives both later passes with complete target bytes')
 checks.check(not any(marker in sb.output() for marker in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
 (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root,'counts',counts,'images',images)

def main():
 checks=Checks('native uniform persistence');selected=set(sys.argv[1:])
 for case in ('control','core','khr','inject','classic','renderpass2','push','disjoint','swap'):
  if not selected or case in selected:scene(checks,case)
 return checks.finish()

if __name__=='__main__':raise SystemExit(main())
