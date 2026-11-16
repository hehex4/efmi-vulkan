"""Validate archive integrity, original-root loading and owned unpack layouts.

The complete installed root is read only. Layout checks use a small synthetic
configuration and copy no installed Mod assets. No real game is started.
"""
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
from build_shader_input_index import inspect as inspect_inputs, MAX_BYTES as INPUT_MAX_BYTES

ap = argparse.ArgumentParser()
ap.add_argument("archive", type=Path)
ap.add_argument("--installation", default="D:/XXMI-Launcher-Portable-v2.1.6/EFMI")
ap.add_argument("--seconds", type=int, default=60)
args = ap.parse_args()

failures = []
GENERATION_LINE = r"configuration loaded: (\d+) variables, (\d+) file resources, (\d+) diagnostics"

def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


# 1. Contents and digest.
with zipfile.ZipFile(args.archive) as z:
    names = sorted(z.namelist())
expected = sorted(build.RELEASE_NAME + "/" + name for name in build.RELEASE_FILES)
input_name = build.RELEASE_NAME + "/" + build.RELEASE_SHADER_INPUTS
input_count = None
if input_name in names:
    expected = sorted(expected + [input_name])
    with zipfile.ZipFile(args.archive) as z:
        if z.getinfo(input_name).file_size > INPUT_MAX_BYTES:
            raise ValueError("Packaged shader input cache exceeds its budget")
        input_data = z.read(input_name)
        input_count = inspect_inputs(input_data)
    check(True, "semantic cache framing/checksum: %d exact identities, SHA-256 %s" %
          (input_count, hashlib.sha256(input_data).hexdigest()))
check(names == expected, "the archive holds exactly the release files " + str(sorted(set(names) ^ set(expected))))
digest = hashlib.sha256(args.archive.read_bytes()).hexdigest()
recorded = (args.archive.parent / (args.archive.name + ".sha256")).read_text(encoding="ascii").split()[0]
check(digest == recorded, "the archive's SHA-256 matches its .sha256 file")
if failures:
    sys.exit("release check: archive preflight failed before extraction/loading")

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
installed = Path(args.installation)
cfg = (release / "efmi_vk.cfg").read_text(encoding="ascii")
check("configuration = auto" in cfg, "release discovers the original root INI")
(release / "efmi_vk.cfg").write_text(cfg.replace("configuration = auto", "configuration = " + installed.as_posix()), encoding="ascii")
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
def run(folder: Path, seconds: int, extra=()):
    """The sandbox with the release in `folder` injected; its log, output and whether a probe pipe appeared."""
    # The program outside the release's folder: a file there is the layer's own, and its faults the layer's.
    program = work / "bin" / "efmi_vk_sandbox.exe"
    if not program.is_file():
        program.parent.mkdir(exist_ok=True)
        shutil.copy2(build.ROOT + "/dist/efmi_vk_sandbox.exe", program)
    harness.SANDBOX_EXE = str(program)
    box = harness.Sandbox(entry="inject", inject_dll=str(folder / build.INJECT_DLL), mode=None, seconds=seconds,
                          validation=True, extra_args=["--api", "1.3", *extra])
    box.__enter__()
    seen = False
    deadline = time.monotonic() + seconds - 10
    while time.monotonic() < deadline and box.proc.poll() is None:
        seen |= box.pid in list_probes()
        time.sleep(1)
    # The program ends by itself after its seconds and prints the validation summary then.
    box.proc.wait(timeout=60)
    box.__exit__(None, None, None)
    text = Path(box.log_path).read_text(encoding="utf-8", errors="replace") if Path(box.log_path).is_file() else ""
    return text, box.output(), seen


log, output, probes_seen = run(release, args.seconds)
check(not probes_seen, "the injected release opens no probe pipe")
check(re.search(r"efmi_vk_layer [0-9.]+ \(release, no probe\) loaded into efmi_vk_sandbox\.exe", log) is not None,
      "the release DLL loaded into the sandbox")
loaded = re.search(GENERATION_LINE, log)
check(loaded is not None, "complete installed root configuration loaded " + (loaded.group(0) if loaded else "(no line)"))
check("crash:" not in log, "no fault in the layer")
for bad in ("configuration refused:", "INI Present refused"):
    check(bad not in log, "the log has no '%s'" % bad)
check("validation: errors=0 warnings=0" in output, "clean core and synchronization validation")

# Unpack discovery follows d3dx.ini, independently of any character content.
efmi = work / "EFMI"
(efmi / "Mods").mkdir(parents=True)
(efmi / "d3dx.ini").write_text("[Include]\ninclude_recursive = Mods\nexclude_recursive = EFMI-Vulkan\n", encoding="ascii")
(efmi / "Mods" / "shared.ini").write_text(
    "[Constants]\nglobal $sentinel = 17\n[ResourceUnused]\nfilename = missing.buf\n", encoding="ascii")
for place in (efmi, efmi / "Mods"):
    label = "unpacked into " + place.relative_to(work).as_posix()
    with zipfile.ZipFile(args.archive) as z:
        z.extractall(place)
    folder = place / build.RELEASE_NAME
    (folder / "settings").mkdir(exist_ok=True)
    (folder / "settings" / "planted.ini").write_text("[Constants]\nthis line is not an INI entry\n", encoding="ascii")
    text, out, _ = run(folder, 30, ["--handled-fault"])
    loaded = re.search(GENERATION_LINE, text)
    check(loaded is not None, label + ": root INI discovered and loaded")
    check("planted.ini" not in text, label + ": the original root exclusion keeps release settings out of includes")
    check("exception: access violation writing address 0000000000000000 at efmi_vk_sandbox.exe" in text and "crash:" not in text,
          label + ": a fault the program handles itself is logged as its exception, not as a crash")
    check("validation: errors=0 warnings=0" in out, label + ": clean validation")
    resolved = folder.resolve()
    if not work.resolve().is_relative_to(ROOT.resolve() / "build") or not resolved.is_relative_to(work.resolve()):
        raise ValueError("Release-check cleanup escaped its owned workspace")
    shutil.rmtree(folder)
print("artifacts:", work)
print("release check: %s (root configuration: %s)" % ("FAILED" if failures else "passed", installed))
sys.exit(1 if failures else 0)
