"""Actual null attachments preserve unbind, restore and original built-in semantics."""
from pathlib import Path
import json,sys,time
from harness import Checks,Sandbox,test_dir, configuration_root
from runtime_test import sandbox_shader_hashes,band_pixels
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
 root=Path(test_dir('native-output-'+case+'-'));mods=root/'Mods';mods.mkdir();_,fragment=sandbox_shader_hashes()
 body='if $saved == 0\nResourceSaved = ref o0\n$saved = 1\n'
 commands={'khr':'o0 = null\n','inject':'o0 = null\n','enabled':'o0 = null\n','once':'o0 = null\n','restore':'o0 = null\no0 = ref ResourceSaved\n','post':'post o0 = null\n','builtins':'run = BuiltInCommandListUnbindAllRenderTargets\n','disabled':'o0 = null\n'}[case]
 body+=('elif o0->bindflags == 32\n' if case=='once' else 'else\n')+commands+'endif\n'
 (mods/'mod.ini').write_text('namespace = native_output\n[Constants]\nglobal $saved = 0\n[ResourceSaved]\n[ShaderOverrideTarget]\nhash = %08x\n'%fragment+body,encoding='ascii')
 settings=root/'settings';settings.mkdir();pixels=root/'screen.raw';owned=root/'owned.raw'
 env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> native output destinations',EFMI_VK_TEST_NOTICES_READBACK=str(pixels),VK_LAYER_VALIDATE_SYNC='1')
 args=['--readable-swapchain','--api','1.3','--dynamic-rendering','2' if case=='khr' else '1','--native-output',str(owned)]
 if case!='disabled':args+=['--unused-attachments']
 with Sandbox(seconds=4,threads=0,entry='inject' if case=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
  time.sleep(1)
  with ProbeClient(sb.pid) as probe:status=probe.status().get('replacement',{})
  sb.proc.wait(timeout=25);checks.equal(sb.proc.returncode,0,case+': normal exit')
  (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
 visible=band_pixels(pixels);redirected=band_pixels(owned)
 if case=='disabled':
  checks.check(visible is not None and redirected is not None and visible>4000 and 100<redirected<1500,case+': feature-disabled path preserves original targets')
 else:
  expected_owned=0 if case in ('enabled','builtins','khr','inject') else 902 if case in ('once','post') else 6418
  expected_visible=902 if case=='post' else 0
  checks.equal(visible,expected_visible,case+': actual original target follows null/pre/post binding order')
  checks.equal(redirected,expected_owned,case+': actual owned target receives only draws while bound')
  checks.check(status.get('shader_overrides',{}).get('executed',0)>0 and not status.get('shader_overrides',{}).get('last_reason'),case+': output unbinding executes without refusal')
 checks.check(not any(marker in sb.output() for marker in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
 (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root,'screen',visible,'owned',redirected)
checks=Checks('native null outputs');selected=set(sys.argv[1:])
for case in ('enabled','khr','inject','once','restore','post','builtins','disabled'):
 if not selected or case in selected:scene(checks,case)
raise SystemExit(checks.finish())
