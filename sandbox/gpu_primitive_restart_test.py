"""GPU frame-analysis geometry, preserving bindings from primary and secondary draws."""
from pathlib import Path
import json
import struct
import sys
import time

from harness import Checks, Sandbox, test_dir
from runtime_fixture import CORE
from efmi_vk.hashes import crc32c


def scene(checks, case):
    root=Path(test_dir("gpu-restart-"+case+"-"));mods=root/"Mods";mods.mkdir()
    (root/"d3dx.ini").write_text("[Include]\ninclude = Core\\EFMI\\main.ini\ninclude_recursive = Mods\n"
        "[Rendering]\nrecursive_include = 1\n[Hunting]\nhunting = 1\nanalyse_frame = no_modifiers VK_F9\nanalyse_options = dump_rt dump_tex dump_cb dump_vb dump_ib buf\n",encoding="ascii")
    settings=root/"settings";settings.mkdir();keys=root/"keys.txt";keys.write_text("1\n",encoding="ascii")
    env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(settings),
             EFMI_VK_M2_ENABLED="1",EFMI_VK_TEST_KEYS=str(keys),VK_LAYER_VALIDATE_SYNC="1")
    dynamic=case.startswith('dynamic')
    static=case=='dynamic_static'
    args=['--geometry-dynamic','3' if static else '2' if case=='dynamic_ext' else '1','--api','1.1' if case=='dynamic_ext' else '1.3'] if dynamic else []
    args += ["--primitive-restart","3" if static else "2" if case=="dynamic_ext" else "1"]
    if case!="usage" and "--baseline" not in sys.argv:args.append("--geometry-capture")
    if case=="direct":args.append("--non-indexed")
    if case=="odd":args.append("--geometry-odd")
    with Sandbox(seconds=5,threads=2 if case in ("secondary","dynamic_secondary") else 0,validation=True,env_extra=env,extra_args=args) as sb:
        if case=="rewrite":sb.rewrite_index_buffer()
        time.sleep(.5);keys.write_text("1 120\n",encoding="ascii");time.sleep(.2);keys.write_text("1\n",encoding="ascii")
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+": normal exit")
        (root/"layer.log").write_bytes(Path(sb.log_path).read_bytes())
    records=[(path.parent,json.loads(line)) for path in settings.glob("FrameAnalysis-*/resources.jsonl") for line in path.read_text().splitlines()]
    data=[(folder,item) for folder,item in records if item.get("state")=="complete" and item.get("kind") in ("vertex","index")]
    if case=="usage":
        checks.check(not data,case+": missing transfer use produces no geometry bytes")
        checks.check(any("TRANSFER_SRC" in item.get("reason","") for _,item in records),case+": missing usage is reported")
    else:
        vertices=[(folder,item) for folder,item in data if item["kind"]=="vertex"]
        indices=[(folder,item) for folder,item in data if item["kind"]=="index"]
        checks.equal(len(vertices),1,case+": shared vertex buffer copied once per pass")
        checks.equal(len(indices),0 if case=="direct" else 2,case+": every used index allocation copied")
        uses=[(folder,item,use) for folder,item in indices for use in item.get("uses",[])]
        checks.equal(len(uses),0 if case=="direct" else 5,case+": per-draw index bindings retained")
        checks.check(all(use.get("primitive_restart")==bool(not static and use["count"]==3) for _,_,use in uses),case+": per-draw static or dynamic index restart state retained")
        hashes=[]
        for folder,item,use in uses:
            raw=(folder/item["file"]).read_bytes();start=use["bind_offset"]+use["first"]*use["index_size"]
            hashes.append("%08x"%crc32c(raw[start:start+use["count"]*use["index_size"]]))
        expected=[] if case=="direct" else sorted(d["hash_after_rewrite" if case=="rewrite" else "hash"] for d in sb.expected["draws"])
        checks.equal(sorted(hashes),expected,case+": exported GPU indices match each draw region")
        if not static:
            cuts=set()
            for folder,item,use in uses:
                if use["count"]!=3:continue
                raw=(folder/item["file"]).read_bytes();start=use["bind_offset"]+use["first"]*use["index_size"]
                if raw[start:start+use["index_size"]]==b"\xff"*use["index_size"]:cuts.add(use["index_size"])
            checks.equal(cuts,{2,4},case+": actual16/32-bit strip-cut indices are exported without reinterpretation")
        if vertices:
            folder,item=vertices[0];raw=(folder/item["file"]).read_bytes()
            checks.equal(len(raw),17*8+(case=="odd"),case+": complete vertex allocation bytes")
            if case=="odd":checks.equal(raw[-1],0x5a,case+": unaligned trailing byte preserved")
            checks.check(abs(struct.unpack_from("<f",raw)[0]+.9)<1e-6,case+": GPU vertex value")
            checks.equal(len(item["uses"]),5,case+": shared vertex uses retain all draws")
            checks.check(all(use["stride"]==(0 if dynamic and not static and use["count"]==3 else 8) and use["slot"]==0 for use in item["uses"]),case+": live stride and slot retained")
            if dynamic:
                checks.check(all(use.get("topology")== (4 if not static and use["count"]==3 else 3) for use in item["uses"]),case+": current dynamic or static topology retained")
                checks.check(all(use.get("bind_size")==136 for use in item["uses"]),case+": bound range survives null-stride and legacy rebinding")
            checks.check(all(use.get("primitive_restart")==bool(not static and use["count"]==3) for use in item["uses"]),case+": vertex uses retain the same primitive restart state")
            checks.check(all(use["attributes"]==[{"location":0,"format":103,"offset":0}] for use in item["uses"]),case+": vertex attribute format retained")
            checks.equal(len({use["command_buffer"] for use in item["uses"]}),2 if case in ("secondary","dynamic_secondary") else 1,case+": recording command identities retained")
    checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(),case+": core and synchronization validation clean")
    print("artifacts:",root)


def main():
    checks=Checks("GPU primitive restart");selected=set(sys.argv[1:])-{"--baseline"}
    for case in ("dynamic","dynamic_ext","dynamic_secondary","dynamic_static"):
        if not selected or case in selected:scene(checks,case)
    return checks.finish()


if __name__=="__main__":
    raise SystemExit(main())
