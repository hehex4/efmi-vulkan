"""Proven absent outputs must not prevent otherwise supported native compute calls."""
from pathlib import Path
import json,sys,struct,time
from harness import Checks,Sandbox,test_dir, configuration_root
from native_compute_test import compile_shader,read_compute_result
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient

def main():
 checks=Checks('native absent outputs');selected=set(sys.argv[1:])
 for case in ('original','core','khr','inject','push','secondary','mixed'):
  if selected and case not in selected:continue
  root=Path(test_dir('native-absent-output-'+case+'-'));mods=root/'Mods';mods.mkdir()
  source=root/'alias.hlsl';source.write_text('[[vk::binding(0,0)]] RWStructuredBuffer<uint> data;\n[[vk::binding(2,0)]] cbuffer Current { uint value; };\n[numthreads(1,1,1)] void main(){data[0]=value+1;}\n',encoding='ascii')
  shader=compile_shader(source,root/'compiled','12345678','cs');config='namespace = absent_outputs\n'
  if case!='original':
   config+='[ShaderOverrideCompute]\nhash = %08x\nrun = BuiltInCommandListUnbindAllRenderTargets\n'%crc32c(shader.read_bytes())
   if case=='mixed':config+='vk_binding = cs-u0, 0, 0\ncs-u0 = ref cs-u0\n'
   config+='handling = skip\ndispatch = 1,1,1\ndispatch = 1,1,1\ndispatch = 1,1,1\n'
  (mods/'test.ini').write_text(config,encoding='ascii');output=root/'result.bin'
  env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(root/'settings'),EFMI_VK_M2_ENABLED='1',VK_LAYER_VALIDATE_SYNC='1')
  args=['--compute-shader',str(shader),'--compute-output',str(output),'--compute-uniform-alias','--api','1.3']
  if case!='mixed':args+=['--submit2','2' if case=='khr' else '1']
  if case=='push':args+=['--descriptors','push']
  if case=='secondary':args+=['--compute-secondary']
  with Sandbox(seconds=3,threads=0,entry='inject' if case=='inject' else 'layer',validation=True,env_extra=env,extra_args=args) as sb:
   time.sleep(.9)
   with ProbeClient(sb.pid) as probe:status=probe.status().get('replacement',{}).get('shader_overrides',{})
   sb.proc.wait(timeout=20);out=sb.output();data=read_compute_result(output)
   checks.equal(sb.proc.returncode,0,case+': normal exit')
   checks.equal(struct.unpack_from('<I',data)[0] if data else None,1 if case=='original' else 3,case+': exact dependent GPU result after absent-output unbind')
   if case!='original':checks.check(status.get('executed',0)>0 and not status.get('last_reason'),case+': supported call plan executes without output refusal')
   checks.check(not any(marker in out for marker in ('Validation Error','VALIDATION ERROR','VUID-','SYNC-HAZARD')),case+': core and synchronization validation clean')
   (root/'stdout.txt').write_text(out,encoding='utf-8');(root/'layer.log').write_bytes(Path(sb.log_path).read_bytes());(root/'status.json').write_text(json.dumps(status,indent=2),encoding='ascii')
  print('artifacts:',root)
 return checks.finish()
if __name__=='__main__':raise SystemExit(main())
