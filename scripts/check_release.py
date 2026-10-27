"""Checks the playable release archive (scripts/build.py release) before it is published.

    python scripts/check_release.py build/release/EFMI-Vulkan-X.Y.Z.zip [--mods D:/.../EFMI/Mods]

1. The archive holds exactly the release files under EFMI-Vulkan/, and its .sha256 matches.
2. efmi_vk_inject.dll carries no probe: no pipe name, and its startup line says so.
3. The release, unpacked, is injected into the sandbox program with the installed Mods (its own
   configuration with the Mods folder named absolutely, as `../Mods` names it in EFMI): it loads every
   character Mod, compiles what the Core needs without dxil.dll, opens no probe pipe, faults nowhere and
   leaves Vulkan validation clean. Judged from its log and output only: the release has no probe to ask.
Never the game."""
import argparse
import hashlib
import os
import re
import shutil
import sys
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "sandbox"))
sys.path.insert(0, str(ROOT / "server"))
import build  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("archive", type=Path)
ap.add_argument("--mods", default="D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Mods")
ap.add_argument("--seconds", type=int, default=60)
args = ap.parse_args()

failures = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


# 1. Contents and digest.
with zipfile.ZipFile(args.archive) as z:
    names = sorted(z.namelist())
expected = sorted(build.RELEASE_NAME + "/" + name for name in build.RELEASE_FILES)
check(names == expected, "the archive holds exactly the release files " + str(sorted(set(names) ^ set(expected))))
digest = hashlib.sha256(args.archive.read_bytes()).hexdigest()
recorded = (args.archive.parent / (args.archive.name + ".sha256")).read_text(encoding="ascii").split()[0]
check(digest == recorded, "the archive's SHA-256 matches its .sha256 file")

work = Path(build.ROOT) / "build" / "tests" / ("release-check-%d" % int(time.time()))
work.mkdir(parents=True)
with zipfile.ZipFile(args.archive) as z:
    z.extractall(work)
release = work / build.RELEASE_NAME

# 2. No probe in the binary.
dll = (release / build.INJECT_DLL).read_bytes()
check(b"efmi-vk-" not in dll and "efmi-vk-".encode("utf-16-le") not in dll, "efmi_vk_inject.dll names no probe pipe")
check(b"(release, no probe)" in dll, "efmi_vk_inject.dll is the release build")
check(not (release / "dxil.dll").exists() and not list(release.glob("*.pdb")), "no dxil.dll and no symbols in the package")

# 3. The release in the sandbox with the installed Mods.
mods = Path(args.mods)
cfg = (release / "efmi_vk.cfg").read_text(encoding="ascii")
check("m2_mods = ../Mods" in cfg, "the release configuration names the Mods folder relative to EFMI-Vulkan")
(release / "efmi_vk.cfg").write_text(cfg.replace("m2_mods = ../Mods", "m2_mods = " + mods.as_posix()), encoding="ascii")
shutil.copy2(build.ROOT + "/dist/efmi_vk_sandbox.exe", release / "efmi_vk_sandbox.exe")
os.environ["EFMI_VK_DIST"] = str(release)
import harness  # noqa: E402
from efmi_vk.probe_client import list_probes  # noqa: E402

normal_env = harness.inject_env


def release_env(log_path=None, mode=None, extra=None):
    env = normal_env(log_path, mode=None, extra=None)
    for name in list(env):
        if name.startswith("EFMI_VK_") and name != "EFMI_VK_LOG":
            del env[name]
    return env


harness.inject_env = release_env
enabled = sorted(p.name for p in mods.iterdir() if p.is_dir() and not p.name.upper().startswith("DISABLED"))
box = harness.Sandbox(entry="inject", inject_dll=str(release / build.INJECT_DLL), mode=None, seconds=args.seconds,
                      validation=True, extra_args=["--api", "1.3"])
box.__enter__()
probes_seen = False
deadline = time.monotonic() + args.seconds - 10
while time.monotonic() < deadline and box.proc.poll() is None:
    probes_seen |= box.pid in list_probes()
    time.sleep(1)
# The program ends by itself after its seconds and prints the validation summary then.
box.proc.wait(timeout=60)
box.__exit__(None, None, None)
log = Path(box.log_path).read_text(encoding="utf-8", errors="replace") if Path(box.log_path).is_file() else ""
output = box.output()
check(not probes_seen, "the injected release opens no probe pipe")
check(re.search(r"efmi_vk_layer [0-9.]+ \(release, no probe\) loaded into efmi_vk_sandbox\.exe", log) is not None,
      "the release DLL loaded into the sandbox")
loaded = re.search(r"folder generation: (\d+) character Mods, (\d+) left out", log)
check(loaded is not None and loaded.group(2) == "0", "the installed Mods' folder generation loaded, none left out " +
      (loaded.group(0) if loaded else "(no line)"))
check("crash:" not in log, "no fault in the layer")
for bad in ("owned GPU work unavailable", "character package refused", "INI Present refused"):
    check(bad not in log, "the log has no '%s'" % bad)
check("validation: errors=0 warnings=0" in output, "clean core and synchronization validation")
print("artifacts:", work)
print("release check: %s (%d enabled folders in the Mods folder: %s)" % ("FAILED" if failures else "passed", len(enabled), ", ".join(enabled)))
sys.exit(1 if failures else 0)
