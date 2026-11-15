"""Noncomponent meshes use the library's opacity shadows and ordered outputs."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from harness import Checks, DIST, ROOT
from native_image_test import dds
from own_shadow_fixture import mesh

sys.path.insert(0, str(Path(ROOT) / 'scripts'))
import replay_capture as replay


def main(merged=False):
    root = Path(ROOT)
    out = root / 'build/replay/vk11'
    prep = subprocess.run([sys.executable, '-B', str(root / 'scripts/replay_capture.py'),
                           '--bundle', 'F:/efmi vulkan/captures/catalogue-20261019/vulkan/vk11',
                           '--copy-passes', '--prepare-only'], cwd=root, capture_output=True, text=True, errors='replace')
    (root / 'build/native-own-shadow-build.out').write_text(
        (prep.stdout + prep.stderr).encode('ascii', 'backslashreplace').decode(), encoding='ascii')
    if prep.returncode:
        print(prep.stdout + prep.stderr)
        return prep.returncode
    source = replay.MODS / 'ardelia lily whisper 1.5'
    prefix = 'merged-opacity-' if merged else 'own-shadow-'
    mods = root / ('build/native-merged-opacity/Mods' if merged else 'build/native-own-shadow/Mods')
    mod = mods / 'mod'
    mod.mkdir(parents=True, exist_ok=True)
    shutil.copytree(replay.MODS / 'EFMITTL', mods / 'EFMITTL', dirs_exist_ok=True)
    original = (source / 'ardelia.ini').read_text(encoding='utf-8-sig')
    for name in re.findall(r'(?mi)^\s*filename\s*=\s*([^;\r\n]+)', original):
        relative = Path(name.strip())
        assert str(relative).isascii() and not relative.is_absolute() and '..' not in relative.parts
        asset, target = source / relative, mod / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or (target.stat().st_size, target.stat().st_mtime_ns) != (asset.stat().st_size, asset.stat().st_mtime_ns):
            shutil.copy2(asset, target)
    # This component deliberately bypasses the merged skeleton. Its vertices/bones
    # are the ordinary mesh route's; this fixture does not claim character acceptance.
    if not merged:
        start = original.index('[TextureOverride_EntryPoint_Component1]\n')
        end = original.index('[TextureOverride_EntryPoint_Component2]\n', start)
        header = original[start:original.index('$object_detected', start)]
        original = original[:start] + header + 'handling = skip\n$lod_level = 0\nrun = CommandList_Draw_Component1\n\n' + original[end:]
        original = mesh(mod, original)
    original += '\n[ResourceShadowDepth]\n[ResourceShadowMask]\nfilename = shadow-mask.dds\n'
    dds(mod / 'shadow-mask.dds', (255, 0, 128, 255))
    call = '    run = CommandList\\EFMITTL\\Draw'
    assert original.count(call) == 1
    guard = '    if $\\EFMITTL\\vs_kind == 2 || $\\EFMITTL\\vs_kind == 3\n'
    checks = Checks('native merged opacity values' if merged else 'native own mesh opacity shadows')
    results = {}
    for case in ('low', 'high', 'restore_before', 'restore', 'mask_zero', 'mask_one', 'mask_invert'):
        before, after = '', ''
        if case in ('restore_before', 'restore'):
            before = '        ResourceShadowDepth = ref oD\n        oD = null\n'
            restore = '        oD = ref ResourceShadowDepth\n'
            if case == 'restore_before':
                before += restore
            else:
                after = restore
        if case.startswith('mask_'):
            before += '        Resource\\EFMITTL\\Mask = ref ResourceShadowMask\n'
            before += '        $\\EFMITTL\\mask_channel = ' + ('0' if case == 'mask_one' else '1') + '\n'
            before += '        $\\EFMITTL\\mask_invert = ' + ('1' if case == 'mask_invert' else '0') + '\n'
        text = original.replace(call, guard + before + call + '\n' + after + '    endif')
        text = text.replace('$\\EFMITTL\\alpha = 0.7', '$\\EFMITTL\\alpha = ' + ('0.25' if case in ('low', 'restore_before', 'restore') else '0.75'))
        (mod / 'mod.ini').write_text(text.encode('ascii', 'backslashreplace').decode(), encoding='ascii')
        replay.MODS = mods
        run = replay.run(out / 'replay_gpu.exe', out, Path(DIST), 30, True,
                         Path('F:/vulkan/validation-1.4.321.0/bin'), name=prefix + case)
        checks.equal(run['returncode'], 0, case + ': normal completion')
        checks.check('validation errors=0 ' in run['output'], case + ': clean core/sync validation')
        replacement = run['status'].get('result', {}).get('replacement', {})
        totals = replacement.get('counters', {})
        checks.check(totals.get('replaced' if merged else 'own_mesh', 0) > 20, case + ': actual requested mesh route draws')
        checks.check(totals.get('translucent_shadows', 0) > 40, case + ': opacity shadow variants execute')
        checks.check(not any(v for k, v in totals.items() if k.startswith('refused_')), case + ': no refused plans')
        index = json.loads((run['folder'] / 'targets.json').read_text())
        targets, complete = {}, True
        for target in index['targets']:
            data = (run['folder'] / target['file']).read_bytes()
            complete &= len(data) == index['width'] * index['height'] * target['bytes']
            targets[target['group'], target['attachment']] = target['format'], hashlib.sha256(data).hexdigest()
        checks.check(complete and bool(targets), case + ': complete fenced target bytes')
        results[case] = targets
        print(case, {k: totals.get(k, 0) for k in ('own_mesh', 'translucent_shadows', 'refused_state', 'refused_variant')})
    changed = {key for key in results['low'] if results['low'][key] != results['high'].get(key)}
    checks.check(bool(changed) and all(124 <= results['low'][key][0] <= 130 for key in changed), 'opacity changes shadow depth only')
    for case, control in (('restore_before', 'low'), ('mask_one', 'high'), ('mask_invert', 'high'), ('mask_zero', 'restore')):
        checks.equal(results[case], results[control], case + ': original mask/output semantics preserve every equivalent target byte')
    checks.check(results['mask_zero'] != results['high'], 'a zero mask actually removes the shadow geometry')
    return checks.finish()


if __name__ == '__main__':
    raise SystemExit(main())
