"""Build/run the headless asset uploader with child-only Vulkan validation."""
from __future__ import annotations
import contextlib
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from build import COMMON, BuildError, find_vcvars, find_vulkan_include, run_cl

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"sandbox"))
from harness import inject_env


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sample",action="store_true",help="Also upload/read back the ten verified sample DDS files")
    options=parser.parse_args()
    out=ROOT/"build/assets-gpu";out.mkdir(parents=True,exist_ok=True)
    report={"scope":"standalone GPU image upload, no layer/game integration", "passed":False}
    try:
        exe=out/"asset_gpu.exe"
        args=COMMON+["/DNOMINMAX","/I"+find_vulkan_include(),"/I"+str(ROOT/"frontend/assets"),
                     "/Fo"+str(out)+"/", "/Fd"+str(out/"test.pdb"),
                     str(ROOT/"sandbox/asset_gpu.cpp"),str(ROOT/"frontend/assets/dds.cpp"),
                     str(ROOT/"frontend/assets/image_upload.cpp"),
                     "/Fe:"+str(exe),"/link","/NOLOGO","user32.lib"]
        log=io.StringIO()
        try:
            with contextlib.redirect_stdout(log):run_cl(find_vcvars(),str(out),args)
        except BuildError as error:
            log.write(str(error));raise
        finally:
            (out/"build.txt").write_text(log.getvalue().encode("ascii","backslashreplace").decode("ascii"),encoding="ascii")
        report["executable_sha256"]=hashlib.sha256(exe.read_bytes()).hexdigest()
        validation=Path(os.environ.get("EFMI_VK_VALIDATION_DIR","F:/vulkan/validation-1.4.321.0/bin"))
        if not (validation/"VkLayer_khronos_validation.json").is_file():raise RuntimeError("Validation layer missing")
        env=inject_env(str(out/"unexpected-layer.log"))
        env.update(VK_LAYER_PATH=str(validation),VK_LAYER_VALIDATE_SYNC="1",VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0",
                   XDG_CACHE_HOME=str(out),TMPDIR=str(out),TEMP=str(out),TMP=str(out))
        result=subprocess.run([str(exe)],cwd=out,env=env,capture_output=True,encoding="utf-8",errors="replace",timeout=45)
        output=result.stdout+result.stderr
        (out/"runtime.txt").write_text(output.encode("ascii","backslashreplace").decode("ascii"),encoding="ascii")
        count=re.search(r"asset_gpu: (\d+) checks passed, (\d+) failed; validation errors=(\d+) warnings=(\d+)",output)
        report.update(returncode=result.returncode,checks_passed=int(count[1]) if count else 0,
                      checks_failed=int(count[2]) if count else None)
        report["passed"]=result.returncode==0 and bool(count) and all(int(count[i])==0 for i in (2,3,4))
        print(count[0] if count else output[-4000:])
        if not report["passed"]:print(output[-5000:])
        if report["passed"] and options.sample:
            source=json.loads((ROOT/"build/xxmi-host/mod-plan-report.json").read_text())
            if not source.get("passed"):raise RuntimeError("First verify the current mod callback/asset report")
            report["sample_textures"]=[]
            for filename,metadata in source["assets"].items():
                asset=Path(filename)
                if asset.suffix.lower()!=".dds":continue
                if hashlib.sha256(asset.read_bytes()).hexdigest()!=metadata["sha256"]:
                    raise RuntimeError("Sample texture changed after callback verification")
                result=subprocess.run([str(exe),"--dds",str(asset)],cwd=out,env=env,
                                      capture_output=True,encoding="utf-8",errors="replace",timeout=45)
                output=result.stdout+result.stderr
                (out/(asset.stem+".txt")).write_text(output.encode("ascii","backslashreplace").decode("ascii"),encoding="ascii")
                count=re.search(r"asset_gpu: (\d+) checks passed, (\d+) failed; validation errors=(\d+) warnings=(\d+)",output)
                ok=result.returncode==0 and bool(count) and all(int(count[i])==0 for i in (2,3,4))
                report["sample_textures"].append(dict(path=str(asset),sha256=metadata["sha256"],passed=ok))
                print(f"{asset.name}: {'passed' if ok else 'failed'}",flush=True)
                if not ok:
                    report["passed"]=False;print(output[-5000:]);break
        return 0 if report["passed"] else 1
    except (OSError,RuntimeError,subprocess.SubprocessError) as error:
        report["passed"]=False;report["error"]=str(error);print(str(error).encode("ascii","backslashreplace").decode("ascii"),file=sys.stderr);return 1
    finally:
        (out/"report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="ascii")


if __name__=="__main__":raise SystemExit(main())
