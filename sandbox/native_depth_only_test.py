"""Actual depth/stencil unbind preserves color and original shader filter timing."""
from pathlib import Path
import json,sys,time,struct
from harness import Checks,Sandbox,test_dir, configuration_root
from runtime_test import sandbox_shader_hashes,band_pixels
from native_shader_test import compile_pixel,colored_pixels
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
 root=Path(test_dir('native-depth-only-'+case+'-'));mods=root/'Mods';mods.mkdir();_,fragment=sandbox_shader_hashes()
 stencil=case.startswith(('stencil','combined'));combined=case.startswith('combined')
 post=case.endswith('post');builtin=case.startswith('builtin');filtered=case.startswith('filter_')
 commands='' if case=='control' else ('post ' if post else '')+('run = BuiltInCommandListUnbindAllRenderTargets\n' if builtin else 'oD = null\n')
 body='if oD->bindflags == 64\n'+commands+'endif\n'
 settings=root/'settings';settings.mkdir();pixels=root/'screen.raw';owned=root/'owned.raw'
 if filtered:
  fixes=settings/'ShaderFixes';fixes.mkdir();(fixes/('%08x-ps_replace.spv'%fragment)).write_bytes(compile_pixel(root,'1,0,1'))
 (mods/'mod.ini').write_text('namespace = depth_null\n[ShaderOverrideTarget]\nvk_hash = %08x\n'%fragment+('depth_filter = depth_'+case.removeprefix('filter_')+'\n' if filtered else '')+body,encoding='ascii')
 env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> native depth null',EFMI_VK_TEST_NOTICES_READBACK=str(pixels),VK_LAYER_VALIDATE_SYNC='1')
 args=['--native-output-no-color','--readable-swapchain','--api','1.3','--dynamic-rendering','2' if case=='khr' else '1','--native-output',str(owned),'--native-output-depth-stencil' if combined else '--native-output-stencil' if stencil else '--native-output-depth']
 with Sandbox(seconds=4,threads=0,entry='inject' if case=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
  time.sleep(1)
  with ProbeClient(sb.pid) as probe:status=probe.status().get('replacement',{}).get('shader_overrides',{})
  sb.proc.wait(timeout=25);checks.equal(sb.proc.returncode,0,case+': normal exit')
  (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
 visible=band_pixels(pixels);source=band_pixels(owned)
 actual_file=Path(str(owned)+('.stencil.raw' if stencil else '.depth.raw'));data=actual_file.read_bytes() if actual_file.exists() else b''
 meta_path=Path(str(owned)+'.json');meta=json.loads(meta_path.read_text()) if meta_path.exists() else {}
 width,height=meta.get('width',0),meta.get('height',0);size=1 if stencil else 4
 checks.check(bool(width and height) and len(data)==width*height*size,case+': complete fenced actual aspect readback')
 count=sum(data[y*width+x]==7 if stencil else struct.unpack_from('<f',data,4*(y*width+x))[0]<.99 for y in range(int(height*.46),int(height*.54)) for x in range(width)) if width and len(data)==width*height*size else None
 checks.equal(count,6418 if case=='control' else 902 if post else 0,case+': actual aspect writes stop at the requested pre/post boundary')
 checks.equal(visible,0,case+': depth-only rendering has no actual color attachment')
 checks.equal(source,902,case+': earlier color-only source pass remains intact')
 if case!='control':checks.check(status.get('executed',0)>0 and not status.get('last_reason'),case+': depth/stencil unbind executes without refusal')
 if filtered:checks.equal(colored_pixels(pixels,(255,0,255)),902 if case=='filter_active' else 5516,case+': shader filter uses pre-list presence and refreshes at the following draw')
 checks.check(not any(marker in sb.output() for marker in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
 (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root,'screen',visible,'source',source,'aspect',count)
checks=Checks('native depth only');selected=set(sys.argv[1:])
for case in ('control','core','khr','inject','post','builtin','builtin_post','stencil','stencil_post','combined','combined_post'):
 if not selected or case in selected:scene(checks,case)
raise SystemExit(checks.finish())
