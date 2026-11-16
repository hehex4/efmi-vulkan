"""Actual color destinations in ordinary classic render passes."""
from pathlib import Path
import json,sys,time,shutil
from harness import Checks,Sandbox,test_dir,DIST, configuration_root
from runtime_test import sandbox_shader_hashes,band_pixels
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
 root=Path(test_dir('native-classic-output-'+case+'-'));mods=root/'Mods';mods.mkdir();_,fragment=sandbox_shader_hashes()
 command='' if case in ('control','owned_control') else 'post o0 = ref ResourceSource\n' if case=='post' else 'ResourceOriginal = ref o0\no0 = ref ResourceSource\npost o0 = ref ResourceOriginal\n' if case in ('restore','restore_once') else 'o0 = null\n' if case=='null' else 'o0 = ref ResourceSource\n'
 if case=='restore_once':command='if $once == 0\n$restore_now = 1\n$once = 1\nelse\n$restore_now = 0\nendif\nif $restore_now == 1\n'+command+'endif\n'
 body='if $saved == 0\nResourceSource = ref o0\n$saved = 1\n$source_draw = 1\nelse\n$source_draw = 0\nendif\nif $source_draw == 0\n'+command+'endif\n'
 (mods/'mod.ini').write_text('namespace = classic_outputs\n[Constants]\nglobal $saved = 0\nglobal $source_draw = 0\nglobal $once = 0\nglobal $restore_now = 0\n[Present]\n$saved = 0\n$once = 0\n[ResourceSource]\n[ResourceOriginal]\n[ShaderOverrideTarget]\nhash = %08x\n'%fragment+body,encoding='ascii')
 settings=root/'settings';settings.mkdir();pixels=root/'screen.raw';owned=root/'owned.raw'
 env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> native classic outputs',EFMI_VK_TEST_NOTICES_READBACK=str(pixels),VK_LAYER_VALIDATE_SYNC='1')
 if case=='analysis':
  (root/'d3dx.ini').write_text('[Include]\ninclude_recursive = Mods\n[Rendering]\nrecursive_include = 1\n[Hunting]\nhunting = 1\nanalyse_frame = no_modifiers VK_F9\nanalyse_options = dump_rt buf\n',encoding='ascii')
  keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii');env['EFMI_VK_TEST_KEYS']=str(keys)
 args=['--readable-swapchain','--api','1.3','--native-output',str(owned),'--native-output-classic','2' if case=='renderpass2' else '1']
 if case in ('owned_control','restore','restore_once'):args+=['--native-output-owned']
 if case=='imageless':args+=['--imageless']
 if case=='query':args+=['--native-output-query']
 if case=='sync2':args+=['--sync2']
 with Sandbox(seconds=12 if case=='swap' else 4,threads=0,entry='inject' if case=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
  time.sleep(1)
  with ProbeClient(sb.pid) as probe:
   if case=='swap':
    module=root/'backend.dll';shutil.copy2(Path(DIST)/'efmi_vk_backend.dll',module);probe.swap_module(str(module));deadline=time.monotonic()+7;generation=0
    while time.monotonic()<deadline:
     generation=probe.status().get('replacement',{}).get('module',{}).get('generation',0)
     if generation>0:break
     time.sleep(.05)
    checks.check(generation>0,case+': requested module exchange completed');time.sleep(.3)
   status=probe.status().get('replacement',{}).get('shader_overrides',{})
  if case=='analysis':keys.write_text('1 120\n',encoding='ascii');time.sleep(.2);keys.write_text('1\n',encoding='ascii')
  sb.proc.wait(timeout=25);checks.equal(sb.proc.returncode,0,case+': normal exit')
  (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
 guarded=case in ('query','sync2');screen,source=band_pixels(pixels),band_pixels(owned)
 checks.equal(screen,6418 if case=='control' or guarded else 902 if case=='post' else 0,case+': actual original color follows pre/post binding order')
 checks.equal(source,902 if case in ('control','owned_control','restore_once','null') or guarded else 6418,case+': independent owned output has the expected geometry')
 if guarded:checks.check(bool(status.get('last_reason')),case+': unsupported scope refuses without partial output changes')
 elif case not in ('control','owned_control'):checks.check(status.get('executed',0)>0 and not status.get('last_reason'),case+': classic output replacement executes without refusal')
 if case in ('owned_control','restore','restore_once'):checks.equal(band_pixels(Path(str(owned)+'.original.raw')),6418 if case=='owned_control' else 5516 if case=='restore_once' else 0,case+': independent original target verifies restoration and subsequent draws')
 checks.check(not any(marker in sb.output() for marker in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
 if case=='analysis':
  records=[(path.parent,json.loads(line)) for path in settings.glob('FrameAnalysis-*/resources.jsonl') for line in path.read_text().splitlines()]
  targets=[(folder,item) for folder,item in records if item.get('state')=='complete' and item.get('set')==0xffffffff and item.get('point')=='end-of-pass']
  counts=[]
  for folder,item in targets:
   path=folder/item['file'];Path(str(path)+'.json').write_text(json.dumps(item),encoding='ascii');counts.append(band_pixels(path))
  checks.equal(sorted(counts),[902,6418],case+': pass analysis captures source then actual private destination contents')
 (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root,'screen',screen,'source',source)
checks=Checks('native classic outputs');selected=set(sys.argv[1:])
for case in ('control','core','renderpass2','imageless','inject','post','owned_control','restore','restore_once','swap','analysis','query','sync2','null'):
 if not selected or case in selected:scene(checks,case)
raise SystemExit(checks.finish())
