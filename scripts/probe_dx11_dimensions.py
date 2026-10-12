"""Observe invalid DX11 shader/view dimensions on hardware and WARP, headlessly.

Passing controls do not make mismatched-dimension shader inputs legal. This is
an offline compatibility investigation, not a game/mod rendering test.
"""
from __future__ import annotations
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from build import COMMON,BuildError,find_vcvars,run_cl

ROOT=Path(__file__).resolve().parents[1]


def main() -> int:
    out=ROOT/"build/dx11-dimensions";out.mkdir(parents=True,exist_ok=True)
    report={"scope":"standalone DX11 dimension-mismatch observation; invalid bindings are not compatibility guarantees","passed":False}
    try:
        source=ROOT/"sandbox/dx11_image_dimensions.cpp";exe=out/"dx11_image_dimensions.exe"
        args=COMMON+["/DNOMINMAX","/Fo"+str(out)+"/","/Fd"+str(out/"test.pdb"),str(source),
                     "/Fe:"+str(exe),"/link","/NOLOGO","d3d11.lib","d3dcompiler.lib","dxgi.lib","user32.lib"]
        log=io.StringIO()
        try:
            with contextlib.redirect_stdout(log):run_cl(find_vcvars(),str(out),args)
        except BuildError as error:log.write(str(error));raise
        finally:(out/"build.txt").write_text(log.getvalue().encode("ascii","backslashreplace").decode("ascii"),encoding="ascii")
        report.update(source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),runs=[])
        for warp in (False,True):
            for debug in (False,True):
                name=f"{'warp' if warp else 'hardware'}-{'debug' if debug else 'normal'}"
                result=subprocess.run([str(exe)]+(["--warp"] if warp else [])+(["--debug"] if debug else []),cwd=out,
                                      env=dict(os.environ,TEMP=str(out),TMP=str(out)),capture_output=True,text=True,errors="replace",timeout=30)
                text=result.stdout+result.stderr;(out/(name+".txt")).write_text(text.encode("ascii","backslashreplace").decode("ascii"),encoding="ascii")
                unavailable=result.returncode==3 and "debug_runtime_unavailable" in text
                pixels={m[0]:list(map(int,m[1:])) for m in re.findall(r"PIXEL (\S+) (\d+) (\d+) (\d+) (\d+)",text)}
                retained={m[0]:bool(int(m[1])) for m in re.findall(r"BOUND (\S+) (\d+)",text)}
                adapter=re.search(r"DEVICE warp=\d+ debug=\d+ vendor=(\d+) device=(\d+) feature=(\d+)",text)
                row=dict(device=name,returncode=result.returncode,debug_unavailable=unavailable,pixels=pixels,
                         bound_view_retained=retained,adapter=list(map(int,adapter.groups())) if adapter else None,
                         controls_passed=result.returncode==0 and "controls_passed=1" in text)
                report["runs"].append(row);print(json.dumps(row),flush=True)
                if not unavailable and not row["controls_passed"]:raise ValueError("DX11 controls failed; see "+name+".txt")
        report["passed"]=True;return 0
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:
        report["error"]=str(error);print(str(error).encode("ascii","backslashreplace").decode("ascii"),file=sys.stderr);return 1
    finally:(out/"report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="ascii")


if __name__=="__main__":raise SystemExit(main())
