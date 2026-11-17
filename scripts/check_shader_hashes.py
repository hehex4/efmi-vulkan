"""Original shader hash modes against compiled DXBC and metadata-only changes."""
import struct
import subprocess
import sys
from check_shader_inputs import ROOT, build, run

sys.path.insert(0, str(ROOT / 'server'))
from efmi_vk.hashes import crc32c


def expected(data):
    fnv = 0
    for byte in data:
        fnv = ((fnv * 0x100000001b3) & 0xffffffffffffffff) ^ byte
    chunks = struct.unpack_from('<I', data, 28)[0]
    payloads = []
    for offset in struct.unpack_from('<%dI' % chunks, data, 32):
        size = struct.unpack_from('<I', data, offset + 4)[0]
        if data[offset:offset + 4] in (b'SHDR', b'SHEX', b'ISGN', b'ISG1', b'PCSG', b'PSG1', b'OSGN', b'OSG5', b'OSG1'):
            payloads.append(data[offset + 8:offset + 8 + size])
    return fnv, int.from_bytes(data[4:12], 'big'), crc32c(b''.join(payloads)) or fnv


def main():
    out = ROOT / 'build/shader-input-test'
    exe = build(out)
    root = out / 'hash-fixture'
    root.mkdir(parents=True, exist_ok=True)
    hlsl, dxbc = root / 'source.hlsl', root / 'source.dxbc'
    hlsl.write_text('RWStructuredBuffer<uint> Result:register(u0);\n[numthreads(1,1,1)] void main(){Result[0]=23;}\n', encoding='ascii')
    run([exe, '--dxbc', hlsl, dxbc])
    source = dxbc.read_bytes()
    variants = {'original': source}
    changed = bytearray(source)
    changed[4:12] = bytes.fromhex('0123456789abcdef')
    variants['embedded'] = bytes(changed)
    chunks = struct.unpack_from('<I', source, 28)[0]
    offsets = struct.unpack_from('<%dI' % chunks, source, 32)
    for offset in offsets:
        if source[offset:offset + 4] == b'STAT':
            changed = bytearray(source)
            changed[offset + 8] ^= 1
            variants['statistics'] = bytes(changed)
            break
    assert 'statistics' in variants
    changed = bytearray(source)
    struct.pack_into('<%dI' % chunks, changed, 32, *reversed(offsets))
    variants['table-order'] = bytes(changed)
    checks = 0
    for name, data in variants.items():
        path = root / (name + '.dxbc')
        path.write_bytes(data)
        output = subprocess.run([str(exe), '--hashes', str(path)], capture_output=True, text=True, check=True, timeout=30)
        actual = tuple(int(value, 16) for value in output.stdout.split())
        assert actual == expected(data), (name, actual, expected(data))
        checks += 3
    assert expected(variants['embedded'])[1] == 0x0123456789abcdef
    assert expected(variants['statistics'])[2] == expected(source)[2]
    assert expected(variants['statistics'])[0] != expected(source)[0]
    assert expected(variants['table-order'])[2] != expected(source)[2]
    print('Original shader hashes: %d checks passed' % (checks + 4))


if __name__ == '__main__':
    main()
