"""Original INI Present work drawn on a real swap chain by the layer (phase 1, D-072).

The sandbox program presents to a window; the character generation of the synthetic fixture
carries a second INI in its Mod folder whose Present list runs a CustomShader into bb. The layer
must draw it into the presented image, ordered by semaphores, with clean validation. A test-only
copy of the 20th owned presentation (efmi_vk_sandbox.exe only) gives the pixels.

Like a Mod's key panel, the list changes an INI parameter and binds a file image every frame: once
the first frames made what the draw needs, a frame allocates no device memory and creates no
descriptor pool (BUG-058; each allocation under the game's locks could wait for the GPU).

The same folder runs a second time, as the next start of the game: the panel's stages come from the
shader cache in the settings folder, none is compiled again, and the picture is the same (D-126).

F10 after the Mod's folder went (BUG-062): the package with Present work is taken out without any wait
for the GPU inside vkQueuePresentKHR (in the game such a wait held work that could complete only after
the presentation went: the game froze). 3DMigoto's reload never waits for the GPU in Present.
"""
import json
import shutil
import time
from pathlib import Path
from harness import Checks, Sandbox, test_dir
from runtime_fixture import fixture, CORE
from efmi_vk.probe_client import ProbeClient

PANEL_HLSL = r"""struct V{float4 position:SV_Position;};
#ifdef VERTEX_SHADER
V main(uint id:SV_VertexID){V v;v.position=float4(id&1?0.0:-1.0,id&2?0.0:1.0,0,1);return v;}
#else
Texture2D<float4> image:register(t100);
Texture1D<float4> IniParams:register(t120);
float4 main(V v):SV_Target{if(IniParams.Load(int2(87,0)).x<0)return image.Load(int3(0,0,0));return float4(0.25,0.5,0.75,1);}
#endif
"""
PANEL_INI = """[Constants]
global $frame = 0
[ResourcePanelImage]
filename = panel.dds
[CustomShaderOwnedPanel]
vs = panel.hlsl
ps = panel.hlsl
cull = none
topology = triangle_strip
o0 = set_viewport bb
draw = 4, 0
[Present]
$frame = $frame + 1
x87 = $frame
ps-t100 = ResourcePanelImage
run = CustomShaderOwnedPanel
"""
# float4(0.25, 0.5, 0.75, 1) in a B8G8R8A8 or R8G8B8A8 UNORM image.
BGRA = (191, 128, 64, 255)
RGBA = (64, 128, 191, 255)


def pixel(data, width, x, y):
    at = (y * width + x) * 4
    return tuple(data[at:at + 4])


def near(a, b):
    return all(abs(p - q) <= 1 for p, q in zip(a, b))


def removed_with_present_work(checks, entry):
    """BUG-062: an F10 that finds a Mod with Present work gone must not wait for that work in Present.
    3DMigoto's reload (ReloadConfig, run in Present) only releases objects and never waits for the GPU;
    the layer counts every wait of its own made inside vkQueuePresentKHR (`present_gpu_waits`)."""
    root = Path(test_dir("owned-present-removed-"))
    mods = root / "Mods"
    folder = mods / "fixture"
    folder.mkdir(parents=True)
    fixture(folder)
    (folder / "panel.hlsl").write_text(PANEL_HLSL, encoding="ascii")
    (folder / "panel.ini").write_text(PANEL_INI, encoding="ascii")
    shutil.copy2(folder / "red.dds", folder / "panel.dds")
    keys = root / "keys.txt"
    keys.write_text("1\n", encoding="ascii")
    extra = dict(EFMI_VK_M2_MODS=str(mods), EFMI_VK_M2_CORE=str(CORE), EFMI_VK_M2_SETTINGS=str(root / "settings"),
                 EFMI_VK_M2_ENABLED="1", EFMI_VK_TEST_KEYS=str(keys))
    label = entry + ", F10 with the Mod gone:"
    before, after, panel = {}, {}, {}
    with Sandbox(seconds=6, env_extra=extra, entry=entry, validation=True) as sandbox:
        with ProbeClient(sandbox.pid) as probe:
            time.sleep(1.5)
            before = probe.status().get("replacement", {})
            # One package: its fields are at the top of the status (more: under "packages").
            panel = before.get("owned_present") or next(
                (p.get("owned_present", {}) for p in before.get("packages", []) if "fixture" in p.get("path", "")), {})
            (mods / "fixture").rename(mods / "DISABLED_fixture")
            keys.write_text("1 121\n", encoding="ascii")  # F10, the original's reload key
            time.sleep(0.4)
            keys.write_text("1\n", encoding="ascii")
            time.sleep(1.0)
            after = probe.status().get("replacement", {})
        sandbox.proc.wait(timeout=90)
        output = sandbox.output()
        log = Path(sandbox.log_path).read_text(errors="replace") if Path(sandbox.log_path).is_file() else ""
    checks.check(panel.get("submitted", 0) > 10, label + " the Mod's Present work ran before (the case under test)", panel)
    checks.check("character package removed with its Mod" in log, label + " the F10 found the Mod gone", log[-2000:])
    checks.equal((before.get("present_gpu_waits"), after.get("present_gpu_waits")), (0, 0),
                 label + " the layer never waited for the GPU inside vkQueuePresentKHR, as 3DMigoto's reload never does")
    checks.equal(sandbox.proc.returncode, 0, label + " the sandbox ran on and ended normally")
    checks.check("validation: errors=0 warnings=0" in output, label + " clean core and synchronization validation", output[-2000:])
    if sandbox.proc.returncode or after.get("present_gpu_waits"):
        print("artifacts:", root)


def main():
    checks = Checks("owned Present on a swap chain")
    for entry in ("layer", "inject"):
        removed_with_present_work(checks, entry)
    for entry in ("layer", "inject"):
        folder = Path(test_dir("owned-present-"))
        package = fixture(folder)
        (folder / "panel").mkdir()
        (folder / "panel/panel.hlsl").write_text(PANEL_HLSL, encoding="ascii")
        (folder / "panel/panel.ini").write_text(PANEL_INI, encoding="ascii")
        shutil.copy2(folder / "red.dds", folder / "panel/panel.dds")
        readback = folder / "present.raw"
        extra = dict(EFMI_VK_M2_PACKAGE=str(package), EFMI_VK_M2_ENABLED="1", EFMI_VK_TEST_PRESENT_READBACK=str(readback),
                     EFMI_VK_M2_SETTINGS=str(folder / "settings"))
        owned = []
        with Sandbox(seconds=5, env_extra=extra, entry=entry, extra_args=["--readable-swapchain"], validation=True) as sandbox:
            # Two looks a second and a half apart, after the first frames made what the panel needs.
            with ProbeClient(sandbox.pid) as probe:
                for _ in range(2):
                    time.sleep(1.5)
                    owned.append(probe.status().get("replacement", {}).get("owned_present", {}))
            sandbox.proc.wait(timeout=90)
            output = sandbox.output()
            log = Path(sandbox.log_path).read_text(errors="replace") if Path(sandbox.log_path).is_file() else ""
        label = entry + ":"
        checks.equal(sandbox.proc.returncode, 0, label + " the sandbox exits normally")
        checks.check("validation: errors=0 warnings=0" in output, label + " clean core and synchronization validation", output[-3000:])
        checks.check("null descriptors added" in log or "null descriptors enabled" in log, label + " null descriptors are on in character mode")
        checks.check("owned GPU work unavailable" not in log, label + " the owned GPU tools are deployed beside the layer")
        if len(owned) == 2:
            first, second = owned
            keys = ("submitted", "allocations", "descriptor_pools", "staging_reused", "sets_reused")
            checks.check(all(k in first and k in second for k in keys), label + " the presenter reports its driver allocations", first)
            checks.check(second.get("submitted", 0) > first.get("submitted", 0) + 20, label + " the panel kept drawing between the looks",
                         {k: (first.get(k), second.get(k)) for k in keys})
            checks.equal((second.get("allocations"), second.get("descriptor_pools")), (first.get("allocations"), first.get("descriptor_pools")),
                         label + " a frame of the panel allocates no device memory and creates no descriptor pool (BUG-058)")
            checks.check(second.get("staging_reused", 0) > first.get("staging_reused", 0) and second.get("sets_reused", 0) > first.get("sets_reused", 0),
                         label + " the panel's parameter uploads and descriptor sets are reused", {k: (first.get(k), second.get(k)) for k in keys})
        else:
            checks.check(False, label + " the probe answered twice during the run", owned)
        checks.check(readback.is_file() and Path(str(readback) + ".json").is_file(), label + " the 20th owned presentation was copied", log[-3000:])
        if not readback.is_file():
            print("artifacts:", folder)
            continue
        meta = json.loads(Path(str(readback) + ".json").read_text())
        width, height, data = meta["width"], meta["height"], readback.read_bytes()
        checks.equal(len(data), width * height * 4, label + " the copy covers the presented image")
        expected = BGRA if meta["format"] in (44, 50) else RGBA
        inside = pixel(data, width, width // 4, height // 4)
        checks.check(near(inside, expected), label + " the Present CustomShader drew into the top-left quarter of the swap chain image", inside)
        for x, y in ((3 * width // 4, height // 4), (width // 4, 3 * height // 4), (3 * width // 4, 3 * height // 4)):
            outside = pixel(data, width, x, y)
            checks.check(not near(outside, expected), label + " the application's own picture remains outside the quarter", (x, y, outside))
        first_shaders = owned[0].get("shaders", {}) if owned else {}
        checks.check(first_shaders.get("compiled", 0) == 2 and not first_shaders.get("cached"), label + " the first start compiles the panel's two stages",
                     first_shaders)
        # The next start of the game, the same Mods and settings folder (D-126).
        readback_again = folder / "present-again.raw"
        extra.update(EFMI_VK_TEST_PRESENT_READBACK=str(readback_again))
        shaders = {}
        with Sandbox(seconds=4, env_extra=extra, entry=entry, extra_args=["--readable-swapchain"], validation=True) as sandbox:
            with ProbeClient(sandbox.pid) as probe:
                time.sleep(1.5)
                shaders = probe.status().get("replacement", {}).get("owned_present", {}).get("shaders", {})
            sandbox.proc.wait(timeout=90)
            output = sandbox.output()
        checks.equal(sandbox.proc.returncode, 0, label + " the second start exits normally")
        checks.check("validation: errors=0 warnings=0" in output, label + " the second start validates clean", output[-3000:])
        checks.check(shaders.get("cached") == 2 and not shaders.get("compiled") and not shaders.get("translated"),
                     label + " the second start reads both stages from the shader cache and compiles nothing", shaders)
        if readback_again.is_file():
            meta = json.loads(Path(str(readback_again) + ".json").read_text())
            again = readback_again.read_bytes()
            checks.check(near(pixel(again, meta["width"], meta["width"] // 4, meta["height"] // 4), expected),
                         label + " the cached stages draw the same quarter")
        else:
            checks.check(False, label + " the second start's presentation was copied")
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
