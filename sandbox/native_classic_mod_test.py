"""Actual Mod draws retain their geometry and shaders inside masked classic passes."""
from pathlib import Path
import sys,json,time,struct,shutil
from harness import Checks,Sandbox,test_dir,DIST, configuration_root
from efmi_vk.hashes import crc32c
from runtime_test import sandbox_shader_hashes,band_pixels
from native_shader_test import compile_pixel,compile_shader,colored_pixels
from efmi_vk.probe_client import ProbeClient

def scene(checks,case,matched=False):
 root=Path(test_dir('classic-null-'+case+'-'));mods=root/'Mods';mods.mkdir();vertex,fragment=sandbox_shader_hashes()
 parts=case.split('_');mrt='mrt' in parts;stencil='stencil' in parts or 'combined' in parts;combined='combined' in parts;ordered='ordered' in parts;beside='beside' in parts
 depth=stencil or 'depth' in parts;mode=parts[-1];hunting=mode in ('original','pink','pinkclear');filtered='filter' in parts;shader=mode=='shader';owned=not depth and not mrt and mode in ('restore','once')
 target='oD' if depth else 'o1' if mrt else 'o0'
 command='' if mode=='control' else 'post '+target+' = null\n' if mode=='post' else 'ResourceOriginal = ref '+target+'\n'+target+' = null\npost '+target+' = ref ResourceOriginal\n' if mode in ('restore','once') else target+' = null\n'
 if beside and mode=='post':command=''
 if beside and mode=='restore':command=command.replace('post '+target+' = ref ResourceOriginal\n','')
 if mode=='builtin':command='run = BuiltInCommandListUnbindAllRenderTargets\n'
 if mode=='first':command='if $once == 0\n$once = 1\n'+command+'endif\n'
 if mode=='once':command='if $once == 0\n$restore_now = 1\n$once = 1\nelse\n$restore_now = 0\nendif\nif $restore_now == 1\n'+command+'endif\n'
 if mode!='control':command='if $once == 0\n$once = 1\n'+command+'endif\n'
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
 ini='namespace = classic_mesh\n[Constants]\nglobal $saved = 0\nglobal $source_draw = 0\nglobal $once = 0\nglobal $restore_now = 0\n[Present]\n$saved = 0\n$once = 0\n[ResourceOriginal]\n[ShaderOverrideTarget]\nhash = %08x\n'%vertex+('' if matched else body)+'checktextureoverride = ib\n'
 if filtered:ini+='[ShaderOverrideFilter]\nhash = %08x\ndepth_filter = depth_'%fragment+mode+'\n'
 (mods/'vertex.buf').write_bytes(struct.pack('<6f',.70,.55,.90,.55,.80,.35))
 (mods/'index.buf').write_bytes(struct.pack('<3H',0,1,2))
 ini+='[TextureOverrideMesh]\nhash = %08x\nmatch_first_index = 6\nmatch_index_count = 3\nhandling = skip\nvb0 = ResourceVertex\nib = ResourceIndex\ndrawindexed = 3,0,0\n[ResourceVertex]\ntype = Buffer\nstride = 8\nfilename = vertex.buf\n[ResourceIndex]\ntype = Buffer\nformat = R16_UINT\nfilename = index.buf\n'%crc32c(struct.pack('<3I',14,15,16))
 if matched:ini=ini.replace('handling = skip\n','handling = skip\n'+command,1)
 if matched and ordered:
  ini=ini.replace('handling = skip\n'+command,'handling = skip\n',1)
  (mods/'left.buf').write_bytes(struct.pack('<6f',-.90,.55,-.70,.55,-.80,.35))
  (mods/'top.buf').write_bytes(struct.pack('<6f',-.10,-.35,.10,-.35,0,-.55))
  sequence='ResourceOriginal = ref '+target+'\nvb0 = ResourceLeft\nib = ResourceIndex\ndrawindexed = '+('0' if mode=='empty' else '3')+',0,0\n'
  if mode!='control':sequence+=target+' = null\n'
  sequence+='vb0 = ResourceVertex\ndrawindexed = 3,0,0\n'
  if mode=='restore':sequence+=target+' = ref ResourceOriginal\nvb0 = ResourceTop\ndrawindexed = 3,0,0\n'
  else:sequence+='post '+target+' = ref ResourceOriginal\n'
  ini=ini.replace('vb0 = ResourceVertex\nib = ResourceIndex\ndrawindexed = 3,0,0\n',sequence,1)
  ini+='[ResourceLeft]\ntype = Buffer\nstride = 8\nfilename = left.buf\n[ResourceTop]\ntype = Buffer\nstride = 8\nfilename = top.buf\n'
 if beside:
  ini=ini.replace('handling = skip\n','')
  if 'empty' in parts and not ordered:ini=ini.replace('drawindexed = 3,0,0\n','drawindexed = 0,0,0\n',1)
  if mode in ('post','restore'):
   # A shader post list runs even after the matched mesh changes the index buffer.
   post='post if $once == 1\npost '+target+(' = null\n' if mode=='post' else ' = ref ResourceOriginal\n')+'post vb0 = ResourceTop\npost drawindexed = 3,0,0\npost endif\n'
   header='[ShaderOverrideTarget]\nhash = %08x\n'%vertex
   ini=ini.replace(header,header+post,1)
   (mods/'top.buf').write_bytes(struct.pack('<6f',-.10,-.35,.10,-.35,0,-.55))
   ini+='[ResourceTop]\ntype = Buffer\nstride = 8\nfilename = top.buf\n'
 (mods/'mod.ini').write_text(ini,encoding='ascii')
 env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(settings),EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> classic null',EFMI_VK_TEST_NOTICES_READBACK=str(screen),VK_LAYER_VALIDATE_SYNC='1')
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
   replacement=probe.status().get('replacement',{});status=replacement.get('shader_overrides',{})
   time.sleep(.2);trigger.touch()
  sb.proc.wait(timeout=25);checks.equal(sb.proc.returncode,0,case+': normal exit')
  (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
 wanted=(6418 if mode in ('control','post') else 5516) if beside else 5516 if matched else 6418 if mode=='control' else 902 if mode=='post' else 5516 if mode=='once' else 0
 counts=[band_pixels(screen),band_pixels(source)]
 checks.equal(counts[0],6418 if beside else 5516,case+': original geometry is retained beside the Mod or hidden as requested')
 checks.check(replacement.get('kind')=='configuration' and replacement.get('configuration_generation',{}).get('ini',{}).get('draws',0)>0,
              case+': shared original registry executes the draw lists')
 meta=json.loads(Path(str(screen)+'.json').read_text());width,height=meta['width'],meta['height'];pixels=screen.read_bytes()
 checks.equal(len(pixels),width*height*4,case+': complete screen readback')
 clear=pixels[4*((height-2)*width+width-2):][:3]
 region=[pixels[4*(y*width+x):][:3] for y in range(int(height*.64),int(height*.8)) for x in range(int(width*.84),int(width*.96))]
 moved=sum(max(abs(a-b) for a,b in zip(pixel,clear))>12 for pixel in region)
 pink=sum(pixel==bytes((255,0,255)) for pixel in region)
 checks.equal(moved>200,not (beside and 'empty' in parts and not ordered),case+': independent Mod geometry is present unless its draw is empty')
 if ordered or (beside and mode in ('post','restore')):
  def colored_region(data,lo,hi,ylo=.64,yhi=.8):
   background=data[4*((height-2)*width+width-2):][:3]
   return sum(max(abs(a-b) for a,b in zip(data[4*(y*width+x):][:3],background))>12 for y in range(int(height*ylo),int(height*yhi)) for x in range(int(width*lo),int(width*hi)))
  if ordered:checks.equal(colored_region(pixels,.04,.16)>200,mode!='empty',case+': independent first mesh precedes the output change or is empty')
  if mode=='restore' or (beside and mode=='post'):checks.check(colored_region(pixels,.44,.56,.2,.34)>200,case+': independent third mesh executes after the application draw')
 if shader or hunting or filtered:checks.equal(pink>200,shader or mode=='pink' or (filtered and mode==('active' if matched else 'inactive')),case+': moved Mod geometry follows the selected shader and actual depth filter')
 checks.equal(counts[1],902,case+': independent source color remains intact')
 if owned or mrt:
  value=band_pixels(Path(str(source)+('.original.raw' if owned else '.mrt.raw')));counts.append(value);checks.equal(value,wanted,case+': actual targeted owned color stops and restores at the requested boundary')
  if ordered and mrt:
   data=Path(str(source)+'.mrt.raw').read_bytes()
   checks.equal(len(data),width*height*4,case+': complete independent MRT readback')
   checks.equal(colored_region(data,.04,.16)>200,mode!='empty',case+': first mesh reaches the original MRT before unbind')
   checks.equal(colored_region(data,.84,.96)>200,mode=='control',case+': second mesh reaches MRT only while still bound')
   if mode=='restore':checks.check(colored_region(data,.44,.56,.2,.34)>200,case+': third mesh reaches the restored MRT')
 if depth:
  meta_path=Path(str(source)+'.json');meta=json.loads(meta_path.read_text()) if meta_path.exists() else {};width,height=meta.get('width',0),meta.get('height',0);size=1 if stencil else 4
  for label,suffix,want in [('original','.stencil.raw' if stencil else '.depth.raw',wanted),('source','.source-stencil.raw' if stencil else '.source-depth.raw',902)]:
   path=Path(str(source)+suffix);data=path.read_bytes() if path.exists() else b''
   checks.check(bool(width and height) and len(data)==width*height*size,case+': complete fenced '+label+' aspect')
   count=sum(data[y*width+x]==7 if stencil else struct.unpack_from('<f',data,4*(y*width+x))[0]<.99 for y in range(int(height*.46),int(height*.54)) for x in range(width)) if width and len(data)==width*height*size else None
   checks.equal(count,6418 if beside else 5516,case+': control original depth follows the requested original geometry') if mode=='control' and label=='original' else checks.equal(count,want,case+': '+label+' aspect follows ordered bindings');counts.append(count)
   if matched and label=='original' and not stencil:
    moved_depth=sum(struct.unpack_from('<f',data,4*(y*width+x))[0]<.99 for y in range(int(height*.64),int(height*.8)) for x in range(int(width*.84),int(width*.96)))
    checks.equal(moved_depth>200,mode in ('control','post') and not (beside and 'empty' in parts and not ordered),case+': independent moved geometry writes depth only before the requested unbind');counts.append(moved_depth)
    if ordered or (beside and mode in ('post','restore')):
     def depth_region(lo,hi,ylo=.64,yhi=.8):
      return sum(struct.unpack_from('<f',data,4*(y*width+x))[0]<.99 for y in range(int(height*ylo),int(height*yhi)) for x in range(int(width*lo),int(width*hi)))
     if ordered:
      left_depth=depth_region(.04,.16);checks.equal(left_depth>200,mode!='empty',case+': first mesh depth remains after the ordered unbind');counts.append(left_depth)
     if mode=='restore' or (beside and mode=='post'):checks.equal(depth_region(.44,.56,.2,.34)>200,mode=='restore',case+': third mesh observes the post-phase null or restored depth')
 if hunting:checks.equal(colored_pixels(screen,(255,0,255)),5516 if mode=='pink' else 0,case+': compatible pipeline preserves marking and clear restoration')
 if filtered:checks.equal(colored_pixels(screen,(255,0,255)),5516 if matched and mode=='active' else 0 if matched else 902 if mode=='active' else 4614,case+': current shader selection survives unbind and next draw refreshes the actual depth filter')
 elif shader:checks.equal(colored_pixels(screen,(255,0,255)),5516,case+': compatible pipeline preserves selected replacement shader')
 if mode!='control' and not matched:checks.check(status.get('executed',0)>0 and not status.get('last_reason'),case+': null binding executes without refusal')
 checks.check(not any(marker in sb.output() for marker in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
 (root/'status.json').write_text(json.dumps(replacement,indent=2),encoding='ascii');print('artifacts:',root,'color/aspect counts',counts)
 return replacement
if __name__=='__main__':
 checks=Checks('native classic Mod variants');selected=set(sys.argv[1:])
 for case in ('depth_mesh_control','depth_mesh_null','mrt_mesh_null','depth_mesh_shader','depth_mesh_original','depth_mesh_pink','depth_mesh_pinkclear','depth_mesh_swap','depth_mesh_filter_active','depth_mesh_filter_inactive'):
  if not selected or case in selected:scene(checks,case)
 raise SystemExit(checks.finish())
