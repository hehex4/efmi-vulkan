"""Shared helpers of the sandbox tests: paths, the check counter and the sandbox process."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.environ.get("EFMI_VK_DIST", os.path.join(ROOT, "dist"))
SANDBOX_EXE = os.path.join(DIST, "efmi_vk_sandbox.exe")
INJECT_DLL = os.path.join(DIST, "efmi_vk_inject.dll")
LAYER_NAME = "VK_LAYER_EFMI_probe"

sys.path.insert(0, os.path.join(ROOT, "server"))


def test_dir(prefix: str) -> str:
    """Keep every test artifact inside the project."""
    parent = os.path.join(ROOT, "build", "tests")
    os.makedirs(parent, exist_ok=True)
    return tempfile.mkdtemp(prefix=prefix, dir=parent)


class Checks:
    """Counts checks and prints the failed ones."""

    def __init__(self, name: str):
        self.name = name
        self.passed = 0
        self.failed = 0

    def check(self, condition, what: str, detail=None) -> bool:
        if condition:
            self.passed += 1
            return True
        self.failed += 1
        print("  FAIL: %s%s" % (what, "" if detail is None else " (%s)" % (detail,)))
        return False

    def equal(self, got, want, what: str) -> bool:
        return self.check(got == want, what, "got %r, want %r" % (got, want))

    def finish(self) -> int:
        total = self.passed + self.failed
        print("%s: %d/%d checks passed" % (self.name, self.passed, total))
        return 0 if self.failed == 0 and total > 0 else 1


# The defaults of layer/src/common.h, as the environment spells them.
PINNED_SETTINGS = {
    "EFMI_VK_RESOURCES": "1",
    "EFMI_VK_SHADOW_USAGE": "40",
    "EFMI_VK_PROFILE_TRACKER": "0",
    "EFMI_VK_UNMAP_SNAPSHOT_ALL": "1",
    "EFMI_VK_UNMAP_SNAPSHOT_LIMIT": str(16 << 20),
    "EFMI_VK_SHADER_BUDGET": str(512 << 20),
    "EFMI_VK_IMAGE_HASH_LIMIT": str(64 << 20),
}


def layer_env(log_path: str | None = None, mode: str = "probe", extra: dict | None = None) -> dict:
    """Environment of a child process that loads the layer from dist/.

    Nothing is registered system-wide: the loader finds the manifest through
    VK_LAYER_PATH and enables it through VK_INSTANCE_LAYERS. Implicit layers of other
    software (overlays, capture hooks) are switched off for the child so that runs
    are repeatable. An elevated process ignores VK_LAYER_PATH, so this works only
    for children without elevation.
    """
    env = inject_env(log_path, mode, extra)
    env["VK_LAYER_PATH"] = DIST
    env["VK_INSTANCE_LAYERS"] = LAYER_NAME
    return env


def inject_env(log_path: str | None = None, mode: str | None = "probe", extra: dict | None = None) -> dict:
    """Environment of a child process into which the DLL is loaded with LoadLibrary.

    No Vulkan loader variable points at the layer here; the variables only configure
    the DLL once it is in the process. With ``mode=None`` the mode is left to the
    settings file next to the DLL.
    """
    env = dict(os.environ)
    for name in ("VK_INSTANCE_LAYERS", "VK_LAYER_PATH", "VK_ADD_LAYER_PATH", "VK_LOADER_LAYERS_ENABLE",
                 "VK_LOADER_LAYERS_ALLOW", "VK_LAYER_SETTINGS_PATH", "EFMI_VK_MODE", "EFMI_VK_ENTRY", "EFMI_VK_LOG"):
        env.pop(name, None)
    env["VK_LOADER_LAYERS_DISABLE"] = "~implicit~"
    # Exercise upload hashes even when dist/ disables them for the user's game session.
    # A test can still select the disabled path explicitly with env_extra.
    env["EFMI_VK_IMAGE_HASH"] = "1"
    if mode is not None:
        env["EFMI_VK_MODE"] = mode
        # dist/efmi_vk.cfg carries diagnostic settings between game sessions (reduced tracking,
        # profiling). A test that names its mode gets the defaults of the source for everything
        # else, whatever that file says; env_extra still selects another value on purpose.
        # mode=None means the caller wants the settings file and nothing is pinned.
        env.update(PINNED_SETTINGS)
    if log_path:
        env["EFMI_VK_LOG"] = log_path
    if extra:
        env.update(extra)
    return env


class Sandbox:
    """Runs the sandbox program and waits until it has written its expected values."""

    def __init__(self, threads: int = 2, seconds: int = 60, with_layer: bool = True, mode: str | None = "probe",
                 env_extra: dict | None = None, entry: str = "layer", inject_dll: str | None = None,
                 descriptors: str = "ordinary", draws: int = 5, extra_args: list[str] | None = None,
                 validation: bool = False):
        """``entry``: ``layer`` (the Vulkan loader loads the layer), ``inject`` (the sandbox loads
        the DLL before it touches Vulkan), ``inject-late`` (after its device exists) or
        ``inject-late-exports`` (late, and the device functions are then resolved again through
        the exports of vulkan-1.dll). Ignored when ``with_layer`` is false. ``inject_dll`` replaces
        the DLL from dist/ for the inject entries."""
        self.threads = threads
        self.seconds = seconds
        self.with_layer = with_layer
        self.entry = entry
        self.inject_dll = inject_dll or INJECT_DLL
        self.mode = mode
        self.env_extra = env_extra
        self.descriptors = descriptors
        self.draws = draws
        self.extra_args = extra_args or []
        self.validation = validation
        self.dir = test_dir("sandbox-")
        self.expected_path = os.path.join(self.dir, "expected.json")
        self.trigger_path = os.path.join(self.dir, "rewrite.trigger")
        self.log_path = os.path.join(self.dir, "layer.log")
        self.stdout_path = os.path.join(self.dir, "stdout.txt")
        self.proc = None
        self.expected = None
        self._stdout = None

    def __enter__(self):
        if not os.path.isfile(SANDBOX_EXE):
            raise RuntimeError("%s is missing; run scripts/build.py" % SANDBOX_EXE)
        args = [SANDBOX_EXE, "--seconds", str(self.seconds), "--threads", str(self.threads),
                "--expected", self.expected_path, "--trigger", self.trigger_path, "--descriptors", self.descriptors,
                "--draws", str(self.draws)] + self.extra_args
        if not self.with_layer:
            env = inject_env(mode=None, extra=self.env_extra)
        elif self.entry == "layer":
            env = layer_env(self.log_path, self.mode, self.env_extra)
        else:
            if not os.path.isfile(self.inject_dll):
                raise RuntimeError("%s is missing; run scripts/build.py" % self.inject_dll)
            env = inject_env(self.log_path, self.mode, self.env_extra)
            args += ["--" + self.entry, self.inject_dll]
        if self.validation:
            validation_dir = os.environ.get("EFMI_VK_VALIDATION_DIR", "F:/vulkan/validation-1.4.321.0/bin")
            if not os.path.isfile(os.path.join(validation_dir, "VkLayer_khronos_validation.json")):
                raise RuntimeError("validation layer missing; run scripts/build_validation.py")
            env["VK_LAYER_PATH"] = os.pathsep.join([DIST, validation_dir])
            env["VK_LAYER_VALIDATE_SYNC"] = "1"
            env["VK_LAYER_DUPLICATE_MESSAGE_LIMIT"] = "0"
            args += ["--validation"]
        # VVL's shader cache checks XDG_CACHE_HOME before TMP/TEMP, even on Windows.
        env.update(XDG_CACHE_HOME=self.dir, TMPDIR=self.dir, TEMP=self.dir, TMP=self.dir)
        self._stdout = open(self.stdout_path, "wb")
        try:
            self.proc = subprocess.Popen(
                args,
                env=env, stdout=self._stdout, stderr=subprocess.STDOUT, cwd=self.dir,
            )
            deadline = time.monotonic() + 30
            while not os.path.isfile(self.expected_path):
                if self.proc.poll() is not None:
                    raise RuntimeError("the sandbox exited early with code %s:\n%s" % (self.proc.returncode, self.output()))
                if time.monotonic() > deadline:
                    raise RuntimeError("the sandbox did not become ready:\n%s" % self.output())
                time.sleep(0.05)
            # The file appears through a rename. Something else (a scanner) may hold it for a moment;
            # a sharing violation inside the readiness deadline is waited out, not treated as a failure.
            while True:
                try:
                    with open(self.expected_path, "r", encoding="utf-8") as f:
                        self.expected = json.load(f)
                    break
                except PermissionError:
                    if time.monotonic() > deadline:
                        raise
                    time.sleep(0.05)
        except BaseException:
            self.__exit__()
            raise
        return self

    @property
    def pid(self) -> int:
        return self.proc.pid

    def output(self) -> str:
        try:
            with open(self.stdout_path, "r", encoding="utf-8", errors="replace") as f:
                return f.read()
        except OSError:
            return ""

    def log(self) -> str:
        try:
            with open(self.log_path, "r", encoding="utf-8", errors="replace") as f:
                return f.read()
        except OSError:
            return ""

    def rewrite_index_buffer(self, timeout: float = 10.0) -> None:
        """Asks the sandbox to rewrite the second mesh and waits until it has done so."""
        with open(self.trigger_path, "w") as f:
            f.write("go")
        done = self.trigger_path + ".done"
        deadline = time.monotonic() + timeout
        while not os.path.isfile(done):
            if self.proc.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("the sandbox did not rewrite its index buffer:\n%s" % self.output())
            time.sleep(0.05)

    def __exit__(self, *exc):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=10)
        if self._stdout is not None:
            self._stdout.close()
