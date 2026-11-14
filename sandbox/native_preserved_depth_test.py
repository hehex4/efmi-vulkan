"""Actual depth contents survive private color-output pass boundaries."""
from pathlib import Path
import json,sys,time,struct
from harness import Checks,Sandbox,test_dir
from runtime_fixture import CORE
from runtime_test import sandbox_shader_hashes,band_pixels
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
 root=Path(test_dir('native-depth-output-'+case+'-'));mods=root/'Mods';mods.mkdir();_,fragment=sandbox_shader_hashes()
 body='if $saved == 0\nResourceSaved = ref o0\n$saved = 1\n'
 commands='' if case=='control' else 'post o0 = null\npost o0 = ref ResourceSaved\n' if case=='post' else 'o0 = null\n' if case=='null' else 'o0 = ref ResourceSaved\n'
 body+='elif oD->bindflags == 64\n'+commands+'endif\n'
 (mods/'mod.ini').write_text('namespace = depth_outputs\n[Constants]\nglobal $saved = 0\n[ResourceSaved]\n[ShaderOverrideTarget]\nhash = %08x\n'%fragment+body,encoding='ascii')
 settings=root/'settings';settings.mkdir();pixels=root/'screen.raw';owned=root/'owned.raw'
 env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> native depth outputs',EFMI_VK_TEST_NOTICES_READBACK=str(pixels),VK_LAYER_VALIDATE_SYNC='1')
 args=['--readable-swapchain','--api','1.3','--dynamic-rendering','2' if case=='khr' else '1','--native-output',str(owned),'--native-output-depth']
 with Sandbox(seconds=4,threads=0,entry='inject' if case=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
  time.sleep(1)
  with ProbeClient(sb.pid) as probe:status=probe.status().get('replacement',{}).get('shader_overrides',{})
  sb.proc.wait(timeout=25);checks.equal(sb.proc.returncode,0,case+': normal exit')
  (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
 visible=band_pixels(pixels);redirected=band_pixels(owned)
 depth_file=Path(str(owned)+'.depth.raw');depth_bytes=depth_file.read_bytes() if depth_file.exists() else b''
 meta_path=Path(str(owned)+'.json');meta=json.loads(meta_path.read_text()) if meta_path.exists() else {}
 width,height=meta.get('width',0),meta.get('height',0)
 checks.check(bool(width and height) and len(depth_bytes)==width*height*4,case+': complete fenced depth readback')
 depth_pixels=sum(struct.unpack_from('<f',depth_bytes,4*(y*width+x))[0]<.99 for y in range(int(height*.46),int(height*.54)) for x in range(width)) if width and len(depth_bytes)==width*height*4 else None
 checks.equal(depth_pixels,6418,case+': exact stored depth geometry survives every color pass boundary')
 checks.equal(visible,6418 if case=='control' else 902 if case=='post' else 0,case+': original color follows binding order')
 checks.equal(redirected,902 if case in ('control','null') else 6418,case+': owned color follows binding order')
 if case!='control':checks.check(status.get('executed',0)>0 and not status.get('last_reason'),case+': color changes preserve active depth without refusal')
 checks.check(not any(marker in sb.output() for marker in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
 (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root,'screen',visible,'owned',redirected,'depth',depth_pixels)
checks=Checks('native preserved depth');selected=set(sys.argv[1:])
for case in ('control','core','khr','inject','post','null'):
 if not selected or case in selected:scene(checks,case)
raise SystemExit(checks.finish())
