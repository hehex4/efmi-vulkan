"""Immediate opacity shadows with ordered outputs, using owned replay readbacks."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from harness import Checks, DIST, ROOT, configuration_root

sys.path.insert(0, str(Path(ROOT) / 'scripts'))
import replay_capture as replay


def main(snapshot=False, forward=False):
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
    source = replay.INSTALLED_ROOT / 'Mods' / 'ardelia lily whisper 1.5'
    mods = root / 'build/native-shadow-output/Mods'
    mod = mods / 'mod'
    mod.mkdir(parents=True, exist_ok=True)
    shutil.copytree(replay.INSTALLED_ROOT / 'Mods' / 'EFMITTL', mods / 'EFMITTL', dirs_exist_ok=True)
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
    guard = '    if $\\EFMITTL\\vs_kind == 1 && o0 !== null && o2 === null\n' if snapshot or forward else '    if $\\EFMITTL\\vs_kind == 2 || $\\EFMITTL\\vs_kind == 3\n'
    save = '        ResourceEventDepth = ref oD\n'
    null = '        oD = null\n'
    restore = '        oD = ref ResourceEventDepth\n'
    checks = Checks('native deferred capture outputs' if forward else 'native capture attachment snapshots' if snapshot else 'native opacity shadow outputs')
    results, coverage, counters = {}, {}, {}
    cases = ('control', 'null', 'colour_null', 'both_null', 'restore_before') if snapshot else ('control', 'null', 'restore', 'restore_before', 'post_restore')
    if forward:
        cases += ('post_null', 'empty', 'interleaved')
    for case in cases:
        before, after = '', ''
        if case == 'null':
            before = guard + null + '    endif\n'
        elif case == 'restore':
            before = guard + save + null + '    endif\n'
            after = '\n' + guard + restore + '    endif'
        elif case in ('restore_before', 'empty'):
            before = guard + save + null + restore + '    endif\n'
        elif case == 'post_restore':
            after = '\n' + guard + save + null + restore + '    endif'
        elif case == 'post_null':
            after = '\n' + guard + null + '    endif'
        elif case == 'interleaved':
            before = guard + save + '        $event_count = $\\EFMITTL\\count\n        $event_first = $\\EFMITTL\\first\n        $event_alpha = $\\EFMITTL\\alpha\n    endif\n'
            repeat = '        $\\EFMITTL\\count = $event_count\n        $\\EFMITTL\\first = $event_first\n        $\\EFMITTL\\alpha = $event_alpha\n' + call + '\n'
            after = '\n' + guard + null + repeat + restore + repeat + '    endif'
        text = original.replace(call, before + call + after)
        if case == 'interleaved':
            text = text.replace('[Constants]\n', '[Constants]\nglobal $event_count = 0\nglobal $event_first = 0\nglobal $event_alpha = 0\n', 1)
        if case == 'empty':
            start = text.index('[CommandList_Draw_Component1]\n')
            end = text.index('[CommandList_Draw_Component2]\n', start)
            body = re.sub(r'(?m)^\s*drawindexed\s*=[^\r\n]+',
                          lambda match: 'if $\\EFMITTL\\vs_kind != 1 || o0 === null || o2 !== null\n' + match[0] + '\nendif', text[start:end])
            text = text[:start] + body + text[end:]
        if snapshot:
            text = original
            header = '[CommandList_Draw_Component2]\n'
            assert text.count(header) == 1
            if case in ('colour_null', 'both_null'):
                before = guard + '        o0 = null\n' + (null if case == 'both_null' else '') + '    endif\n'
            text = text.replace(header, header + before, 1)
        (mod / 'mod.ini').write_text(text.encode('ascii', 'backslashreplace').decode(), encoding='ascii')
        configuration = Path(configuration_root(mods))
        run = replay.run(out / 'replay_gpu.exe', out, Path(DIST), 30, True,
                         Path('F:/vulkan/validation-1.4.321.0/bin'), configuration=configuration, name=('capture-output-' if forward else 'capture-snapshot-' if snapshot else 'shadow-output-') + case)
        checks.equal(run['returncode'], 0, case + ': normal completion')
        checks.check('validation errors=0 ' in run['output'], case + ': clean core/sync validation')
        replacement = run['status'].get('result', {}).get('replacement', {})
        totals = replacement.get('counters', {})
        checks.check(totals.get('translucent_shadows', 0) > 40, case + ': opacity shadow variants execute')
        checks.check(not any(v for k, v in totals.items() if k.startswith('refused_')), case + ': every matched plan has an executor')
        transparency = replacement.get('translucency', {})
        if forward and case in ('null', 'restore'):
            checks.check(transparency.get('captures', 0) == transparency.get('composites', 0) == 0 and not transparency.get('last_error'),
                         case + ': depth absent at capture retains the original incomplete-layer behavior')
        else:
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
        counters[case] = totals
        print(case, {k: totals.get(k, 0) for k in ('items', 'translucent_shadows', 'refused_state', 'refused_variant')}, depths)
    if forward:
        for case in ('restore_before', 'post_restore', 'interleaved'):
            checks.equal(results[case], results['control'], case + ': original capture order preserves every control target byte')
        checks.check(counters['interleaved'].get('translucent', 0) > 60, 'three ordered captures actually reach the deferred queue')
        checks.check(counters['empty'].get('items', 0) < counters['control'].get('items', 0) and counters['empty'].get('translucent', 0) > 20,
                     'a capture-only component runs its output events with no immediate mesh draw')
        changed = {key for key in results['control'] if results['control'][key] != results['restore'].get(key)}
        checks.check(bool(changed) and all(key[1] == 0 and results['control'][key][0] < 124 for key in changed),
                     'restoring after a cold capture removes only its composed colour while preserving opaque depth')
        return checks.finish()
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
