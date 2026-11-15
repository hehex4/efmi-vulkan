"""Actual destination rebinding preserves stored passes, layout restoration and runtime exchange."""
from pathlib import Path
import json,sys,time,shutil,struct
from harness import Checks,Sandbox,test_dir,DIST
from runtime_fixture import CORE
from runtime_test import sandbox_shader_hashes,band_pixels
from efmi_vk.probe_client import ProbeClient
from native_shader_test import compile_shader,colored_pixels,wait_graphics_ready

def black_pixels(path):
 if not path.is_file() or not Path(str(path)+'.json').is_file():return None
 meta=json.loads(Path(str(path)+'.json').read_text());w,h=meta['width'],meta['height'];data=path.read_bytes()
 if len(data)!=w*h*4:return None
 return sum(data[4*(y*w+x):4*(y*w+x)+3]==bytes(3) for y in range(int(h*.46),int(h*.54)) for x in range(w))

def scene(checks,case,constants=False,buffers=False,images=False,null_images=False,memory=0,input_only=False,application_images=False):
 prefix='native-output-null-image-' if null_images else 'native-output-image-' if images else 'native-output-buffer-' if buffers else 'native-output-constant-' if constants else 'native-output-'
 if memory:prefix='native-output-memory-%d-'%memory
 if input_only:prefix='native-input-memory-%d-'%memory
 if application_images:prefix='native-output-application-image-'
 root=Path(test_dir(prefix+case+'-'));mods=root/'Mods';mods.mkdir();_,fragment=sandbox_shader_hashes()
 body='if $saved == 0\nResourceSaved = ref o0\n$saved = 1\n'
 body+=('elif o0->bindflags == 32\no0 = ref ResourceSaved\n' if case=='once' else ('else\npost o0 = ref ResourceSaved\n' if case=='post' else ('else\no0 = null\n' if case=='null' else ('else\no0 = ref ResourceSaved\n' if case!='control' else ''))))+'endif\n'
 declarations=''
 if application_images:
  command='ps-t0 = ref ps-t0\n'
  body='vk_binding = ps-t0, 1, 5\n'+(command+body if case=='before' else body+command)
 if constants:
  alias='vk_binding = ps-cb0, '+('1, 5' if case=='wrong_kind' else '0, 0')+'\n'
  command='ps-cb0 = ref ps-cb0\n'
  if case=='image_mixed':alias='vk_binding = ps-t0, 1, 5\n';command='ps-t0 = ref ps-t0\n'
  if case in ('file','file_before'):
   command='ps-cb0 = ResourceUniform\n'
   declarations='[ResourceUniform]\ntype = Buffer\nstride = 16\nbind_flags = constant_buffer\nfilename = uniform.buf\n'
   (mods/'uniform.buf').write_bytes(struct.pack('<24f',*([0,0,0,0,.2,.3,1,1]+[0]*16)))
  body=alias+(command+body if case in ('before','file_before') else body+command)
 if buffers:
  alias='vk_binding = ps-t2, '+('0, 0' if case=='wrong_kind' else '1, 2, 0')+'\n'
  command='ps-t2 = ResourceTint\n'
  declarations='[ResourceTint]\ntype = Buffer\nstride = %d\nbind_flags = shader_resource\nfilename = tint.buf\n'%(3 if case=='bad_stride' else 16)
  (mods/'tint.buf').write_bytes(struct.pack('<4f',*([0,0,0,1] if case=='black' else [1,1,1,1])))
  if case=='null_buffer':command='ps-t2 = null\n'
  if case=='application':command='ps-t2 = ref ps-t2\n'
  if case=='image_mixed':alias='vk_binding = ps-t0, 1, 5\n';command='ps-t0 = ref ps-t0\n'
  body=alias+(command+body if case=='before' else body+command)
 if images:
  from native_image_test import dds
  alias='vk_binding = ps-t0, '+('0, 0' if case=='wrong_kind' else '1, 5')+'\n'
  command='ps-t0 = ref ps-t0\n' if case=='image_mixed' else 'ps-t0 = ResourceTexture\n'
  declarations='[ResourceTexture]\nfilename = tint.dds\n'+('bind_flags = shader_resource unordered_access\n' if case=='mutable' else '')
  dds(mods/'tint.dds',(0,0,0,255) if case=='black' else (255,255,255,255),2 if case=='array' else 1)
  body=alias+(command+body if case=='before' else body+command)
 if null_images:
  alias='vk_binding = ps-t0, '+('0, 0' if case=='wrong_kind' else '1, 5')+'\n'
  command='ps-t0 = ref ps-t0\n' if case=='image_mixed' else 'ps-t0 = null\n'
  body=alias+(command+body if case=='before' else body+command)
 if input_only:
  body='vk_binding = ps-cb1, 1, 6\nif o0->bindflags == 40\nps-cb1 = ref ps-cb1\nendif\n' if case!='control' else ''
 (mods/'mod.ini').write_text('namespace = native_output\n[Constants]\nglobal $saved = 0\n[ResourceSaved]\n[ShaderOverrideTarget]\nhash = %08x\n'%fragment+body+declarations,encoding='ascii')
 settings=root/'settings';settings.mkdir();pixels=root/'screen.raw';owned=root/'owned.raw'
 if constants and case in ('file','file_before'):
  fixes=settings/'ShaderFixes';fixes.mkdir()
  shader=root/'uniform.hlsl';shader.write_text('[[vk::binding(0,0)]] cbuffer U { float4 offset; float4 color; };\nfloat4 main() : SV_Target0 { return color.z > .9 ? float4(0,1,0,1) : float4(1,0,1,1); }\n',encoding='ascii')
  compile_shader(shader,fixes,'%08x'%fragment,'ps')
 trigger=root/'capture.trigger';keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii')
 env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> native output destinations',EFMI_VK_TEST_NOTICES_READBACK=str(pixels),VK_LAYER_VALIDATE_SYNC='1',EFMI_VK_TEST_KEYS=str(keys))
 env['EFMI_VK_TEST_NOTICES_READBACK_TRIGGER']=str(trigger)
 args=['--readable-swapchain','--api','1.3','--dynamic-rendering','2' if case=='khr' else '1','--native-output',str(owned)]
 if case=='query':args+=['--native-output-query']
 if case=='sync2':args+=['--submit2','1']
 if (buffers or images or null_images or memory) and case=='push':args+=['--descriptors','push']
 if case in ('classic','renderpass2') or (application_images and case!='core'):
  args=['--readable-swapchain','--api','1.3','--native-output',str(owned),'--native-output-classic','2' if case=='renderpass2' else '1']
 if null_images:args+=['--mutable-sampler']
 if null_images and case=='no_feature':args+=['--no-null-descriptors']
 if memory:args+=['--native-output-memory',str(memory)]
 with Sandbox(seconds=12 if case in ('reload','swap') else 4,threads=0,entry='inject' if case=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
  time.sleep(1)
  with ProbeClient(sb.pid) as probe:
   if case=='swap':
    module=root/'backend.dll';shutil.copy2(Path(DIST)/'efmi_vk_backend.dll',module);probe.swap_module(str(module))
   elif case=='reload':
    if images:dds(mods/'tint.dds',(0,0,0,255))
    keys.write_text('1 121\n',encoding='ascii');time.sleep(.4);keys.write_text('1\n',encoding='ascii')
   if case in ('reload','swap'):
    deadline=time.monotonic()+7
    while time.monotonic()<deadline:
     status=probe.status().get('replacement',{})
     count=status.get('module' if case=='swap' else 'folder_generation',{}).get('generation' if case=='swap' else 'reloads',0)
     if count>0:break
     time.sleep(.05)
    checks.check(count>0,case+': requested generation exchange completed')
   status,ready=wait_graphics_ready(probe,lambda s:s.get('textures',{}).get('loading',0)==0,2)
   checks.check(ready,case+': file publication and recorded frames settle before capture')
   trigger.touch()
  sb.proc.wait(timeout=25);checks.equal(sb.proc.returncode,0,case+': normal exit')
  (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
 visible=band_pixels(pixels);redirected=band_pixels(owned)
 if case=='control':checks.check(visible is not None and redirected is not None and visible>3*redirected and redirected>100,case+': main and owned fixture each have their original GPU geometry')
 elif input_only:
  checks.check(visible==6418 and redirected==902,case+': input-only binding preserves both original GPU results')
  reason=status.get('shader_overrides',{}).get('last_reason','')
  checks.check('attachment memory' in reason if memory==1 else not reason and status.get('shader_overrides',{}).get('executed',0)>0,
               case+': input-only allocation overlap is refused' if memory==1 else case+': disjoint retained input binds without refusal')
 elif (buffers and case in ('black','null_buffer')) or (images and case in ('black','reload')):
  black=black_pixels(owned)
  checks.check(visible is not None and visible<30 and redirected is not None and redirected>6000 and black is not None and black>6000,case+': zero SRV values produce exact black geometry in the redirected target')
  checks.check(status.get('shader_overrides',{}).get('executed',0)>0 and not status.get('shader_overrides',{}).get('last_reason'),case+': combined SRV and output list executes')
 elif memory==1:
  checks.check(visible is not None and redirected is not None and visible>4000 and 100<redirected<1500,case+': alias refusal preserves original pixels')
  checks.check('overlaps input memory' in status.get('shader_overrides',{}).get('last_reason',''),case+': actual allocation overlap is refused')
 elif case in ('query','sync2','null','wrong_kind','application','bad_stride','array','mutable','no_feature'):
  checks.check(visible is not None and redirected is not None and visible>4000 and 100<redirected<1500,case+': unsupported destination change preserves both original GPU results')
  reason=status.get('shader_overrides',{}).get('last_reason','')
  checks.check({'query':'active query','sync2':'legacy synchronization','null':'enabled unused attachments','wrong_kind':'kind','application':'writable buffers','bad_stride':'stride','array':'shape','mutable':'compatible usage','no_feature':'nullDescriptor'}[case] in reason,case+': precise boundary refusal is reported')
 else:
  checks.check(visible is not None and (500<visible<1500 if case=='post' else visible<30),case+': target switch follows pre/post command order')
  checks.check(redirected is not None and redirected>(500 if case in ('file','file_before') else 2000),case+': actual owned destination receives main geometry')
  checks.check(status.get('shader_overrides',{}).get('executed',0)>0 and not status.get('shader_overrides',{}).get('last_reason'),case+': output destination assignment executes without refusal')
 if case in ('file','file_before'):
  checks.check(colored_pixels(owned,(0,255,0))>500 and colored_pixels(owned,(255,0,255))<20,
               case+': the published constant buffer controls pixels in the switched target')
 if null_images and case not in ('control','no_feature','wrong_kind','image_mixed'):
  checks.check((black_pixels(owned) or 0)>6000,case+': null image reads produce exact black geometry in the redirected target')
 if null_images and case=='no_feature':
  checks.check('null descriptors off' in (root/'layer.log').read_text(encoding='utf-8'),case+': actual device nullDescriptor feature is disabled')
 checks.check(not any(marker in sb.output() for marker in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
 if memory:checks.check('output storage: image size' in sb.output(),case+': actual driver allocation ranges were reported')
 (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root,'screen',visible,'owned',redirected)
 return status
def main(constants=False,buffers=False,images=False,null_images=False):
 label='mixed output and null image bindings' if null_images else 'mixed output and file image bindings' if images else 'mixed output and buffer SRV bindings' if buffers else 'mixed output and constant-buffer bindings' if constants else 'native output destinations'
 checks=Checks(label);selected=set(sys.argv[1:])
 if null_images:cases=('control','core','before','khr','inject','post','classic','renderpass2','push','no_feature','wrong_kind','image_mixed','swap')
 elif images:cases=('control','core','before','khr','inject','post','classic','renderpass2','push','black','array','wrong_kind','image_mixed','mutable','reload','swap')
 elif buffers:cases=('control','core','before','khr','inject','post','classic','renderpass2','push','black','null_buffer','wrong_kind','application','bad_stride','swap')
 elif constants:cases=('control','core','before','khr','inject','post','classic','renderpass2','file','file_before','wrong_kind','image_mixed','swap')
 else:cases=('control','core','khr','inject','once','post','reload','swap','query','sync2','null')
 for case in cases:
  if not selected or case in selected:scene(checks,case,constants,buffers,images,null_images)
 return checks.finish()

if __name__=='__main__':
 raise SystemExit(main())
