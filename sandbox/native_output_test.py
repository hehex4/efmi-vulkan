"""Actual destination rebinding preserves stored passes, layout restoration and runtime exchange."""
from pathlib import Path
import json,sys,time,shutil
from harness import Checks,Sandbox,test_dir,DIST
from runtime_fixture import CORE
from runtime_test import sandbox_shader_hashes,band_pixels
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
 root=Path(test_dir('native-output-'+case+'-'));mods=root/'Mods';mods.mkdir();_,fragment=sandbox_shader_hashes()
 body='if $saved == 0\nResourceSaved = ref o0\n$saved = 1\n'
 body+=('elif o0->bindflags == 32\no0 = ref ResourceSaved\n' if case=='once' else ('else\npost o0 = ref ResourceSaved\n' if case=='post' else ('else\no0 = null\n' if case=='null' else ('else\no0 = ref ResourceSaved\n' if case!='control' else ''))))+'endif\n'
 (mods/'mod.ini').write_text('namespace = native_output\n[Constants]\nglobal $saved = 0\n[ResourceSaved]\n[ShaderOverrideTarget]\nhash = %08x\n'%fragment+body,encoding='ascii')
 settings=root/'settings';settings.mkdir();pixels=root/'screen.raw';owned=root/'owned.raw'
 trigger=root/'capture.trigger';keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii')
 env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> native output destinations',EFMI_VK_TEST_NOTICES_READBACK=str(pixels),VK_LAYER_VALIDATE_SYNC='1',EFMI_VK_TEST_KEYS=str(keys))
 if case in ('reload','swap'):env['EFMI_VK_TEST_NOTICES_READBACK_TRIGGER']=str(trigger)
 args=['--readable-swapchain','--api','1.3','--dynamic-rendering','2' if case=='khr' else '1','--native-output',str(owned)]
 if case=='query':args+=['--native-output-query']
 if case=='sync2':args+=['--submit2','1']
 with Sandbox(seconds=12 if case in ('reload','swap') else 4,threads=0,entry='inject' if case=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
  time.sleep(1)
  with ProbeClient(sb.pid) as probe:
   if case=='swap':
    module=root/'backend.dll';shutil.copy2(Path(DIST)/'efmi_vk_backend.dll',module);probe.swap_module(str(module))
   elif case=='reload':
    keys.write_text('1 121\n',encoding='ascii');time.sleep(.4);keys.write_text('1\n',encoding='ascii')
   if case in ('reload','swap'):
    deadline=time.monotonic()+7
    while time.monotonic()<deadline:
     status=probe.status().get('replacement',{})
     count=status.get('module' if case=='swap' else 'folder_generation',{}).get('generation' if case=='swap' else 'reloads',0)
     if count>0:break
     time.sleep(.05)
    checks.check(count>0,case+': requested generation exchange completed')
    time.sleep(.3);trigger.touch()
   status=probe.status().get('replacement',{})
  sb.proc.wait(timeout=25);checks.equal(sb.proc.returncode,0,case+': normal exit')
  (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
 visible=band_pixels(pixels);redirected=band_pixels(owned)
 if case=='control':checks.check(visible is not None and redirected is not None and visible>3*redirected and redirected>100,case+': main and owned fixture each have their original GPU geometry')
 elif case in ('query','sync2','null'):
  checks.check(visible is not None and redirected is not None and visible>4000 and 100<redirected<1500,case+': unsupported destination change preserves both original GPU results')
  reason=status.get('shader_overrides',{}).get('last_reason','')
  checks.check({'query':'active query','sync2':'legacy synchronization','null':'enabled unused attachments'}[case] in reason,case+': precise boundary refusal is reported')
 else:
  checks.check(visible is not None and (500<visible<1500 if case=='post' else visible<30),case+': target switch follows pre/post command order')
  checks.check(redirected is not None and redirected>2000,case+': actual owned destination receives main geometry')
  checks.check(status.get('shader_overrides',{}).get('executed',0)>0 and not status.get('shader_overrides',{}).get('last_reason'),case+': output destination assignment executes without refusal')
 checks.check(not any(marker in sb.output() for marker in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
 (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root,'screen',visible,'owned',redirected)
checks=Checks('native output destinations');selected=set(sys.argv[1:])
for case in ('control','core','khr','inject','once','post','reload','swap','query','sync2','null'):
 if not selected or case in selected:scene(checks,case)
raise SystemExit(checks.finish())
