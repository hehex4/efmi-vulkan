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
    parser.add_argument("--buffers",action="store_true",help="Also upload/read back all selected sample mesh BUF files")
    parser.add_argument("--resources",action="store_true",help="Also prepare the sample's complete bound resource set in one cache")
    options=parser.parse_args()
    out=ROOT/"build/assets-gpu";out.mkdir(parents=True,exist_ok=True)
    report={"scope":"standalone GPU resource upload/cache/materials, no game integration", "passed":False}
    try:
        exe=out/"asset_gpu.exe"
        args=COMMON+["/DNOMINMAX","/I"+find_vulkan_include(),"/I"+str(ROOT/"frontend/assets"),
                     "/Fo"+str(out)+"/", "/Fd"+str(out/"test.pdb"),
                     str(ROOT/"sandbox/asset_gpu.cpp"),str(ROOT/"frontend/assets/dds.cpp"),
                     str(ROOT/"frontend/assets/image_upload.cpp"),str(ROOT/"frontend/assets/resource_cache.cpp"),
                     str(ROOT/"frontend/assets/buffer_upload.cpp"),
                     str(ROOT/"frontend/assets/file_plan.cpp"),
                     str(ROOT/"frontend/assets/file_draws.cpp"),
                     str(ROOT/"frontend/gpu/fragment_images.cpp"),
                     str(ROOT/"frontend/gpu/material_plan.cpp"),
                     str(ROOT/"frontend/gpu/draw_transaction.cpp"),
                     *[str(ROOT/"layer/src"/name) for name in ("pipeline_recipe.cpp","pipeline_variant.cpp",
                         "spirv_repoint.cpp","variant_cache.cpp","draw_snapshots.cpp")],
                     "/Fe:"+str(exe),"/link","/NOLOGO","user32.lib"]
        log=io.StringIO()
        try:
            with contextlib.redirect_stdout(log):run_cl(find_vcvars(),str(out),args)
        except BuildError as error:
            log.write(str(error));raise
        finally:
            (out/"build.txt").write_text(log.getvalue().encode("ascii","backslashreplace").decode("ascii"),encoding="ascii")
        report["executable_sha256"]=hashlib.sha256(exe.read_bytes()).hexdigest()
        inputs=list((ROOT/"frontend/assets").glob("*.h"))+list((ROOT/"frontend/assets").glob("*.cpp"))
        inputs += [ROOT/"sandbox/asset_gpu.cpp",ROOT/"sandbox/asset_cache_test.cpp",ROOT/"sandbox/asset_buffer_test.cpp",ROOT/"sandbox/xxmi_host/dds_test.cpp",Path(__file__)]
        inputs += [ROOT/"sandbox/asset_file_plan_test.cpp"]
        inputs += [ROOT/"frontend/core/palette_bank.h",ROOT/"layer/src/merged_skeleton.h"]
        inputs += [ROOT/"frontend/gpu/fragment_images.h",ROOT/"frontend/gpu/fragment_images.cpp",
                   ROOT/"sandbox/material_fixture.h",ROOT/"sandbox/asset_material_test.cpp",ROOT/"sandbox/src/spirv.h"]
        inputs += [ROOT/"frontend/gpu/material_plan.h",ROOT/"frontend/gpu/material_plan.cpp",
                   ROOT/"sandbox/asset_material_plan_test.cpp",ROOT/"layer/src/draw_snapshots.h",
                   ROOT/"layer/src/pipeline_variant.h",ROOT/"layer/src/pipeline_recipe.h",
                   ROOT/"layer/src/spirv_repoint.h"]
        inputs += [ROOT/"frontend/gpu/draw_transaction.h",ROOT/"frontend/gpu/draw_transaction.cpp",
                   ROOT/"sandbox/asset_transaction_test.cpp",ROOT/"sandbox/variant_fixture.h"]
        inputs += [ROOT/"layer/src"/name for name in ("pipeline_recipe.cpp","pipeline_variant.cpp","spirv_repoint.cpp",
                   "variant_cache.h","variant_cache.cpp","draw_snapshots.cpp","common.h","state.h","json.h")]
        report["source_sha256"]={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
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
            if report["passed"]:
                textures=[x["path"] for x in report["sample_textures"]]
                if not textures:raise RuntimeError("No sample textures in the callback report")
                result=subprocess.run([str(exe),"--cache-dds",*textures],cwd=out,env=env,
                                      capture_output=True,encoding="utf-8",errors="replace",timeout=90)
                output=result.stdout+result.stderr
                (out/"sample-cache.txt").write_text(output.encode("ascii","backslashreplace").decode("ascii"),encoding="ascii")
                count=re.search(r"asset_gpu: (\d+) checks passed, (\d+) failed; validation errors=(\d+) warnings=(\d+)",output)
                report["sample_cache_passed"]=result.returncode==0 and bool(count) and all(int(count[i])==0 for i in (2,3,4))
                report["passed"]=report["sample_cache_passed"]
                print(output[-5000:],flush=True)
        if report["passed"] and options.buffers:
            source=json.loads((ROOT/"build/xxmi-host/mod-plan-report.json").read_text())
            if not source.get("passed"):raise RuntimeError("First verify the current mod callback/asset report")
            report["sample_buffers"]=[]
            for filename,metadata in source["assets"].items():
                asset=Path(filename)
                if asset.suffix.lower()!=".buf":continue
                if hashlib.sha256(asset.read_bytes()).hexdigest()!=metadata["sha256"]:
                    raise RuntimeError("Sample buffer changed after callback verification")
                result=subprocess.run([str(exe),"--buffer",str(asset)],cwd=out,env=env,
                                      capture_output=True,encoding="utf-8",errors="replace",timeout=45)
                output=result.stdout+result.stderr
                (out/(asset.stem+"-buffer.txt")).write_text(output.encode("ascii","backslashreplace").decode("ascii"),encoding="ascii")
                count=re.search(r"asset_gpu: (\d+) checks passed, (\d+) failed; validation errors=(\d+) warnings=(\d+)",output)
                ok=result.returncode==0 and bool(count) and all(int(count[i])==0 for i in (2,3,4))
                report["sample_buffers"].append(dict(path=str(asset),sha256=metadata["sha256"],passed=ok))
                print(f"{asset.name}: {'passed' if ok else 'failed'}",flush=True)
                if not ok:report["passed"]=False;print(output[-5000:]);break
            if not report["sample_buffers"]:raise RuntimeError("No mesh buffers in the callback report")
        if report["passed"] and options.resources:
            source=json.loads((ROOT/"build/xxmi-host/mod-plan-report.json").read_text())
            if not source.get("passed"):raise RuntimeError("First verify the current callback/asset report")
            for filename,metadata in source["assets"].items():
                if hashlib.sha256(Path(filename).read_bytes()).hexdigest()!=metadata["sha256"]:
                    raise RuntimeError("Sample resource changed after callback verification")
            result=subprocess.run([str(exe),"--cache-resources",*source["assets"]],cwd=out,env=env,
                                  capture_output=True,encoding="utf-8",errors="replace",timeout=90)
            output=result.stdout+result.stderr
            (out/"sample-resources.txt").write_text(output.encode("ascii","backslashreplace").decode("ascii"),encoding="ascii")
            count=re.search(r"asset_gpu: (\d+) checks passed, (\d+) failed; validation errors=(\d+) warnings=(\d+)",output)
            report["sample_resources_passed"]=result.returncode==0 and bool(count) and all(int(count[i])==0 for i in (2,3,4))
            report["sample_resources"]={filename:metadata["sha256"] for filename,metadata in source["assets"].items()}
            report["passed"]=report["sample_resources_passed"]
            print(output[-5000:],flush=True)
        return 0 if report["passed"] else 1
    except (OSError,RuntimeError,ValueError,KeyError,TypeError,subprocess.SubprocessError) as error:
        report["passed"]=False;report["error"]=str(error);print(str(error).encode("ascii","backslashreplace").decode("ascii"),file=sys.stderr);return 1
    finally:
        (out/"report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="ascii")


if __name__=="__main__":raise SystemExit(main())
