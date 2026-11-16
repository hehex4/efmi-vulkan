"""Classic null bindings require real unused attachment references and compatible selected shaders."""
from pathlib import Path
import sys,json,time,struct,shutil
from harness import Checks,Sandbox,test_dir,DIST, configuration_root
from runtime_test import sandbox_shader_hashes,band_pixels
from native_shader_test import compile_pixel,compile_shader,colored_pixels
from efmi_vk.probe_client import ProbeClient

def scene(checks,case):
 root=Path(test_dir('classic-null-'+case+'-'));mods=root/'Mods';mods.mkdir();vertex,fragment=sandbox_shader_hashes()
 parts=case.split('_');mrt='mrt' in parts;stencil='stencil' in parts or 'combined' in parts;combined='combined' in parts
 depth=stencil or 'depth' in parts;mode=parts[-1];hunting=mode in ('original','pink','pinkclear');filtered='filter' in parts;shader=mode=='shader';owned=not depth and not mrt and mode in ('restore','once')
 target='oD' if depth else 'o1' if mrt else 'o0'
 command='' if mode=='control' else 'post '+target+' = null\n' if mode=='post' else 'ResourceOriginal = ref '+target+'\n'+target+' = null\npost '+target+' = ref ResourceOriginal\n' if mode in ('restore','once') else target+' = null\n'
 if mode=='builtin':command='run = BuiltInCommandListUnbindAllRenderTargets\n'
 if mode=='first':command='if $once == 0\n$once = 1\n'+command+'endif\n'
 if mode=='once':command='if $once == 0\n$restore_now = 1\n$once = 1\nelse\n$restore_now = 0\nendif\nif $restore_now == 1\n'+command+'endif\n'
 body='if $saved == 0\n$saved = 1\n$source_draw = 1\nelse\n$source_draw = 0\nendif\nif $source_draw == 0\n'+command+'endif\n'
 settings=root/'settings';settings.mkdir();screen=root/'screen.raw';source=root/'source.raw'
 if shader and mrt:
  with Sandbox(seconds=2,threads=0,with_layer=False,extra_args=['--api','1.3','--native-output',str(root/'identity.raw'),'--native-output-classic','1','--native-output-mrt']) as identity:
   fragment=next(int(stage['hash'],16) for stage in identity.expected['shaders'] if stage['stage']==16);identity.proc.wait(timeout=30)
 if filtered or shader or mode=='original':
  fixes=settings/'ShaderFixes';fixes.mkdir()
  if shader and mrt:
   hlsl=root/'mrt.hlsl';hlsl.write_text('struct Output { float4 a : SV_Target0; float4 b : SV_Target1; }; Output main() { Output o; o.a=o.b=float4(1,0,1,1); return o; }',encoding='ascii')
   compile_shader(hlsl,fixes,'%08x'%fragment,'ps')
  else:(fixes/('%08x-ps_replace.spv'%fragment)).write_bytes(compile_pixel(root,'1,0,1'))
 ini='namespace = classic_null\n[Constants]\nglobal $saved = 0\nglobal $source_draw = 0\nglobal $once = 0\nglobal $restore_now = 0\n[Present]\n$saved = 0\n$once = 0\n[ResourceOriginal]\n[ShaderOverrideTarget]\nhash = %08x\n'%vertex+body
 if filtered:ini+='[ShaderOverrideFilter]\nhash = %08x\ndepth_filter = depth_'%fragment+mode+'\n'
 (mods/'mod.ini').write_text(ini,encoding='ascii')
 env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> classic null',EFMI_VK_TEST_NOTICES_READBACK=str(screen),VK_LAYER_VALIDATE_SYNC='1')
 if hunting or mode=='swap':
  trigger=root/'readback.trigger';env['EFMI_VK_TEST_NOTICES_READBACK_TRIGGER']=str(trigger)
 if hunting:
  keys=root/'keys.txt';keys.write_text('1\n',encoding='ascii');env['EFMI_VK_TEST_KEYS']=str(keys)
  (root/'d3dx.ini').write_text('[Include]\ninclude_recursive = Mods\n[Rendering]\nrecursive_include = 1\n[Hunting]\nhunting = 1\nmarking_mode = '+('original' if mode=='original' else 'pink')+'\nnext_pixelshader = no_modifiers VK_NUMPAD2\ndone_hunting = no_modifiers VK_ADD\n',encoding='ascii')
 args=['--readable-swapchain','--api','1.3','--native-output',str(source),'--native-output-classic','2' if mode=='rp2' else '1']
 if mrt:args+=['--native-output-mrt']
 if owned:args+=['--native-output-owned']
 if depth:args+=['--native-depth-destination','--native-output-depth-stencil' if combined else '--native-output-stencil' if stencil else '--native-output-depth']
 if mode=='imageless':args+=['--imageless']
 with Sandbox(seconds=12 if mode=='swap' or hunting else 4,threads=0,entry='inject' if mode=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
  time.sleep(1)
  with ProbeClient(sb.pid) as probe:
   if hunting:
    keys.write_text('1 98\n',encoding='ascii');deadline=time.monotonic()+7;selection={}
    while time.monotonic()<deadline:
     selection=probe.status().get('replacement',{}).get('hunting',{}).get('selected',{})
     if selection.get('pixel'):break
     time.sleep(.05)
    checks.equal(selection.get('pixel'),'%08x'%fragment,case+': hunting selected the actual fragment shader');keys.write_text('1\n',encoding='ascii')
    if mode=='pinkclear':
     time.sleep(.2);keys.write_text('1 107\n',encoding='ascii');time.sleep(.2);keys.write_text('1\n',encoding='ascii')
   if mode=='swap':
    module=root/'backend.dll';shutil.copy2(Path(DIST)/'efmi_vk_backend.dll',module);probe.swap_module(str(module));deadline=time.monotonic()+7;generation=0
    while time.monotonic()<deadline:
     generation=probe.status().get('replacement',{}).get('module',{}).get('generation',0)
     if generation>0:break
     time.sleep(.05)
    checks.check(generation>0,case+': module generation exchange completed');time.sleep(.3)
   status=probe.status().get('replacement',{}).get('shader_overrides',{})
   if hunting or mode=='swap':time.sleep(.2);trigger.touch()
  sb.proc.wait(timeout=25);checks.equal(sb.proc.returncode,0,case+': normal exit')
  (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
 wanted=6418 if mode=='control' else 902 if mode=='post' else 5516 if mode=='once' else 0
 counts=[band_pixels(screen),band_pixels(source)]
 checks.equal(counts[0],0 if owned or mode=='builtin' else 6418 if depth or mrt else wanted,case+': original first color follows ordered bindings')
 checks.equal(counts[1],902,case+': independent source color remains intact')
 if owned or mrt:
  value=band_pixels(Path(str(source)+('.original.raw' if owned else '.mrt.raw')));counts.append(value);checks.equal(value,wanted,case+': actual targeted owned color stops and restores at the requested boundary')
 if depth:
  meta_path=Path(str(source)+'.json');meta=json.loads(meta_path.read_text()) if meta_path.exists() else {};width,height=meta.get('width',0),meta.get('height',0);size=1 if stencil else 4
  for label,suffix,want in [('original','.stencil.raw' if stencil else '.depth.raw',wanted),('source','.source-stencil.raw' if stencil else '.source-depth.raw',902)]:
   path=Path(str(source)+suffix);data=path.read_bytes() if path.exists() else b''
   checks.check(bool(width and height) and len(data)==width*height*size,case+': complete fenced '+label+' aspect')
   count=sum(data[y*width+x]==7 if stencil else struct.unpack_from('<f',data,4*(y*width+x))[0]<.99 for y in range(int(height*.46),int(height*.54)) for x in range(width)) if width and len(data)==width*height*size else None
   checks.equal(count,want,case+': '+label+' aspect follows ordered bindings');counts.append(count)
 if hunting:checks.equal(colored_pixels(screen,(255,0,255)),6418 if mode=='pink' else 0,case+': compatible pipeline preserves marking and clear restoration')
 if filtered:checks.equal(colored_pixels(screen,(255,0,255)),902 if mode=='active' else 5516,case+': current shader selection survives unbind and next draw refreshes the actual depth filter')
 elif shader:checks.equal(colored_pixels(screen,(255,0,255)),6418,case+': compatible pipeline preserves selected replacement shader')
 if mode!='control':checks.check(status.get('executed',0)>0 and not status.get('last_reason'),case+': null binding executes without refusal')
 checks.check(not any(marker in sb.output() for marker in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
 (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii');print('artifacts:',root,'color/aspect counts',counts)
checks=Checks('native classic null');selected=set(sys.argv[1:])
for case in ('color_control','color_first','color_pre','color_post','color_restore','color_once','color_rp2','color_imageless','color_inject','color_swap','mrt_control','mrt_pre','mrt_post','mrt_restore','mrt_shader','depth_control','depth_first','depth_builtin','mrt_combined_builtin','depth_original','depth_pink','depth_pinkclear','depth_pre','depth_post','depth_restore','depth_once','stencil_pre','combined_pre','combined_post','combined_restore','depth_filter_active','depth_filter_inactive'):
 if not selected or case in selected:scene(checks,case)
raise SystemExit(checks.finish())
