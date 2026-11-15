"""Application image references move between logical pixel slots on matched draws."""
from pathlib import Path
from unittest.mock import patch
import sys
import os
import native_classic_mod_character_test as character
import runtime_test
from harness import Checks


def scene(checks, case):
    original_env = character.layer_env
    merged = case.startswith('merged')
    absent = case.endswith('absent')
    def fixture_env(path, *args, **kwargs):
        extra = kwargs['extra']
        root = Path(extra['EFMI_VK_M2_MODS']).parent
        ini = root / ('Mods/fixture/mod.ini' if merged else 'Mods/props/prop.ini')
        text = ini.read_text()
        source = 9 if absent else 2
        commands = 'ResourceSaved = ref ps-t%d\n' % source
        if case.endswith('chain'):
            commands += 'ps-t2 = %s\n' % ('ResourceRed' if merged else 'ResourceBorrowed')
        commands += 'ps-t1 = ref ResourceSaved\n'
        if merged:
            marker = 'ps-t18 = ResourceGreen\n'
            assert marker in text
            text = text.replace(marker, marker + commands, 1)
        else:
            text = text.replace('ps-t1 = ResourceBorrowed\n', commands, 1)
        text += '\n[ResourceSaved]\n'
        ini.write_text(text, encoding='ascii')
        extra['EFMI_VK_TEST_CROSS_TEXTURE'] = '1'
        if case.endswith('general'):
            extra['EFMI_VK_TEST_CROSS_GENERAL'] = '1'
        extra['EFMI_VK_TEST_A_SHADE' if merged else 'EFMI_VK_TEST_UNMATCHED_SHADE'] = '7' if absent else '8'
        return original_env(path, *args, **kwargs)
    with patch.object(character, 'layer_env', fixture_env):
        status = character.scene(checks, 'control', matched=True, merged_outputs=merged)
    checks.equal(status.get('textures', {}).get('draw_waits'), 0, case + ': application views add no file wait')


def own_mesh(checks, beside):
    original_env = runtime_test.layer_env
    def fixture_env(path, *args, **kwargs):
        extra = kwargs['extra']
        ini = Path(extra['EFMI_VK_M2_MODS']) / 'props/prop.ini'
        text = ini.read_text().replace('drawindexed = 3, 0, 0\n',
            'ResourceSaved = ref ps-t2\nps-t1 = ref ResourceSaved\ndrawindexed = 3, 0, 0\n', 1)
        ini.write_text(text + '\n[ResourceSaved]\n', encoding='ascii')
        extra['EFMI_VK_TEST_CROSS_TEXTURE'] = '1'
        extra['EFMI_VK_TEST_OWN_SHADE'] = '8'
        return original_env(path, *args, **kwargs)
    with patch.object(runtime_test, 'layer_env', fixture_env):
        runtime_test.own_mesh_case(checks, os.environ.get('EFMI_VK_VALIDATION_DIR', 'F:/vulkan/validation-1.4.321.0/bin'), beside)


if __name__ == '__main__':
    checks = Checks('application texture rebinding')
    selected = set(sys.argv[1:])
    for case in ('texture', 'texture_absent', 'texture_chain', 'texture_general',
                 'merged', 'merged_absent', 'merged_chain', 'merged_general', 'own', 'own_beside'):
        if not selected or case in selected:
            if case.startswith('own'):
                own_mesh(checks, case == 'own_beside')
            else:
                scene(checks, case)
    raise SystemExit(checks.finish())
