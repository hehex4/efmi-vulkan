"""Bounded compressed original bytecode, exact identities and retained owners."""
import hashlib
import struct
import sys
from pathlib import Path
from build_shader_model_index import encode_sources,dxbc_model
from shader_source_compression import unpack
from check_shader_inputs import build, run, ROOT


def main():
    if len(sys.argv)==3 and sys.argv[1]=='--archive':
        data=Path(sys.argv[2]).read_bytes();count,blobs,size,codec=struct.unpack_from('<4I',data,8)
        assert data[:8]==b'EFVSB001' and size==len(data)-56 and codec==4
        assert hashlib.sha256(data[56:]).digest()==data[24:56]
        pos=56+40*count
        for _ in range(blobs):
            raw,packed=struct.unpack_from('<2I',data,pos);digest=data[pos+8:pos+40];pos+=40
            code=unpack(data[pos:pos+packed],raw);pos+=packed
            assert hashlib.sha256(code).digest()==digest
            dxbc_model(code)
        assert pos==len(data)
        print('Source archive: %d exact identities, %d original blobs verified'%(count,blobs))
        return
    out=ROOT/'build/shader-input-test';exe=build(out)
    root=out/'source-fixture';root.mkdir(parents=True,exist_ok=True)
    module=root/'fixture.spv';run([exe,'--fixture',module]);code=module.read_bytes()
    dxbc=b'DXBC'+bytes(16)+struct.pack('<4I',1,52,1,36)+b'SHEX'+struct.pack('<3I',8,0x10050,2)
    good=encode_sources([(hashlib.sha256(code).digest(),len(code),dxbc)])
    (root/'good.bin').write_bytes(good);(root/'empty.bin').write_bytes(encode_sources([]))
    bad=dict(truncated=good[:-1],magic=b'UNKNOWN!'+good[8:],checksum=good[:24]+bytes(32)+good[56:],trailing=good+b'\0')
    def framed(data):
        return good[:16]+struct.pack('<2I',len(data),4)+hashlib.sha256(data).digest()+data
    for name,offset,value in [('size',32,17),('source',36,1),('raw-budget',40,5*1024*1024),('packed-size',44,0)]:
        data=bytearray(good[56:]);struct.pack_into('<I',data,offset,value);bad[name]=framed(data)
    for name,data in bad.items():(root/('bad-'+name+'.bin')).write_bytes(data)
    data=bytearray(good[56:]);data[80:]=bytes(len(data)-80)
    (root/'corrupt.bin').write_bytes(framed(data))
    run([exe,'--sources',root])


if __name__=='__main__':main()
