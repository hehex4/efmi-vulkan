"""Installation keys and native overrides remain active without any Mod INI."""
from pathlib import Path
import struct
import sys
import time

from harness import Checks, Sandbox, test_dir, configuration_root
from native_compute_test import compile_compute
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def main():
    checks = Checks("native empty inputs")
    selected = set(sys.argv[1:])
    for case in ("empty", "removed", "root_override", "reload", "reenable"):
        if selected and case not in selected: continue
        root = Path(test_dir("native-input-" + case + "-"))
        original = compile_compute(root, "original", 7)
        shader_hash = crc32c(original.read_bytes())
        replacement = compile_compute(root, "replacement", 42)
        mods = root / "Mods"; mods.mkdir()
        ini = mods / "mod.ini"
        if case == "removed": ini.write_text("[Constants]\nglobal $loaded = 1\n", encoding="ascii")
        settings = root / "settings"; fixes = settings / "ShaderFixes"; fixes.mkdir(parents=True)
        shader = fixes / ("%08x-cs_replace.spv" % shader_hash); shader.write_bytes(replacement.read_bytes())
        config = ("[Include]\ninclude_recursive = Mods\n[Hunting]\nhunting = 1\nreload_config = F10\nmarking_mode = original\nmarking_actions = asm\n"
                  "next_computeshader = no_modifiers VK_NUMPAD8\nmark_computeshader = no_modifiers VK_NUMPAD9\n")
        override = "[ShaderOverrideCompute]\nvk_hash = %08x\nfilter_index = 29\nif cs == 29\nhandling = skip\nendif\n" % shader_hash
        if case == "root_override": config += override
        (root / "d3dx.ini").write_text(config, encoding="ascii")
        keys, output = root / "keys.txt", root / "compute.raw"; keys.write_text("1\n", encoding="ascii")
        env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(settings),
                   EFMI_VK_M2_ENABLED="1", EFMI_VK_TEST_KEYS=str(keys))
        with Sandbox(seconds=9, validation=True, env_extra=env,
                     extra_args=["--compute-shader", str(original), "--compute-output", str(output)]) as sb:
            def key(code):
                keys.write_text("1 %d\n" % code, encoding="ascii"); time.sleep(.18); keys.write_text("1\n", encoding="ascii")
            def value():
                data = output.read_bytes() if output.exists() else b""
                return struct.unpack_from("<I", data)[0] if len(data) >= 4 else None
            with ProbeClient(sb.pid) as probe:
                time.sleep(.5)
                before = probe.status().get("replacement", {})
                checks.equal(value(), 0 if case == "root_override" else 42, case + ": initial GPU result")
                if case == "removed": ini.unlink(); key(121); time.sleep(1)
                if case == "reload":
                    shader.write_bytes(compile_compute(root, "reload", 91).read_bytes())
                    (root / "d3dx.ini").write_text(config.replace("marking_mode = original", "marking_mode = skip"), encoding="ascii")
                    key(121); time.sleep(1)
                    checks.equal(value(), 91, "reload: F10 publishes new shader with no Mod inputs")
                if case == "reenable":
                    ini.write_text(override, encoding="ascii"); key(121); time.sleep(1)
                    checks.equal(value(), 0, "reenable: first Mod added by F10 runs its override")
                    ini.unlink(); key(121); time.sleep(1)
                    checks.equal(value(), 42, "reenable: removing the last Mod clears its override")
                state = probe.status().get("replacement", {})
                checks.check(state.get("hunting", {}).get("keys", False), case + ": installation hunting keys registered")
                if case == "reload":
                    checks.check(state.get("shader_fixes", {}).get("revision") != before.get("shader_fixes", {}).get("revision"), "reload: shader revision advanced")
                    checks.equal(state.get("hunting", {}).get("marking_mode"), "skip", "reload: installation settings reloaded")
                key(104); time.sleep(.3); key(105); time.sleep(.3)
                # Original hunting clears idle visited sets until the first key.
                # Query after selection, while collection is active.
                state = probe.status().get("replacement", {})
                checks.equal(state.get("hunting", {}).get("visited", {}).get("compute"), 1, case + ": compute visited without a Mod")
                checks.equal(value(), 0 if case in ("root_override", "reload") else 7, case + ": selected original or skipped GPU result")
                checks.check((fixes / ("%08x-cs.spv" % shader_hash)).is_file(), case + ": key dumps original module")
            sb.proc.wait(timeout=30)
            checks.equal(sb.proc.returncode, 0, case + ": normal exit")
        checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(), case + ": Vulkan validation clean")
        checks.equal(list(mods.iterdir()), [], case + ": no generated input files in Mods")
        print("artifacts:", root)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
