"""TextureOverride commands need an original explicit entry, even without regex."""
import json
from pathlib import Path
import struct
import time

from harness import Checks, ROOT, Sandbox, configuration_root
from runtime_test import band_pixels, sandbox_shader_hashes
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def main():
    checks = Checks('native original command entry')
    root = Path(ROOT) / 'build/native-original-entry'
    commands = root / 'Commands'; commands.mkdir(parents=True, exist_ok=True)
    vertex, _ = sandbox_shader_hashes()
    for mode in ('absent', 'empty', 'gated', 'enabled'):
        ini = ('namespace = entry\n[Constants]\nglobal $hit = 0\n[TextureOverrideMesh]\nhash = %08x\n'
               'match_first_index = 6\nmatch_index_count = 3\n'
               '$hit = $hit + 1\npost $hit = $hit + 100\nhandling = skip\n' %
               crc32c(struct.pack('<3I', 14, 15, 16)))
        if mode != 'absent':
            ini += '[ShaderOverrideEntry]\nvk_hash = %08x\n' % vertex
        if mode in ('gated', 'enabled'):
            ini += 'if %d\nchecktextureoverride = ib\nendif\n' % (mode == 'enabled')
        (commands / 'entry.ini').write_text(ini, encoding='ascii')
        screen = root / (mode + '.raw')
        env = dict(EFMI_VK_CONFIGURATION=configuration_root(commands), EFMI_VK_M2_ENABLED='1',
                   EFMI_VK_M2_SETTINGS=str(root / 'settings'), EFMI_VK_TEST_NOTICE='> original command entry',
                   EFMI_VK_TEST_NOTICES_READBACK=str(screen))
        with Sandbox(seconds=3, threads=0, validation=True, env_extra=env,
                     extra_args=['--readable-swapchain']) as sb:
            time.sleep(.8)
            with ProbeClient(sb.pid) as probe:
                status = probe.status().get('replacement', {})
            sb.proc.wait(timeout=20)
            stdout = sb.output()
            (root / (mode + '-stdout.txt')).write_text(stdout, encoding='utf-8')
            (root / (mode + '-layer.log')).write_bytes(Path(sb.log_path).read_bytes())
            checks.equal(sb.proc.returncode, 0, mode + ': sandbox exits normally')
        hit = status.get('configuration_generation', {}).get('ini', {}).get('variables', {}).get('$\\entry\\hit')
        checks.check(hit is not None and (hit > 0 and hit % 101 == 0 if mode == 'enabled' else hit == 0),
                     mode + ': original command controls both IB phases', hit)
        checks.equal(band_pixels(screen), 5516 if mode == 'enabled' else 6418,
                     mode + ': real pixels follow the explicit original command')
        checks.check(not any(x in stdout for x in ('Validation Error', 'VUID-', 'SYNC-HAZARD')),
                     mode + ': Vulkan validation clean')
        (root / (mode + '-status.json')).write_text(json.dumps(status, indent=2), encoding='ascii')
    return checks.finish()


if __name__ == '__main__':
    raise SystemExit(main())
