"""Native ShaderOverride calls execute on the application pipeline, with fenced GPU evidence."""
from pathlib import Path
import json
import struct
import subprocess
import sys
import time

from harness import Checks, Sandbox, test_dir, configuration_root
from native_compute_test import DXC, VALIDATOR
from runtime_test import sandbox_shader_hashes, band_pixels
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def compute(checks, case):
    root = Path(test_dir("native-execution-" + case + "-"))
    source, shader = root / "count.hlsl", root / "count.spv"
    source.write_text("[[vk::binding(0,0)]] RWStructuredBuffer<uint> data;\n"
                      "[numthreads(1,1,1)] void main() { uint old; InterlockedAdd(data[0], 1, old); }\n", encoding="ascii")
    for command in ([str(DXC), "-spirv", "-fspv-target-env=vulkan1.1", "-T", "cs_6_0", "-E", "main", "-Fo", str(shader), str(source)],
                    [str(VALIDATOR), "--target-env", "vulkan1.1", str(shader)]):
        subprocess.run(command, check=True, capture_output=True, timeout=30)
    commands, expected = {
        "caller": ("handling = skip\ndraw = from_caller\ndraw = from_caller\n", 2),
        "post": ("draw = from_caller\npost draw = from_caller\n", 3),
        "post_skip": ("handling = skip\ndraw = from_caller\npost draw = from_caller\n", 2),
        "late_skip": ("draw = from_caller\npost handling = skip\n", 2),
        "wrong_domain": ("handling = skip\ndraw = 3, 0\n", 1),
        "dispatch": ("handling = skip\ndispatch = 1, 1, 1\ndispatch = 1, 1, 1\n", 2),
        "indirect": ("handling = skip\ndraw = from_caller\npost draw = from_caller\n", 2),
        "inject": ("draw = from_caller\npost draw = from_caller\n", 3),
        "zero": ("handling = skip\ndispatch = 0, 1, 1\n", 0),
    }[case]
    mods = root / "Mods"; mods.mkdir()
    (mods / "mod.ini").write_text("namespace = native_execution\n[ShaderOverrideCompute]\nhash = %08x\n" % crc32c(shader.read_bytes()) + commands, encoding="ascii")
    settings = root / "settings"; settings.mkdir()
    output = root / "compute.raw"
    env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(settings), EFMI_VK_M2_ENABLED="1")
    args = ["--compute-shader", str(shader), "--compute-output", str(output)]
    if case == "indirect": args.append("--compute-indirect")
    with Sandbox(seconds=3, validation=True, entry="inject" if case == "inject" else "layer", env_extra=env, extra_args=args) as sb:
        time.sleep(.9)
        with ProbeClient(sb.pid) as probe:
            (root / "status.json").write_text(json.dumps(probe.status(), indent=2), encoding="ascii")
        sb.proc.wait(timeout=30)
        checks.equal(sb.proc.returncode, 0, case + ": normal exit")
        (root / "layer.log").write_bytes(Path(sb.log_path).read_bytes())
    data = output.read_bytes() if output.exists() else b""
    checks.equal(struct.unpack_from("<I", data)[0] if len(data) >= 4 else None, expected, case + ": actual GPU invocation count")
    checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(), case + ": Vulkan validation clean")
    print("artifacts:", root)


def graphics(checks, fragment, command, draw_kind, expected_max=None, zero_count=False):
    root = Path(test_dir("native-execution-graphics-"))
    mods = root / "Mods"; mods.mkdir()
    if command:
        (mods / "mod.ini").write_text("namespace = native_execution\n[ShaderOverridePixel]\nhash = %08x\nhandling = skip\n" % fragment + command, encoding="ascii")
    settings = root / "settings"; settings.mkdir()
    pixels = root / "pixels.raw"
    env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(settings), EFMI_VK_M2_ENABLED="1",
               EFMI_VK_TEST_NOTICE="> native calls", EFMI_VK_TEST_NOTICES_READBACK=str(pixels))
    args=["--readable-swapchain"] + ([] if draw_kind == "indexed" else ["--" + draw_kind]) + (["--indirect-zero-count"] if zero_count else [])
    with Sandbox(seconds=3, validation=True, env_extra=env, extra_args=args) as sb:
        time.sleep(.9)
        with ProbeClient(sb.pid) as probe: status = probe.status().get("replacement", {}).get("shader_overrides", {})
        sb.proc.wait(timeout=30)
        checks.equal(sb.proc.returncode, 0, draw_kind + ": normal exit")
        (root / "layer.log").write_bytes(Path(sb.log_path).read_bytes())
    amount = band_pixels(pixels) if pixels.exists() else 0
    checks.check(amount < 30 if zero_count else amount > 30, draw_kind + (": zero GPU count draws nothing" if zero_count else ": native geometry visible"))
    if command: checks.check(status.get("executed", 0) > 0, draw_kind + ": native call plan consumed")
    if expected_max is not None: checks.check(amount < expected_max, draw_kind + ": explicit arguments changed actual geometry")
    checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(), draw_kind + ": Vulkan validation clean")
    print("artifacts:", root)
    return amount


def main():
    checks = Checks("native execution"); selected = set(sys.argv[1:])
    for case in ("caller", "post", "post_skip", "late_skip", "wrong_domain", "dispatch", "indirect", "inject", "zero"):
        if not selected or case in selected: compute(checks, case)
    if not selected or "graphics" in selected:
        _, fragment = sandbox_shader_hashes()
        baseline = graphics(checks, fragment, "", "indexed")
        graphics(checks, fragment, "drawindexed = 3, 0, 0\n", "indexed", baseline * .7)
        graphics(checks, fragment, "draw = 3, 0\n", "indexed", baseline * .5)
        for kind in ("indexed", "non-indexed", "indexed-indirect", "indexed-indirect-count"):
            graphics(checks, fragment, "post draw = from_caller\n", kind)
        graphics(checks,fragment,"post draw = from_caller\n","indexed-indirect-count",zero_count=True)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
