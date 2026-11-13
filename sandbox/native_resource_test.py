"""Native descriptor aliases query real application resources and original predicates."""
from pathlib import Path
import json
import struct
import sys
import time

from harness import Checks, Sandbox, test_dir
from native_compute_test import compile_compute
from runtime_fixture import CORE
from runtime_test import sandbox_shader_hashes, band_pixels
from efmi_vk.hashes import crc32c
from efmi_vk.probe_client import ProbeClient


def run_compute(checks, case):
    root = Path(test_dir("native-resources-" + case + "-"))
    original = compile_compute(root, "original", 7)
    mods = root / "Mods"; mods.mkdir()
    (mods / "other.ini").write_text("namespace = unrelated\n[Constants]\nglobal $kept = 1\n", encoding="ascii")
    alias = "cs-cb0" if case == "kind" else "cs-u0"
    address = "0, 0, 1" if case == "element" else "0, 0"
    binding = "vk_binding = %s, %s\n" % (alias, address)
    if case == "conflict": binding += "vk_binding = cs-u0, 0, 1\n"
    if case == "duplicate": binding += binding
    body = "$size = %s->size\n$width = %s->width\nchecktextureoverride = %s\n" % (alias, alias, alias)
    if case == "alias":
        body = "cs-u1 = ref cs-u0\n$size = cs-u1->size\n$width = cs-u1->width\nchecktextureoverride = cs-u1\n"
    if case in ("reference", "reference_flags"):
        body = "ResourceSaved = ref cs-u0\n$size = ResourceSaved->size\n$width = ResourceSaved->width\nchecktextureoverride = cs-u0\n"
    text = ("namespace = native_resources\n[Constants]\nglobal $size = -99\nglobal $width = -99\nglobal $matched = 0\n"
            "[ResourceSaved]\n[ShaderOverrideCompute]\nhash = %08x\n" % crc32c(original.read_bytes()) + binding + body +
            "if $size == 16 && $width == -4 && $matched == 1\nhandling = skip\nendif\n"
            "[TextureOverrideBuffer]\nmatch_type = Buffer\nmatch_byte_width = 16\n$matched = 1\n")
    if case == "flags": text = text.replace("$matched == 1", "$matched == 1 && cs-u0->bindflags == 136")
    if case == "reference_flags":
        text = text.replace("global $matched = 0", "global $matched = 0\nglobal $flags = -99")
        text = text.replace("$size = ResourceSaved->size", "$flags = ResourceSaved->bindflags\n$size = ResourceSaved->size")
        # Original GetResourceBindFlags returns this unused declaration's accumulated flags (zero),
        # not the referenced buffer's capabilities. No creation/substantiation occurs for a ref.
        text = text.replace("$matched == 1", "$matched == 1 && $flags == 0 && cs-u0->bindflags == 136")
    ini = mods / "mod.ini"; ini.write_text(text, encoding="ascii")
    settings = root / "settings"; settings.mkdir()
    output, keys = root / "compute.raw", root / "keys.txt"; keys.write_text("1\n", encoding="ascii")
    env = dict(EFMI_VK_M2_MODS=str(mods), EFMI_VK_M2_CORE=str(CORE), EFMI_VK_M2_SETTINGS=str(settings), EFMI_VK_M2_ENABLED="1", EFMI_VK_TEST_KEYS=str(keys))
    args = ["--compute-shader", str(original), "--compute-output", str(output)]
    if case == "indirect": args.append("--compute-indirect")
    with Sandbox(seconds=5 if case in ("reload", "reload_conflict") else 3, validation=True, entry="inject" if case == "inject" else "layer", env_extra=env, extra_args=args) as sb:
        time.sleep(.8)
        with ProbeClient(sb.pid) as probe:
            status = probe.status().get("replacement", {})
            if case in ("reload", "reload_conflict"):
                data = output.read_bytes() if output.exists() else b""
                checks.equal(struct.unpack_from("<I", data)[0] if len(data) >= 4 else None, 0, "reload: initial mapping acts")
                replacement = "vk_binding = cs-u0, 0, 0\nvk_binding = cs-u0, 0, 9" if case == "reload_conflict" else "vk_binding = cs-u0, 0, 9"
                ini.write_text(text.replace("vk_binding = cs-u0, 0, 0", replacement), encoding="ascii")
                keys.write_text("1 121\n", encoding="ascii"); time.sleep(.18); keys.write_text("1\n", encoding="ascii"); time.sleep(1)
                status = probe.status().get("replacement", {})
            (root / "status.json").write_text(json.dumps(status, indent=2), encoding="ascii")
        sb.proc.wait(timeout=30)
        checks.equal(sb.proc.returncode, 0, case + ": normal exit")
        (root / "layer.log").write_bytes(Path(sb.log_path).read_bytes())
    checks.equal(status.get("shader_overrides", {}).get("native_bindings", 0), 0 if case in ("conflict", "reload_conflict") else 1, case + ": native alias generation loaded")
    if case in ("conflict", "reload_conflict"):
        folder = status.get("folder_generation", {})
        checks.check(any("Conflicting vk_binding" in item.get("reason", "") for item in folder.get("left_inputs", [])), case + ": invalid input isolated with its reason")
        checks.equal(folder.get("ini", {}).get("variables", {}).get("$\\unrelated\\kept"), 1, case + ": unrelated input remains active")
    data = output.read_bytes() if output.exists() else b""
    checks.equal(struct.unpack_from("<I", data)[0] if len(data) >= 4 else None, 7 if case in ("kind", "element", "conflict", "reload", "reload_conflict") else 0,
                 case + ": descriptor queries control the real dispatch")
    checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(), case + ": Vulkan validation clean")
    print("artifacts:", root)


def run_graphics(checks, mode, vertex, fragment, srgb=False):
    root = Path(test_dir("native-resources-graphics-"))
    mods = root / "Mods"; mods.mkdir()
    (mods / "mod.ini").write_text(
        "namespace = native_resources\n[Constants]\nglobal $matched = 0\n"
        "[ShaderOverrideVertex]\nhash = %08x\nvk_binding = vs-cb0, 0, 0\n"
        "[ShaderOverridePixel]\nhash = %08x\nvk_binding = ps-t0, 1, 5\nvk_binding = ps-t2, 1, 2, 0\nvk_binding = ps-t3, 1, 2, 1\n"
        "checktextureoverride = ps-t0\n"
        "if vs-cb0->size == 96 && vs-cb0->stride == 16 && ps-t0->width == 4 && ps-t0->height == 4 && ps-t0->format == %d && ps-t0->mips == 1 && ps-t0->array == 1 && ps-t0->size == -3 && ps-t0->stride == -3 && ps-t0->offset == -3 && ps-t2->size == 16 && ps-t3->size == 16 && (ps-t2->offset + ps-t3->offset) %% 1280 == 256 && $matched == 1\nhandling = skip\nendif\n"
        "[TextureOverrideImage]\nmatch_type = Texture2D\nmatch_width = 4\nmatch_height = 4\nmatch_format = R8G8B8A8_UNORM\n$matched = 1\n" % (vertex, fragment, 29 if srgb else 28), encoding="ascii")
    settings = root / "settings"; settings.mkdir()
    pixels = root / "pixels.raw"
    env = dict(EFMI_VK_M2_MODS=str(mods), EFMI_VK_M2_CORE=str(CORE), EFMI_VK_M2_SETTINGS=str(settings), EFMI_VK_M2_ENABLED="1",
               EFMI_VK_TEST_NOTICE="> native resources", EFMI_VK_TEST_NOTICES_READBACK=str(pixels))
    with Sandbox(seconds=3, validation=True, env_extra=env, extra_args=["--readable-swapchain", "--descriptors", ("ordinary", "template", "push", "push-template")[mode]] + (["--srgb-texture-view"] if srgb else [])) as sb:
        sb.proc.wait(timeout=30)
        checks.equal(sb.proc.returncode, 0, "graphics %d: normal exit" % mode)
        (root / "layer.log").write_bytes(Path(sb.log_path).read_bytes())
    checks.check(pixels.exists() and band_pixels(pixels) < 30, "graphics %d: dynamic/array/image queries skip every draw" % mode)
    checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(), "graphics %d: Vulkan validation clean" % mode)
    print("artifacts:", root)


def main():
    checks = Checks("native resources"); selected = set(sys.argv[1:])
    for case in ("direct", "flags", "indirect", "inject", "alias", "reference", "reference_flags", "kind", "element", "duplicate", "conflict", "reload", "reload_conflict"):
        if not selected or case in selected: run_compute(checks, case)
    if not selected or "graphics" in selected or "srgb" in selected:
        vertex, fragment = sandbox_shader_hashes()
        if not selected or "graphics" in selected:
            for mode in range(4): run_graphics(checks, mode, vertex, fragment)
        if not selected or "srgb" in selected: run_graphics(checks, 0, vertex, fragment, True)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
