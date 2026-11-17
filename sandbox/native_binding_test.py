"""Native descriptor changes execute on the GPU and persist through the original call boundary."""
from pathlib import Path
import json
import struct
import sys
import time

from harness import Checks, Sandbox, test_dir, configuration_root
from native_compute_test import compile_shader, read_compute_result
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def scene(checks, case):
    root = Path(test_dir("native-binding-" + case + "-"))
    source = root / "scene.hlsl"
    source.write_text("[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;\n"
                      "[[vk::binding(2,0)]] StructuredBuffer<uint> input;\n"
                      "[numthreads(1,1,1)] void main() { output[0] += input[0]; }\n", encoding="ascii")
    shader = compile_shader(source, root / "compiled", "12345678", "cs")
    body = "ResourceOriginal = ref cs-u0\ncs-u0 = ref cs-u1\n"
    expected = (0, 14)
    if case == "retained":
        body = "if $saved == 0\nResourceSaved = ref cs-u1\n$saved = 1\nendif\ncs-u0 = ref ResourceSaved\n"
    elif case == "post":
        body = "if $moved == 0\npost cs-u0 = ref cs-u1\npost $moved = 1\nendif\n"
        expected = (7, 7)
    elif case == "caller":
        body += "handling = skip\ndraw = from_caller\npost cs-u0 = ref ResourceOriginal\npost draw = from_caller\n"
        expected = (14, 14)
    elif case == "late": expected = (0, 22)
    elif case == "two_queues": expected = (14, 0)
    elif case == "wrong_kind":
        body = "cs-cb0 = ref cs-u1\n"
        expected = (14, 0)
    mods = root / "Mods"; mods.mkdir()
    (mods / "test.ini").write_text(
        "namespace = native_binding\n[Constants]\nglobal $saved = 0\nglobal $moved = 0\n"
        "[Present]\n$moved = 0\n[ResourceSaved]\n[ResourceOriginal]\n[ShaderOverrideCompute]\nvk_hash = %08x\n" % crc32c(shader.read_bytes()) +
        "vk_binding = cs-u0, 0, 0\nvk_binding = cs-u1, 0, 1\nvk_binding = cs-cb0, 0, 0\n" + body, encoding="ascii")
    settings = root / "settings"; settings.mkdir()
    output = root / "compute.raw"
    env = dict(EFMI_VK_CONFIGURATION=configuration_root(mods), EFMI_VK_M2_SETTINGS=str(settings), EFMI_VK_M2_ENABLED="1")
    args = ["--compute-shader", str(shader), "--compute-output", str(output), "--compute-bindings", "--compute-repeat", "2"]
    if case == "late": args.append("--compute-late-data")
    if case == "two_queues": args.append("--two-queues")
    if case == "indirect": args.append("--compute-indirect")
    if case in ("template", "push", "push-template"): args += ["--descriptors", case]
    with Sandbox(seconds=3, validation=True, entry="inject" if case == "inject" else "layer", env_extra=env, extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe: status = probe.status().get("replacement", {})
        sb.proc.wait(timeout=30)
        checks.equal(sb.proc.returncode, 0, case + ": normal exit")
        (root / "layer.log").write_bytes(Path(sb.log_path).read_bytes())
    data = read_compute_result(output)
    checks.equal(struct.unpack_from("<II", data) if len(data) >= 8 else None, expected, case + ": GPU buffer destinations and values")
    calls = status.get("shader_overrides", {})
    if case == "two_queues": checks.check("queue" in calls.get("last_reason", ""), case + ": missing cross-queue hazard contract leaves the original")
    elif case == "wrong_kind": checks.check("kind" in calls.get("last_reason", ""), case + ": incompatible descriptor leaves the original")
    else: checks.check(calls.get("executed", 0) > 0, case + ": descriptor plan executed")
    checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(), case + ": Vulkan validation clean")
    (root / "status.json").write_text(json.dumps(status, indent=2), encoding="ascii")
    print("artifacts:", root)


def main():
    checks = Checks("native bindings"); selected = set(sys.argv[1:])
    for case in ("direct", "retained", "post", "caller", "late", "indirect", "inject", "wrong_kind", "template", "push", "push-template", "two_queues"):
        if not selected or case in selected: scene(checks, case)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
