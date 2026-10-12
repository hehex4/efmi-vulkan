"""Verify the actual candidate/deployed M2 settings in a sandbox, never in the game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time

ROOT=Path(__file__).resolve().parents[1]


def source_version():
    """The version the source states; the binaries under test have to be built from it."""
    for line in (ROOT/"layer/src/common.h").read_text(encoding="utf-8").splitlines():
        if line.startswith("#define EFVK_VERSION"):
            return line.split('"')[1]
    raise ValueError("EFVK_VERSION not found")


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dist",type=Path,required=True)
    ap.add_argument("--out",type=Path,required=True)
    args=ap.parse_args()
    target=args.dist.resolve();out=args.out.resolve()
    if not target.is_relative_to(ROOT) or not out.is_relative_to(ROOT):
        raise ValueError("candidate and report must stay in the project")
    os.environ["EFMI_VK_DIST"]=str(target)
    sys.path.insert(0,str(ROOT/"sandbox"))
    import harness
    from efmi_vk.probe_client import ProbeClient
    log=ROOT/"dist/efmi_vk_layer.log"
    before=hashlib.sha256(log.read_bytes()).hexdigest()
    normal_env=harness.inject_env

    def actual_env(log_path=None,mode=None,extra=None):
        env=normal_env(log_path,mode=None,extra=None)
        for name in list(env):
            if name.startswith("EFMI_VK_") and name!="EFMI_VK_LOG":
                del env[name]
        return env

    harness.inject_env=actual_env
    with harness.Sandbox(entry="inject",mode=None,seconds=5,validation=True) as box:
        with ProbeClient(box.pid) as client:
            deadline=time.monotonic()+3
            status=client.status()
            while not status["replacement"]["ready"] and time.monotonic()<deadline:
                time.sleep(.05);status=client.status()
            assert status["version"]==source_version() and status["entry"]=="inject" and status["mode"]=="probe"
            assert status["resources"] and not status["counts"]["image_content"]["enabled"]
            assert not status["tracker_work"]["enabled"] and status["unmap_snapshot_all"]
            replacement=status["replacement"]
            assert all(replacement[key] for key in ("configured","enabled","ready")),replacement
            assert replacement["matched"]==replacement["replaced"]==0
            first=client.capture(wait_draws=True,snapshot={"all":True})
            assert first["snapshot"]["draws"]==5
            assert [d["ib"]["hash"] for d in first["draws"]]==[d["hash"] for d in box.expected["draws"]]
            pipe=client.pipeline(first["draws"][0]["pipeline"])
            assert pipe["recipe"]["complete"] and pipe["layout_recipe"]["complete"]
            box.rewrite_index_buffer()
            second=client.capture(wait_draws=True)
            assert [d["ib"]["hash"] for d in second["draws"]]==[d["hash_after_rewrite"] for d in box.expected["draws"]]
            report={"target":str(target),"sandbox":box.dir,"status":status,
                    "hashes_before":[d["ib"]["hash"] for d in first["draws"]],
                    "hashes_after":[d["ib"]["hash"] for d in second["draws"]]}
        assert box.proc.wait(timeout=12)==0,box.output()
        assert "validation: errors=0 warnings=0" in box.output(),box.output()
    after=hashlib.sha256(log.read_bytes()).hexdigest()
    assert before==after,"game log changed during sandbox smoke; inspect its source separately"
    report["game_log"]={"path":str(log),"before_sha256":before,"after_sha256":after}
    report["validation"]="core/sync clean, normal sandbox exit"
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("Actual M2 config smoke passed: "+str(out))


if __name__=="__main__":
    main()
