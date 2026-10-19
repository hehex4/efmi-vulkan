"""Original INI Present work drawn on a real swap chain by the layer (phase 1, D-072).

The sandbox program presents to a window; the character generation of the synthetic fixture
carries a second INI in its Mod folder whose Present list runs a CustomShader into bb. The layer
must draw it into the presented image, ordered by semaphores, with clean validation. A test-only
copy of the 20th owned presentation (efmi_vk_sandbox.exe only) gives the pixels.
"""
import json
from pathlib import Path
from harness import Checks, Sandbox, test_dir
from runtime_fixture import fixture

PANEL_HLSL = r"""struct V{float4 position:SV_Position;};
#ifdef VERTEX_SHADER
V main(uint id:SV_VertexID){V v;v.position=float4(id&1?0.0:-1.0,id&2?0.0:1.0,0,1);return v;}
#else
float4 main(V v):SV_Target{return float4(0.25,0.5,0.75,1);}
#endif
"""
PANEL_INI = """[CustomShaderOwnedPanel]
vs = panel.hlsl
ps = panel.hlsl
cull = none
topology = triangle_strip
o0 = set_viewport bb
draw = 4, 0
[Present]
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


def main():
    checks = Checks("owned Present on a swap chain")
    for entry in ("layer", "inject"):
        folder = Path(test_dir("owned-present-"))
        package = fixture(folder)
        (folder / "panel").mkdir()
        (folder / "panel/panel.hlsl").write_text(PANEL_HLSL, encoding="ascii")
        (folder / "panel/panel.ini").write_text(PANEL_INI, encoding="ascii")
        readback = folder / "present.raw"
        extra = dict(EFMI_VK_M2_PACKAGE=str(package), EFMI_VK_M2_ENABLED="1", EFMI_VK_TEST_PRESENT_READBACK=str(readback))
        with Sandbox(seconds=5, env_extra=extra, entry=entry, extra_args=["--readable-swapchain"], validation=True) as sandbox:
            sandbox.proc.wait(timeout=90)
            output = sandbox.output()
            log = Path(sandbox.log_path).read_text(errors="replace") if Path(sandbox.log_path).is_file() else ""
        label = entry + ":"
        checks.equal(sandbox.proc.returncode, 0, label + " the sandbox exits normally")
        checks.check("validation: errors=0 warnings=0" in output, label + " clean core and synchronization validation", output[-3000:])
        checks.check("null descriptors added" in log or "null descriptors enabled" in log, label + " null descriptors are on in character mode")
        checks.check("owned GPU work unavailable" not in log, label + " the owned GPU tools are deployed beside the layer")
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
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
