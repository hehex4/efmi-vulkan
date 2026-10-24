"""Execute an unchanged EFMITTL library on owned GPU targets; no game integration claim."""
from __future__ import annotations
import argparse
import contextlib
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from build import find_vcvars, run_cl
from build_xxmi_runtime import build_runtime, runtime_flags
from probe_xxmi_host import ROOT, write_ascii


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin", type=Path, default=Path("D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Mods/EFMITTL"))
    parser.add_argument("--reuse", action="store_true")
    args = parser.parse_args()
    out = ROOT / "build/efmittl-gpu"
    out.mkdir(parents=True, exist_ok=True)
    report = {"scope": __doc__, "passed": False, "inputs": {}}
    try:
        fixtures = Path(tempfile.mkdtemp(prefix="run-", dir=out))
        report["fixtures"] = str(fixtures)
        for source in sorted(args.plugin.rglob("*")):
            if not source.is_file():
                continue
            report["inputs"][str(source)] = hashlib.sha256(source.read_bytes()).hexdigest()
            for mode in ("cached", "source"):
                if mode == "source" and source.suffix.lower() == ".bin":
                    continue
                target = fixtures / mode / "library" / source.relative_to(args.plugin)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
        if not (fixtures / "cached/library/translucency.ini").is_file():
            raise RuntimeError("Installed transparency library INI is missing")
        executable = out / "efmittl_gpu.exe"
        if not args.reuse:
            objects = build_runtime(ROOT / "build/xxmi-runtime")
            log = io.StringIO()
            try:
                with contextlib.redirect_stdout(log):
                    run_cl(find_vcvars(), str(out), runtime_flags(out) + ["/DNOMINMAX",
                        str(ROOT / "sandbox/efmittl_gpu.cpp"), str(ROOT / "layer/src/merged_skeleton.cpp"),
                        *objects, "/Fe:" + str(executable), "/link", "/NOLOGO", "/OPT:REF", "/OPT:ICF"])
            finally:
                write_ascii(out / "build.txt", log.getvalue())
        sys.path.insert(0, str(ROOT / "sandbox"))
        from harness import inject_env
        env = inject_env(str(out / "unexpected-injection.log"))
        env.update(VK_LAYER_PATH="F:/vulkan/validation-1.4.321.0/bin", VK_LAYER_VALIDATE_SYNC="1",
                   VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0", XDG_CACHE_HOME=str(out), TEMP=str(out), TMP=str(out))
        report["executable_sha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()
        report["sources"] = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in
                             ("sandbox/efmittl_gpu.cpp", "frontend/gpu/shader_commands.cpp", "scripts/check_efmittl.py")}
        report["runs"] = []
        for mode in ("cached", "source"):
            result = subprocess.run([str(executable), str(fixtures / mode), mode], cwd=out, env=env,
                                    capture_output=True, encoding="utf-8", errors="replace", timeout=120)
            output = result.stdout + result.stderr
            write_ascii(out / ("runtime-" + mode + ".txt"), output)
            summary = re.search(r"efmittl_gpu: (\d+) checks passed; validation errors=0 warnings=0", output)
            cases = re.findall(r"^EFMITTL ([\w-]+): .* passed$", output, re.MULTILINE)
            cache = re.search(r"cache translations per fixture: min=(\d+) max=(\d+)", output)
            report["runs"].append({"mode": mode, "returncode": result.returncode,
                                   "checks": int(summary[1]) if summary else 0, "cases": cases,
                                   "cache_translations": [int(cache[1]), int(cache[2])] if cache else None,
                                   "passed": result.returncode == 0 and bool(summary) and bool(cases)})
            print(mode + ":\n" + output[-6000:])
        report["inputs_unchanged"] = all(hashlib.sha256(Path(p).read_bytes()).hexdigest() == digest
                                         for p, digest in report["inputs"].items())
        report["passed"] = all(run["passed"] for run in report["runs"]) and report["inputs_unchanged"]
    except Exception as error:
        report["error"] = str(error)
        print(str(error).encode("ascii", "backslashreplace").decode())
    write_ascii(out / "report.json", json.dumps(report, indent=2, ensure_ascii=True) + "\n")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
