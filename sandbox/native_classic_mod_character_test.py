"""Masked classic passes through merged skeletons and a separate texture-only Mod."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

from harness import Checks, DIST, INJECT_DLL, inject_env, layer_env, test_dir
from runtime_fixture import CORE, fixture
from efmi_vk.hashes import crc32c


def scene(checks, case):
    root = Path(test_dir('classic-mod-character-' + case + '-'))
    mods = root / 'Mods'
    folder = mods / 'fixture'
    folder.mkdir(parents=True)
    fixture(folder)
    vertex = root / 'vertex.spv'
    subprocess.run([str(Path(DIST) / 'efmi_vk_variant_gpu.exe'), '--character-vertex', str(vertex)], check=True)
    shader_hash = crc32c(vertex.read_bytes())
    ini = (folder / 'mod.ini').read_text(encoding='ascii')
    ini = ini.replace('[Constants]\n', '[Constants]\nglobal $null_once = 0\n', 1)
    ini = ini.replace('[Present]\n', '[Present]\n$null_once = 0\n', 1)
    ini += '\n[ShaderOverrideNull]\nhash = %08x\n' % shader_hash
    if case != 'control':
        ini += 'if $null_once == 0\n$null_once = 1\noD = null\nendif\n'
    (folder / 'mod.ini').write_text(ini, encoding='ascii')
    props = mods / 'props'
    props.mkdir()
    (props / 'prop.ini').write_text(
        'namespace = runtime_prop\n[TextureOverrideProp]\nhash = %08x\nmatch_index_count = 3\n'
        'match_first_index = 30\nps-t1 = ResourceBorrowed\n[ResourceBorrowed]\n'
        'filename = ..\\fixture\\red.dds\n' % crc32c(struct.pack('<3H', 0, 1, 2)), encoding='ascii')
    (root / 'd3dx.ini').write_text('[Include]\ninclude = Core\\EFMI\\main.ini\ninclude_recursive = Mods\n'
                                  '[Rendering]\nrecursive_include = 1\n', encoding='ascii')
    settings = root / 'settings'
    settings.mkdir()
    validation = os.environ.get('EFMI_VK_VALIDATION_DIR', 'F:/vulkan/validation-1.4.321.0/bin')
    extra = dict(EFMI_VK_M2_MODS=str(mods), EFMI_VK_M2_CORE=str(CORE), EFMI_VK_M2_SETTINGS=str(settings),
                 EFMI_VK_M2_ENABLED='1', EFMI_VK_M2_TEXTURE_PRELOAD='1', EFMI_VK_M2_IDENTITY_EXPIRY_MS='500',
                 EFMI_VK_TEST_UNMATCHED_SHADE='5', EFMI_VK_TEST_CLASSIC_NULL='1',
                 VK_LAYER_PATH=os.pathsep.join([DIST, validation]), VK_LAYER_VALIDATE_SYNC='1',
                 VK_LAYER_DUPLICATE_MESSAGE_LIMIT='0', XDG_CACHE_HOME=str(root), TMPDIR=str(root), TEMP=str(root), TMP=str(root))
    env = (inject_env if case == 'rp2_inject' else layer_env)(str(root / 'layer.log'), extra=extra)
    env['VK_LAYER_PATH'] = extra['VK_LAYER_PATH']
    args = [str(Path(DIST) / 'efmi_vk_variant_gpu.exe'), '--character', '--material', '--lazy-packages']
    if case == 'rp2_inject':
        args += ['--renderpass2', '--inject', INJECT_DLL]
    result = subprocess.run(args, env=env, cwd=folder, capture_output=True, text=True, errors='replace', timeout=120)
    output = result.stdout + result.stderr
    (root / 'stdout.txt').write_text(output.encode('ascii', 'backslashreplace').decode(), encoding='ascii')
    checks.equal(result.returncode, 0, case + ': independent scene pixels, changing bones and restoration')
    checks.check('validation: errors=0 warnings=0' in output, case + ': clean core and synchronization validation')
    report = folder / 'character-final.json'
    status = json.loads(report.read_text(encoding='utf-8')).get('result', {}).get('replacement', {}) if report.exists() else {}
    packages = status.get('packages', [])
    merged = next((item for item in packages if item.get('components', 0) > 0), {})
    texture = next((item for item in packages if Path(item.get('path', '')).parent.name == 'props'), {})
    checks.check(merged.get('counters', {}).get('replaced', 0) > 0, case + ': merged bone variants actually execute')
    checks.check(texture.get('counters', {}).get('textured', 0) > 0 and not texture.get('last_reason'),
                 case + ': texture-only private image variants actually execute')
    if case != 'control':
        overrides = status.get('shader_overrides', {})
        checks.check(overrides.get('executed', 0) > 0 and not overrides.get('last_reason'), case + ': depth unbind executes')
    if result.returncode:
        print(output[-5000:])
    print('artifacts:', root)


if __name__ == '__main__':
    checks = Checks('native classic Mod character')
    selected = set(sys.argv[1:])
    for case in ('control', 'null', 'rp2_inject'):
        if not selected or case in selected:
            scene(checks, case)
    raise SystemExit(checks.finish())
