"""Actual destination rebinding preserves stored passes, layout restoration and runtime exchange."""
from pathlib import Path
import json,sys,time,shutil,struct
from harness import Checks,Sandbox,test_dir,DIST
from runtime_fixture import CORE
from runtime_test import sandbox_shader_hashes,band_pixels
from efmi_vk.probe_client import ProbeClient
from native_shader_test import compile_shader,colored_pixels

def black_pixels(path):
 if not path.is_file() or not Path(str(path)+'.json').is_file():return None
 meta=json.loads(Path(str(path)+'.json').read_text());w,h=meta['width'],meta['height'];data=path.read_bytes()
 if len(data)!=w*h*4:return None
 return sum(data[4*(y*w+x):4*(y*w+x)+3]==bytes(3) for y in range(int(h*.46),int(h*.54)) for x in range(w))

def scene(checks,case,constants=False,buffers=False):
 root=Path(test_dir(('native-output-buffer-' if buffers else 'native-output-constant-' if constants else 'native-output-')+case+'-'));mods=root/'Mods';mods.mkdir();_,fragment=sandbox_shader_hashes()
 body='if $saved == 0\nResourceSaved = ref o0\n$saved = 1\n'
 body+=('elif o0->bindflags == 32\no0 = ref ResourceSaved\n' if case=='once' else ('else\npost o0 = ref ResourceSaved\n' if case=='post' else ('else\no0 = null\n' if case=='null' else ('else\no0 = ref ResourceSaved\n' if case!='control' else ''))))+'endif\n'
 declarations=''
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
 (mods/'mod.ini').write_text('namespace = native_output\n[Constants]\nglobal $saved = 0\n[ResourceSaved]\n[ShaderOverrideTarget]\nhash = %08x\n'%fragment+body+declarations,encoding='ascii')
 settings=root/'settings';settings.mkdir();pixels=root/'screen.raw';owned=root/'owned.raw'
 if constants and case in ('file','file_before'):
  fixes=settings/'ShaderFixes';fixes.mkdir()
  shader=root/'uniform.hlsl';shader.write_text('[[vk::binding(0,0)]] cbuffer U { float4 offset; float4 color; };\nfloat4 main() : SV_Target0 { return color.z > .9 ? float4(0,1,0,1) : float4(1,0,1,1); }\n',encoding='ascii')
  compile_shader(shader,fixes,'%08x'%fragment,'ps')
 trigger=root/'capture.trigger';keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii')
 env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> native output destinations',EFMI_VK_TEST_NOTICES_READBACK=str(pixels),VK_LAYER_VALIDATE_SYNC='1',EFMI_VK_TEST_KEYS=str(keys))
 if case in ('reload','swap'):env['EFMI_VK_TEST_NOTICES_READBACK_TRIGGER']=str(trigger)
 args=['--readable-swapchain','--api','1.3','--dynamic-rendering','2' if case=='khr' else '1','--native-output',str(owned)]
 if case=='query':args+=['--native-output-query']
 if case=='sync2':args+=['--submit2','1']
 if buffers and case=='push':args+=['--descriptors','push']
 if case in ('classic','renderpass2'):
  args=['--readable-swapchain','--api','1.3','--native-output',str(owned),'--native-output-classic','2' if case=='renderpass2' else '1']
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
 elif buffers and case in ('black','null_buffer'):
  black=black_pixels(owned)
  checks.check(visible is not None and visible<30 and redirected is not None and redirected>6000 and black is not None and black>6000,case+': zero SRV values produce exact black geometry in the redirected target')
  checks.check(status.get('shader_overrides',{}).get('executed',0)>0 and not status.get('shader_overrides',{}).get('last_reason'),case+': combined buffer and output list executes')
 elif case in ('query','sync2','null','wrong_kind','image_mixed','application','bad_stride'):
  checks.check(visible is not None and redirected is not None and visible>4000 and 100<redirected<1500,case+': unsupported destination change preserves both original GPU results')
  reason=status.get('shader_overrides',{}).get('last_reason','')
  checks.check({'query':'active query','sync2':'legacy synchronization','null':'enabled unused attachments','wrong_kind':'kind','image_mixed':'sequential alias preparation','application':'writable buffers','bad_stride':'stride'}[case] in reason,case+': precise boundary refusal is reported')
 else:
  checks.check(visible is not None and (500<visible<1500 if case=='post' else visible<30),case+': target switch follows pre/post command order')
  checks.check(redirected is not None and redirected>(500 if case in ('file','file_before') else 2000),case+': actual owned destination receives main geometry')
  checks.check(status.get('shader_overrides',{}).get('executed',0)>0 and not status.get('shader_overrides',{}).get('last_reason'),case+': output destination assignment executes without refusal')
 if case in ('file','file_before'):
  checks.check(colored_pixels(owned,(0,255,0))>500 and colored_pixels(owned,(255,0,255))<20,
               case+': the published constant buffer controls pixels in the switched target')
 checks.check(not any(marker in sb.output() for marker in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
 (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root,'screen',visible,'owned',redirected)
def main(constants=False,buffers=False):
 checks=Checks('mixed output and buffer SRV bindings' if buffers else 'mixed output and constant-buffer bindings' if constants else 'native output destinations');selected=set(sys.argv[1:])
 cases=('control','core','before','khr','inject','post','classic','renderpass2','push','black','null_buffer','wrong_kind','image_mixed','application','bad_stride','swap') if buffers else ('control','core','before','khr','inject','post','classic','renderpass2','file','file_before','wrong_kind','image_mixed','swap') if constants else ('control','core','khr','inject','once','post','reload','swap','query','sync2','null')
 for case in cases:
  if not selected or case in selected:scene(checks,case,constants,buffers)
 return checks.finish()

if __name__=='__main__':
 raise SystemExit(main())
