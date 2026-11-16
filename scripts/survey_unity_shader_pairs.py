"""Survey paired DXBC/SPIR-V input metadata in explicitly supplied local assets.

Outputs evidence only. Does not change assets, generate character profiles, or
install a runtime shader index. See docs/shader-input-metadata.md for the schema,
dependencies and observed compiler encoding this survey checks.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from unity_shader_metadata import Smolv, channel_semantics, program, require, shader_tree, signature, span

ROOT = Path(__file__).resolve().parents[1]


def owned_output(path):
    path = path.resolve()
    require(path.is_relative_to(ROOT / 'build'), 'Survey output must stay inside this project build directory')
    return path


def fingerprint(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def survey(args):
    sys.path.insert(0, str(args.packages.resolve()))
    import UnityPy
    import lz4.block
    require(UnityPy.__version__ == '1.25.2', 'Use the audited UnityPy 1.25.2 schema database')
    decoder = Smolv(args.decoder.resolve())
    output = owned_output(args.output)
    validation_dir = owned_output(args.validation_dir) if args.validation_dir else None
    require(bool(validation_dir) == bool(args.validator), 'Supply both --validator and --validation-dir')
    if validation_dir:
        validation_dir.mkdir(parents=True, exist_ok=True)
    captures = {}
    if args.captures:
        require(args.captures.is_dir(), 'Capture directory does not exist')
        for path in sorted(args.captures.rglob('*.spv')):
            captures.setdefault(fingerprint(path), path.relative_to(args.captures).as_posix())
    stats, sources, matches, examples, vertices, refused = Counter(), [], [], [], {}, []
    unique_pairs, signatures, mappings = set(), defaultdict(set), defaultdict(set)
    for path in args.assets:
        path = path.resolve()
        before = fingerprint(path)
        env = UnityPy.load(str(path))
        source = dict(path=str(path), sha256=before, shader_objects=0, parsed=0)
        sources.append(source)
        for obj in env.objects:
            if obj.type.name != 'Shader':
                continue
            source['shader_objects'] += 1
            data = obj.read_typetree(nodes=shader_tree(obj._get_typetree_node()), check_read=True)
            source['parsed'] += 1
            source['serialized_version'] = str(obj.version)
            stats['shaders'] += 1
            require(data['platforms'] == [4, 18] and data['m_CompressionType'] == 3,
                    'Unsupported platform/compression contract')
            require(data['m_UseExternalBlobs'] and not data['compressedBlob'], 'Expected inline per-LOD payloads')
            chunks, subshaders = data['m_SubShaderBinaryData'], data['m_ParsedForm']['m_SubShaders']
            require(len(chunks) == len(subshaders), 'Subshader binary count differs')
            for sub, chunk in zip(subshaders, chunks):
                require(sub['m_LOD'] == chunk['m_LOD'], 'Subshader LOD identity differs')
                blob, platforms = bytes(chunk['compressedBlob']), []
                for platform in range(2):
                    sizes, offsets, decoded = [chunk[k][platform] for k in
                                               ['compressedLengths', 'offsets', 'decompressedLengths']]
                    require(len(sizes) == len(offsets) == len(decoded) and sizes, 'Invalid segment dimensions')
                    require(all(0 < size <= 64 * 1024 * 1024 for size in decoded), 'Oversized decoded segment')
                    segments = [lz4.block.decompress(span(blob, off, size), uncompressed_size=length)
                                for off, size, length in zip(offsets, sizes, decoded)]
                    require([len(x) for x in segments] == decoded, 'Decoded segment length differs')
                    platforms.append(segments)
                for pass_index, render_pass in enumerate(sub['m_Passes']):
                    for tier, programs in enumerate(render_pass['progVertex']['m_PlayerSubPrograms']):
                        groups = defaultdict(lambda: defaultdict(list))
                        for item in programs:
                            key = (tuple(item['m_KeywordIndices']), item['m_ShaderRequirements'])
                            groups[key][item['m_GpuProgramType']].append(item)
                        for key, pair in groups.items():
                            identity = dict(source=str(path), object_id=obj.path_id, lod=chunk['m_LOD'],
                                            shader=data['m_ParsedForm']['m_Name'], render_pass=pass_index, tier=tier)
                            if set(pair) != {25, 33} or any(len(x) != 1 for x in pair.values()):
                                stats['unpaired'] += 1
                                refused.append(dict(identity, reason='Missing or ambiguous platform pairing',
                                                    program_types=sorted(pair)))
                                continue
                            stats['vertex_pairs'] += 1
                            dx, vk = pair[33][0]['m_BlobIndex'], pair[25][0]['m_BlobIndex']
                            pair_key = (str(path), obj.path_id, chunk['m_LOD'], dx, vk)
                            if pair_key in unique_pairs:
                                continue
                            unique_pairs.add(pair_key)
                            vcode, vtail = program(platforms[1], vk, 25)
                            dcode, dtail = program(platforms[0], dx, 33)
                            module, locations = decoder.vertex(vcode)
                            digest = hashlib.sha256(module).hexdigest()
                            dxhash, inputs = signature(dcode)
                            row = dict(identity, vk_blob=vk, dx_blob=dx, spirv_sha256=digest, dxbc_sha256=dxhash,
                                       locations=locations, signature=inputs, vk_channels=vtail, dx_channels=dtail)
                            stats['unique_blob_pairs'] += 1
                            stats['located_inputs'] += len(locations)
                            stats['named_inputs'] += sum(bool(name) for _, name in locations)
                            same_registers = {r['register'] for r in inputs if not r['system']} == {r[0] for r in locations}
                            stats['same_register_location_set'] += same_registers
                            signatures[digest].add(json.dumps(inputs, sort_keys=True))
                            try:
                                mapped = channel_semantics(vtail, locations, inputs)
                                require(dtail == vtail, 'Paired APIs disagree on channel metadata')
                                row['candidate_semantics'] = mapped
                                mappings[digest].add(json.dumps(mapped))
                                stats['channel_mapping_valid'] += 1
                            except ValueError as error:
                                stats['channel_mapping_refused'] += 1
                                refused.append(dict(identity, reason=str(error)))
                            if digest not in vertices:
                                if validation_dir:
                                    destination = validation_dir / (digest + '.spv')
                                    destination.write_bytes(module)
                                    check = subprocess.run([str(args.validator.resolve()), '--target-env', 'vulkan1.1',
                                                            str(destination)], capture_output=True, text=True, timeout=30)
                                    require(check.returncode == 0, f'SPIR-V validation failed: {check.stdout}{check.stderr}')
                                    stats['validated_unique_vertex_modules'] += 1
                                vertices[digest] = row
                            if len(examples) < 8 or (not same_registers and len(examples) < 16):
                                examples.append(row)
                            if digest in captures:
                                matches.append(dict(row, capture=captures[digest]))
        require(before == fingerprint(path), 'Source asset changed during the survey')
        print(path.name, source['parsed'], 'shader objects parsed completely', flush=True)
    stats['unique_vertex_spirv'] = len(vertices)
    stats['signature_conflicts'] = sum(len(value) > 1 for value in signatures.values())
    stats['semantic_mapping_conflicts'] = sum(len(value) > 1 for value in mappings.values())
    stats['capture_matches'] = len(matches)
    stats['unique_capture_matches'] = len({row['spirv_sha256'] for row in matches})
    report = dict(scope='Explicitly supplied unbundled assets; offline evidence only. No runtime index or game load.',
                  counts=dict(stats), sources=sources, examples=examples, matches=matches, vertices=vertices, refused=refused,
                  dependencies=dict(UnityPy=UnityPy.__version__, smolv_bridge_sha256=fingerprint(args.decoder)))
    if args.validator:
        report['dependencies']['spirv_validator_sha256'] = fingerprint(args.validator)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, ensure_ascii=True) + '\n', encoding='ascii')
    print(json.dumps(dict(stats), indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets', type=Path, nargs='+')
    parser.add_argument('--packages', type=Path, default=Path('F:/vulkan/unity-analysis/packages'))
    parser.add_argument('--decoder', type=Path, default=ROOT / 'build/shader-survey-decoder/smolv_bridge.dll')
    parser.add_argument('--captures', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--validator', type=Path)
    parser.add_argument('--validation-dir', type=Path)
    survey(parser.parse_args())
