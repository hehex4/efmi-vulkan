"""Indirect argument snapshots and geometry associations from completed GPU copies."""
from pathlib import Path
import json
import struct
import sys
import time

from harness import Checks, Sandbox, test_dir
from runtime_fixture import CORE
from native_compute_test import compile_shader
from efmi_vk.hashes import crc32c


def scene(checks,case):
    root=Path(test_dir("gpu-indirect-"+case+"-"));mods=root/"Mods";mods.mkdir()
    (root/"d3dx.ini").write_text("[Include]\ninclude = Core\\EFMI\\main.ini\ninclude_recursive = Mods\n"
        "[Rendering]\nrecursive_include = 1\n[Hunting]\nhunting = 1\nanalyse_frame = no_modifiers VK_F9\n",encoding="ascii")
    settings=root/"settings";settings.mkdir();keys=root/"keys.txt";keys.write_text("1\n",encoding="ascii")
    env=dict(EFMI_VK_M2_MODS=str(mods),EFMI_VK_M2_CORE=str(CORE),EFMI_VK_M2_SETTINGS=str(settings),
        EFMI_VK_M2_ENABLED="1",EFMI_VK_TEST_KEYS=str(keys),VK_LAYER_VALIDATE_SYNC="1")
    compute=case.startswith("compute");missing=case.endswith("usage");count=case in ("count","zero","nonindexed_count")
    indexed=not case.startswith("nonindexed")
    if compute:
        source=root/"scene.hlsl";source.write_text("[[vk::binding(0,0)]] RWStructuredBuffer<uint> output;\n"
            "[numthreads(1,1,1)] void main() { output[0] += 19; }\n",encoding="ascii")
        shader=compile_shader(source,root/"compiled","12345678","cs")
        args=["--compute-shader",str(shader),"--compute-indirect","--compute-repeat","2"]
        if not missing:args.append("--compute-capture")
        if case=="compute_secondary":args.append("--compute-secondary")
    else:
        args=[("--indexed-indirect" if indexed else "--non-indexed-indirect")+("-count" if count else "")]
        if not missing:args.append("--geometry-capture")
        if case=="zero":args.append("--indirect-zero-count")
    with Sandbox(seconds=5,threads=2 if case=="secondary" else 0,validation=True,env_extra=env,extra_args=args) as sb:
        time.sleep(.5);keys.write_text("1 120\n",encoding="ascii");time.sleep(.2);keys.write_text("1\n",encoding="ascii")
        sb.proc.wait(timeout=30);checks.equal(sb.proc.returncode,0,case+": normal exit")
        (root/"layer.log").write_bytes(Path(sb.log_path).read_bytes())
    records=[(path.parent,json.loads(line)) for path in settings.glob("FrameAnalysis-*/resources.jsonl") for line in path.read_text().splitlines()]
    complete=[(folder,item) for folder,item in records if item.get("state")=="complete"]
    arguments=[(folder,item) for folder,item in complete if item.get("kind")=="indirect"]
    counts=[(folder,item) for folder,item in complete if item.get("kind")=="indirect-count"]
    if missing:
        checks.check(not arguments,case+": missing transfer use produces no fabricated arguments")
        checks.check(any("TRANSFER_SRC" in item.get("reason","") for _,item in records),case+": missing transfer usage reported")
    elif compute:
        checks.equal(len(arguments),2,case+": separate post-dispatch argument copies")
        for folder,item in arguments:
            checks.equal(struct.unpack_from("<3I",(folder/item["file"]).read_bytes()),(1,1,1),case+": actual dispatch group arguments")
            checks.equal(item.get("point"),"post-dispatch",case+": snapshot point explicit")
            checks.equal(item["uses"][0]["operation"],"dispatch_indirect",case+": indirect dispatch metadata")
    else:
        checks.equal(len(arguments),1,case+": shared draw argument allocation copied once")
        checks.equal(len(counts),1 if count else 0,case+": count allocation captured only when used")
        if counts:
            folder,item=counts[0];checks.equal(struct.unpack_from("<I",(folder/item["file"]).read_bytes(),100)[0],0 if case=="zero" else 1,case+": actual GPU count value")
        if arguments:
            folder,item=arguments[0];raw=(folder/item["file"]).read_bytes();uses=item.get("uses",[])
            checks.equal(len(uses),5,case+": argument uses preserve each indirect call")
            checks.equal(sorted(use["argument_offset"] for use in uses),[0,20,40,60,80],case+": argument offsets retained")
            checks.check(all(use["argument_stride"]==20 and use["max_draws"]==1 and use["operation"]==("indexed_indirect" if indexed else "indirect") for use in uses),case+": stride and maximum draw count retained")
            checks.equal(len({use["command_buffer"] for use in uses}),2 if case=="secondary" else 1,case+": recording identities retained")
            vertices=[(p,i) for p,i in complete if i.get("kind")=="vertex"]
            checks.equal(len(vertices),1,case+": indirect vertex allocation captured")
            if vertices:
                vertex_uses=vertices[0][1].get("uses",[])
                checks.equal(len(vertex_uses),5,case+": every indirect vertex binding retained")
                checks.check(all(use["argument_resource"]==item["resource"] for use in vertex_uses),case+": vertex uses refer to the actual argument snapshot")
            indices=[(p,i) for p,i in complete if i.get("kind")=="index"]
            hashes=[]
            for path,entry in indices:
                data=(path/entry["file"]).read_bytes()
                for use in entry.get("uses",[]):
                    args=struct.unpack_from("<IIIiI",raw,use["argument_offset"])
                    start=use["bind_offset"]+args[2]*use["index_size"]
                    hashes.append("%08x"%crc32c(data[start:start+args[0]*use["index_size"]]))
                    checks.check("count" not in use and "first" not in use,case+": no fabricated recording-time draw arguments")
            checks.equal(sorted(hashes),sorted(draw["hash"] for draw in sb.expected["draws"]) if indexed else [],case+": GPU arguments associate with exported index regions")
    checks.check("Validation Error" not in sb.output() and "VUID-" not in sb.output(),case+": core and synchronization validation clean")
    print("artifacts:",root)


def main():
    checks=Checks("GPU indirect analysis");selected=set(sys.argv[1:])
    for case in ("indexed","count","zero","secondary","nonindexed","nonindexed_count","usage","compute","compute_secondary","compute_usage"):
        if not selected or case in selected:scene(checks,case)
    return checks.finish()


if __name__=="__main__":raise SystemExit(main())
