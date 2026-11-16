"""Version-5 index framing and range checks without a game installation."""
from pathlib import Path
import struct
import sys
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from read_shader_vfs import parse_plain_index

checks = 0


def check(ok, label):
    global checks
    if not ok:
        raise AssertionError(label)
    checks += 1


def crc(body):
    return body + struct.pack('<I', zlib.crc32(body))


def rejected(data, label):
    try:
        parse_plain_index(data)
    except (ValueError, struct.error):
        check(True, label)
    else:
        check(False, label)


def string(value):
    data = value.encode('ascii')
    return struct.pack('<H', len(data)) + data


header = struct.pack('<ii', 5, 123) + string('InitBundle') + struct.pack('<qiqBi', 0, 0, 0, 1, 1)
chunk = bytes.fromhex('00112233445566778899aabbccddeeff') + bytes(16) + struct.pack('<qBqi', 128, 0, 0x123456789, 1)
file_start = header + chunk + string('Data/Bundles/Windows/initial/example.ab') + struct.pack('<q', 17) + bytes(32)
record = struct.pack('<qqBBq', 16, 32, 2, 0, 0x23456789a)
body = file_start + record + bytes(10)
parsed = parse_plain_index(crc(body))
entry = parsed['entries'][0]
check(parsed['version'] == 5 and parsed['label'] == 'InitBundle', 'Version and label preserve framing')
check(entry['chunk'] == '00112233445566778899AABBCCDDEEFF.chk', 'Chunk identity comes from its indexed digest')
check(entry['offset'] == 16 and entry['length'] == 32 and entry['file_type'] == 2,
      'Eight-byte version-5 fields do not shift the payload range')
check(entry['name_hash'] == 17 and entry['iv'] == 0 and not entry['encrypted'], 'Unencrypted metadata is exact')
encrypted = file_start + struct.pack('<qqBBqq', 16, 32, 2, 1, -123, 0x23456789a) + bytes(10)
check(parse_plain_index(crc(encrypted))['entries'][0]['iv'] == -123, 'Encrypted nonce seed preserves all 64 bits')
rejected(b'', 'Empty envelope')
rejected(crc(body)[:-1], 'Truncated checksum')
corrupt = bytearray(crc(body)); corrupt[20] ^= 1
rejected(bytes(corrupt), 'Payload corruption fails CRC')
rejected(crc(struct.pack('<i', 4) + body[4:]), 'Unknown index version')
rejected(crc(body[:-1]), 'Unknown short trailer')
rejected(crc(body + b'\0'), 'Unknown long trailer')
for offset, length, flag in [(-1, 32, 0), (16, 0, 0), (16, -1, 0), (16, 32, 2)]:
    rejected(crc(file_start + struct.pack('<qqBBq', offset, length, 2, flag, 0) + bytes(10)), 'Invalid range or flags')
rejected(crc(header[:-4] + struct.pack('<i', 2_000_001)), 'Unbounded chunk count')
rejected(crc(header + chunk[:-4] + struct.pack('<i', -1)), 'Negative file count')
print(f'Shader VFS metadata: {checks}/{checks} checks passed')
