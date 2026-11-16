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


def scene(checks, case, matched=False, output_only=False, merged_outputs=False, cold=False, module_exchange=False):
    root = Path(test_dir('classic-mod-character-' + case + '-'))
    mods = root / 'Mods'
    folder = mods / 'fixture'
    folder.mkdir(parents=True)
    fixture(folder)
    vertex = root / 'vertex.spv'
    subprocess.run([str(Path(DIST) / 'efmi_vk_variant_gpu.exe'), '--character-vertex', str(vertex)], check=True)
    shader_hash = crc32c(vertex.read_bytes())
    ini = (folder / 'mod.ini').read_text(encoding='ascii')
    if merged_outputs and case != 'control':
        component=1 if case.startswith('between') else 0
        header='[CommandListDraw%d]\n'%component
        first=ini.index(header)+len(header)
        last=ini.index('[CommandListDraw%d]'%(component+1),first)
        body=ini[first:last]
        if case.startswith('between'):
            marker='drawindexed = 3,0,0\n';body=body.replace(marker,marker+'oD = null\n',1)
            if case=='between_restore':body='ResourceDepth = ref oD\n'+body+'oD = ref ResourceDepth\n'
        elif case=='post':body+='oD = null\n'
        elif case=='restore':body='ResourceDepth = ref oD\noD = null\n'+body+'oD = ref ResourceDepth\n'
        else:body='oD = null\n'+body
        ini=ini[:first]+body+ini[last:]+'\n[ResourceDepth]\n'
    ini = ini.replace('[Constants]\n', '[Constants]\nglobal $null_once = 0\n', 1)
    ini = ini.replace('[Present]\n', '[Present]\n$null_once = 0\n', 1)
    if not merged_outputs:ini += '\n[ShaderOverrideNull]\nhash = %08x\n' % shader_hash
    if case != 'control' and not matched:
        ini += 'if $null_once == 0\n$null_once = 1\noD = null\nendif\n'
    (folder / 'mod.ini').write_text(ini, encoding='ascii')
    props = mods / 'props'
    props.mkdir()
    prop_ini=(
        'namespace = runtime_prop\n[TextureOverrideProp]\nhash = %08x\nmatch_index_count = 3\n'
        'match_first_index = 30\nps-t1 = ResourceBorrowed\n[ResourceBorrowed]\n'
        'filename = ..\\fixture\\red.dds\n' % crc32c(struct.pack('<3H', 0, 1, 2)))
    if matched and not merged_outputs:
        command='' if case=='control' else 'post oD = null\n' if case=='post' else 'ResourceDepth = ref oD\noD = null\npost oD = ref ResourceDepth\n' if case=='restore' else 'oD = null\n'
        if case=='hide':command='handling = skip\n'+command
        prop_ini=prop_ini.replace('ps-t1 = ResourceBorrowed\n',command+'ps-t1 = ResourceBorrowed\n',1)+'[ResourceDepth]\n'
    if output_only or module_exchange or (merged_outputs and case=='swap'):
        prop_ini=prop_ini.replace('ps-t1 = ResourceBorrowed\n','').replace('[ResourceBorrowed]\nfilename = ..\\fixture\\red.dds\n','')
    (props / 'prop.ini').write_text(prop_ini,encoding='ascii')
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
    if cold:extra['EFMI_VK_M2_TEXTURE_PRELOAD']='0'
    if matched:extra['EFMI_VK_TEST_MOD_OUTPUT_DEPTH']='1'
    if output_only:extra['EFMI_VK_TEST_UNMATCHED_SHADE']='0' if case=='hide' else '1'
    if merged_outputs and case=='swap':extra['EFMI_VK_TEST_UNMATCHED_SHADE']='1'
    if module_exchange:extra['EFMI_VK_TEST_UNMATCHED_SHADE']='1'
    env = (inject_env if case == 'rp2_inject' else layer_env)(str(root / 'layer.log'), extra=extra)
    env['VK_LAYER_PATH'] = extra['VK_LAYER_PATH']
    args = [str(Path(DIST) / 'efmi_vk_variant_gpu.exe'), '--character', '--material', '--lazy-packages']
    if case == 'rp2_inject':
        args += ['--renderpass2', '--inject', INJECT_DLL]
    if case=='swap' or module_exchange:args+=['--module',str(Path(DIST)/'efmi_vk_backend.dll')]
    result = subprocess.run(args, env=env, cwd=folder, capture_output=True, text=True, errors='replace', timeout=120)
    output = result.stdout + result.stderr
    (root / 'stdout.txt').write_text(output.encode('ascii', 'backslashreplace').decode(), encoding='ascii')
    checks.equal(result.returncode, 0, case + ': independent scene pixels, changing bones and restoration')
    checks.check('validation: errors=0 warnings=0' in output, case + ': clean core and synchronization validation')
    report = folder / ('character-swapped.json' if module_exchange else 'character-final.json')
    if matched and not report.exists():report=folder/'character-active.json'
    status = json.loads(report.read_text(encoding='utf-8')).get('result', {}).get('replacement', {}) if report.exists() else {}
    if module_exchange:
        checks.check(status.get('module', {}).get('generation', 0) > 0, case + ': backend exchange actually completed')
    packages = status.get('packages', [])
    merged = next((item for item in packages if item.get('components', 0) > 0), {})
    texture = next((item for item in packages if Path(item.get('path', '')).parent.name == 'props'), {})
    checks.check(merged.get('counters', {}).get('replaced', 0) > 0, case + ': merged bone variants actually execute')
    if module_exchange or (merged_outputs and case=='swap'):
        checks.equal(texture.get('counters',{}).get('textured',0),0,case+': original application images survive the module exchanges')
    elif output_only:
        checks.equal(texture.get('counters',{}).get('textured',0),0,case+': application images stay original without a private texture variant')
        if case!='control':checks.check(texture.get('counters',{}).get('output_calls',0)>0 and not texture.get('last_reason'),case+': matched native application-call executor runs without mesh activation')
        if case=='hide':checks.check(texture.get('counters',{}).get('hidden',0)>0,case+': handling skip still executes its output list')
    else:
        checks.check(texture.get('counters', {}).get('textured', 0) > 0 and not texture.get('last_reason'),
                     case + ': texture-only private image variants actually execute')
    if case != 'control' and not matched:
        overrides = status.get('shader_overrides', {})
        checks.check(overrides.get('executed', 0) > 0 and not overrides.get('last_reason'), case + ': depth unbind executes')
    if matched:
        depth_path=folder/'depth.raw';data=depth_path.read_bytes() if depth_path.exists() else b''
        checks.equal(len(data),64*64*4,case+': complete fenced depth readback')
        if len(data)==64*64*4:
            values=struct.unpack('<4096f',data)
            checks.check(all(0<=value<=1 for value in values),case+': every depth texel has a valid value')
            def depth_at(x,y):return values[int((y+1)*32)*64+int((x+1)*32)]<.99
            checks.equal(depth_at(-.6,.3),not merged_outputs or case in ('control','post','between','between_restore'),case+': merged geometry observes its own output boundary')
            if merged_outputs:
                checks.equal(depth_at(0,0),case in ('control','restore','between','between_restore'),case+': first draw of the second component observes its output prefix')
                checks.equal(depth_at(.6,.3),case in ('control','restore'),case+': second component draw with shared bones observes the intervening output event')
            checks.equal(depth_at(.6,-.1),case in (('control','restore','between_restore') if merged_outputs else ('control','post')),case+': application geometry writes depth at the requested pre/post boundary')
            checks.equal(depth_at(0,.6),case in ('control','restore','between_restore'),case+': following independent geometry observes null or restored depth')
    if result.returncode:
        print(output[-5000:])
    print('artifacts:', root)
    return status


if __name__ == '__main__':
    checks = Checks('native classic Mod character')
    selected = set(sys.argv[1:])
    for case in ('control', 'null', 'rp2_inject'):
        if not selected or case in selected:
            scene(checks, case)
    raise SystemExit(checks.finish())
