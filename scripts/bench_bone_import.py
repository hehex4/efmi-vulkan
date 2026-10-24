"""Interleaved CPU microbenchmark of committed and working production bone importers.

Outputs stay under build/. This measures cached CPU memory, not Vulkan mapped reads
or game frame time. Run with no concurrent build, replay or game for useful timings.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

from build import COMMON, ROOT, find_vcvars, run_cl


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-ref", required=True)
    parser.add_argument("--rounds", type=int, default=7)
    args = parser.parse_args()
    if args.rounds < 3:
        parser.error("at least three interleaved rounds are required")
    root = Path(ROOT)
    baseline = subprocess.check_output(["git", "rev-parse", "--verify", args.baseline_ref + "^{commit}"], cwd=root, text=True).strip()
    out = root / "build/bone-bench"
    out.mkdir(parents=True, exist_ok=True)
    vcvars = find_vcvars()
    binaries, digests = {}, {}
    for name in ("baseline", "candidate"):
        folder = out / name
        folder.mkdir(exist_ok=True)
        digests[name] = {}
        for file in ("merged_skeleton.h", "merged_skeleton.cpp"):
            relative = "layer/src/" + file
            data = (subprocess.check_output(["git", "show", baseline + ":" + relative], cwd=root)
                    if name == "baseline" else (root / relative).read_bytes())
            (folder / file).write_bytes(data)
            digests[name][file] = hashlib.sha256(data).hexdigest()
        exe = folder / "bone_bench.exe"
        run_cl(vcvars, str(folder), COMMON + ["/I" + str(folder), "/Fo" + str(folder) + "/",
               "/Fd" + str(folder / "bench.pdb"), str(folder / "merged_skeleton.cpp"),
               str(root / "sandbox/bone_bench.cpp"), "/Fe:" + str(exe), "/link", "/NOLOGO"])
        binaries[name] = exe
        digests[name]["executable"] = hashlib.sha256(exe.read_bytes()).hexdigest()

    def run(name: str) -> dict:
        return json.loads(subprocess.check_output([str(binaries[name])], cwd=out, text=True))

    for name in binaries:
        run(name)  # Exclude process/cache warmup from the paired samples.
    samples = {name: [] for name in binaries}
    for i in range(args.rounds):
        for name in (("baseline", "candidate") if i % 2 == 0 else ("candidate", "baseline")):
            samples[name].append(run(name))
    summary = []
    for i, base in enumerate(samples["baseline"][0]["cases"]):
        rows = [sample["cases"][i] for values in samples.values() for sample in values]
        assert all((row["bones"], row["remapped"], row["checksum"]) ==
                   (base["bones"], base["remapped"], base["checksum"]) for row in rows), "output differs"
        medians = {name: statistics.median(sample["cases"][i]["ns_per_import"] for sample in values)
                   for name, values in samples.items()}
        summary.append({"bones": base["bones"], "remapped": base["remapped"],
                        "median_ns": medians, "ratio": medians["candidate"] / medians["baseline"]})
    result = {"scope": "cached CPU bone import; no mapped GPU memory or game FPS claim", "baseline": baseline,
              "rounds": args.rounds, "sources": digests, "output_equal": True, "summary": summary, "samples": samples}
    (out / "report.json").write_text(json.dumps(result, indent=2) + "\n", encoding="ascii")
    print(json.dumps(summary, indent=2))
    print("Output bytes agree in every run; report:", out / "report.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
