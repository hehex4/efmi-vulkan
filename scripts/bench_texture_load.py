"""First-use texture loads: the original's (D3D11, DirectXTK) against the layer's loader, same files.

Builds sandbox/texture_load_bench.cpp and runs it on the installed Mods' largest DDS maps (read-only),
after reading each file once so that both paths start from the file cache, as the game's frames do
after a Mod's first load. Writes build/texture-bench/report.json. No game, window or layer.
"""
from __future__ import annotations
import argparse
import json
import statistics
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from build import COMMON, find_vcvars, find_vulkan_include, run_cl  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
MODS = Path("D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Mods")
TK = Path("F:/vulkan/XXMI-Libs-Package/DirectXTK")


def build(out: Path) -> Path:
    out.mkdir(parents=True, exist_ok=True)
    exe = out / "texture_load_bench.exe"
    args = COMMON + ["/I" + find_vulkan_include(), "/I" + str(TK / "Inc"), "/I" + str(TK / "Src"),
                     "/Fo" + str(out) + "/", "/Fd" + str(out / "bench.pdb"),
                     str(ROOT / "sandbox/texture_load_bench.cpp"), str(TK / "Src/DDSTextureLoader.cpp"),
                     *[str(ROOT / "frontend/assets" / n) for n in ("dds.cpp", "image_upload.cpp", "buffer_upload.cpp", "resource_cache.cpp")],
                     "/Fe:" + str(exe), "/link", "/NOLOGO", "d3d11.lib", "dxguid.lib", "ole32.lib"]
    run_cl(find_vcvars(), str(out), args)
    return exe


def run(exe: Path, args: list[str]) -> list[dict]:
    result = subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=600)
    if result.returncode:
        raise RuntimeError(result.stderr[-2000:] or result.stdout[-2000:])
    return [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--count", type=int, default=10, help="the largest N DDS files of the Mods folder")
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--check", action="store_true",
                        help="fail when a first use waits longer than the original's load: one map alone, and all of them")
    args = parser.parse_args()
    out = ROOT / "build/texture-bench"
    exe = out / "texture_load_bench.exe" if args.no_build else build(out)
    files = sorted((p for p in MODS.rglob("*") if p.suffix.lower() == ".dds" and p.is_file()), key=lambda p: -p.stat().st_size)[:args.count]
    for path in files:
        path.read_bytes()  # into the file cache
    names = [str(p) for p in files]
    report = {"scope": "standalone first-use texture loads, no game", "files": [{"path": n, "bytes": p.stat().st_size} for n, p in zip(names, files)]}
    d3d = [run(exe, ["--d3d11", *names]) for _ in range(args.rounds)]
    vk = run(exe, ["--vulkan", str(args.rounds), *names])
    report["d3d11"] = d3d
    report["vulkan"] = vk
    original_sum = statistics.median(rows[-1]["sum_call_us"] for rows in d3d)
    original_gpu = statistics.median(rows[-1]["sum_gpu_done_us"] for rows in d3d)
    original_one = statistics.median(r["call_us"] for rows in d3d for r in rows if "file" in r)
    together = [r for r in vk if r.get("mode") == "together" and "wall_us" in r]
    alone = [r for r in vk if r.get("mode") == "alone" and "file" in r]
    summary = {
        "files": len(files), "megabytes": round(sum(p.stat().st_size for p in files) / 1048576, 1),
        "original_one_file_median_us": original_one,
        "original_all_files_us": original_sum, "original_all_files_gpu_done_us": original_gpu,
        "layer_one_file_median_us": statistics.median(r["ready_us"] for r in alone),
        "layer_all_files_together_us": statistics.median(r["wall_us"] for r in together),
        "layer_steps_per_load_us": {k: round(statistics.median(r[k] / max(1, r["ready"]) for r in together))
                                    for k in ("read_us", "prepare_us", "complete_us")},
    }
    report["summary"] = summary
    (out / "report.json").write_text(json.dumps(report, indent=1) + "\n", encoding="ascii")
    print(json.dumps(summary, indent=1))
    if args.check:
        slow = []
        if summary["layer_one_file_median_us"] > summary["original_one_file_median_us"]:
            slow.append("one map alone takes longer than the original's load")
        if summary["layer_all_files_together_us"] > summary["original_all_files_us"]:
            slow.append("all maps take longer than the original's loads one after another")
        for text in slow:
            print("CHECK FAILED:", text)
        return 1 if slow else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
