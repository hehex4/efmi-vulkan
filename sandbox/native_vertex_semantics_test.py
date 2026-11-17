"""Actual matched Vulkan draws apply cached shader semantics to file input layouts."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
from unittest.mock import patch

from harness import Checks, DIST, ROOT, Sandbox, test_dir, configuration_root
from runtime_test import band_pixels, sandbox_shader_hashes
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient

sys.path.insert(0, str(Path(ROOT) / 'scripts'))
from build_shader_input_index import encode
from check_shader_inputs import build


def scene(checks, case, emitter):
    root = Path(test_dir('native-vertex-' + case + '-'))
    mods, settings = root / 'Mods', root / 'settings'
    mods.mkdir(); settings.mkdir()
    module = root / 'vertex.spv'
    subprocess.run([str(emitter), '--sandbox-vertex', str(module)], check=True, timeout=10)
    code = module.read_bytes()
    vertex, _ = sandbox_shader_hashes()
    checks.equal(crc32c(code), vertex, case + ': metadata names the actual sandbox vertex module')
    identity = hashlib.sha256(code if case != 'unknown' else code + b'changed').digest()
    cache = (settings if case in ('default', 'swap') else root) / 'shader-inputs.bin'
    cache.write_bytes(encode([(identity, len(code), [(0, 'POSITION', 0)])]))
    vertices = [(10, 10, .70, .55, -.90, .55), (10, 10, .90, .55, -.70, .55), (10, 10, .80, .35, -.80, .35)]
    (mods / 'vertex.buf').write_bytes(b''.join(struct.pack('<6f', *row) for row in vertices))
    (mods / 'index.buf').write_bytes(struct.pack('<3H', 0, 1, 2))
    edits = 'vb0->ElementOffset(POSITION, 0) = 8\ndrawindexed = 3,0,0\n'
    if case == 'numeric':
        edits = edits.replace('drawindexed', 'vb0->ElementFormat(POSITION, 0) = R8G8B8A8_UINT\ndrawindexed')
    if case == 'ordered':
        edits += 'vb0->ElementOffset(POSITION, 0) = 16\ndrawindexed = 3,0,0\n'
    ini = ('namespace = vertex_identity\n[ShaderOverrideEntry]\nhash = %08x\nchecktextureoverride = ib\n' % vertex +
           '[TextureOverrideMesh]\nhash = %08x\nmatch_first_index = 6\n'
           'match_index_count = 3\nhandling = skip\nvb0 = ResourceVertex\nib = ResourceIndex\n' %
           crc32c(struct.pack('<3I', 14, 15, 16))) + edits
    ini += ('[ResourceVertex]\ntype = Buffer\nstride = 24\nfilename = vertex.buf\n'
            '[ResourceIndex]\ntype = Buffer\nformat = R16_UINT\nfilename = index.buf\n')
    (mods / 'mod.ini').write_text(ini, encoding='ascii')
    screen, source, trigger = root / 'screen.raw', root / 'source.raw', root / 'readback.trigger'
    env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(settings),
               EFMI_VK_M2_ENABLED='1', EFMI_VK_SHADER_INPUTS=str(cache), EFMI_VK_TEST_NOTICE='> vertex identity',
               EFMI_VK_TEST_NOTICES_READBACK=str(screen), EFMI_VK_TEST_NOTICES_READBACK_TRIGGER=str(trigger),
               VK_LAYER_VALIDATE_SYNC='1')
    if case in ('default', 'swap'):
        del env['EFMI_VK_SHADER_INPUTS']
    args = ['--readable-swapchain', '--api', '1.3', '--native-output', str(source), '--native-output-classic', '1',
            '--native-depth-destination', '--native-output-depth']
    with Sandbox(seconds=9 if case == 'swap' else 5, threads=0, validation=True,
                 entry='inject' if case == 'inject' else 'layer', env_extra=env, extra_args=args) as sb:
        with ProbeClient(sb.pid) as probe:
            end = time.monotonic() + 2.5
            while time.monotonic() < end:
                status = probe.status().get('replacement', {})
                if status.get('shader_overrides', {}).get('executed', 0) > 2:
                    break
                time.sleep(.05)
            if case == 'swap':
                backend = root / 'backend.dll'
                shutil.copy2(Path(DIST) / 'efmi_vk_backend.dll', backend)
                probe.swap_module(str(backend)); end = time.monotonic() + 3
                while time.monotonic() < end:
                    status = probe.status().get('replacement', {})
                    if status.get('module', {}).get('generation', 0) > 0:
                        break
                    time.sleep(.05)
                checks.check(status.get('module', {}).get('generation', 0) > 0, 'swap: module exchange completed')
            time.sleep(.3); trigger.touch()
            status = probe.status().get('replacement', {})
        sb.proc.wait(timeout=25)
        checks.equal(sb.proc.returncode, 0, case + ': normal sandbox exit')
        output = sb.output()
        log = Path(sb.log_path).read_text(encoding='utf-8', errors='replace')
    (root / 'layer.log').write_text(log, encoding='utf-8')
    (root / 'status.json').write_text(json.dumps(status, indent=2), encoding='ascii')
    expected = case not in ('unknown', 'numeric')
    checks.equal(bool(status.get('shader_overrides', {}).get('last_reason')), not expected,
                 case + ': unsupported input identity or format reports an admission refusal')
    checks.equal(band_pixels(screen), 5516 if expected else 6418, case + ': original mesh hidden only after a proven replacement')
    checks.equal(band_pixels(source), 902, case + ': independent source color remains intact')
    hooks = status.get('shader_overrides', {})
    checks.check(hooks.get('executed', 0) > 0 and not hooks.get('last_reason') if expected else
                 bool(hooks.get('last_reason')) and hooks.get('kept', 0) + hooks.get('refused', 0) > 0,
                 case + ': matched geometry admission agrees with independent pixels')
    meta = json.loads(Path(str(screen) + '.json').read_text())
    data = screen.read_bytes(); width, height = meta['width'], meta['height']
    clear = data[4 * ((height - 2) * width + width - 2):][:3]
    def region(lo, hi):
        return sum(max(abs(a-b) for a, b in zip(data[4 * (y * width + x):][:3], clear)) > 12
                   for y in range(int(height * .64), int(height * .8))
                   for x in range(int(width * lo), int(width * hi)))
    checks.equal(region(.84, .96) > 200, expected, case + ': POSITION at byte 8 renders the right-hand triangle')
    checks.equal(region(.04, .16) > 200, case == 'ordered', case + ': second offset has an independent cached layout')
    checks.equal(status.get('record', {}).get('variant_waits', -1), 0, case + ': no synchronous pipeline waits')
    checks.check(not any(marker in output for marker in ('Validation Error', 'VALIDATION ERROR', 'VUID-', 'SYNC-HAZARD')),
                 case + ': core and synchronization validation clean')
    print('artifacts:', root)


def merged(checks, layouts=False, exchange=False, legacy=None):
    import native_classic_mod_character_test as character
    original = character.layer_env
    original_fixture = character.fixture
    roots = []
    def fixture(folder, *args, **kwargs):
        result = original_fixture(folder, *args, **kwargs)
        if layouts:
            # Three declarations of one file must retain independent bone masks.
            streams = [(folder / ('stream-%d.buf' % i)).read_bytes() for i in (4, 6, 8)]
            assert len({len(s) for s in streams}) == 1 and len(streams[0]) % 16 == 0
            combined = b''.join(s[at:at + 16] for at in range(0, len(streams[0]), 16) for s in streams)
            (folder / 'shared-blend.buf').write_bytes(combined)
            path = folder / 'mod.ini'
            ini = path.read_text(encoding='ascii')
            for offset, stream in enumerate((4, 6, 8)):
                ini = ini.replace('vb2 = ResourceS%d\n' % stream,
                                  'vb2 = ResourceSharedBlend\n'
                                  'vb2->ElementOffset(BLENDWEIGHT, 0) = %d\n'
                                  'vb2->ElementOffset(BLENDINDICES, 0) = %d\n' % (offset * 16, offset * 16 + 8))
            ini += '\n[ResourceSharedBlend]\ntype = Buffer\nstride = 48\nfilename = shared-blend.buf\n'
            path.write_text(ini, encoding='ascii')
        if legacy:
            path = folder / 'mod.ini'
            ini = path.read_text(encoding='ascii')
            for stream in (4, 6, 8):
                buf = folder / ('stream-%d.buf' % stream)
                data = buf.read_bytes()
                # The first index is the only weighted index; zero extension
                # gives identical R16/R32 interpretations of this same file.
                buf.write_bytes(b''.join((bytes(16) + data[at:at + 16]) if legacy == 'offsets'
                                        else (data[at:at + 16] + bytes(8)) for at in range(0, len(data), 16)))
                marker = 'filename = stream-%d.buf\nstride = 16' % stream
                assert marker in ini
                ini = ini.replace(marker, marker[:-2] + ('32' if legacy == 'offsets' else '24'))
            edit = 'vb2->ElementFormat(BLENDINDICES, 0) = R16G16B16A16_UINT\n'
            assert ini.count(edit) == 2
            # Only the common attachment sets the format, alternating each
            # attachment without changing pixels or requiring semantic identity.
            ini = ini.replace(edit, '')
            ini = ini.replace('[Constants]\n', '[Constants]\nglobal $wide = 0\n', 1)
            switch = ('$wide = 1 - $wide\nif $wide == 0\n' + edit + 'else\n' +
                      edit.replace('R16G16B16A16', 'R32G32B32A32') + 'endif\n')
            if legacy == 'offsets':
                # Existing paired-offset evidence names the source and final
                # offsets. The default bytes are deliberately invalid weights.
                switch = (edit + 'vb2->ElementOffset(BLENDWEIGHT, 0) = 0\n'
                          'vb2->ElementOffset(BLENDINDICES, 0) = 8\n'
                          'vb2->ElementOffset(BLENDWEIGHT, 0) = 16\n'
                          'vb2->ElementOffset(BLENDINDICES, 0) = 24\n')
            ini = ini.replace('[CommandListInitializeMergedSkeleton]', switch + '[CommandListInitializeMergedSkeleton]', 1)
            if legacy == 'ordered':
                first = ini.index('[CommandListDraw0]')
                at = ini.index('drawindexed = 3,0,0\n', first) + len('drawindexed = 3,0,0\n')
                ini = ini[:at] + edit.replace('R16G16B16A16', 'R32G32B32A32') + 'drawindexed = 3,0,0\n' + edit + 'drawindexed = 3,0,0\n' + ini[at:]
            path.write_text(ini, encoding='ascii')
        return result
    def environment(path, *args, **kwargs):
        extra = kwargs['extra']; root = Path(extra['EFMI_VK_M2_MODS']).parent; roots.append(root)
        code = (root / 'vertex.spv').read_bytes()
        cache = root / 'shader-inputs.bin'
        # Names come from this synthetic fixture's declarations, not a runtime table.
        if not legacy:
            cache.write_bytes(encode([(hashlib.sha256(code).digest(), len(code),
                                      [(0, 'POSITION', 0), (1, 'TEXCOORD', 0),
                                       (2, 'BLENDWEIGHT', 0), (3, 'BLENDINDICES', 0)])]))
        extra['EFMI_VK_SHADER_INPUTS'] = str(cache)
        return original(path, *args, **kwargs)
    with patch.object(character, 'layer_env', environment), patch.object(character, 'fixture', fixture):
        character.scene(checks, 'control', matched=True, module_exchange=exchange)
    checks.equal(any('shader input semantics resolved for module' in (root / 'layer.log').read_text(errors='replace')
                     for root in roots), not bool(legacy), 'merged: semantic evidence is used only when supplied')


if __name__ == '__main__':
    checks = Checks('native shader input semantics')
    cases = sys.argv[1:] or ['offset', 'ordered', 'unknown', 'numeric', 'default', 'inject', 'swap', 'merged',
                            'merged-layouts', 'merged-layouts-swap', 'merged-legacy-widths',
                            'merged-legacy-ordered', 'merged-legacy-offsets', 'merged-legacy-swap']
    emitter = build(Path(ROOT) / 'build/shader-input-test') if any(not case.startswith('merged') for case in cases) else None
    for case in cases:
        if case.startswith('merged'):
            merged(checks, layouts=case.startswith('merged-layouts'), exchange=case.endswith('-swap'),
                   legacy=(case.rsplit('-', 1)[1]) if case.startswith('merged-legacy') else None)
        else:
            scene(checks, case, emitter)
    raise SystemExit(checks.finish())
