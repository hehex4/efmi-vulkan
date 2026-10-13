"""Capture frames from the running game and keep those with character draws on unprofiled fragment shaders.

Usage: flicker_capture.py PID TRIES OUT_DIR

Read-only on the game: status and capture requests of the probe. For every matched draw of a
component the mod draws, the fragment shader of its pipeline is compared with the fragment
shaders the package has material profiles for. Saves the frames that contain such a draw.
"""
import collections
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))
from efmi_vk.probe_client import ProbeClient  # noqa: E402

PID = int(sys.argv[1])
TRIES = int(sys.argv[2]) if len(sys.argv) > 2 else 40
OUT = Path(sys.argv[3])
OUT.mkdir(parents=True, exist_ok=True)
NONE = 0xFFFFFFFF

manifest = json.loads((ROOT / "build/character/arcane-materials.json").read_text(encoding="utf-8"))
profiles = {p["fragment"] for p in manifest["materials"]["profiles"]}
sections = manifest["sections"]
components = manifest["components"]
by_count = collections.defaultdict(list)
for s in sections:
    by_count[s["match_count"]].append(s)

pipelines = {}
found = collections.Counter()
seen_frames = 0
with ProbeClient(PID) as client:
    for attempt in range(TRIES):
        frame = client.capture(wait_draws=True)
        draws = frame.get("draws") or []
        if not draws:
            continue
        seen_frames += 1
        hits = []
        lod = 0
        for d in draws:
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
            component = components[s["component"]]
            if not component["draws"][lod]:
                continue
            pid = d["pipeline"]
            if pid not in pipelines:
                pipelines[pid] = client.pipeline(pid)
            p = pipelines[pid]
            fragment = [x for x in p["shaders"] if x["stage"] == 16]
            name = fragment[0]["hash"] if fragment else None
            sets = len(p.get("layout_sets") or [])
            if name is not None and name not in profiles:
                hits.append((d["n"], s["name"].replace("TextureOverride_EntryPoint_", ""), pid, name, sets, d["rp"], d["instances"]))
        if hits:
            for h in hits:
                found[(h[1], h[2], h[3], h[4])] += 1
            path = OUT / f"frame-{frame.get('frame')}.json"
            path.write_text(json.dumps({"frame": frame, "hits": hits}), encoding="utf-8")
            print("frame", frame.get("frame"), "draws", len(draws), "| suspect character draws:", hits)
        time.sleep(0.05)

print("frames with draws:", seen_frames, "| by (section, pipeline, fragment, layout sets):")
for key, count in found.most_common():
    print("  ", count, key)
for (section, pid, name, sets), count in found.most_common():
    p = pipelines[pid]
    recipe = p.get("recipe") or {}
    rp = recipe.get("render_pass") or {}
    sub = (rp.get("subpasses") or [{}])[recipe.get("subpass", 0)]
    print("pipeline", pid, "| layout sets", p.get("layout_sets"), "| recipe complete", recipe.get("complete"),
          "| preflight", (p.get("variant_preflight") or {}).get("supported"), (p.get("variant_preflight") or {}).get("reason"))
    print("   attachments", [(a.get("format"), a.get("samples")) for a in rp.get("attachments", [])],
          "| colors", len([c for c in sub.get("colors", []) if c.get("attachment") != NONE]),
          "| depth-stencil", json.dumps(recipe.get("depth_stencil"))[:260])
    print("   blend", json.dumps((recipe.get("blend") or {}).get("attachments"))[:300])
    print("   shaders", [(x["stage"], x["hash"], x["size"]) for x in p["shaders"]],
          "| vertex bindings", [(b["binding"], b["stride"]) for b in p.get("bindings", [])])
    (OUT / f"pipeline-{pid}.json").write_text(json.dumps(p), encoding="utf-8")
