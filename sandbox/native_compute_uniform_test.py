"""Repeated native compute writes must be visible to an unchanged uniform-buffer alias."""
from pathlib import Path
import sys,struct
from harness import Checks,Sandbox,test_dir,configuration_root
from native_compute_test import compile_shader,read_compute_result
from efmi_vk.hashes import crc32c
def main():
 checks=Checks('native compute uniform alias');selected=set(sys.argv[1:])
 for case in ('original','core','khr','push','secondary'):
  if selected and case not in selected:continue
  root=Path(test_dir('compute-uniform-alias-'+case+'-'));mods=root/'Mods';mods.mkdir()
  source=root/'alias.hlsl';source.write_text('[[vk::binding(0,0)]] RWStructuredBuffer<uint> data;\n[[vk::binding(2,0)]] cbuffer Current { uint value; };\n[numthreads(1,1,1)] void main(){data[0]=value+1;}\n',encoding='ascii')
  shader=compile_shader(source,root/'compiled','12345678','cs')
  config='namespace = uniform_alias\n'
  if case!='original':config+='[ShaderOverrideCompute]\nvk_hash = %08x\nhandling = skip\ndispatch = 1,1,1\ndispatch = 1,1,1\ndispatch = 1,1,1\n'%crc32c(shader.read_bytes())
  (mods/'test.ini').write_text(config,encoding='ascii');output=root/'result.bin'
  env=dict(EFMI_VK_CONFIGURATION=configuration_root(mods),EFMI_VK_M2_SETTINGS=str(root/'settings'),EFMI_VK_M2_ENABLED='1',VK_LAYER_VALIDATE_SYNC='1')
  args=['--compute-shader',str(shader),'--compute-output',str(output),'--compute-uniform-alias','--submit2','2' if case=='khr' else '1','--api','1.3']
  if case=='push':args+=['--descriptors','push']
  if case=='secondary':args+=['--compute-secondary']
  with Sandbox(seconds=3,threads=0,validation=True,env_extra=env,extra_args=args) as sb:
   sb.proc.wait(timeout=20);out=sb.output();data=read_compute_result(output)
   checks.equal(sb.proc.returncode,0,case+': normal exit')
   checks.equal(struct.unpack_from('<I',data)[0] if data else None,1 if case=='original' else 3,case+': uniform reads observe preceding dispatch writes')
   checks.check('VALIDATION ERROR' not in out and 'VUID-' not in out,case+': core and synchronization validation clean')
   (root/'stdout.txt').write_text(out,encoding='utf-8');(root/'layer.log').write_bytes(Path(sb.log_path).read_bytes())
  print('artifacts:',root)
 return checks.finish()
if __name__=='__main__':raise SystemExit(main())
