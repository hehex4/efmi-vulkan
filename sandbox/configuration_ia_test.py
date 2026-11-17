"""Original IA property queries see actual application bindings and retained references."""
import json
from pathlib import Path
import struct
import time

from harness import Checks, Sandbox, configuration_root, test_dir
from runtime_test import sandbox_shader_hashes
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def scene(checks, region_hashes):
    root = Path(test_dir('configuration-ia-'))
    mods = root / 'Mods'; mods.mkdir()
    names = ('vertex_stride', 'vertex_offset', 'vertex_size', 'index_stride', 'index_offset', 'index_size',
             'index_format', 'saved_size', 'saved_offset', 'saved_stride', 'matched', 'restored_match', 'unbound_size')
    constants = ''.join('global $%s = -999\n' % name for name in names)
    body = ('$vertex_stride = vb0->stride\n$vertex_offset = vb0->offset\n$vertex_size = vb0->size\n'
            '$index_stride = ib->stride\n$index_offset = ib->offset\n$index_size = ib->size\n$index_format = ib->format\n'
            'ResourceSaved = ref ib\n$saved_size = ResourceSaved->size\n$saved_offset = ResourceSaved->offset\n'
            '$saved_stride = ResourceSaved->stride\n$matched = ib\n$unbound_size = vb3->size\n'
            'ib = ref ResourceSaved\n$restored_match = ib\n'
            'if $active == 1\n$nested_calls = $nested_calls + 1\nelse\n'
            '$active = 1\nchecktextureoverride = ib\n$active = 0\nendif\n')
    vertex, _ = sandbox_shader_hashes()
    (mods / 'queries.ini').write_text(
        'namespace = ia\n[Constants]\n' + constants + 'global $active = 0\nglobal $nested_calls = 0\n' +
        '[ShaderOverrideEntry]\nvk_hash = %08x\nchecktextureoverride = ib\n' % vertex +
        '[TextureOverrideQueries]\nhash = %08x\nmatch_first_index = 6\nmatch_index_count = 3\nfilter_index = 7\n' %
        crc32c(struct.pack('<3I', 14, 15, 16)) + body + '[ResourceSaved]\n', encoding='ascii')
    env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(root / 'settings'), EFMI_VK_M2_ENABLED='1')
    with (root / 'd3dx.ini').open('a', encoding='ascii') as ini:
        ini.write('[Rendering]\ntrack_region_hashes = %d\n' % region_hashes)
    with Sandbox(seconds=3, threads=0, validation=True, env_extra=env) as sb:
        with ProbeClient(sb.pid) as probe:
            time.sleep(.5)
            status = probe.status().get('replacement', {})
        sb.proc.wait(timeout=15)
        checks.equal(sb.proc.returncode, 0, 'normal sandbox exit')
        checks.check(not any(x in sb.output() for x in ('Validation Error', 'VUID-', 'SYNC-HAZARD')), 'validation clean')
        (root / 'layer.log').write_bytes(Path(sb.log_path).read_bytes())
    variables = status.get('configuration_generation', {}).get('ini', {}).get('variables', {})
    # Independent fixture declarations: 17 float2 vertices; 16-byte pad + nine uint32 indices.
    # Original GetResourceStride omits the index-format output; GetResourceOffset
    # adds FirstIndex only for IA slots and returns UNKNOWN for custom resources.
    expected = (8, 0, 136, -1, 40, 52, 42, 52, -1, 16, 7, 7, -2)
    for name, value in zip(names, expected):
        checks.equal(variables.get('$\\ia\\' + name), value, name + ': original resource property')
    checks.check(variables.get('$\\ia\\nested_calls', 0) > 0, 'nested CheckTextureOverride uses the current index region')
    checks.check(status.get('shader_overrides', {}).get('executed', 0) > 0, 'retained current index restores successfully')
    (root / 'status.json').write_text(json.dumps(status, indent=2), encoding='ascii')
    print('artifacts:', root)


def main():
    checks = Checks('configuration IA facts')
    for region_hashes in (False, True):
        scene(checks, region_hashes)
    return checks.finish()


if __name__ == '__main__':
    raise SystemExit(main())
