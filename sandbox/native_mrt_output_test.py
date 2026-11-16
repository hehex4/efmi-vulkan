"""Independent readbacks distinguish multiple actual color output destinations."""
from pathlib import Path
import json,sys,time
from harness import Checks,Sandbox,test_dir, configuration_root
from runtime_test import sandbox_shader_hashes,band_pixels
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
 root=Path(test_dir('native-mrt-output-'+case+'-'));mods=root/'Mods';mods.mkdir();vertex,_=sandbox_shader_hashes()
 commands={'khr':'o1 = ref ResourceSaved\n','inject':'o1 = ref ResourceSaved\n','control':'','slot1':'o1 = ref ResourceSaved\n','slot0':'o0 = ref ResourceSaved\n','null1':'o1 = null\n',
           'both':'o0 = ref ResourceSaved\no1 = null\n','restore':'o1 = null\no1 = ref ResourceSaved\n',
           'post':'post o1 = ref ResourceSaved\n','alias':'o0 = ref o1\n'}[case]
 body='if $saved == 0\nResourceSaved = ref o0\n$saved = 1\nelif o1->width > 0\n'+commands+'endif\n'
 (mods/'mod.ini').write_text('namespace = mrt_outputs\n[Constants]\nglobal $saved = 0\n[ResourceSaved]\n[ShaderOverrideTarget]\nhash = %08x\n'%vertex+body,encoding='ascii')
 settings=root/'settings';settings.mkdir();pixels=root/'screen.raw';owned=root/'owned.raw'
 env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> native MRT outputs',EFMI_VK_TEST_NOTICES_READBACK=str(pixels),VK_LAYER_VALIDATE_SYNC='1')
 args=['--readable-swapchain','--api','1.3','--dynamic-rendering','2' if case=='khr' else '1','--native-output',str(owned),'--native-output-mrt']
 with Sandbox(seconds=4,threads=0,entry='inject' if case=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
  time.sleep(1)
  with ProbeClient(sb.pid) as probe:status=probe.status().get('replacement',{}).get('shader_overrides',{})
  sb.proc.wait(timeout=25);checks.equal(sb.proc.returncode,0,case+': normal exit')
  (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
 observed=(band_pixels(pixels),band_pixels(owned),band_pixels(Path(str(owned)+'.mrt.raw')))
 expected={'khr':(6418,6418,0),'inject':(6418,6418,0),'control':(6418,902,6418),'slot1':(6418,6418,0),'slot0':(0,6418,6418),'null1':(6418,902,0),
           'both':(0,6418,0),'restore':(6418,6418,0),'post':(6418,6418,902),'alias':(6418,902,6418)}[case]
 checks.equal(observed,expected,case+': exact independent screen/owned/second-color GPU geometry')
 if case not in ('control','alias'):checks.check(status.get('executed',0)>0 and not status.get('last_reason'),case+': output slot assignment executes without refusal')
 if case=='alias':checks.check('alias' in status.get('last_reason','') or 'overlap' in status.get('last_reason',''),case+': active output image alias refuses explicitly')
 checks.check(not any(marker in sb.output() for marker in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
 (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root,'outputs',observed)
checks=Checks('native MRT outputs');selected=set(sys.argv[1:])
for case in ('control','slot1','khr','inject','slot0','null1','both','restore','post','alias'):
 if not selected or case in selected:scene(checks,case)
raise SystemExit(checks.finish())
