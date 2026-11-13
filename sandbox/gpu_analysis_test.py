"""Frame analysis exports completed GPU results, distinct from record-time CPU shadows."""
from pathlib import Path
import json
import struct
import sys
import time

from harness import Checks, Sandbox, test_dir
from native_compute_test import compile_shader
from runtime_fixture import CORE
from efmi_vk.probe_client import ProbeClient


def scene(checks, case):
    root = Path(test_dir("gpu-analysis-" + case + "-"))
    source = root / "scene.hlsl"
    image_case = case in ("image", "image_push")
    source.write_text(("[[vk::binding(2,0)]] RWTexture2D<uint> target;\n" if image_case else "") + "[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;\n"
                      "[numthreads(1,1,1)] void main() { output[0] += 19; " + ("target[int2(0,0)] += 19; " if image_case else "") + "}\n", encoding="ascii")
    shader = compile_shader(source, root / "compiled", "12345678", "cs")
    mods = root / "Mods"; mods.mkdir()
    (root / "d3dx.ini").write_text("[Include]\ninclude = Core\\EFMI\\main.ini\ninclude_recursive = Mods\n"
        "[Rendering]\nrecursive_include = 1\n[Hunting]\nhunting = 1\nanalyse_frame = no_modifiers VK_F9\n", encoding="ascii")
    settings = root / "settings";settings.mkdir()
    keys = root / "keys.txt";keys.write_text("1\n", encoding="ascii")
    env = dict(EFMI_VK_M2_MODS=str(mods), EFMI_VK_M2_CORE=str(CORE), EFMI_VK_M2_SETTINGS=str(settings),
               EFMI_VK_M2_ENABLED="1", EFMI_VK_TEST_KEYS=str(keys), VK_LAYER_VALIDATE_SYNC="1")
    args = ["--compute-shader", str(shader), "--compute-repeat", "2"]
    if case != "usage" and "--baseline" not in sys.argv: args.append("--compute-capture")
    if case in ("api13", "sync2"): args += ["--api", "1.3"]
    if case == "sync2": args.append("--sync2")
    if image_case: args.append("--compute-storage-image")
    if case == "image_push": args += ["--descriptors", "push"]
    if case == "reusable": args.append("--reusable-recording")
    if case == "secondary": args.append("--compute-secondary")
    if case == "two_queues": args.append("--two-queues")
    with Sandbox(seconds=8 if case == "swap" else 5, validation=True, env_extra=env, extra_args=args) as sb:
        time.sleep(1)
        keys.write_text("1 120\n", encoding="ascii");time.sleep(.2);keys.write_text("1\n", encoding="ascii")
        time.sleep(1)
        with ProbeClient(sb.pid) as probe:
            if case == "swap":
                import shutil
                module = root / "analysis-backend.dll"
                shutil.copy2(Path(__file__).resolve().parents[1] / "dist/efmi_vk_backend.dll", module)
                probe.swap_module(str(module));time.sleep(1.1)
                keys.write_text("1 120\n", encoding="ascii");time.sleep(.2);keys.write_text("1\n", encoding="ascii");time.sleep(.5)
            status=probe.status().get("replacement", {})
            if case == "swap": checks.check(status.get("module", {}).get("generation", 0)>0, "swap: replacement module exchanged")
        sb.proc.wait(timeout=30)
        checks.equal(sb.proc.returncode, 0, case + ": normal exit")
        (root / "layer.log").write_bytes(Path(sb.log_path).read_bytes())
    folders = list(settings.glob("FrameAnalysis-*"))
    checks.equal(len(folders), 2 if case == "swap" else 1, case + ": one exclusive analysis directory")
    manifests = [(path.parent, json.loads(line)) for path in settings.glob("FrameAnalysis-*/resources.jsonl") for line in path.read_text().splitlines()]
    completed = [(folder, item) for folder,item in manifests if item.get("state") == "complete" and item.get("point") == "post-dispatch"]
    files = [folder/item["file"] for folder,item in completed if item["file"].startswith("buffer-")]
    values = sorted(struct.unpack_from("<I", path.read_bytes())[0] for path in files if path.stat().st_size >= 4)
    if case in ("usage", "two_queues", "reusable", "sync2"):
        checks.check(not files, case + ": unsupported GPU capture produces no fabricated bytes")
        checks.check(any(item.get("state")=="refused" for _,item in manifests), case + ": refusal is included in resource manifest")
    else:
        checks.equal(values, [19,19,38,38] if case == "swap" else [19,38], case + ": separate post-dispatch GPU snapshots")
        checks.equal(len(completed), 4 if image_case or case == "swap" else 2, case + ": only completed copies are exported")
        if image_case:
            images=[folder/item["file"] for folder,item in completed if item["file"].startswith("image-")]
            checks.equal(sorted(struct.unpack_from("<I", path.read_bytes())[0] for path in images), [19,38], case + ": GPU-written image snapshots are distinct")
            checks.check(all(item.get("format")==98 and item.get("width")==4 and item.get("height")==4 for _,item in completed if item["file"].startswith("image-")), case + ": image format and extent recorded")
    checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(), case + ": Vulkan validation clean")
    (root / "status.json").write_text(json.dumps(status,indent=2), encoding="ascii")
    print("artifacts:",root)


def main():
    checks = Checks("GPU frame analysis");selected=set(sys.argv[1:]) - {"--baseline"}
    for case in ("compute", "secondary", "image", "image_push", "usage", "two_queues", "reusable", "swap", "api13", "sync2"):
        if not selected or case in selected: scene(checks,case)
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
