"""Classic multiple outputs and depth/stencil with independent fenced image readbacks."""
from pathlib import Path
import sys,json,time,struct,shutil
from harness import Checks,Sandbox,test_dir,DIST
from runtime_fixture import CORE
from runtime_test import sandbox_shader_hashes,band_pixels
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
 root=Path(test_dir('classic-attachments-'+case+'-'));mods=root/'Mods';mods.mkdir();vertex,_=sandbox_shader_hashes()
 parts=case.split('_');mrt='mrt' in parts;stencil='stencil' in parts or 'combined' in parts;combined='combined' in parts
 depth=stencil or 'depth' in parts;mode=parts[-1];dsv=mode in ('dsv','dsvpost','dsvrestore');target='oD' if dsv else 'o1' if mrt else 'o0'
 command='' if mode=='control' else 'post '+target+' = ref ResourceSource\n' if mode in ('post','dsvpost') else 'ResourceOriginal = ref '+target+'\n'+target+' = ref ResourceSource\npost '+target+' = ref ResourceOriginal\n' if mode in ('restore','dsvrestore') else target+' = ref ResourceSource\n'
 body='if $saved == 0\nResourceSource = ref '+('oD' if dsv else 'o0')+'\n$saved = 1\n$source_draw = 1\nelse\n$source_draw = 0\nendif\nif $source_draw == 0\n'+command+'endif\n'
 (mods/'mod.ini').write_text('namespace = classic_attachments\n[Constants]\nglobal $saved = 0\nglobal $source_draw = 0\n[Present]\n$saved = 0\n[ResourceSource]\n[ResourceOriginal]\n[ShaderOverrideTarget]\nhash = %08x\n'%vertex+body,encoding='ascii')
 settings=root/'settings';settings.mkdir();screen=root/'screen.raw';owned=root/'owned.raw'
 env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> classic attachments',EFMI_VK_TEST_NOTICES_READBACK=str(screen),VK_LAYER_VALIDATE_SYNC='1')
 args=['--readable-swapchain','--api','1.3','--native-output',str(owned),'--native-output-classic','2' if mode=='rp2' else '1']
 if mrt:args+=['--native-output-mrt']
 if depth:args+=['--native-depth-destination','--native-output-depth-stencil' if combined else '--native-output-stencil' if stencil else '--native-output-depth']
 if mode=='imageless':args+=['--imageless']
 with Sandbox(seconds=12 if mode=='swap' else 4,threads=0,entry='inject' if mode=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
  time.sleep(1)
  with ProbeClient(sb.pid) as probe:
   if mode=='swap':
    module=root/'backend.dll';shutil.copy2(Path(DIST)/'efmi_vk_backend.dll',module);probe.swap_module(str(module));deadline=time.monotonic()+7;generation=0
    while time.monotonic()<deadline:
     generation=probe.status().get('replacement',{}).get('module',{}).get('generation',0)
     if generation>0:break
     time.sleep(.05)
    checks.check(generation>0,case+': module generation exchange completed');time.sleep(.3)
   status=probe.status().get('replacement',{}).get('shader_overrides',{})
  sb.proc.wait(timeout=25);checks.equal(sb.proc.returncode,0,case+': normal exit')
  (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
 color_changed=mode!='control' and not dsv
 wanted=902 if mode=='post' else 0
 counts=[band_pixels(screen),band_pixels(owned)]
 checks.equal(counts[0],wanted if color_changed and not mrt else 6418,case+': original first color follows ordered bindings')
 checks.equal(counts[1],6418 if color_changed else 902,case+': source color retains or receives the expected geometry')
 if mrt:
  value=band_pixels(Path(str(owned)+'.mrt.raw'));counts.append(value);checks.equal(value,wanted if color_changed else 6418,case+': original second color follows ordered bindings')
 if depth:
  meta_path=Path(str(owned)+'.json');meta=json.loads(meta_path.read_text()) if meta_path.exists() else {};width,height=meta.get('width',0),meta.get('height',0);size=1 if stencil else 4
  for label,suffix,want in [('original','.stencil.raw' if stencil else '.depth.raw',902 if mode=='dsvpost' else 0 if dsv else 6418),('source','.source-stencil.raw' if stencil else '.source-depth.raw',6418 if dsv else 902)]:
   path=Path(str(owned)+suffix);data=path.read_bytes() if path.exists() else b''
   checks.check(bool(width and height) and len(data)==width*height*size,case+': complete fenced '+label+' aspect')
   count=sum(data[y*width+x]==7 if stencil else struct.unpack_from('<f',data,4*(y*width+x))[0]<.99 for y in range(int(height*.46),int(height*.54)) for x in range(width)) if width and len(data)==width*height*size else None
   checks.equal(count,want,case+': '+label+' aspect follows ordered bindings');counts.append(count)
 if mode!='control':checks.check(status.get('executed',0)>0 and not status.get('last_reason'),case+': native bindings execute without refusal')
 checks.check(not any(marker in sb.output() for marker in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
 (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root,'color/aspect counts',counts)
checks=Checks('native classic attachments');selected=set(sys.argv[1:])
for case in ('mrt_control','mrt_slot1','mrt_post','mrt_restore','mrt_rp2','mrt_imageless','mrt_inject','depth_control','depth_color','depth_dsv','depth_dsvpost','depth_dsvrestore','stencil_control','stencil_color','stencil_dsv','combined_control','combined_color','combined_dsv','combined_dsvrestore','mrt_depth_color','mrt_combined_dsv','mrt_swap'):
 if not selected or case in selected:scene(checks,case)
raise SystemExit(checks.finish())
