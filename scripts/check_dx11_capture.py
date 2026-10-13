"""Offline capture-bookkeeping checks using a fake probe; no game connection."""
from __future__ import annotations
import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
from types import ModuleType, SimpleNamespace
from unittest.mock import patch
import dx11_baseline as capture


def main():
    out = capture.ROOT / "build/dx11-capture"
    out.mkdir(parents=True, exist_ok=True)
    checks = 0
    def check(ok, why):
        nonlocal checks
        if not ok:
            raise AssertionError(why)
        checks += 1
    report = dict(scope="fake-probe shader archiving and texture selection only", passed=False)
    log = io.StringIO()
    try:
        with tempfile.TemporaryDirectory(prefix="capture-check-", dir=out) as temp, contextlib.redirect_stdout(log):
            folder = Path(temp).resolve()
            check(folder.is_relative_to(capture.ROOT.resolve()), "Temporary files stay inside the project")
            names = ("0123456789abcdef", "fedcba9876543210")
            code = {names[0]:b"first-source-bytecode", names[1]:b"second-source-bytecode"}
            calls = []
            class Probe:
                def shader(self, h):
                    calls.append(h)
                    return {}, code[h]
            module = ModuleType("efmi_mcp")
            def disassemble(data):
                if data == code[names[1]]:
                    raise ValueError("Synthetic disassembler failure")
                return "declared-source"
            module.shader = SimpleNamespace(disassemble=disassemble)
            doc = dict(shaders=[dict(hash=h, known=True) for h in names], draws=[dict(sh=[0,1])])
            archive = folder / "frame.dxbc"
            with patch.dict("sys.modules", {"efmi_mcp":module}), patch.object(capture.dx11_pair, "parse_declarations", return_value={"source":True}):
                known = capture.fetch_declarations(Probe(), doc, {}, archive)
                index = json.loads((archive/"index.json").read_text())
                check(set(known) == {names[0]} and set(index["shaders"]) == set(names), "DXBC survives failed declaration listing")
                check(index["errors"].get(names[1]) == "Synthetic disassembler failure", "Partial declaration errors remain explicit")
                check(all((archive/v["file"]).read_bytes() == code[h] and v["sha256"] == hashlib.sha256(code[h]).hexdigest()
                          for h,v in index["shaders"].items()), "Original bytes and their fingerprints round-trip")
                before = len(calls)
                capture.fetch_declarations(Probe(), dict(shaders=doc["shaders"], draws=[dict(sh=[0])]), known, folder/"known.dxbc")
                check(len(calls) == before+1, "Archiving fetches bytes even when declarations already exist")
                original = (archive/(names[0]+".dxbc")).read_bytes()
                code[names[0]] = b"different-bytes-for-the-same-name"
                capture.fetch_declarations(Probe(), dict(shaders=doc["shaders"], draws=[dict(sh=[0])]), {}, archive)
                index = json.loads((archive/"index.json").read_text())
                check((archive/(names[0]+".dxbc")).read_bytes() == original and names[0] in index["errors"], "Hash-name collision preserves prior bytes and records refusal")
                before = len(calls)
                capture.fetch_declarations(Probe(), dict(shaders=[dict(hash="../escape",known=True)], draws=[dict(sh=[0])]), {}, folder/"invalid")
                check(len(calls) == before and "../escape" in json.loads((folder/"invalid/index.json").read_text())["errors"], "Invalid shader names cannot become paths or probe requests")
                try:
                    capture.fetch_declarations(Probe(), doc, {}, capture.ROOT.parent/"outside-capture-check")
                except ValueError:
                    checks += 1
                else:
                    raise AssertionError("Out-of-project archive was accepted")
            reads = []
            class Textures:
                frame = 17
                def draws(self):
                    return [dict(type=0, shaders={}, tex={"ps-t15":dict(res=1,hash="a"),"ps-t16":dict(res=2,hash="b"),
                            "ps-t17":dict(res=3,hash="c"),"ps-t18":dict(res=4,hash="d"),"ps-t100":dict(res=5,hash="e")})]
                def resource(self, rid):
                    return dict(kind="tex2d", origin="xxmi" if rid==3 else "game", bind=40 if rid==4 else 8)
            class TextureProbe:
                def read_texture(self, frame, rid, mip, layer):
                    reads.append((frame,rid,mip,layer))
                    if rid == 2:
                        raise ValueError("Synthetic unreadable texture")
                    return dict(width=1,height=1,format_name="R8G8B8A8_UNORM",mips=1,array=1), b"rgba"
            with patch.object(capture.dx11_pair, "declared", side_effect=lambda _a,_b,s:s=="ps-t15"):
                result = capture.hash_textures(TextureProbe(), Textures(), {})
                check(list(result) == ["a"] and reads == [(17,1,0,0)], "Default selection keeps declared file-backed game textures")
                reads.clear()
                result = capture.hash_textures(TextureProbe(), Textures(), {}, all_bound=True)
                check(set(result) == {"a","b"} and reads == [(17,1,0,0),(17,2,0,0)], "Expanded selection includes inactive slots without mod/render-target/extended-slot reads")
                check("sha256" in result["a"] and result["b"] == {"error":"Synthetic unreadable texture"}, "Unreadable identity evidence does not masquerade as a content hash")
        report.update(passed=True, checks_passed=checks)
        print(f"DX11 capture bookkeeping: {checks} checks passed; no probe connection")
        return 0
    except (AssertionError, OSError, ValueError) as error:
        report["error"] = str(error)
        print(error)
        return 1
    finally:
        (out/"runtime.txt").write_text(log.getvalue().encode("ascii","backslashreplace").decode("ascii"), encoding="ascii")
        (out/"report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True)+"\n", encoding="ascii")


if __name__ == "__main__":
    raise SystemExit(main())
