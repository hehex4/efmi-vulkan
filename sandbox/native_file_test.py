"""File resources bound by native ShaderOverride lists, with real GPU results and reloads."""
from pathlib import Path
import json
import struct
import sys
import time

from harness import Checks, Sandbox, test_dir
from native_compute_test import compile_shader, read_compute_result
from runtime_fixture import CORE
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def buffers(checks, case):
    root = Path(test_dir("native-file-" + case + "-"))
    source = root / "scene.hlsl"
    source.write_text("[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;\n"
                      "[[vk::binding(2,0)]] StructuredBuffer<uint> input;\n"
                      "[numthreads(1,1,1)] void main() { output[0] += input[0]; }\n", encoding="ascii")
    shader = compile_shader(source, root / "compiled", "12345678", "cs")
    mods = root / "Mods"; mods.mkdir()
    data_file = mods / "input.buf"; data_file.write_bytes(struct.pack("<4I", 17, 0, 0, 0))
    body = "vk_binding = cs-t2, 0, 2\ncs-t2 = ResourceInput\n"
    if case == "null": body = "vk_binding = cs-t2, 0, 2\ncs-t2 = null\n"
    if case == "write_refusal": body = "vk_binding = cs-u0, 0, 0\ncs-u0 = ResourceInput\n"
    stride = 3 if case == "bad_stride" else 4
    (mods / "test.ini").write_text(
        "namespace = native_file\n[Constants]\nglobal $flags = -1\n[ShaderOverrideCompute]\nhash = %08x\n" % crc32c(shader.read_bytes()) +
        body + "$flags = ResourceInput->bindflags\n"
        "[ResourceInput]\ntype = Buffer\nstride = %d\nfilename = input.buf\n" % stride +
        ("bind_flags = shader_resource\n" if case == "write_refusal" else ""), encoding="ascii")
    if case == "empty_swap": (mods / "test.ini").unlink()
    settings = root / "settings"; settings.mkdir()
    output, keys = root / "compute.raw", root / "keys.txt"; keys.write_text("1\n", encoding="ascii")
    env = dict(EFMI_VK_M2_MODS=str(mods), EFMI_VK_M2_CORE=str(CORE), EFMI_VK_M2_SETTINGS=str(settings),
               EFMI_VK_M2_ENABLED="1", EFMI_VK_TEST_KEYS=str(keys))
    args = ["--compute-shader", str(shader), "--compute-output", str(output), "--compute-bindings", "--compute-repeat", "2"]
    if case == "push": args += ["--descriptors", "push"]
    if case == "two_queues": args.append("--two-queues")
    if case == "secondary": args.append("--compute-secondary")
    with Sandbox(seconds=6 if case in ("reload", "swap", "empty_swap") else 3, validation=True, env_extra=env, extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:
            if case in ("swap", "empty_swap"):
                import shutil
                module=root / "native-backend.dll"
                shutil.copy2(Path(__file__).resolve().parents[1] / "dist/efmi_vk_backend.dll",module)
                probe.swap_module(str(module));time.sleep(2)
            if case == "reload":
                checks.equal(struct.unpack_from("<I", read_compute_result(output))[0], 34, "reload: initial file reaches the shader")
                data_file.write_bytes(struct.pack("<4I", 23, 0, 0, 0))
                keys.write_text("1 121\n", encoding="ascii");time.sleep(.2);keys.write_text("1\n", encoding="ascii");time.sleep(1)
            status = probe.status().get("replacement", {})
            if case in ("swap", "empty_swap"): checks.check(status.get("module", {}).get("generation", 0)>0, case + ": new module active")
        sb.proc.wait(timeout=30)
        checks.equal(sb.proc.returncode, 0, case + ": normal exit")
        (root / "layer.log").write_bytes(Path(sb.log_path).read_bytes())
    data = read_compute_result(output)
    expected = 0 if case == "null" else 14 if case in ("write_refusal", "bad_stride", "empty_swap") else 46 if case == "reload" else 34
    checks.equal(struct.unpack_from("<I", data)[0] if data else None, expected, case + ": expected GPU bytes")
    if case in ("write_refusal", "bad_stride"):
        checks.check(bool(status.get("shader_overrides", {}).get("last_reason")), case + ": unsupported file contract is reported")
    elif case != "empty_swap": checks.check(status.get("shader_overrides", {}).get("executed", 0) > 0, case + ": native file plan executes")
    checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(), case + ": Vulkan validation clean")
    (root / "status.json").write_text(json.dumps(status, indent=2), encoding="ascii")
    print("artifacts:", root)


def main():
    checks = Checks("native files"); selected = set(sys.argv[1:])
    for case in ("buffer", "push", "reload", "null", "write_refusal", "bad_stride", "two_queues", "secondary", "swap", "empty_swap"):
        if not selected or case in selected: buffers(checks, case)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
