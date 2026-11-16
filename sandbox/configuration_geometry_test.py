"""Generic file geometry through the original root, without a character package."""
import json
from pathlib import Path
import shutil
import struct
import sys
import time

from harness import Checks, DIST, Sandbox, configuration_root, test_dir
from runtime_test import band_pixels
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def scene(checks, case):
    root = Path(test_dir('configuration-geometry-' + case + '-'))
    mods = root / 'Mods'; mods.mkdir()
    assets = mods / 'shared'; assets.mkdir()
    vertices = [(0.70, 0.55), (0.90, 0.55), (0.80, 0.35)]
    (assets / 'vertices.buf').write_bytes(b''.join(struct.pack('<2f', *point) for point in vertices))
    wide = case == 'index32'
    (assets / 'indices.buf').write_bytes(struct.pack('<3I' if wide else '<3H', 0, 1, 2))
    (assets / 'resources.ini').write_text(
        'namespace = shared\n[ResourceVertex]\ntype = Buffer\nstride = 8\nfilename = vertices.buf\n'
        '[ResourceIndex]\ntype = Buffer\nformat = R%d_UINT\nfilename = indices.buf\n' % (32 if wide else 16), encoding='ascii')
    body = ('vb0 = Resource\\shared\\Vertex\nib = Resource\\shared\\Index\n'
            'drawindexed = 3,0,0\n')
    if case == 'bounds': body = body.replace('3,0,0', '4,0,0')
    if case == 'post': body = ''.join('post ' + line + '\n' for line in body.splitlines())
    if case == 'restore':
        body = ('ResourceSavedVertex = ref vb0\nResourceSavedIndex = ref ib\n' + body +
                'post vb0 = ref ResourceSavedVertex\npost ib = ref ResourceSavedIndex\n')
    (mods / 'rules.ini').write_text(
        '[TextureOverrideMesh]\nhash = %08x\nmatch_first_index = 6\nmatch_index_count = 3\nhandling = skip\n' %
        crc32c(struct.pack('<3I', 14, 15, 16)) + body +
        ('[ResourceSavedVertex]\n[ResourceSavedIndex]\n' if case == 'restore' else ''), encoding='ascii')
    screen, source, trigger = root / 'screen.raw', root / 'source.raw', root / 'readback.trigger'
    keys = root / 'keys.txt'; keys.write_text('1\n', encoding='ascii')
    env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(root / 'settings'),
               EFMI_VK_M2_ENABLED='1', EFMI_VK_TEST_NOTICE='> configuration geometry',
               EFMI_VK_TEST_NOTICES_READBACK=str(screen), EFMI_VK_TEST_NOTICES_READBACK_TRIGGER=str(trigger),
               EFMI_VK_TEST_KEYS=str(keys))
    args = ['--readable-swapchain', '--api', '1.3', '--native-output', str(source),
            '--native-output-classic', '1', '--native-depth-destination', '--native-output-depth']
    if case == 'two_queues': args += ['--two-queues']
    with Sandbox(seconds=5, threads=0, validation=True, env_extra=env, extra_args=args) as sb:
        with ProbeClient(sb.pid) as probe:
            deadline = time.monotonic() + 1.0
            while time.monotonic() < deadline:
                status = probe.status().get('replacement', {})
                if status.get('shader_overrides', {}).get('executed', 0) > 2: break
                time.sleep(.05)
            if case == 'swap':
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
    expected = case != 'bounds'
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
    checks.equal(region(.84, .96) > 200, expected and case != 'reload', case + ': shared file vertices reach actual pixels')
    if case == 'reload': checks.check(region(.04, .16) > 200, 'reload: changed file content reaches the left triangle')
    if not expected:
        checks.check(bool(status.get('shader_overrides', {}).get('last_reason')), case + ': refusal is reported')
    print('artifacts:', root)


def main():
    checks = Checks('configuration geometry')
    for case in sys.argv[1:] or ['index16', 'index32', 'bounds', 'post', 'restore', 'two_queues', 'swap', 'reload']:
        scene(checks, case)
    return checks.finish()


if __name__ == '__main__':
    raise SystemExit(main())
