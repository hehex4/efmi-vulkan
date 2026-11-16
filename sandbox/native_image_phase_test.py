"""A target write becomes a later sampled input through ordered native commands."""
from pathlib import Path
import json,shutil,struct,sys,time
from harness import Checks,Sandbox,test_dir,DIST
from runtime_fixture import CORE
from runtime_test import sandbox_shader_hashes
from efmi_vk.probe_client import ProbeClient
from native_shader_test import compile_shader,colored_pixels,wait_graphics_ready
from native_image_test import dds


def scene(checks,case):
 label=case
 color_layout=case.startswith('color-')
 if color_layout:case=case[len('color-'):]
 dynamic=case.startswith('dynamic')
 if dynamic:case=case[len('dynamic'):].lstrip('-') or 'after'
 rp2=case=='rp2'
 if rp2:case='after'
 root=Path(test_dir('native-image-phase-'+label+'-'));mods=root/'Mods';mods.mkdir()
 settings=root/'settings';settings.mkdir();fixes=settings/'ShaderFixes';fixes.mkdir()
 _,fragment=sandbox_shader_hashes()
 shader=root/'sample.hlsl'
 shader.write_text('''[[vk::binding(0,0)]] cbuffer U { float4 offset; float4 color; };
[[vk::binding(5,1)]] [[vk::combinedImageSampler]] Texture2D<float4> Source;
[[vk::binding(5,1)]] [[vk::combinedImageSampler]] SamplerState SampleState;
float4 main() : SV_Target0 {
 if (color.w < -.5) return float4(0,1,0,1);
 return Source.SampleLevel(SampleState,float2(.1,.5),0);
}
''',encoding='ascii')
 compile_shader(shader,fixes,'%08x'%fragment,'ps')
 (mods/'control.buf').write_bytes(struct.pack('<8f',0,0,0,0,0,0,1,-1))
 dds(mods/'white.dds',(255,255,255,255))
 write='o0 = ref ResourcePaint\nps-cb0 = ResourceControl\ndraw = from_caller\n'
 leave='o0 = ref ResourceScreen\nps-cb0 = ref ResourceConstant\n'
 read='ps-t0 = ref ResourcePaint\ndraw = from_caller\nps-t0 = ResourceWhite\n'
 if case=='control':read='ps-t0 = ResourceWhite\ndraw = from_caller\n'
 if case=='overlap':commands=write+'ps-t0 = ref ResourcePaint\ndraw = from_caller\n'+leave
 elif case=='late_overlap':commands=write+leave+read.replace('ps-t0 = ResourceWhite\n','')+write
 elif case=='read_before':commands=read+write+leave
 elif case=='repeat':commands=read+write+leave+read
 elif case=='cycle':commands=write+leave+read+write+leave+read
 elif case=='post':commands=write+''.join('post '+line+'\n' for line in (leave+read).splitlines())
 else:commands=write+leave+read
 ini='''namespace = native_image_phase
[Constants]
global $seen = 0
global $source = 0
[Present]
$seen = 0
[ResourcePaint]
[ResourceScreen]
[ResourceConstant]
[ResourceWhite]
filename = white.dds
[ResourceControl]
type = Buffer
bind_flags = constant_buffer
filename = control.buf
[ShaderOverridePhase]
hash = %08x
handling = skip
vk_binding = ps-cb0, 0, 0
vk_binding = ps-t0, 1, 5
$source = $seen == 0
if $source
 $seen = 1
 ResourcePaint = ref o0
 ps-t0 = ResourceWhite
 draw = from_caller
else
 ResourceScreen = ref o0
 ResourceConstant = ref ps-cb0
%sendif
'''%(fragment,commands)
 (mods/'mod.ini').write_text(ini,encoding='ascii')
 screen=root/'screen.raw';owned=root/'owned.raw';trigger=root/'capture.trigger'
 env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(settings),
          EFMI_VK_M2_ENABLED='1',EFMI_VK_TEST_NOTICE='> ordered native image phases',
          EFMI_VK_TEST_NOTICES_READBACK=str(screen),EFMI_VK_TEST_NOTICES_READBACK_TRIGGER=str(trigger),
          VK_LAYER_VALIDATE_SYNC='1')
 if color_layout:env['EFMI_VK_TEST_PHASE_COLOR_LAYOUT']='1'
 args=['--readable-swapchain','--api','1.3','--native-output',str(owned),'--native-output-owned']
 args+=['--dynamic-rendering','1'] if dynamic else ['--native-output-classic','2' if rp2 else '1']
 with Sandbox(seconds=12,threads=0,entry='inject' if case=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
  with ProbeClient(sb.pid) as probe:
   if case=='swap':
    module=root/'backend.dll';shutil.copy2(Path(DIST)/'efmi_vk_backend.dll',module);probe.swap_module(str(module))
   status,ready=wait_graphics_ready(probe,lambda s:s.get('textures',{}).get('loading',0)==0 and
       s.get('shader_overrides',{}).get('executed',0)>0 and
       (case!='swap' or s.get('module',{}).get('generation',0)>0))
   checks.check(ready and status.get('shader_fixes',{}).get('binds',0)>0,case+': shader, files and recorded frames are ready')
   trigger.write_text('capture',encoding='ascii')
   deadline=time.monotonic()+3
   while not screen.exists() and time.monotonic()<deadline:time.sleep(.03)
   status=probe.status().get('replacement',{})
   (root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii')
  sb.proc.wait(timeout=25)
  checks.check(sb.proc.returncode==0,case+': sandbox exits normally')
  (root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'stdout.txt').write_text(sb.output(),encoding='utf-8')
  log=sb.output();checks.check(not any(s in log for s in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
 main=Path(str(owned)+'.original.raw')
 green=colored_pixels(main,(0,255,0));white=colored_pixels(main,(255,255,255));painted=colored_pixels(owned,(0,255,0))
 print(label,'screen green/white, target green:',green,white,painted,flush=True)
 reason=status.get('shader_overrides',{}).get('last_reason','')
 refused=case in ('overlap','late_overlap')
 checks.check(('overlaps' in reason) if refused else not reason,case+': '+('simultaneous target sampling is refused' if refused else 'ordered image plan executes without refusal'))
 if refused:checks.check(green==0 and colored_pixels(main,(103,100,97))>5000 and painted==0,case+': refusal retains original screen and target data')
 else:
  checks.check(painted>5000,case+': introduced target actually receives the new green draw data')
  # The first main draw reads the old white source, then paints its triangle.
  # Later draws sample that newly green triangle at the same fixed coordinate.
  expected=(white==902 and green==5516) if case=='read_before' else (white>5000 and green<20) if case=='control' else (green>5000 and white<20)
  checks.check(expected,case+': sampled screen data matches the selected read phase')
 if case=='swap':checks.check(status.get('module',{}).get('generation',0)>0,case+': module exchange completed')


def main():
 checks=Checks('native image phases')
 for case in sys.argv[1:] or ('control','after','read_before','repeat','post','overlap','late_overlap','inject','swap',
                            'color-control','color-after','color-cycle','color-post','color-overlap','color-late_overlap','color-inject','color-swap',
                            'color-rp2','color-dynamic','color-dynamic-cycle','color-dynamic-swap'):scene(checks,case)
 return checks.finish()


if __name__=='__main__':raise SystemExit(main())
