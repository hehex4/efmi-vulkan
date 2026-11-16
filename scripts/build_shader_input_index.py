"""Build a content-addressed input-semantic cache from checked whole-asset evidence.

No shader or character names select entries. The runtime checks the complete
SPIR-V SHA-256 and byte length; a changed/unknown module has no correspondence.
The cache is generated data, never committed or a per-character runtime profile.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from unity_shader_metadata import channel_semantics, require, vertex_locations

ROOT = Path(__file__).resolve().parents[1]
MAGIC = b'EFVIS001'
MAX_BYTES = 8 * 1024 * 1024


def encode(records):
    require(len(records) <= 65536, 'Too many shader input entries')
    payload = bytearray()
    for digest, size, semantics in sorted(records):
        require(len(digest) == 32 and 20 <= size <= 16 * 1024 * 1024 and size % 4 == 0,
                'Invalid shader identity')
        require(len(semantics) <= 32, 'Too many vertex semantics')
        require(len({s[0] for s in semantics}) == len(semantics) and
                len({(s[1], s[2]) for s in semantics}) == len(semantics), 'Duplicate vertex semantics')
        payload += digest + struct.pack('<2I', size, len(semantics))
        for location, name, index in sorted(semantics):
            require(0 <= location < 256 and 0 <= index < 256 and 0 < len(name) <= 128 and
                    all(33 <= ord(c) <= 126 for c in name), 'Invalid vertex semantic')
            text = name.encode('ascii')
            payload += struct.pack('<3I', location, index, len(text)) + text
    require(len({row[0] for row in records}) == len(records), 'Duplicate shader identities')
    require(len(payload) <= MAX_BYTES - 48, 'Shader input cache budget exceeded')
    return MAGIC + struct.pack('<2I', len(records), len(payload)) + hashlib.sha256(payload).digest() + payload


def build(report, validation, modules):
    counts = report['counts']
    require(counts.get('channel_observed_vertex_modules', 0) >= len(report['vertices']),
            'Survey must examine unpaired Vulkan variants as well as paired programs')
    require(validation['counts']['validated_unique_vertex_modules'] == len(validation['vertices']) and
            validation['dependencies'].get('spirv_validator_sha256'), 'Independent validation evidence is incomplete')
    for source in report['sources']:
        require(hashlib.sha256(Path(source['path']).read_bytes()).hexdigest() == source['sha256'],
                'Survey source asset changed')
    records = []
    for digest, row in sorted(report['vertices'].items()):
        if 'candidate_semantics' not in row:
            continue
        require(digest in validation['vertices'] and digest not in report['signature_conflicts'],
                'Candidate lacks independent validation or has conflicting signatures')
        module = (modules / (digest + '.spv')).read_bytes()
        require(hashlib.sha256(module).hexdigest() == digest, 'Validated shader bytes changed')
        locations = vertex_locations(module)
        require(locations == [tuple(value) for value in row['locations']], 'Shader locations differ from survey')
        mapped = channel_semantics(row['vk_channels'], locations, row['signature'])
        require(row['vk_channels'] == row['dx_channels'] and mapped == [tuple(s) for s in row['candidate_semantics']],
                'Candidate semantic proof differs')
        records.append((bytes.fromhex(digest), len(module), mapped))
    return encode(records), len(records)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--validation-report', type=Path, required=True)
    parser.add_argument('--modules', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    destination = args.output.resolve()
    require(destination.is_relative_to(ROOT / 'build'), 'Generated cache must stay under this checkout build directory')
    data, count = build(json.loads(args.report.read_text()), json.loads(args.validation_report.read_text()), args.modules)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix('.tmp')
    temporary.write_bytes(data)
    temporary.replace(destination)
    print(json.dumps(dict(entries=count, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()), indent=2))
