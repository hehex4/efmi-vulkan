"""Completed owned attachment exports do not depend on shader reflection."""
from pathlib import Path
import json
import sys
import time

from harness import Checks, Sandbox, test_dir


def scene(checks, case):
    root=Path(test_dir("gpu-target-"+case+"-"));mods=root/"Mods";mods.mkdir()
    (root/"d3dx.ini").write_text("[Include]\ninclude_recursive = Mods\n"
        "[Rendering]\nrecursive_include = 1\n[Hunting]\nhunting = 1\nanalyse_frame = no_modifiers VK_F9\nanalyse_options = dump_rt dump_tex dump_cb dump_vb dump_ib buf\n",encoding="ascii")
    settings=root/"settings";settings.mkdir();keys=root/"keys.txt";keys.write_text("1\n",encoding="ascii")
    env=dict(EFMI_VK_CONFIGURATION=str(root),EFMI_VK_M2_SETTINGS=str(settings),
             EFMI_VK_M2_ENABLED="1",EFMI_VK_TEST_KEYS=str(keys),VK_LAYER_VALIDATE_SYNC="1")
    args=["--capture-target"]
    layered=case=="layers" or case.startswith("multiview")
    if layered:args += ["--capture-target-multiview" if case.startswith("multiview") else "--capture-target-layers"]
    if case in ("after_graphics","after_secondary","multiview_after"):args.append("--capture-target-after")
    if case=="unused":args.append("--capture-target-unused")
    if case=="usage":args.append("--capture-target-no-transfer")
    if case in ("discard","resolve_discard"):args.append("--capture-target-discard")
    if case.startswith("resolve"):args.append("--capture-target-resolve")
    with Sandbox(seconds=5,threads=2 if case=="after_secondary" else 0,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(.5);keys.write_text("1 120\n",encoding="ascii");time.sleep(.2);keys.write_text("1\n",encoding="ascii")
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+": normal exit")
        (root/"layer.log").write_bytes(Path(sb.log_path).read_bytes())
        (root/"stdout.txt").write_text(sb.output(),encoding="utf-8")
    records=[(path.parent,json.loads(line)) for path in settings.glob("FrameAnalysis-*/resources.jsonl") for line in path.read_text().splitlines()]
    targets=[(folder,item) for folder,item in records if item.get("state")=="complete" and item.get("set")==0xffffffff and item.get("width")==4]
    if case in ("usage","discard","unused"):
        checks.check(not targets,case+": no unavailable attachment bytes")
        if case=="usage":checks.check(any("Image was not created with TRANSFER_SRC usage" in item.get("reason","") for _,item in records),case+": missing transfer usage reported")
    else:
        checks.equal(len(targets),4 if layered else 2,case+": separate stored attachment captures")
        checks.check(bool(targets) and all(item["format"]==37 and item["height"]==4 and item["bytes"]==64 and item["point"]=="end-of-pass" for _,item in targets),case+": attachment format and extent retained")
        if layered:
            checks.equal(sorted((item['mip'],item['layer']) for _,item in targets),
                         sorted([(1,1),(1,3 if case.startswith('multiview') else 2)]*2),case+': capture only framebuffer or multiview layers')
        if case.startswith("resolve"):
            checks.check(bool(targets) and all(item['element']==1 for _,item in targets),case+": exports resolved samples independently of source storeOp")
        actual=sorted((folder/item["file"]).read_bytes() for folder,item in targets)
        expected=sorted([bytes((0,255,0,255))*16,bytes((255,0,255,255))*16]*(2 if layered else 1))
        checks.equal(actual,expected,case+": both completed GPU clear results preserved")
    checks.check(not any(marker in sb.output() for marker in ("Validation Error","VALIDATION ERROR","VUID-","SYNC-HAZARD")),case+": core and synchronization validation clean")
    print("artifacts:",root)


def main():
    checks=Checks("GPU target analysis");selected=set(sys.argv[1:])
    for case in ("clear","after_graphics","after_secondary","usage","discard","layers","multiview","multiview_after","unused"):
        if not selected or case in selected:scene(checks,case)
    return checks.finish()


if __name__=="__main__":
    raise SystemExit(main())
