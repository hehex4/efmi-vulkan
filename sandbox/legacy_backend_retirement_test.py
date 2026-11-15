"""Retired single-component packages never activate a second replacement backend."""
from pathlib import Path
import struct
from harness import Checks, Sandbox, test_dir
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient, ProbeError


def legacy_package(path):
    positions = b''.join(struct.pack('<4f', x + 1, y, 0, 0) for x, y in ((-.22, -.2), (.22, -.2), (0, .22)))
    payload = positions + bytes(24) + struct.pack('<8H', 65535, 0, 0, 0, 266, 0, 0, 0) * 3 + struct.pack('<3I', 0, 1, 2)
    header = struct.pack('<8s12I', b'EFMIM201', crc32c(struct.pack('<3H', 0, 1, 2)), 3, 0, 499, 266, 12, 3, 3, 16, 8, 16, crc32c(payload))
    path.write_bytes(header + payload)


def main():
    checks = Checks('legacy backend retirement')
    root = Path(test_dir('legacy-package-'))
    package = root / 'legacy.bin'
    legacy_package(package)
    for entry in ('layer', 'inject'):
        with Sandbox(seconds=3, entry=entry, validation=True,
                     env_extra={'EFMI_VK_M2_PACKAGE': str(package), 'EFMI_VK_M2_ENABLED': '0'}) as app:
            with ProbeClient(app.pid) as probe:
                replacement = probe.status().get('replacement', {})
                checks.equal(replacement.get('configured'), False, entry + ': legacy package never activates')
                checks.equal(replacement.get('enabled'), False, entry + ': replacement remains disabled')
                refused = False
                try:
                    probe.request('m2', enabled=0)
                except ProbeError as exc:
                    refused = 'not configured' in str(exc)
                checks.check(refused, entry + ': legacy enable control reports unavailable backend')
            app.proc.wait(timeout=30)
            checks.equal(app.proc.returncode, 0, entry + ': original sandbox runs and exits normally')
            checks.check('validation: errors=0 warnings=0' in app.output(), entry + ': clean core/sync validation')
            checks.check('single-component M2 packages are retired' in app.log(), entry + ': explicit migration diagnostic')
        print('artifacts:', app.dir)
    return checks.finish()


if __name__ == '__main__':
    raise SystemExit(main())
