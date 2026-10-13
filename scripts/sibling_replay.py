"""Replay the sibling derivation over the saved frames in order, as the layer would see the draws.

Usage: sibling_replay.py DIR (after flicker_capture.py and flicker_shaders.py); writes DIR/derived-rules.json.

Signature of a profiled shader for a component: the images its fragment stage had bound at the
latest draw. An unprofiled shader's rules are derived from the profiled shaders of the same
component and the same kind of pass. Tokens: the image id where both lists hold that image,
otherwise the image's shape; occurrences are matched in binding order. Everything from disk.
"""
import collections
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))
from efmi_vk import spirv  # noqa: E402

OUT = Path(sys.argv[1])
NONE = 0xFFFFFFFF
manifest = json.loads((ROOT / "build/character/arcane-materials.json").read_text(encoding="utf-8"))
profiles = {p["fragment"]: p["rules"] for p in manifest["materials"]["profiles"]}
by_count = collections.defaultdict(list)
for s in manifest["sections"]:
    by_count[s["match_count"]].append(s)
components = manifest["components"]
pipes = {}
for path in OUT.glob("pipeline-*.json"):
    p = json.loads(path.read_text(encoding="utf-8"))
    pipes[p["id"]] = p
shader_images = {}


def images_of(name):
    if name not in shader_images:
        info = spirv.inspect_spirv((OUT / f"fs-{name}.spv").read_bytes())
        shader_images[name] = sorted({(b["set"], b["binding"]) for b in info["bindings"] if b["storage_class"] == 0})
    return shader_images[name]


def derive(p_bindings, rules, u_bindings):
    """bindings: list of ((set, binding), (id, shape) or None) in binding order."""
    p_ids = {im[0] for sb, im in p_bindings if im}
    u_ids = {im[0] for sb, im in u_bindings if im}
    common = p_ids & u_ids

    def tokens(bindings):
        out = collections.defaultdict(list)
        for sb, im in bindings:
            if im:
                out[("id", im[0]) if im[0] in common else ("shape", im[1])].append(sb)
        return out

    pt, ut = tokens(p_bindings), tokens(u_bindings)
    mapping, missing = {}, 0
    for token, places in pt.items():
        other = ut.get(token, [])
        for a, b in zip(places, other):
            mapping[a] = b
        missing += max(0, len(places) - len(other))
    extras = sum(len(v) for v in ut.values()) - len(mapping)
    out = []
    for slot, kind, s, b in rules:
        if kind == 1:
            out.append((slot, 1, 0, 0))
        elif (s, b) in mapping:
            out.append((slot, kind, *mapping[(s, b)]))
        else:
            return None
    return missing, extras, tuple(out)


signatures = {}  # (component, pass kind, fs) -> bindings
results = collections.defaultdict(collections.Counter)
draws_total = collections.Counter()
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
        p = pipes[d["pipeline"]]
        fragment = [x for x in p["shaders"] if x["stage"] == 16]
        if not fragment:
            continue
        fs = fragment[0]["hash"]
        recipe = p.get("recipe") or {}
        rpass = recipe.get("render_pass") or {}
        ds = recipe.get("depth_stencil") or {}
        kind = "/".join(str(a.get("format")) for a in rpass.get("attachments", [])) + f" w{int(bool(ds.get('write')))}c{ds.get('compare')}"
        name = s["name"].replace("TextureOverride_EntryPoint_", "")
        bound = {}
        for index, st in enumerate(d.get("sets") or []):
            for b in ((st or {}).get("contents") or {}).get("bindings") or []:
                e = (b.get("elements") or [{}])[0]
                if "image" in e:
                    bound[(index, b["binding"])] = (e["image"], (tuple(e.get("extent") or ()), e.get("image_format"), e.get("view_type")))
        bindings = [(sb, bound.get(sb)) for sb in images_of(fs)]
        if fs in profiles:
            signatures[(name, kind, fs)] = bindings
            continue
        key = (name, kind, fs, len(p.get("layout_sets") or []))
        draws_total[key] += 1
        found = []
        for (n2, k2, f2), sig in signatures.items():
            if n2 != name or k2 != kind:
                continue
            got = derive(sig, profiles[f2], bindings)
            if got and got[0] == 0:
                found.append((got[1], f2, got[2]))
        if not found:
            results[key]["no sibling"] += 1
            continue
        found.sort()
        best = [f for f in found if f[0] == found[0][0]]
        if len({f[2] for f in best}) > 1:
            results[key][f"ambiguous between {[f[1] for f in best]}"] += 1
        else:
            results[key][f"sibling {best[0][1]} extras {best[0][0]} rules {[list(r) for r in best[0][2]]}"] += 1

export = []
for key in sorted(results):
    outcomes = results[key]
    if len(outcomes) == 1 and next(iter(outcomes)).startswith("sibling"):
        text = next(iter(outcomes))
        export.append(dict(component=key[0], kind=key[1], fragment=key[2], sibling=text.split()[1],
                           rules=json.loads(text[text.index("rules ") + 6:]), images=[list(sb) for sb in images_of(key[2])]))
(OUT / "derived-rules.json").write_text(json.dumps(export, indent=1), encoding="utf-8")
solved = 0
for key in sorted(results):
    name, kind, fs, sets = key
    outcomes = results[key]
    good = len(outcomes) == 1 and next(iter(outcomes)).startswith("sibling")
    solved += good
    print(f"{name:16s} [{kind}] fs {fs} sets {sets} draws {draws_total[key]}: " + " || ".join(f"{k} x{v}" for k, v in outcomes.most_common()))
print("unprofiled combinations:", len(results), "| one stable derivation:", solved)
