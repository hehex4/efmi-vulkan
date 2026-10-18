"""Directory language regression through the original parser, without graphics."""
from __future__ import annotations
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "build/xxmi-host/host_probe.exe"


class FolderLanguage(unittest.TestCase):
    def setUp(self):
        output = ROOT / "build/ini-folder-fixtures"
        output.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=output)
        self.root = Path(self.temp.name)
        self.write("Core/main.ini", "; fixture\n")
        (self.root / "Mods").mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="ascii")

    def parse(self, success=True):
        """`success`: no original warning expected. The original loader completes either way and
        keeps what it could parse; a refused construct must still be reported, never dropped
        silently."""
        result = subprocess.run([str(HOST), "--parse-folder", str(self.root),
                                 str(self.root / "Core"), str(self.root / "Mods")],
                                cwd=self.root, capture_output=True, text=True, errors="replace", timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        parsed = json.loads(result.stdout)
        self.assertEqual(parsed["diagnostics"] == 0, success, result.stderr)
        self.assertEqual(len(parsed["diagnostic_messages"]), min(parsed["diagnostics"], 256))
        self.assertEqual(parsed["dire"], 0)
        return parsed

    def test_full_language_and_cross_file_references(self):
        self.write("Core/main.ini", "[Include]\ninclude = shared.ini\n")
        self.write("Core/shared.ini", "namespace = shared\n[Constants]\nglobal $value = 1\n"
                   "[CommandListUpdate]\n$value = $value + 2\n")
        self.write("Mods/one/1.ini", "namespace = first\n[Constants]\nglobal persist $choice = 0\n"
                   "[KeyChoice]\nkey = ctrl 5\ncondition = $choice < 3\ntype = cycle\n$choice = 0,1,2,\n"
                   "[ResourceData]\ntype = Buffer\nformat = R32_UINT\ndata = 0 1 2\n"
                   "[CommandListWork]\nrun = CommandList\\shared\\Update\n"
                   "if $choice == 0\n$choice = (2 + 3) * 4 % 7\nelse if $choice == 1\n$choice = 2\nelse\n$choice = 0\nendif\n"
                   "[TextureOverrideObject]\nhash = 12345678\nmatch_index_count = 3\nhandling = skip\n"
                   "ib = ResourceData\nrun = CommandListWork\ndrawindexed = 3,0,0\n"
                   "[CustomShaderCompute]\ncs = arbitrary.hlsl\ncs-u0 = ResourceData\ndispatch = 1,1,1\n"
                   "[ShaderRegexFixture]\nshader_model = ps_5_0\n[ShaderRegexFixture.Pattern]\n(?<capture>sample).*\n")
        # Same implicit filename under different directories must not collide.
        self.write("Mods/two/1.ini", "[Constants]\nglobal $choice = 9\n[ResourceData]\nfilename = local.buf\n")
        self.write("Mods/three/1.ini", "[Constants]\nglobal $choice = 4\n[ResourceData]\nfilename = local.buf\n")
        report = self.parse()
        self.assertEqual(report["persistent"], 1)
        self.assertEqual(report["keys"], 1)
        self.assertEqual(report["texture_overrides"], 1)
        self.assertEqual(report["shader_regex_patterns"], 1)
        names = {r["name"] for r in report["resource_declarations"]}
        self.assertIn("resource\\mods\\two\\1.ini\\data", names)
        self.assertIn("resource\\mods\\three\\1.ini\\data", names)
        self.assertTrue(any("Dispatch" in k or "Draw" in k for k in report["command_types"]))
        shaders = [s for s in report["shader_declarations"] if s["name"].startswith("customshader\\")]
        self.assertEqual(len(shaders), 1)
        self.assertEqual(shaders[0]["sources"], ["", "", "", "", "", "arbitrary.hlsl"])
        self.assertEqual(shaders[0]["flags"], 1 << 15)
        self.assertEqual(shaders[0]["namespace_path"].lower(), "mods\\one\\")

    def test_original_shader_flags_namespace_and_null_stages(self):
        self.write("Core/main.ini", "[Rendering]\nrecursive_include = -1\n")
        self.write("Mods/test/graphics.ini", "namespace = example\n[CustomShaderDraw]\n"
                   "vs = Shared.hlsl\nps = Shared.hlsl\ngs = null\nflags = skip_optimization ieee_strictness\n")
        shader = next(s for s in self.parse()["shader_declarations"] if s["name"] == "customshader\\example\\draw")
        self.assertEqual(shader["name"], "customshader\\example\\draw")
        self.assertEqual(shader["namespace_path"].lower(), "mods\\test\\")
        self.assertEqual(shader["flags"], (1 << 2) | (1 << 13))
        # The pinned boolean getter evaluates the nonzero expression first;
        # GetIniBoolOrInt consequently returns true here, not the literal -1.
        self.assertEqual(shader["recursive_include"], 1)
        self.assertEqual(shader["sources"], ["Shared.hlsl", "", "", "null", "Shared.hlsl", ""])

    def test_disabled_and_static_include_condition(self):
        invalid = "[CommandListBad]\nunsupported_operation = 1\n"
        self.write("Mods/DISABLED-mod/main.ini", invalid)
        self.write("Mods/DISABLED-file.ini", invalid)
        self.write("Mods/desktop.ini", invalid)
        self.write("Mods/conditional.ini", "condition = 0\n" + invalid)
        self.parse()

    def test_unknown_command_is_not_silently_accepted(self):
        self.write("Mods/bad.ini", "[CommandListBad]\nunsupported_operation = 1\n")
        self.parse(False)

    def test_missing_command_dependency(self):
        self.write("Mods/bad.ini", "[Present]\nrun = CommandListDoesNotExist\n")
        self.parse(False)

    def test_invalid_expression(self):
        self.write("Mods/bad.ini", "[Constants]\nglobal $x = 0\n[Present]\n$x = (\n")
        self.parse(False)

    def test_invalid_resource_declaration(self):
        self.write("Mods/bad.ini", "[ResourceBad]\ntype = NotAResourceKind\n")
        self.parse(False)

    def test_invalid_shader_pattern(self):
        self.write("Mods/bad.ini", "[ShaderRegexBad]\nshader_model = ps_5_0\n[ShaderRegexBad.Pattern]\n(?<bad>\n")
        self.parse(False)

    def test_missing_include(self):
        self.write("Mods/bad.ini", "[Include]\ninclude = absent.ini\n")
        self.parse(False)


if __name__ == "__main__":
    unittest.main()
