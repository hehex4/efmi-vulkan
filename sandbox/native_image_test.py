"""Native sampled file images, measured from the application's rendered pixels."""
from pathlib import Path
import json
import struct
import sys
import time
import zlib

from harness import Checks, Sandbox, test_dir
from runtime_fixture import CORE
from runtime_test import sandbox_shader_hashes
from native_shader_test import colored_pixels, compile_shader
from efmi_vk.probe_client import ProbeClient
from efmi_vk.hashes import crc32c
from native_compute_test import read_compute_result


def dds(path, color, layers=1):
    header = [124, 0x100F, 4, 4, 16, 0, 1] + [0] * 11
    header += [32, 4, int.from_bytes(b"DX10", "little"), 0, 0, 0, 0, 0]
    header += [0x1000, 0, 0, 0, 0]
    path.write_bytes(b"DDS " + struct.pack("<31I", *header) + struct.pack("<5I", 28, 3, 0, layers, 0) + bytes(color) * (16 * layers))


def scene(checks, case):
    root = Path(test_dir("native-image-" + case + "-"))
    _, fragment = sandbox_shader_hashes()
    mods = root / "Mods"; mods.mkdir()
    file = mods / ("input.png" if case == "wic" else "input.dds")
    if case == "wic":
        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        file.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">2I5B", 4, 4, 8, 6, 0, 0, 0)) +
                         chunk(b"IDAT", zlib.compress((b"\0" + bytes((255, 0, 255, 255)) * 4) * 4)) + chunk(b"IEND", b""))
    else: dds(file, (255, 0, 255, 255), 2 if case == "array" else 1)
    declaration = "format = R8G8B8A8_UNORM_SRGB\n" if case == "format" else "bind_flags = render_target\n" if case == "no_srv" else ""
    (mods / "test.ini").write_text(
        "namespace = native_image\n[Constants]\nglobal $loaded = 1\n[ShaderOverridePixel]\nhash = %08x\n" % fragment +
        "vk_binding = ps-t0, 1, 5\nps-t0 = " + ("null" if case == "null" else "ResourceTexture") +
        "\n[ResourceTexture]\nfilename = " + file.name + "\n" + declaration, encoding="ascii")
    settings = root / "settings"; fixes = settings / "ShaderFixes"; fixes.mkdir(parents=True)
    shader = root / "sample.hlsl"
    shader.write_text("[[vk::binding(5,1)]] [[vk::combinedImageSampler]] Texture2D<float4> tex;\n"
                      "[[vk::binding(5,1)]] [[vk::combinedImageSampler]] SamplerState samp;\n"
                      "float4 main() : SV_Target0 { return tex.SampleLevel(samp, float2(0,0), 0); }\n", encoding="ascii")
    compile_shader(shader, fixes, "%08x" % fragment, "ps")
    pixels, keys = root / "pixels.raw", root / "keys.txt"; keys.write_text("1\n", encoding="ascii")
    env = dict(EFMI_VK_M2_MODS=str(mods), EFMI_VK_M2_CORE=str(CORE), EFMI_VK_M2_SETTINGS=str(settings),
               EFMI_VK_M2_ENABLED="1", EFMI_VK_TEST_KEYS=str(keys), EFMI_VK_TEST_NOTICE="> native image",
               EFMI_VK_TEST_NOTICES_READBACK=str(pixels))
    trigger = root / "capture.now"
    if case == "reload": env["EFMI_VK_TEST_NOTICES_READBACK_TRIGGER"] = str(trigger)
    args = ["--readable-swapchain"]
    if case == "null": args.append("--mutable-sampler")
    if case == "push": args += ["--descriptors", "push"]
    if case == "two_queues": args.append("--two-queues")
    with Sandbox(seconds=6 if case == "reload" else 3, validation=True, env_extra=env, extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:
            if case == "reload":
                checks.equal(probe.status().get("replacement", {}).get("textures", {}).get("ready"), 1, "reload: initial file texture published")
                dds(file, (0, 255, 0, 255));keys.write_text("1 121\n", encoding="ascii")
                time.sleep(.2);keys.write_text("1\n", encoding="ascii");time.sleep(1)
                trigger.touch()
            status = probe.status().get("replacement", {})
        sb.proc.wait(timeout=30)
        checks.equal(sb.proc.returncode, 0, case + ": normal exit")
        (root / "layer.log").write_bytes(Path(sb.log_path).read_bytes())
    calls = status.get("shader_overrides", {})
    if case in ("array", "format", "no_srv"):
        checks.check(colored_pixels(pixels, (255, 0, 255)) < 30 and bool(calls.get("last_reason")), case + ": incompatible image declaration is reported and leaves the original")
    else:
        color = (0, 0, 0) if case == "null" else (0, 255, 0) if case == "reload" else (255, 0, 255)
        checks.check(colored_pixels(pixels, color) > 100, case + ": rendered pixels match the native binding")
        checks.check(calls.get("executed", 0) > 0, case + ": descriptor plan executed")
    checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(), case + ": Vulkan validation clean")
    (root / "status.json").write_text(json.dumps(status, indent=2), encoding="ascii")
    print("artifacts:", root)


def compute(checks, case):
    root = Path(test_dir("native-image-" + case + "-"))
    source = root / "scene.hlsl"
    source.write_text("[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;\n"
                      "[[vk::binding(2,0)]] Texture2D<float4> input;\n"
                      "[numthreads(1,1,1)] void main() { output[0] += uint(round(input.Load(int3(0,0,0)).r * 17)); }\n", encoding="ascii")
    shader = compile_shader(source, root / "compiled", "12345678", "cs")
    mods = root / "Mods"; mods.mkdir(); dds(mods / "input.dds", (255, 0, 255, 255))
    (mods / "test.ini").write_text("namespace = native_image\n[Constants]\nglobal $loaded = 1\n"
        "[ShaderOverrideCompute]\nhash = %08x\nvk_binding = cs-t0, 0, 2\ncs-t0 = ResourceTexture\n"
        "[ResourceTexture]\nfilename = input.dds\n" % crc32c(shader.read_bytes()), encoding="ascii")
    output = root / "result.bin"
    env = dict(EFMI_VK_M2_MODS=str(mods), EFMI_VK_M2_CORE=str(CORE), EFMI_VK_M2_ENABLED="1")
    args = ["--compute-shader", str(shader), "--compute-output", str(output), "--compute-image", "--compute-repeat", "2"]
    if case == "compute_push": args += ["--descriptors", "push"]
    if case == "compute_secondary": args.append("--compute-secondary")
    with Sandbox(seconds=3, validation=True, env_extra=env, extra_args=args) as sb:
        time.sleep(1)
        with ProbeClient(sb.pid) as probe: status = probe.status().get("replacement", {})
        sb.proc.wait(timeout=30)
        checks.equal(sb.proc.returncode, 0, case + ": normal exit")
        (root / "layer.log").write_bytes(Path(sb.log_path).read_bytes())
    checks.equal(struct.unpack_from("<I", read_compute_result(output))[0], 34, case + ": shader reads uploaded image")
    checks.check(status.get("shader_overrides", {}).get("executed", 0) > 0, case + ": native file plan executed")
    checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(), case + ": Vulkan validation clean")
    (root / "status.json").write_text(json.dumps(status, indent=2), encoding="ascii")
    print("artifacts:", root)


def main():
    checks = Checks("native images"); selected = set(sys.argv[1:])
    for case in ("dds", "wic", "push", "reload", "null", "array", "format", "no_srv", "two_queues"):
        if not selected or case in selected: scene(checks, case)
    for case in ("compute", "compute_push", "compute_secondary"):
        if not selected or case in selected: compute(checks, case)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
