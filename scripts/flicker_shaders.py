"""Compare the images a component's draws bind under profiled and unprofiled fragment shaders.

Usage: flicker_shaders.py PID DIR (the directory flicker_capture.py wrote; fetches pipelines and fragment modules into it)

Reads the saved frames; fetches pipelines and fragment shaders it does not have from the running
game (read-only probe requests). For every matched draw of a component the mod draws: the fragment
shader's image variables and the image bound at each, from the descriptor snapshot of the draw.
"""
import collections
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))
from efmi_vk.probe_client import ProbeClient  # noqa: E402
from efmi_vk import spirv  # noqa: E402

PID = int(sys.argv[1])
OUT = Path(sys.argv[2])
NONE = 0xFFFFFFFF
manifest = json.loads((ROOT / "build/character/arcane-materials.json").read_text(encoding="utf-8"))
profiles = {p["fragment"]: p["rules"] for p in manifest["materials"]["profiles"]}
by_count = collections.defaultdict(list)
for s in manifest["sections"]:
    by_count[s["match_count"]].append(s)
components = manifest["components"]

pipes, shaders = {}, {}
for path in OUT.glob("pipeline-*.json"):
    p = json.loads(path.read_text(encoding="utf-8"))
    pipes[p["id"]] = p


def images_of(code):
    info = spirv.inspect_spirv(code)
    return sorted({(b["set"], b["binding"]) for b in info["bindings"] if b["storage_class"] == 0})


rows = collections.OrderedDict()  # (section, rp kind, fs hash) -> info
with ProbeClient(PID) as client:
    for path in sorted(OUT.glob("frame-*.json"), key=lambda p: int(p.stem.split("-")[1])):
        frame = json.loads(path.read_text(encoding="utf-8"))["frame"]
        lod = 0
        for d in frame.get("draws") or []:
            ib = d.get("ib") or {}
            if not ib.get("hash"):
                continue
            match = [s for s in by_count.get(d["count"], [])
                     if int(ib["hash"], 16) == s["hash"] and s["match_first"] in (NONE, d["first"])]
            if not match:
                continue
            s = match[0]
            if s["lod"] != NONE:
                lod = s["lod"]
            if not components[s["component"]]["draws"][lod]:
                continue
            pid = d["pipeline"]
            if pid not in pipes:
                pipes[pid] = client.pipeline(pid)
                (OUT / f"pipeline-{pid}.json").write_text(json.dumps(pipes[pid]), encoding="utf-8")
            p = pipes[pid]
            fragment = [x for x in p["shaders"] if x["stage"] == 16]
            if not fragment:
                continue
            fs = fragment[0]
            if fs["hash"] not in shaders:
                file = OUT / f"fs-{fs['hash']}.spv"
                if not file.exists():
                    got = client.shader(fs["id"])
                    code = got[1] if isinstance(got, tuple) else got
                    file.write_bytes(code)
                shaders[fs["hash"]] = images_of(file.read_bytes())
            recipe = p.get("recipe") or {}
            rpass = recipe.get("render_pass") or {}
            kind = "/".join(str(a.get("format")) for a in rpass.get("attachments", []))
            ds = recipe.get("depth_stencil") or {}
            kind += f" w{int(bool(ds.get('write')))}c{ds.get('compare')}"
            name = s["name"].replace("TextureOverride_EntryPoint_", "")
            bound = {}
            for index, st in enumerate(d.get("sets") or []):
                contents = (st or {}).get("contents") or {}
                for b in contents.get("bindings") or []:
                    e = (b.get("elements") or [{}])[0]
                    if "image" in e:
                        bound[(index, b["binding"])] = (e.get("image"), tuple(e.get("extent") or ()), e.get("image_format"), e.get("view_type"))
            key = (name, kind, fs["hash"])
            row = rows.setdefault(key, dict(count=0, sets=len(p.get("layout_sets") or []), variants=collections.Counter()))
            row["count"] += 1
            row["variants"][tuple((sb, bound.get(sb)) for sb in shaders[fs["hash"]])] += 1

by_component = collections.defaultdict(list)
for (name, kind, fs), row in rows.items():
    by_component[name].append((kind, fs, row))
result = {}
for name in sorted(by_component):
    print("=" * 100)
    print(name)
    for kind, fs, row in sorted(by_component[name], key=lambda t: (t[0], t[1] not in profiles, t[1])):
        rules = profiles.get(fs)
        print(f"  pass [{kind}] fs {fs} sets {row['sets']} x{row['count']}  " + (f"PROFILE {rules}" if rules is not None else "no profile"))
        for variant, count in row["variants"].most_common(2):
            text = "; ".join(f"({sb[0]},{sb[1]})=" + (f"img{im[0]} {im[1][0]}x{im[1][1]}" + (f"x{im[1][2]}" if im[1][2] > 1 else "") + f" f{im[2]}" if im else "-")
                             for sb, im in variant)
            print(f"      x{count}: {text}")
        result.setdefault(name, []).append(dict(kind=kind, fs=fs, sets=row["sets"], count=row["count"], rules=rules,
                                                bindings=[[list(sb), list(im) if im else None] for sb, im in row["variants"].most_common(1)[0][0]]))
(OUT / "variant-images.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
