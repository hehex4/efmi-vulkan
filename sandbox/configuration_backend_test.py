"""Production loading and matching use one root registry, without character units."""
from pathlib import Path
import json
import shutil
import struct
import time

from harness import Checks, DIST, Sandbox, test_dir
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def main():
    checks = Checks("configuration backend")
    root = Path(test_dir("configuration-"))
    first, second = root / "Mods/one", root / "Mods/two"
    first.mkdir(parents=True); second.mkdir(parents=True)
    settings = root / "settings"; settings.mkdir()
    (root / "d3dx.ini").write_text(
        "[Include]\ninclude_recursive = Mods\n[Hunting]\nhunting = 2\nreload_config = F10\n", encoding="ascii")
    hashed = crc32c(struct.pack("<3I", 14, 15, 16))
    (first / "first.ini").write_text(
        "namespace = shared\n[Constants]\nglobal $first = 0\nglobal $second = 0\nglobal $initializer = 0\n"
        "[TextureOverrideFirst]\nhash = %08x\nmatch_index_count = 3\n"
        "allow_duplicate_hash = true\n$first = $first + 1\nrun = CommandListOther\n"
        "[CommandListInitializeMergedSkeleton]\n$initializer = 1\n"
        "[ResourceUnused]\nfilename = missing.buf\n" % hashed, encoding="ascii")
    def write_second(value):
        (second / "second.ini").write_text(
            "namespace = shared\n[CommandListOther]\n$second = %d\n"
            "[TextureOverrideSecond]\nhash = %08x\nmatch_index_count = 3\n"
            "allow_duplicate_hash = true\nhandling = skip\n" % (value, hashed), encoding="ascii")
    write_second(17)
    keys = root / "keys.txt"; keys.write_text("1\n", encoding="ascii")
    env = dict(EFMI_VK_CONFIGURATION=str(root), EFMI_VK_M2_SETTINGS=str(settings),
               EFMI_VK_M2_ENABLED="1", EFMI_VK_TEST_KEYS=str(keys))
    with Sandbox(seconds=15, validation=True, env_extra=env) as sb:
        with ProbeClient(sb.pid) as probe:
            def status(): return probe.status().get("replacement", {})
            def wait_for(predicate):
                deadline = time.monotonic() + 6
                result = status()
                while not predicate(result) and time.monotonic() < deadline:
                    time.sleep(.1); result = status()
                return result
            def variables(s): return s.get("configuration_generation", {}).get("ini", {}).get("variables", {})
            def value(s, name): return variables(s).get("$\\shared\\" + name)
            initial = wait_for(lambda s: value(s, "second") == 17)
            checks.equal(initial.get("kind"), "configuration", "production uses configuration backend")
            checks.check(initial.get("ready"), "complete root loaded")
            checks.equal(value(initial, "second"), 17, "cross-directory list under shared namespace executes")
            checks.check((value(initial, "first") or 0) > 0, "hash matcher runs without skeleton/package")
            checks.equal(value(initial, "initializer"), 0, "initializer-like name is not pre-executed")
            checks.check("packages" not in initial, "no package collection in runtime status")
            checks.check(initial.get("shader_overrides", {}).get("skipped", 0) > 0, "second matching section also executes")
            write_second(23)
            keys.write_text("1 121\n", encoding="ascii"); time.sleep(.25); keys.write_text("1\n", encoding="ascii")
            reloaded = wait_for(lambda s: value(s, "second") == 23)
            checks.equal(value(reloaded, "second"), 23, "F10 replaces the complete registry")
            checks.check(reloaded.get("configuration_generation", {}).get("reloads", 0) > 0, "configuration exchange recorded")
            module = root / "backend.dll"; shutil.copy2(Path(DIST) / "efmi_vk_backend.dll", module)
            probe.swap_module(str(module))
            swapped = wait_for(lambda s: s.get("module", {}).get("generation", 0) > 0)
            checks.check(swapped.get("module", {}).get("generation", 0) > 0, "replacement module exchanges")
            checks.equal(value(swapped, "second"), 23, "new module retains original configuration behavior")
            (root / "status.json").write_text(json.dumps(swapped, indent=2), encoding="ascii")
        sb.proc.wait(timeout=30)
        checks.equal(sb.proc.returncode, 0, "normal sandbox exit")
        checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(), "core and synchronization validation clean")
        (root / "layer.log").write_bytes(Path(sb.log_path).read_bytes())
    print("artifacts:", root)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
