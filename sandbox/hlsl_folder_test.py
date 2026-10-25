"""Source resolution and immutable compiler evidence for the folder audit."""
from __future__ import annotations
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_hlsl_folder import inspect_source, source_path


class FolderShaders(unittest.TestCase):
    def setUp(self):
        out = ROOT / "build/hlsl-folder"
        out.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="fixture-", dir=out)
        self.root = Path(self.temporary.name)
        self.executable = out / "shader_inspect.exe"

    def tearDown(self):
        self.temporary.cleanup()

    def test_original_namespace_then_module_fallback(self):
        scoped = self.root / "Mods/example"
        scoped.mkdir(parents=True)
        local = scoped / "shared.hlsl"
        fallback = self.root / "shared.hlsl"
        fallback.write_text("module", encoding="ascii")
        declaration = {"namespace_path": "Mods\\example\\"}
        self.assertEqual(source_path(self.root, declaration, "shared.hlsl"), fallback)
        local.write_text("scoped", encoding="ascii")
        self.assertEqual(source_path(self.root, declaration, "shared.hlsl"), local)
        self.assertEqual(source_path(self.root, {"namespace_path": ""}, "shared.hlsl"), fallback)
        self.assertEqual(source_path(self.root, declaration, "missing.hlsl"), self.root / "missing.hlsl")

    def test_compiler_evidence_and_missing_include(self):
        self.assertTrue(self.executable.is_file(), "Build the folder inspection compiler first")
        source = self.root / "compute.hlsl"
        header = self.root / "value.hlsl"
        source.write_text('#include "value.hlsl"\n#ifndef COMPUTE_SHADER\n#error stage\n#endif\n'
                          'RWStructuredBuffer<uint> Result:register(u1);\n[numthreads(1,1,1)] void main(){Result[0]=VALUE;}\n', encoding="ascii")
        header.write_text('#define VALUE 17\n', encoding="ascii")
        before = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in (source, header)}
        result = inspect_source(self.executable, source, self.root, 1 << 15, "c", 0, "1.2", self.root / "passed")
        self.assertTrue(result["passed"], result.get("diagnostics"))
        self.assertEqual(len(result["dependencies"]), 2)
        self.assertEqual(result["bindings"][0]["source_type"], "u")
        self.assertEqual(result["bindings"][0]["slot"], 1)
        self.assertTrue(all(d["unchanged"] for d in result["dependencies"]))
        self.assertEqual(before, {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in before})
        source.write_text('#include "absent.hlsl"\n', encoding="ascii")
        failed = inspect_source(self.executable, source, self.root, 1 << 15, "c", 0, "1.2", self.root / "failed")
        self.assertFalse(failed["passed"])
        self.assertFalse(failed["compiled"])
        self.assertTrue(failed["source_unchanged"])
        self.assertTrue(failed["diagnostics"])
        self.assertFalse((self.root / "failed/module.spv").exists())

    def test_folder_outside_ascii_and_bytecode_translation(self):
        # BUG-046: DXC faulted on a diagnostic naming a folder outside ASCII (it ended the game), and
        # it refuses a structured buffer's Length, which the original compiler takes.
        self.assertTrue(self.executable.is_file(), "Build the folder inspection compiler first")
        folder = self.root / "mod-\u00e9\uff1a\u4e2d"
        folder.mkdir()
        source = folder / "shape.hlsl"
        source.write_text('RWStructuredBuffer<float> Output:register(u5);\n'
                          '[numthreads(4,1,1)] void main(uint3 id:SV_DispatchThreadID){uint count=Output.Length;\n'
                          'if(id.x>=count)return;Output[id.x]+=count;}\n', encoding="ascii")
        result = inspect_source(self.executable, source, folder, 1 << 15, "c", 0, "1.2", self.root / "translated")
        self.assertEqual(result["returncode"], 0, result.get("diagnostics"))
        self.assertTrue(result["passed"], result.get("diagnostics"))
        self.assertTrue(result["bytecode_translation"])
        self.assertIn("Length", result["diagnostics"])
        self.assertIn("shape.hlsl", result["diagnostics"])
        self.assertNotIn("source0.hlsl", result["diagnostics"])
        self.assertNotIn("source.hlsl", result["diagnostics"])
        self.assertEqual([b["slot"] for b in result["bindings"]], [5])
        # A valid source in the same folder compiles through DXC.
        source.write_text('RWStructuredBuffer<float> Output:register(u5);\n'
                          '[numthreads(1,1,1)] void main(){Output[0]=1;}\n', encoding="ascii")
        plain = inspect_source(self.executable, source, folder, 1 << 15, "c", 0, "1.2", self.root / "plain")
        self.assertTrue(plain["passed"], plain.get("diagnostics"))
        self.assertFalse(plain["bytecode_translation"])


if __name__ == "__main__":
    unittest.main()
