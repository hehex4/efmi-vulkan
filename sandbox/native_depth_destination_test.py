"""Independent original and introduced DSV images establish actual output destinations."""
from pathlib import Path
import json,sys,time,struct,shutil
from harness import Checks,Sandbox,test_dir,DIST, configuration_root
from runtime_test import sandbox_shader_hashes,band_pixels
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
 root=Path(test_dir('native-depth-target-'+case+'-'));mods=root/'Mods';mods.mkdir();_,fragment=sandbox_shader_hashes()
 parts=case.split('_');stencil='stencil' in parts or 'combined' in parts;combined='combined' in parts;depthonly='depthonly' in parts;absent='absent' in parts
 mode=parts[-1]
 commands={'control':'','core':'oD = ref ResourceSource\n','khr':'oD = ref ResourceSource\n','inject':'oD = ref ResourceSource\n','swap':'oD = ref ResourceSource\n',
 'post':'post oD = ref ResourceSource\n','restore':'ResourceOriginal = ref oD\noD = null\npost oD = ref ResourceOriginal\n',
 'roundtrip':'ResourceOriginal = ref oD\noD = ref ResourceSource\npost oD = ref ResourceOriginal\n',
 'nullrestore':'oD = null\noD = ref ResourceSource\n'}[mode]
 body='if $saved == 0\nResourceSource = ref oD\n$saved = 1\n$source_draw = 1\nelse\n$source_draw = 0\nendif\nif $source_draw == 0\n'+commands+'endif\n'
 (mods/'mod.ini').write_text('namespace = depth_targets\n[Constants]\nglobal $saved = 0\nglobal $source_draw = 0\n[Present]\n$saved = 0\n[ResourceSource]\n[ResourceOriginal]\n[ShaderOverrideTarget]\nvk_hash = %08x\n'%fragment+body,encoding='ascii')
 settings=root/'settings';settings.mkdir();pixels=root/'screen.raw';owned=root/'owned.raw'
 env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> native depth targets',EFMI_VK_TEST_NOTICES_READBACK=str(pixels),VK_LAYER_VALIDATE_SYNC='1')
 args=['--readable-swapchain','--api','1.3','--dynamic-rendering','2' if mode=='khr' else '1','--native-output',str(owned),'--native-depth-destination','--native-output-depth-stencil' if combined else '--native-output-stencil' if stencil else '--native-output-depth']
 if depthonly:args+=['--native-output-no-color']
 if absent:args+=['--native-depth-absent']
 with Sandbox(seconds=12 if mode=='swap' else 4,threads=0,entry='inject' if mode=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
  time.sleep(1)
  with ProbeClient(sb.pid) as probe:
   if mode=='swap':
    module=root/'backend.dll';shutil.copy2(Path(DIST)/'efmi_vk_backend.dll',module);probe.swap_module(str(module));deadline=time.monotonic()+7;generation=0
    while time.monotonic()<deadline:
     generation=probe.status().get('replacement',{}).get('module',{}).get('generation',0)
     if generation>0:break
     time.sleep(.05)
    checks.check(generation>0,case+': requested module generation exchange completed');time.sleep(.3)
   status=probe.status().get('replacement',{}).get('shader_overrides',{})
  sb.proc.wait(timeout=25);checks.equal(sb.proc.returncode,0,case+': normal exit')
  (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
 meta_path=Path(str(owned)+'.json');meta=json.loads(meta_path.read_text()) if meta_path.exists() else {}
 width,height=meta.get('width',0),meta.get('height',0);size=1 if stencil else 4
 actual=[]
 for label,suffix,want in [('original','.stencil.raw' if stencil else '.depth.raw',0 if absent else 6418 if mode=='control' else 902 if mode=='post' else 0),
                          ('source','.source-stencil.raw' if stencil else '.source-depth.raw',902 if mode in ('control','restore') else 6418)]:
  path=Path(str(owned)+suffix);data=path.read_bytes() if path.exists() else b''
  checks.check(bool(width and height) and len(data)==width*height*size,case+': complete fenced '+label+' aspect readback')
  count=sum(data[y*width+x]==7 if stencil else struct.unpack_from('<f',data,4*(y*width+x))[0]<.99 for y in range(int(height*.46),int(height*.54)) for x in range(width)) if width and len(data)==width*height*size else None
  checks.equal(count,want,case+': '+label+' actual aspect follows pre/post binding order');actual.append(count)
 checks.equal(band_pixels(pixels),0 if depthonly else 6418,case+': original color remains intact during depth changes')
 checks.equal(band_pixels(owned),902,case+': earlier source color remains intact')
 if mode!='control':checks.check(status.get('executed',0)>0 and not status.get('last_reason'),case+': retained DSV binding executes without refusal')
 checks.check(not any(marker in sb.output() for marker in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
 (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root,'original/source aspects',actual)
checks=Checks('native depth destinations');selected=set(sys.argv[1:])
for case in ('control','core','khr','inject','post','restore','roundtrip','nullrestore','stencil_control','stencil_core','stencil_roundtrip','combined_control','combined_core','combined_roundtrip','depthonly_core','swap','absent_control','absent_core','absent_stencil_core','absent_combined_core'):
 if not selected or case in selected:scene(checks,case)
raise SystemExit(checks.finish())
