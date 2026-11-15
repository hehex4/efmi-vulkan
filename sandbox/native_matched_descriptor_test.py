"""Matched geometry and private texture variants execute ordered native descriptors."""
from pathlib import Path
from unittest.mock import patch
import json
import struct
import subprocess
import sys
import native_classic_mod_test as mesh
import native_classic_mod_character_test as character
from harness import Checks


def own_mesh(checks, case):
    class Fixture(mesh.Sandbox):
        def __enter__(self):
            mods = Path(self.env_extra['EFMI_VK_M2_MODS'])
            root = mods.parent
            ini = mods / 'mod.ini'
            text = ini.read_text()
            vertex, fragment = mesh.sandbox_shader_hashes()
            text = text.replace('[ShaderOverrideTarget]\nhash = %08x\n' % vertex,
                '[ShaderOverrideTarget]\nhash = %08x\nvk_binding = vs-cb0, 0, 0\n' % vertex)
            text = text.replace('handling = skip\n',
                'handling = skip\nResourceUniform = ref vs-cb0\nvs-cb0 = ResourceGreen\npost vs-cb0 = ref ResourceUniform\n', 1)
            if case == 'ordered':
                text = text.replace('vb0 = ResourceVertex\ndrawindexed',
                    'vs-cb0 = ResourceBlue\nvb0 = ResourceVertex\ndrawindexed', 1)
                text = text.replace('vb0 = ResourceTop\ndrawindexed',
                    'vs-cb0 = ResourceGreen\nvb0 = ResourceTop\ndrawindexed', 1)
            text += '\n[ResourceUniform]\n'
            for name, color in (('Green', [0, 1, 0, 1]), ('Blue', [0, 0, 1, 1])):
                text += '[Resource%s]\ntype = Buffer\nstride = 16\nbind_flags = constant_buffer\nfilename = %s.buf\n' % (name, name)
                (mods / (name + '.buf')).write_bytes(struct.pack('<24f', *([0] * 4 + color + [0] * 16)))
            ini.write_text(text, encoding='ascii')
            fixes = root / 'settings/ShaderFixes'
            fixes.mkdir()
            hlsl = root / 'constant-color.hlsl'
            hlsl.write_text('[[vk::binding(0,0)]] cbuffer U { float4 offset; float4 color; };\n'
                'float4 main() : SV_Target0 { return color.y > .9 ? float4(0,1,0,1) : '
                'color.z > .9 ? float4(0,0,1,1) : float4(1,0,1,1); }\n', encoding='ascii')
            mesh.compile_shader(hlsl, fixes, '%08x' % fragment, 'ps')
            Fixture.pixels = Path(self.env_extra['EFMI_VK_TEST_NOTICES_READBACK'])
            return super().__enter__()

    with patch.object(mesh, 'Sandbox', Fixture):
        result = mesh.scene(checks, 'depth_mesh_ordered_restore' if case == 'ordered' else
                            'depth_mesh_swap' if case == 'swap' else 'depth_mesh_control', matched=True)
    pixels = Fixture.pixels
    meta = json.loads(Path(str(pixels) + '.json').read_text())
    width, height = meta['width'], meta['height']
    data = pixels.read_bytes()
    def colored(rgb, lo, hi, ylo=.64, yhi=.8):
        if 44 <= meta['format'] <= 50:
            rgb = tuple(reversed(rgb))
        return sum(data[4 * (y * width + x):][:3] == bytes(rgb)
            for y in range(int(height * ylo), int(height * yhi))
            for x in range(int(width * lo), int(width * hi)))
    checks.check(colored((0, 0, 255) if case == 'ordered' else (0, 255, 0), .84, .96) > 200,
                 case + ': independent Mod pixels read the explicit native constant')
    if case == 'ordered':
        checks.check(colored((0, 255, 0), .04, .16) > 200 and colored((0, 255, 0), .44, .56, .2, .34) > 200,
                     case + ': the first and third meshes read their own ordered bindings')
    checks.equal(result.get('textures', {}).get('draw_waits'), 0, case + ': native file readiness does not wait under the draw lock')


def texture_constant(checks):
    original_env = character.layer_env
    def fixture_env(path, *args, **kwargs):
        extra = kwargs['extra']
        root = Path(extra['EFMI_VK_M2_MODS']).parent
        props = root / 'Mods/props'
        ini = props / 'prop.ini'
        vertex = character.crc32c((root / 'vertex.spv').read_bytes())
        text = ini.read_text().replace('ps-t1 = ResourceBorrowed\n',
            'ResourceUniform = ref vs-cb13\nvs-cb13 = ResourceBlack\n'
            'ps-t1 = ResourceBorrowed\npost vs-cb13 = ref ResourceUniform\n', 1)
        text += '\n[ShaderOverrideAlias]\nhash = %08x\nvk_binding = vs-cb13, 2, 0\n' % vertex
        text += '[ResourceUniform]\n[ResourceBlack]\ntype = Buffer\nstride = 16\nbind_flags = constant_buffer\nfilename = black.buf\n'
        # This synthetic scene's unmatched component has these offsets. Runtime code has no fixture tables.
        data = bytearray(65536)
        struct.pack_into('<4f', data, 48, 10, 20, 30, 1)
        struct.pack_into('<4I', data, 80, 2800, 2900, 258, 0x3f800000)
        (props / 'black.buf').write_bytes(data)
        ini.write_text(text, encoding='ascii')
        extra['EFMI_VK_TEST_UNMATCHED_SHADE'] = '7'
        return original_env(path, *args, **kwargs)
    with patch.object(character, 'layer_env', fixture_env):
        status = character.scene(checks, 'control', matched=True)
    checks.equal(status.get('textures', {}).get('draw_waits'), 0,
                 'texture-only: native constant and private image publish without recording waits')
    textures = status.get('textures', {})
    checks.check(textures.get('submissions', 0) >= 3 and textures.get('file_reads', 9999) < 30,
                 'texture-only: a resident image arena does not starve the buffer upload')


def merged_constant(checks):
    original_env = character.layer_env
    def fixture_env(path, *args, **kwargs):
        extra = kwargs['extra']
        root = Path(extra['EFMI_VK_M2_MODS']).parent
        folder = root / 'Mods/fixture'
        fragment = root / 'fragment.spv'
        subprocess.run([str(Path(character.DIST) / 'efmi_vk_character_test.exe'), '--material-shader', str(fragment)], check=True)
        shader_hash = character.crc32c(fragment.read_bytes())
        ini = folder / 'mod.ini'
        text = ini.read_text().replace('[CommandListDraw0]\n',
            '[CommandListDraw0]\nResourceUniform = ref ps-cb13\nps-cb13 = ResourceBlack\npost ps-cb13 = ref ResourceUniform\n', 1)
        text += '\n[ShaderOverrideAlias]\nhash = %08x\nvk_binding = ps-cb13, 2, 0\n' % shader_hash
        text += '[ResourceUniform]\n[ResourceBlack]\ntype = Buffer\nstride = 16\nbind_flags = constant_buffer\nfilename = black.buf\n'
        (folder / 'black.buf').write_bytes(bytes(65536))
        ini.write_text(text, encoding='ascii')
        extra['EFMI_VK_TEST_A_SHADE'] = '7'
        return original_env(path, *args, **kwargs)
    with patch.object(character, 'layer_env', fixture_env):
        status = character.scene(checks, 'control', matched=True, merged_outputs=True)
    checks.equal(status.get('textures', {}).get('draw_waits'), 0,
                 'merged: native constants publish without recording waits')
    textures = status.get('textures', {})
    checks.check(textures.get('submissions', 0) >= 3 and textures.get('file_reads', 9999) < 30,
                 'merged: mixed image and buffer uploads make bounded progress')


def main():
    checks = Checks('matched native descriptors')
    selected = set(sys.argv[1:])
    for case in ('file', 'ordered', 'swap', 'texture', 'merged'):
        if selected and case not in selected:
            continue
        if case == 'texture':
            texture_constant(checks)
        elif case == 'merged':
            merged_constant(checks)
        else:
            own_mesh(checks, case)
    return checks.finish()


if __name__ == '__main__':
    raise SystemExit(main())
