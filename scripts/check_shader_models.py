"""Bounded source-model index and paired-program parser checks; no game writes."""
import hashlib
from pathlib import Path
import struct

from build_shader_model_index import dxbc_model,encode,payloads,spirv_stage
from check_shader_inputs import build,run,ROOT


def main():
    out=ROOT/'build/shader-input-test'
    exe=build(out)
    root=out/'model-fixture';root.mkdir(parents=True,exist_ok=True)
    module=root/'fixture.spv';run([exe,'--fixture',module]);code=module.read_bytes()
    good=encode([(hashlib.sha256(code).digest(),len(code),0x10050)])
    (root/'good.bin').write_bytes(good)
    bad=dict(truncated=good[:-1],magic=b'UNKNOWN!'+good[8:],checksum=good[:16]+bytes(32)+good[48:],trailing=good+b'\0')
    def framed(name,data,count=1):
        bad[name]=good[:8]+struct.pack('<2I',count,len(data))+hashlib.sha256(data).digest()+data
    original=good[48:]
    framed('duplicate',original*2,2)
    for name,offset,value in [('size',32,17),('model-type',36,0x60050),('model-major',36,0x10060),
                              ('model-minor',36,0x10052),('model-reserved',36,0x10150)]:
        changed=bytearray(original);struct.pack_into('<I',changed,offset,value);framed(name,changed)
    for name,data in bad.items():(root/('bad-'+name+'.bin')).write_bytes(data)
    run([exe,'--models',root])
    checks=0
    def check(value,message):
        nonlocal checks
        if not value:raise AssertionError(message)
        checks+=1
    def refuses(function,*args):
        try:function(*args)
        except (ValueError,struct.error):return True
        return False
    shader=struct.pack('<2I',0x10050,2)
    dxbc=b'DXBC'+bytes(16)+struct.pack('<4I',1,52,1,36)+b'SHEX'+struct.pack('<I',8)+shader
    check(dxbc_model(dxbc)==0x10050,'DXBC executable proves the model')
    for offset,value in [(24,100),(28,100),(32,20),(40,100),(48,1)]:
        data=bytearray(dxbc);struct.pack_into('<I',data,offset,value)
        check(refuses(dxbc_model,data),'Malformed DXBC framing must refuse')
    check(spirv_stage(code)=='v','Actual entry point determines the stage')
    check(refuses(spirv_stage,code[:-1]),'Unaligned module refuses')
    combined=bytearray(176)+dxbc
    struct.pack_into('<3I',combined,0,0x02000021,176,len(dxbc))
    check(payloads(combined)==[dxbc],'Versioned range table admits opaque non-layout flags')
    for offset,value in [(0,0x03000000),(4,175),(8,100),(12,176),(52,1)]:
        data=bytearray(combined);struct.pack_into('<I',data,offset,value)
        check(refuses(payloads,data),'Malformed or unknown combined-program ranges refuse')
    check(refuses(encode,[(bytes(32),20,0x10060)]),'Unsupported source model cannot be encoded')
    print('Shader model parser: %d checks passed'%checks)


if __name__=='__main__':main()
