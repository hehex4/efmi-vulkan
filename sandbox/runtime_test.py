"""Keys, live INI conditions, persistence and transactional F10 through Vulkan hooks."""
import json
import os
from pathlib import Path
import subprocess
from harness import Checks, DIST, INJECT_DLL, Sandbox, inject_env, layer_env, test_dir
from runtime_fixture import fixture
from efmi_vk.probe_client import ProbeClient


def refused_package(checks):
    """BUG-026: a package whose Mod files went missing is refused, and the application runs on."""
    for entry in ("layer", "inject"):
        folder = Path(test_dir("runtime-refused-"))
        package = fixture(folder)
        config = json.loads(Path(str(package) + ".runtime.json").read_text(encoding="ascii"))
        Path(config["stream_0"]).unlink()
        extra = dict(EFMI_VK_M2_PACKAGE=str(package), EFMI_VK_M2_ENABLED="1")
        status = {}
        with Sandbox(seconds=4, env_extra=extra, entry=entry) as sb:
            with ProbeClient(sb.pid) as probe:
                status = probe.status().get("replacement", {})
            sb.proc.wait(timeout=60)
            code = sb.proc.returncode
        log = Path(sb.log_path).read_text(encoding="utf-8", errors="replace") if sb.log_path and Path(sb.log_path).is_file() else ""
        label = f"{entry}, missing Mod file"
        checks.equal(code, 0, label + ": the application runs and exits normally (BUG-026)")
        checks.check("character package refused: Runtime resource missing" in log, label + ": the refusal and its reason are logged")
        checks.check(not status.get("ready") and not status.get("enabled"), label + ": the refused backend neither is ready nor enabled")


def main():
    checks = Checks("live INI rendering")
    validation = os.environ.get("EFMI_VK_VALIDATION_DIR", "F:/vulkan/validation-1.4.321.0/bin")
    for entry in ("layer", "inject"):
        for queues in (1, 2):
            folder = Path(test_dir("runtime-"))
            package = fixture(folder)
            extra = dict(EFMI_VK_M2_PACKAGE=str(package), EFMI_VK_M2_ENABLED="1", EFMI_VK_TEST_KEYS=str(folder / "keys.txt"),
                         VK_LAYER_PATH=os.pathsep.join([DIST, validation]), VK_LAYER_VALIDATE_SYNC="1", VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0",
                         XDG_CACHE_HOME=str(folder), TEMP=str(folder), TMP=str(folder), TMPDIR=str(folder))
            env = (layer_env if entry == "layer" else inject_env)(str(folder / "layer.log"), extra=extra)
            env["VK_LAYER_PATH"] = extra["VK_LAYER_PATH"]
            args = [str(Path(DIST) / "efmi_vk_variant_gpu.exe"), "--character", "--material", "--validation",
                    "--module", str(Path(DIST) / "efmi_vk_backend.dll"), "--bad-module", str(Path(DIST) / "efmi_vk_backend_mismatch.dll")]
            if queues == 2:
                args += ["--graphics-queues", "2", "--split-material-queues"]
            if entry == "inject":
                args += ["--inject", INJECT_DLL]
            result = subprocess.run(args, env=env, cwd=folder, capture_output=True, text=True, errors="replace", timeout=120)
            output = result.stdout + result.stderr
            (folder / "stdout.txt").write_text(output.encode("ascii", "backslashreplace").decode(), encoding="ascii")
            label = f"{entry}, {queues} queue(s)"
            checks.equal(result.returncode, 0, label + " GPU keys, reload, rollback and retained-command checks")
            checks.check("validation: errors=0 warnings=0" in output, label + " clean core/sync validation including shutdown")
            report = folder / "runtime-final.json"
            checks.check(report.is_file(), label + " final status saved")
            if result.returncode:
                print(output[-10000:]); print("artifacts:", folder)
            if not report.is_file():
                continue
            status = json.loads(report.read_text())["result"]["replacement"]
            checks.equal((status["ini_generation"], status["ini_reloads"], status["ini_reload_rejected"], status["ini_reload_pending"], status["ini_reload_error"]),
                         (2, 2, 1, False, ""), label + " two accepted reloads, one with an original warning, and one refused")
            checks.equal(status["ini"]["diagnostics"], 0, label + " the last generation has no warning left")
            checks.equal(status["ini"]["variables"]["$\\runtime_fixture\\tint"], 1, label + " persistent variable survives F10")
            saved = folder / "settings/fixture.ini"
            checks.check(saved.is_file() and "$\\runtime_fixture\\tint = 1" in saved.read_text(), label + " settings atomically saved outside mod source")
            refused = {k: v for k, v in status["counters"].items() if k.startswith(("refused_", "unreadable_", "import_", "exhausted")) and v}
            checks.equal(refused, {}, label + " no refused drawing after successful reload")
    refused_package(checks)
    several_packages(checks, validation)
    return checks.finish()


def several_packages(checks, validation):
    """A directory of packages: another character's package, which matches no draw of the scene,
    sorts first, so every draw passes it before the fixture's. The scene must behave as with one
    package, and the other package must never go to the GPU."""
    import shutil
    folder = Path(test_dir("runtime-several-"))
    (folder / "b").mkdir()
    fixture(folder / "b", salt=0x5A5A5A5A, settings="other", name="other.bin")
    package = fixture(folder)
    for suffix in ("", ".runtime.json"):
        shutil.copyfile(folder / "b" / ("other.bin" + suffix), folder / ("0-other.bin" + suffix))
    extra = dict(EFMI_VK_M2_PACKAGE=str(folder), EFMI_VK_M2_ENABLED="1", EFMI_VK_TEST_KEYS=str(folder / "keys.txt"),
                 VK_LAYER_PATH=os.pathsep.join([DIST, validation]), VK_LAYER_VALIDATE_SYNC="1", VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0",
                 XDG_CACHE_HOME=str(folder), TEMP=str(folder), TMP=str(folder), TMPDIR=str(folder))
    env = layer_env(str(folder / "layer.log"), extra=extra)
    env["VK_LAYER_PATH"] = extra["VK_LAYER_PATH"]
    args = [str(Path(DIST) / "efmi_vk_variant_gpu.exe"), "--character", "--material", "--validation", "--lazy-packages",
            "--module", str(Path(DIST) / "efmi_vk_backend.dll"), "--bad-module", str(Path(DIST) / "efmi_vk_backend_mismatch.dll")]
    result = subprocess.run(args, env=env, cwd=folder, capture_output=True, text=True, errors="replace", timeout=120)
    output = result.stdout + result.stderr
    (folder / "stdout.txt").write_text(output.encode("ascii", "backslashreplace").decode(), encoding="ascii")
    label = "two packages in a directory"
    checks.equal(result.returncode, 0, label + ": the scene's keys, reloads, exchanges and pixels as with one package")
    checks.check("validation: errors=0 warnings=0" in output, label + ": clean core/sync validation")
    if result.returncode:
        print(output[-6000:]); print("artifacts:", folder)
    report = folder / "runtime-final.json"
    status = json.loads(report.read_text())["result"]["replacement"] if report.is_file() else {}
    packages = status.get("packages", [])
    names = [Path(p.get("path", "")).name for p in packages]
    checks.equal(names, ["0-other.bin", "character.bin"], label + ": both packages listed in name order")
    if len(packages) == 2:
        other, own = packages
        checks.equal((other["lazy"], other["activated"], other.get("images_ready", 0), other["counters"].get("replaced", 0)),
                     (True, False, 0, 0), label + ": the other character's package never matched and never went to the GPU")
        checks.check(own["lazy"] and own["activated"] and own["counters"].get("replaced", 0) > 0 and own["images_ready"] == 2,
                     label + ": the fixture's package went to the GPU on its first match and replaced draws")
        checks.equal((own["ini_generation"], own["ini_reloads"]), (2, 2), label + ": its reloads as with one package")
        log = (folder / "layer.log").read_text(encoding="utf-8", errors="replace")
        activations = [line for line in log.splitlines() if "activated on its first match" in line]
        checks.check(activations and all("character.bin" in line and "0-other.bin" not in line for line in activations),
                     label + ": only the fixture's package was activated, in each backend after the exchanges")


if __name__ == "__main__":
    raise SystemExit(main())
