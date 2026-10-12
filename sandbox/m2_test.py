"""Actual draw-hook replacement, animation, restoration and live enable/disable in headless Vulkan."""
import json
import os
from pathlib import Path
import struct
import subprocess

from harness import Checks,DIST,INJECT_DLL,inject_env,layer_env,test_dir
from efmi_vk.hashes import crc32c


def fixture_package(path):
    positions=b"".join(struct.pack("<4f",x+1,y,0,0) for x,y in ((-.22,-.2),(.22,-.2),(0,.22)))
    texcoord=bytes(3*8)
    blend=struct.pack("<8H",65535,0,0,0,266,0,0,0)*3
    indices=struct.pack("<3I",0,1,2)
    payload=positions+texcoord+blend+indices
    header=struct.pack("<8s12I",b"EFMIM201",crc32c(struct.pack("<3H",0,1,2)),3,0,499,266,12,3,3,16,8,16,crc32c(payload))
    path.write_bytes(header+payload)


def main():
    checks=Checks("M2 hook rendering")
    native=Path(test_dir("m2-mesh-"))
    proc=subprocess.run([os.path.join(DIST,"efmi_vk_mesh_test.exe")],cwd=native,
                        capture_output=True,text=True,timeout=30)
    (native/"stdout.txt").write_text(proc.stdout+proc.stderr)
    print(proc.stdout,end="")
    checks.equal(proc.returncode,0,"native package rejection and vertex-input tests")
    validation=os.environ.get("EFMI_VK_VALIDATION_DIR","F:/vulkan/validation-1.4.321.0/bin")
    for entry in ("layer","inject"):
        for rp in (1,2):
            folder=Path(test_dir("m2-"));package=folder/"mesh.bin";fixture_package(package)
            env=layer_env(str(folder/"layer.log")) if entry=="layer" else inject_env(str(folder/"layer.log"))
            env.update(VK_LAYER_PATH=os.pathsep.join([DIST,validation]),VK_LAYER_VALIDATE_SYNC="1",
                       VK_LAYER_DUPLICATE_MESSAGE_LIMIT="0",XDG_CACHE_HOME=str(folder),TMPDIR=str(folder),
                       TEMP=str(folder),TMP=str(folder),EFMI_VK_M2_PACKAGE=str(package),EFMI_VK_M2_ENABLED="1")
            args=[os.path.join(DIST,"efmi_vk_variant_gpu.exe"),"--m2"]
            if entry=="inject":args.extend(["--inject",INJECT_DLL])
            if rp==2:args.append("--renderpass2")
            proc=subprocess.run(args,cwd=folder,env=env,capture_output=True,text=True,timeout=60)
            output=proc.stdout+proc.stderr;(folder/"stdout.txt").write_text(output)
            label=f"{entry}/renderpass{rp}"
            checks.equal(proc.returncode,0,label+" actual hook and pixels")
            checks.check("validation: errors=0 warnings=0" in output,label+" core/sync validation")
            if proc.returncode:
                print(output)
                print(f"artifacts: {folder}")
            final=folder/"m2-final.json"
            checks.check(final.exists(),label+" status saved")
            if not final.exists():continue
            status=json.loads(final.read_text())["result"];r=status["replacement"]
            checks.check(r["configured"] and r["enabled"] and r["ready"],label+" explicit backend active")
            checks.check(r["replaced"]>=35 and r["sealed_batches"]>=35,label+" replacements and submission sealing counted")
            checks.equal(r["unsupported"],0,label+" supported target")
            checks.equal(r["unreadable"],1,label+" one deliberate unreadable palette falls back")
            checks.equal(r["exhausted"],0,label+" completed recording reuses storage")
            checks.check(not json.loads((folder/"m2-disabled.json").read_text())["result"]["replacement"]["enabled"],label+" disable acknowledged")
            checks.equal((folder/"pixels.rgba").stat().st_size,64*64*4,label+" pixel artifact")
            checks.check(f"entry={entry}" in (folder/"layer.log").read_text(),label+" intended layer entry")
    return checks.finish()


if __name__=="__main__":
    raise SystemExit(main())
