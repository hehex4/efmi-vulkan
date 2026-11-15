"""Original-library depth history through the native compositor and complete owned readbacks."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from harness import Checks, DIST, ROOT

sys.path.insert(0, str(Path(ROOT) / 'scripts'))
import replay_capture as replay


def main():
    root = Path(ROOT)
    out = root / 'build/replay/vk11'
    prep = subprocess.run([sys.executable, '-B', str(root / 'scripts/replay_capture.py'),
                           '--bundle', 'F:/efmi vulkan/captures/catalogue-20261019/vulkan/vk11',
                           '--copy-passes', '--prepare-only'], cwd=root, capture_output=True, text=True, errors='replace')
    (root / 'build/native-translucency-depth-build.out').write_text(
        (prep.stdout + prep.stderr).encode('ascii', 'backslashreplace').decode(), encoding='ascii')
    if prep.returncode:
        print(prep.stdout + prep.stderr)
        return prep.returncode
    checks = Checks('native translucency depth history')
    results = {}
    for name, null_at, restore_at in (('control', None, None), ('cold', 0, None),
                                      ('warm', 60, None), ('recover', 0, 60), ('warm-restore', 40, 70)):
        run = replay.run(out / 'replay_gpu.exe', out, Path(DIST), 100, True,
                         Path('F:/vulkan/validation-1.4.321.0/bin'), name='depth-' + name,
                         depth_null_after=null_at, depth_restore_after=restore_at)
        checks.equal(run['returncode'], 0, name + ': normal completion')
        checks.check('validation errors=0 ' in run['output'], name + ': clean core/synchronization errors')
        replacement = run['status'].get('result', {}).get('replacement', {})
        transparency = replacement.get('translucency', {})
        captures = transparency.get('captures', 0)
        composites = transparency.get('composites', 0)
        if name in ('cold', 'recover'):
            checks.check(captures == composites == 0, name + ': original incomplete layer remains uncaptured')
        else:
            checks.check(captures > 80 and composites > 80, name + ': capture and composite continue past depth changes')
        checks.check(not transparency.get('last_error'), name + ': absent depth is an ordinary library state')
        index = json.loads((run['folder'] / 'targets.json').read_text())
        targets = {}
        complete = True
        for target in index['targets']:
            data = (run['folder'] / target['file']).read_bytes()
            complete &= len(data) == index['width'] * index['height'] * target['bytes']
            targets[(target['group'], target['attachment'])] = (target['format'], hashlib.sha256(data).hexdigest())
        checks.check(complete and bool(targets), name + ': every final color/depth readback is complete')
        results[name] = targets
        print(name, transparency, next((line for line in run['output'].splitlines() if line.startswith('replay:')), 'missing summary'))
    # Both final passes have no DSV. Only a complete retained layer distinguishes them.
    cold, warm = results['cold'], results['warm']
    groups = {group for group, _ in cold}
    depth_formats = set(range(124, 131))
    forward = {(group, min(slot for (g, slot), (fmt, _) in cold.items() if g == group and fmt not in depth_formats))
               for group in groups if sum(g == group and fmt not in depth_formats for (g, _), (fmt, _) in cold.items()) == 2}
    changed = {key for key in cold if cold[key] != warm.get(key)}
    checks.check(bool(changed) and changed <= forward, 'warm null depth changes only the independent forward color0 readbacks')
    checks.equal(results['warm-restore'], results['control'], 'restoring scene depth recovers every control target byte')
    return checks.finish()


if __name__ == '__main__':
    raise SystemExit(main())
