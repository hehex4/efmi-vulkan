"""Generic file geometry through the original root, without a character package."""
import json
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

from harness import Checks, DIST, ROOT, Sandbox, configuration_root, test_dir
from runtime_test import band_pixels
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient

LAYOUT_CASES = ('offset', 'ordered', 'unknown', 'numeric', 'layout_swap', 'caller_layout', 'implicit_layout',
                'dynamic_layout', 'rp2_layout', 'masked_layout')


def scene(checks, case, emitter=None):
    root = Path(test_dir('configuration-geometry-' + case + '-'))
    mods = root / 'Mods'; mods.mkdir()
    assets = mods / 'shared'; assets.mkdir()
    vertices = [(0.70, 0.55), (0.90, 0.55), (0.80, 0.35)]
    layout = case in LAYOUT_CASES
    stride = 24 if layout else 16 if case == 'stride' else 8
    raw = b''.join(struct.pack('<6f', 10, 10, x, y, -x, y) if layout else
                   struct.pack('<4f', x, y, 10, 10) if stride == 16 else struct.pack('<2f', x, y) for x, y in vertices)
    (assets / 'vertices.buf').write_bytes(raw)
    wide = case == 'index32'
    indices = (0,) * 6 + (0, 1, 2) if case in ('caller_layout', 'implicit_layout') else (0, 1, 2)
    (assets / 'indices.buf').write_bytes(struct.pack('<%d%s' % (len(indices), 'I' if wide else 'H'), *indices))
    (assets / 'resources.ini').write_text(
        'namespace = shared\n[ResourceVertex]\ntype = Buffer\nstride = %d\nfilename = vertices.buf\n'
        '[ResourceIndex]\ntype = Buffer\nformat = R%d_UINT\nfilename = indices.buf\n' % (stride, 32 if wide else 16), encoding='ascii')
    body = ('vb0 = Resource\\shared\\Vertex\nib = Resource\\shared\\Index\n'
            'drawindexed = 3,0,0\n')
    if case == 'bounds': body = body.replace('3,0,0', '4,0,0')
    if layout:
        edit = 'vb0->ElementOffset(POSITION, 0) = 8\n'
        if case == 'numeric': edit += 'vb0->ElementFormat(POSITION, 0) = R8G8B8A8_UINT\n'
        body = body.replace('drawindexed', edit + 'drawindexed')
        if case == 'ordered': body += 'vb0->ElementOffset(POSITION, 0) = 16\ndrawindexed = 3,0,0\n'
        if case == 'caller_layout': body = body.replace('drawindexed = 3,0,0', 'draw = from_caller')
        if case == 'implicit_layout': body = body.replace('drawindexed = 3,0,0\n', '')
        if case == 'masked_layout': body = 'o0 = null\n' + body
    if case == 'post': body = ''.join('post ' + line + '\n' for line in body.splitlines())
    if case == 'restore':
        body = ('ResourceSavedVertex = ref vb0\nResourceSavedIndex = ref ib\n' + body +
                'post vb0 = ref ResourceSavedVertex\npost ib = ref ResourceSavedIndex\n')
    (mods / 'rules.ini').write_text(
        '[TextureOverrideMesh]\nhash = %08x\nmatch_first_index = 6\nmatch_index_count = 3\n' %
        crc32c(struct.pack('<3I', 14, 15, 16)) + ('' if case == 'implicit_layout' else 'handling = skip\n') + body +
        ('[ResourceSavedVertex]\n[ResourceSavedIndex]\n' if case == 'restore' else ''), encoding='ascii')
    screen, source, trigger = root / 'screen.raw', root / 'source.raw', root / 'readback.trigger'
    keys = root / 'keys.txt'; keys.write_text('1\n', encoding='ascii')
    env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(root / 'settings'),
               EFMI_VK_M2_ENABLED='1', EFMI_VK_TEST_NOTICE='> configuration geometry',
               EFMI_VK_TEST_NOTICES_READBACK=str(screen), EFMI_VK_TEST_NOTICES_READBACK_TRIGGER=str(trigger),
               EFMI_VK_TEST_KEYS=str(keys))
    if layout:
        from build_shader_input_index import encode
        module = root / 'vertex.spv'
        subprocess.run([str(emitter), '--sandbox-vertex', str(module)], check=True, timeout=10)
        code = module.read_bytes()
        settings = root / 'settings'; settings.mkdir(exist_ok=True)
        (settings / 'shader-inputs.bin').write_bytes(encode([
            (hashlib.sha256(code if case != 'unknown' else code + b'changed').digest(), len(code), [(0, 'POSITION', 0)])]))
    args = ['--readable-swapchain', '--api', '1.3', '--native-output', str(source),
            '--native-depth-destination', '--native-output-depth']
    args += ['--dynamic-rendering', '1'] if case == 'dynamic_layout' else ['--native-output-classic', '2' if case == 'rp2_layout' else '1']
    if case == 'two_queues': args += ['--two-queues']
    with Sandbox(seconds=5, threads=0, validation=True, env_extra=env, extra_args=args) as sb:
        with ProbeClient(sb.pid) as probe:
            deadline = time.monotonic() + 1.0
            while time.monotonic() < deadline:
                status = probe.status().get('replacement', {})
                if status.get('shader_overrides', {}).get('executed', 0) > 2: break
                time.sleep(.05)
            if case in ('swap', 'layout_swap'):
                module = root / 'backend.dll'; shutil.copy2(Path(DIST) / 'efmi_vk_backend.dll', module)
                probe.swap_module(str(module))
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    status = probe.status().get('replacement', {})
                    if status.get('module', {}).get('generation', 0) > 0: break
                    time.sleep(.05)
                checks.check(status.get('module', {}).get('generation', 0) > 0, 'swap: module exchange completed')
            if case == 'reload':
                # Move the file triangle to the left without replacing any configuration file.
                (assets / 'vertices.buf').write_bytes(b''.join(struct.pack('<2f', -x, y) for x, y in vertices))
                keys.write_text('1 121\n', encoding='ascii'); time.sleep(.2); keys.write_text('1\n', encoding='ascii')
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    status = probe.status().get('replacement', {})
                    if status.get('configuration_generation', {}).get('reloads', 0) > 0: break
                    time.sleep(.05)
                checks.check(status.get('configuration_generation', {}).get('reloads', 0) > 0, 'reload: configuration exchanged')
            time.sleep(.2); trigger.touch()
            status = probe.status().get('replacement', {})
        sb.proc.wait(timeout=20)
        checks.equal(sb.proc.returncode, 0, case + ': normal exit')
        checks.check(not any(marker in sb.output() for marker in ('Validation Error', 'VUID-', 'SYNC-HAZARD')),
                     case + ': core and synchronization validation clean')
        (root / 'layer.log').write_bytes(Path(sb.log_path).read_bytes())
    (root / 'status.json').write_text(json.dumps(status, indent=2), encoding='ascii')
    expected = case not in ('bounds', 'unknown', 'numeric')
    checks.equal(band_pixels(screen), 5516 if expected else 6418, case + ': original geometry changes only after complete preflight')
    checks.equal(band_pixels(source), 902, case + ': independent original output remains intact')
    checks.equal(status.get('shader_overrides', {}).get('executed', 0) > 0, expected, case + ': generic geometry plan executes')
    meta = json.loads(Path(str(screen) + '.json').read_text())
    data = screen.read_bytes(); width, height = meta['width'], meta['height']
    clear = data[4 * ((height - 2) * width + width - 2):][:3]
    def region(lo, hi):
        return sum(max(abs(a-b) for a, b in zip(data[4 * (y * width + x):][:3], clear)) > 12
                  for y in range(int(height * .64), int(height * .8))
                  for x in range(int(width * lo), int(width * hi)))
    checks.equal(region(.84, .96) > 200, expected and case not in ('reload', 'masked_layout'), case + ': shared file vertices reach actual pixels')
    if case == 'reload': checks.check(region(.04, .16) > 200, 'reload: changed file content reaches the left triangle')
    if case == 'ordered': checks.check(region(.04, .16) > 200, 'ordered: independent second layout reaches the left triangle')
    if not expected:
        checks.check(bool(status.get('shader_overrides', {}).get('last_reason')), case + ': refusal is reported')
    print('artifacts:', root)


def main():
    checks = Checks('configuration geometry')
    cases = sys.argv[1:] or ['index16', 'index32', 'bounds', 'post', 'restore', 'two_queues', 'swap', 'reload',
                            'stride', *LAYOUT_CASES]
    emitter = None
    if any(case in LAYOUT_CASES for case in cases):
        sys.path.insert(0, str(Path(ROOT) / 'scripts'))
        from check_shader_inputs import build
        emitter = build(Path(ROOT) / 'build/shader-input-test')
    for case in cases:
        scene(checks, case, emitter)
    return checks.finish()


if __name__ == '__main__':
    raise SystemExit(main())
