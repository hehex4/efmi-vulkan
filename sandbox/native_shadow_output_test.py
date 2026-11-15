"""Immediate opacity shadows with ordered outputs, using owned replay readbacks."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from harness import Checks, DIST, ROOT

sys.path.insert(0, str(Path(ROOT) / 'scripts'))
import replay_capture as replay


def main(snapshot=False):
    root = Path(ROOT)
    out = root / 'build/replay/vk11'
    prep = subprocess.run([sys.executable, '-B', str(root / 'scripts/replay_capture.py'),
                           '--bundle', 'F:/efmi vulkan/captures/catalogue-20261019/vulkan/vk11',
                           '--copy-passes', '--prepare-only'], cwd=root, capture_output=True, text=True, errors='replace')
    (root / 'build/native-shadow-output-build.out').write_text(
        (prep.stdout + prep.stderr).encode('ascii', 'backslashreplace').decode(), encoding='ascii')
    if prep.returncode:
        print(prep.stdout + prep.stderr)
        return prep.returncode
    source = replay.MODS / 'ardelia lily whisper 1.5'
    mods = root / 'build/native-shadow-output/Mods'
    mod = mods / 'mod'
    mod.mkdir(parents=True, exist_ok=True)
    shutil.copytree(replay.MODS / 'EFMITTL', mods / 'EFMITTL', dirs_exist_ok=True)
    original = (source / 'ardelia.ini').read_text(encoding='utf-8-sig')
    # Preserve original relative declarations; installed assets are read only.
    for name in re.findall(r'(?mi)^\s*filename\s*=\s*([^;\r\n]+)', original):
        relative = Path(name.strip())
        assert str(relative).isascii() and not relative.is_absolute() and '..' not in relative.parts
        asset, target = source / relative, mod / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or (target.stat().st_size, target.stat().st_mtime_ns) != (asset.stat().st_size, asset.stat().st_mtime_ns):
            shutil.copy2(asset, target)
    original += '\n[ResourceEventDepth]\n'
    call = '    run = CommandList\\EFMITTL\\Draw'
    assert original.count(call) == 1
    guard = '    if $\\EFMITTL\\vs_kind == 1 && o0 !== null && o2 === null\n' if snapshot else '    if $\\EFMITTL\\vs_kind == 2 || $\\EFMITTL\\vs_kind == 3\n'
    save = '        ResourceEventDepth = ref oD\n'
    null = '        oD = null\n'
    restore = '        oD = ref ResourceEventDepth\n'
    checks = Checks('native capture attachment snapshots' if snapshot else 'native opacity shadow outputs')
    results, coverage = {}, {}
    cases = ('control', 'null', 'colour_null', 'both_null', 'restore_before') if snapshot else ('control', 'null', 'restore', 'restore_before', 'post_restore')
    for case in cases:
        before, after = '', ''
        if case == 'null':
            before = guard + null + '    endif\n'
        elif case == 'restore':
            before = guard + save + null + '    endif\n'
            after = '\n' + guard + restore + '    endif'
        elif case == 'restore_before':
            before = guard + save + null + restore + '    endif\n'
        elif case == 'post_restore':
            after = '\n' + guard + save + null + restore + '    endif'
        text = original.replace(call, before + call + after)
        if snapshot:
            text = original
            header = '[CommandList_Draw_Component2]\n'
            assert text.count(header) == 1
            if case in ('colour_null', 'both_null'):
                before = guard + '        o0 = null\n' + (null if case == 'both_null' else '') + '    endif\n'
            text = text.replace(header, header + before, 1)
        (mod / 'mod.ini').write_text(text.encode('ascii', 'backslashreplace').decode(), encoding='ascii')
        replay.MODS = mods
        run = replay.run(out / 'replay_gpu.exe', out, Path(DIST), 30, True,
                         Path('F:/vulkan/validation-1.4.321.0/bin'), name=('capture-snapshot-' if snapshot else 'shadow-output-') + case)
        checks.equal(run['returncode'], 0, case + ': normal completion')
        checks.check('validation errors=0 ' in run['output'], case + ': clean core/sync validation')
        replacement = run['status'].get('result', {}).get('replacement', {})
        totals = replacement.get('counters', {})
        checks.check(totals.get('translucent_shadows', 0) > 40, case + ': opacity shadow variants execute')
        checks.check(not any(v for k, v in totals.items() if k.startswith('refused_')), case + ': every matched plan has an executor')
        transparency = replacement.get('translucency', {})
        checks.check(transparency.get('captures', 0) > 20 and transparency.get('composites', 0) > 20 and not transparency.get('last_error'),
                     case + ': deferred forward transparency remains active')
        index = json.loads((run['folder'] / 'targets.json').read_text())
        targets, depths, complete = {}, {}, True
        for target in index['targets']:
            data = (run['folder'] / target['file']).read_bytes()
            complete &= len(data) == index['width'] * index['height'] * target['bytes']
            key = target['group'], target['attachment']
            targets[key] = target['format'], hashlib.sha256(data).hexdigest()
            if target['format'] in (124, 126, 130):
                depths[key] = replay.picture(data, target['format'], index['width'] * index['height'])[1]
        checks.check(complete and bool(targets), case + ': every fenced target readback is complete')
        results[case], coverage[case] = targets, depths
        print(case, {k: totals.get(k, 0) for k in ('items', 'translucent_shadows', 'refused_state', 'refused_variant')}, depths)
    if snapshot:
        checks.equal(results['restore_before'], results['control'], 'restored subsequent output bindings preserve every earlier captured target byte')
        return checks.finish()
    for case in ('null', 'restore'):
        changed = {key for key in results['control'] if results['control'][key] != results[case].get(key)}
        checks.check(bool(changed) and all(124 <= results['control'][key][0] <= 130 for key in changed),
                     case + ': output changes affect only shadow depth targets')
    checks.equal(results['restore_before'], results['control'], 'restoring before the shadow recovers every control target byte')
    checks.equal(results['post_restore'], results['control'], 'post-draw null/restoration preserves every earlier target byte')
    checks.check(any(coverage['null'].get(key, 0) < value < coverage['control'].get(key, 0)
                     for key, value in coverage['restore'].items()),
                 'restoration preserves later shadow geometry while the unbound shadow contributes no depth')
    return checks.finish()


if __name__ == '__main__':
    raise SystemExit(main())
