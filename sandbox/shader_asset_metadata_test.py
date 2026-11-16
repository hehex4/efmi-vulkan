"""Fail-closed input identity and byte-range checks; no game or third-party parser."""
import copy
from pathlib import Path
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from unity_shader_metadata import channel_semantics, program, span, vertex_locations

checks = 0


def check(ok, label):
    global checks
    if not ok:
        raise AssertionError(label)
    checks += 1


def rejected(action, label):
    try:
        action()
    except (ValueError, struct.error):
        check(True, label)
    else:
        check(False, label)


tail = [27, 4, 0, 13, 1, 15, 4, 16, 3, 20]
locations = [(0, ''), (2, ''), (3, ''), (7, '')]
inputs = [dict(semantic=name, index=0, system=0, register=reg)
          for reg, name in enumerate(['POSITION', 'NORMAL', 'TEXCOORD', 'COLOR'])]
expected = [(0, 'POSITION', 0), (2, 'NORMAL', 0), (3, 'TEXCOORD', 0), (7, 'COLOR', 0)]
check(channel_semantics(tail, locations, inputs) == expected,
      'Sparse Vulkan locations use source channels, not sequential DXBC registers')
check(channel_semantics(tail, locations, list(reversed(inputs))) == expected,
      'Signature record order does not determine identity')
check(channel_semantics(tail, locations, inputs + [dict(semantic='SV_InstanceID', index=0, system=8)]) == expected,
      'System inputs do not become vertex-buffer semantics')
for bad, label in [([], 'empty table'), (tail[:-1], 'truncated table'),
                   ([0] + tail[1:], 'inconsistent mask'),
                   (tail[:2] + [64] + tail[3:], 'unrecognized source'),
                   (tail[:4] + [0] + tail[5:], 'duplicate source'),
                   (tail[:-1] + [13], 'duplicate target')]:
    rejected(lambda: channel_semantics(bad, locations, inputs), label)
rejected(lambda: channel_semantics(tail, locations[:-1], inputs), 'Missing SPIR-V location')
rejected(lambda: channel_semantics(tail, locations, inputs[:-1]), 'Missing DXBC semantic')
unknown = copy.deepcopy(inputs)
unknown[0]['semantic'] = 'CUSTOM'
rejected(lambda: channel_semantics(tail, locations, unknown), 'No guessed identity for unknown semantics')
rejected(lambda: channel_semantics(tail, locations, inputs + [inputs[0]]), 'Duplicate semantic rejected')
check(span(b'abcd', 4, 0) == b'', 'Zero-length end range is legal')
for offset, size in [(-1, 1), (0, -1), (4, 1), (5, 0), (1, 2**64)]:
    rejected(lambda: span(b'abcd', offset, size), 'Out-of-range binary access')
for data in [b'', b'\x03\x02\x23\x07', struct.pack('<6I', 0x07230203, 0x10000, 0, 2, 0, 0),
             struct.pack('<6I', 0x07230203, 0x10000, 0, 2, 0, (5 << 16) | 15)]:
    rejected(lambda: vertex_locations(data), 'Truncated SPIR-V cannot establish input identity')
raw = struct.pack('<7I', 202506210, 25, 0, 0, 0, 0, 0) + struct.pack('<I', 4) + b'code' + struct.pack('<2I', 0, 0)
segment = struct.pack('<4I', 1, 16, len(raw), 0) + raw
check(program([segment], 0, 25) == (b'code', [0, 0]), 'Program and trailing channels retain exact ranges')
rejected(lambda: program([segment], 1, 25), 'Missing program index')
rejected(lambda: program([segment[:-1]], 0, 25), 'Truncated program entry')
rejected(lambda: program([segment], 0, 33), 'Different platform type')
print(f'Shader asset metadata: {checks}/{checks} checks passed')
