"""Native compute replacement, direct/indirect interception and GPU readback through the real layer."""
from pathlib import Path
import struct
import subprocess
import sys
import time

from harness import Checks, Sandbox, test_dir
from runtime_fixture import CORE
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from compile_game_shader import compile_shader, DXC, VALIDATOR


def compile_compute(root, name, value, binding=0, threads=1):
    source, output = root / (name + ".hlsl"), root / (name + ".spv")
    source.write_text("[[vk::binding(%d,0)]] RWStructuredBuffer<uint> data;\n"
                      "[numthreads(%d,1,1)] void main() { data[0] = %d; }\n" % (binding, threads, value), encoding="ascii")
    for command in ([str(DXC), "-spirv", "-fspv-target-env=vulkan1.1", "-T", "cs_6_0", "-E", "main", "-Fo", str(output), str(source)],
                    [str(VALIDATOR), "--target-env", "vulkan1.1", str(output)]):
        subprocess.run(command, check=True, capture_output=True, timeout=30)
    published = compile_shader(source, root / "compiled", "%08x" % crc32c(name.encode("ascii")), "cs")
    assert published.read_bytes() == output.read_bytes()
    return output


def read_compute_result(path):
    # The sandbox publishes a fenced four-word result repeatedly. Its writer can briefly own
    # the Windows file or have truncated it; wait for a complete publication, never for a value.
    deadline = time.monotonic() + .5
    while True:
        try:
            data = path.read_bytes()
            if len(data) == 16: return data
        except (PermissionError, FileNotFoundError):
            pass
        if time.monotonic() >= deadline: return b""
        time.sleep(.01)


def main():
    checks = Checks("native compute")
    selected = set(sys.argv[1:])
    for case in ("direct", "indirect", "inject", "interface", "workgroup", "builtin", "builtin_changed", "builtin_specialized", "shared", "shared_limit", "skip", "skip_indirect", "lists", "analysis", "depth", "original", "hunt_skip", "pink", "reload", "invalid", "removed"):
        if selected and case not in selected: continue
        root = Path(test_dir("native-compute-" + case + "-"))
        original = compile_compute(root, "original", 7)
        shader_hash = crc32c(original.read_bytes())
        replacement = compile_compute(root, "replacement", 42, binding=1 if case == "interface" else 0, threads=2 if case == "workgroup" else 1)
        if case.startswith("builtin"):
            # WorkgroupSize overrides LocalSize even when the latter says (1,1,1).
            words = list(struct.unpack("<%dI" % (replacement.stat().st_size // 4), replacement.read_bytes()))
            at = 5; instructions = []
            while at < len(words):
                count = words[at] >> 16; instructions.append(words[at:at + count]); at += count
            uint = next(w[1] for w in instructions if w[0] & 65535 == 21 and w[2:] == [32, 0])
            base = words[3]; words[3] += 5
            annotation = [(4 << 16) | 71, base + 1, 11, 25]
            dims = (2 if case == "builtin_changed" else 1, 1, 1)
            declarations = [[(4 << 16) | 23, base, uint, 3]]
            specialized = case == "builtin_specialized"
            declarations += [[(4 << 16) | (50 if specialized else 43), uint, base + 2 + i, dims[i]] for i in range(3)]
            declarations += [[(6 << 16) | (51 if specialized else 44), base, base + 1, base + 2, base + 3, base + 4]]
            annotated = declared = False; result = words[:5]
            for instruction in instructions:
                op = instruction[0] & 65535
                if not annotated and 19 <= op <= 39: result += annotation; annotated = True
                if not declared and op == 54:
                    for declaration in declarations: result += declaration
                    declared = True
                result += instruction
            replacement.write_bytes(struct.pack("<%dI" % len(result), *result))
            subprocess.run([str(VALIDATOR), "--target-env", "vulkan1.1", str(replacement)], check=True, capture_output=True)
        if case in ("shared", "shared_limit"):
            words = list(struct.unpack("<%dI" % (replacement.stat().st_size // 4), replacement.read_bytes()))
            at = 5; instructions = []
            while at < len(words):
                count = words[at] >> 16; instructions.append(words[at:at + count]); at += count
            uint = next(w[1] for w in instructions if w[0] & 65535 == 21 and w[2:] == [32, 0])
            base = words[3]; words[3] += 4
            declarations = [(4 << 16) | 43, uint, base, 4 if case == "shared" else (1 << 24),
                            (4 << 16) | 28, base + 1, uint, base,
                            (4 << 16) | 32, base + 2, 4, base + 1,
                            (4 << 16) | 59, base + 2, base + 3, 4]
            result = words[:5]; added = False
            for instruction in instructions:
                if not added and instruction[0] & 65535 == 54: result += declarations; added = True
                result += instruction
            replacement.write_bytes(struct.pack("<%dI" % len(result), *result))
            subprocess.run([str(VALIDATOR), "--target-env", "vulkan1.1", str(replacement)], check=True, capture_output=True)
        mods = root / "Mods"; (mods / "native").mkdir(parents=True)
        ini = "namespace = native_compute\n[Constants]\nglobal $loaded = 1\nglobal $state = 0\n"
        if case in ("skip", "skip_indirect"):
            ini += "[ShaderOverrideCompute]\nhash = %08x\nfilter_index = 29\nif draw_type == %d && cs == 29\nhandling = skip\nendif\n" % (shader_hash, 9 if case == "skip_indirect" else 8)
        if case == "lists": ini += "[ShaderOverrideCompute]\nhash = %08x\nif $state == 3\nhandling = skip\nendif\npost $state = 3\n" % shader_hash
        if case == "depth": ini += "[ShaderOverrideCompute]\nhash = %08x\ndepth_filter = depth_active\n" % shader_hash
        (mods / "native/mod.ini").write_text(ini, encoding="ascii")
        (root / "d3dx.ini").write_text("[Include]\ninclude = Core\\EFMI\\main.ini\ninclude_recursive = Mods\n"
            "[Rendering]\nrecursive_include = 1\n[Hunting]\nhunting = 1\nmarking_mode = " + ("skip" if case == "hunt_skip" else "pink" if case == "pink" else "original") +
            "\nnext_computeshader = no_modifiers VK_NUMPAD8\nmark_computeshader = no_modifiers VK_NUMPAD9\nmarking_actions = asm\nanalyse_frame = no_modifiers VK_NUMPAD7\n", encoding="ascii")
        settings = root / "settings"; fixes = settings / "ShaderFixes"; fixes.mkdir(parents=True)
        path = fixes / ("%08x-cs_replace.spv" % shader_hash); path.write_bytes(replacement.read_bytes())
        output, keys = root / "compute.raw", root / "keys.txt"; keys.write_text("1\n", encoding="ascii")
        env = dict(EFMI_VK_M2_MODS=str(mods), EFMI_VK_M2_CORE=str(CORE), EFMI_VK_M2_SETTINGS=str(settings),
                   EFMI_VK_M2_ENABLED="1", EFMI_VK_TEST_KEYS=str(keys))
        args = ["--compute-shader", str(original), "--compute-output", str(output)]
        if case in ("indirect", "skip_indirect"): args += ["--compute-indirect"]
        with Sandbox(seconds=7, entry="inject" if case == "inject" else "layer", validation=True, env_extra=env, extra_args=args) as sb:
            time.sleep(1)
            with ProbeClient(sb.pid) as probe:
                before = probe.status().get("replacement", {})
                if case in ("original", "hunt_skip", "pink"):
                    keys.write_text("1 104\n", encoding="ascii"); time.sleep(.25); keys.write_text("1\n", encoding="ascii"); time.sleep(.3)
                    keys.write_text("1 105\n", encoding="ascii"); time.sleep(.25); keys.write_text("1\n", encoding="ascii")
                if case == "analysis":
                    keys.write_text("1 103\n", encoding="ascii"); time.sleep(.25); keys.write_text("1\n", encoding="ascii")
                if case in ("reload", "invalid", "removed"):
                    if case == "reload": path.write_bytes(compile_compute(root, "reloaded", 91).read_bytes())
                    elif case == "invalid": path.write_bytes(b"broken")
                    else: path.unlink()
                    keys.write_text("1 121\n", encoding="ascii"); time.sleep(.25); keys.write_text("1\n", encoding="ascii")
                time.sleep(1)
                status = probe.status().get("replacement", {})
                data = read_compute_result(output)
                observed = struct.unpack_from("<I", data)[0] if len(data) >= 4 else None
                expected = 0 if case in ("skip", "skip_indirect", "lists", "hunt_skip") else 7 if case in ("interface", "workgroup", "builtin_changed", "builtin_specialized", "shared_limit", "original", "removed") else 91 if case == "reload" else 42
                checks.equal(observed, expected, case + ": fenced GPU result")
                if case == "analysis":
                    logs = list(settings.glob("FrameAnalysis-*/log.txt"))
                    log = logs[0].read_text() if logs else ""
                    checks.check("CSSetShader hash=%08x" % shader_hash in log and "Dispatch(1,1,1)" in log, "analysis: original compute identity and call logged")
                checks.equal(before.get("shader_fixes", {}).get("loaded", 0), 0 if case in ("builtin_specialized", "shared_limit") else 1, case + ": validated compute files")
                if case in ("original", "hunt_skip", "pink"):
                    checks.equal(status.get("hunting", {}).get("selected", {}).get("compute"), "%08x" % shader_hash, case + ": original hash selected")
                    checks.check((fixes / ("%08x-cs.spv" % shader_hash)).exists(), case + ": original module dumped")
            sb.proc.wait(timeout=30)
            checks.equal(sb.proc.returncode, 0, case + ": normal exit")
        checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(), case + ": Vulkan validation clean")
        print("artifacts:", root)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
