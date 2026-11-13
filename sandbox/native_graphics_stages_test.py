"""Geometry/hull/domain replacement and hunting against real application pipelines."""
from pathlib import Path
import sys
import time

from harness import Checks, Sandbox, test_dir
from runtime_fixture import CORE
from runtime_test import band_pixels
from native_shader_test import compile_shader, colored_pixels
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def compile_stage(root, stage, offset=0, vertices=3):
    common = "struct V { float4 pos : SV_Position; };\n"
    patch = "struct P { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };\n"
    if stage == "gs":
        code = (common + "[maxvertexcount(%d)] void main(triangle V input[3], inout TriangleStream<V> output) {\n"
                "[unroll] for (uint i=0; i<3; ++i) { V v=input[i]; v.pos.y += %d * v.pos.w; output.Append(v); } }\n" % (vertices, offset))
    elif stage == "hs":
        code = (common + patch + "P factors(InputPatch<V,3> p) { P o; o.edges[0]=o.edges[1]=o.edges[2]=1; o.inside=1; return o; }\n"
                "[domain(\"tri\")][partitioning(\"integer\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(3)][patchconstantfunc(\"factors\")]\n"
                "V main(InputPatch<V,3> p, uint i : SV_OutputControlPointID) { V o=p[i]; o.pos.y += %d * o.pos.w; return o; }\n" % offset)
    else:
        code = (common + patch + "[domain(\"tri\")] V main(P f, float3 bary : SV_DomainLocation, const OutputPatch<V,3> p) {\n"
                "V o; o.pos=bary.x*p[0].pos+bary.y*p[1].pos+bary.z*p[2].pos; o.pos.y += %d * o.pos.w; return o; }\n" % offset)
    source = root / (stage + str(offset) + ".hlsl"); source.write_text(code, encoding="ascii")
    return compile_shader(source, root / (stage + str(offset)), "12345678", stage)


def main():
    checks = Checks("native graphics stages")
    selected = set(sys.argv[1:])
    for stage in ("gs", "hs", "ds"):
        for case in ("baseline", "replace", "skip", "original", "pink", "reload", "contract", "combined"):
            if case in ("contract", "combined") and stage != "gs": continue
            label = stage + ":" + case
            if selected and label not in selected: continue
            root = Path(test_dir("native-" + stage + "-" + case + "-"))
            stages = ["gs", "hs", "ds"] if case == "combined" else [stage] if stage == "gs" else ["hs", "ds"]
            originals = {name: compile_stage(root, name) for name in stages}
            shader_hash = crc32c(originals[stage].read_bytes())
            mods = root / "Mods"; mods.mkdir()
            ini = "[Constants]\nglobal $loaded = 1\n"
            if case == "skip": ini += "[ShaderOverrideStage]\nhash = %08x\nhandling = skip\n" % shader_hash
            (mods / "mod.ini").write_text(ini, encoding="ascii")
            kind = {"gs": "geometry", "hs": "hull", "ds": "domain"}[stage]
            (root / "d3dx.ini").write_text(
                "[Hunting]\nhunting = 1\nmarking_mode = %s\nmarking_actions = asm\n"
                "next_%sshader = no_modifiers VK_NUMPAD8\nmark_%sshader = no_modifiers VK_NUMPAD9\n" %
                ("pink" if case == "pink" else "original", kind, kind), encoding="ascii")
            settings = root / "settings"; fixes = settings / "ShaderFixes"; fixes.mkdir(parents=True)
            shader = fixes / ("%08x-%s_replace.spv" % (shader_hash, stage))
            if case not in ("baseline", "skip", "pink"): shader.write_bytes(compile_stage(root, stage, 4, vertices=4 if case == "contract" else 3).read_bytes())
            readback, trigger, keys = root / "pixels.raw", root / "read.trigger", root / "keys.txt"
            keys.write_text("1\n", encoding="ascii")
            env = dict(EFMI_VK_M2_MODS=str(mods), EFMI_VK_M2_CORE=str(CORE), EFMI_VK_M2_SETTINGS=str(settings), EFMI_VK_M2_ENABLED="1",
                EFMI_VK_TEST_KEYS=str(keys), EFMI_VK_TEST_NOTICE="> native graphics stage test", EFMI_VK_TEST_NOTICES_READBACK=str(readback),
                EFMI_VK_TEST_NOTICES_READBACK_TRIGGER=str(trigger))
            args = ["--readable-swapchain"]
            for name, path in originals.items(): args += ["--" + {"gs": "geometry", "hs": "hull", "ds": "domain"}[name] + "-shader", str(path)]
            with Sandbox(seconds=5, validation=True, env_extra=env, extra_args=args) as sb:
                time.sleep(.6)
                def key(code):
                    keys.write_text("1 %d\n" % code, encoding="ascii"); time.sleep(.18); keys.write_text("1\n", encoding="ascii")
                with ProbeClient(sb.pid) as probe:
                    if case in ("original", "pink"):
                        key(104); time.sleep(.2); key(105)
                    if case == "reload":
                        shader.write_bytes(originals[stage].read_bytes()); key(121); time.sleep(.8)
                    time.sleep(.3); trigger.write_text("capture\n", encoding="ascii")
                    status = probe.status().get("replacement", {})
                    if case in ("original", "pink"):
                        checks.equal(status.get("hunting", {}).get("selected", {}).get(kind), "%08x" % shader_hash, label + ": original hash selected")
                        checks.check((fixes / ("%08x-%s.spv" % (shader_hash, stage))).is_file(), label + ": original stage dumped")
                sb.proc.wait(timeout=30)
                checks.equal(sb.proc.returncode, 0, label + ": normal exit")
            if case == "pink": checks.check(colored_pixels(readback, (255, 0, 255)) > 200, label + ": selected stage marks the draw pink")
            else: checks.equal((band_pixels(readback) or 0) > 200, case not in ("replace", "skip", "combined"), label + ": expected visible original or hidden replacement")
            if case in ("replace", "reload", "combined"):
                checks.check(status.get("shader_fixes", {}).get("binds", 0) > 0, label + ": native variant bound")
            if case == "contract": checks.equal(status.get("shader_fixes", {}).get("binds", 0), 0, label + ": incompatible execution contract keeps original")
            checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(), label + ": Vulkan validation clean")
            print("artifacts:", root)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
