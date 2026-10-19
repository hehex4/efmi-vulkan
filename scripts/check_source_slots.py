"""Check the DX11 register rule of server/efmi_vk/source_slots.py against observed ground truth (D-076).

    check_source_slots.py                       check docs/source-slot-truth.json
    check_source_slots.py --refresh --catalogue <catalogue.json> --bundles <dir>... [--catalogue ... --bundles ...]

The truth file holds, per game shader seen in paired scenes, its resource declarations as read
from the SPIR-V (kinds, sets, bindings, block sizes; no byte code) and every DX11 slot the
catalogue placed in one Vulkan binding. ``--refresh`` rebuilds it from catalogues and the capture
bundles their shaders came from. The check applies the rule to the declarations: every t register
must come out as observed; constant buffer registers are compared by size only.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))

from efmi_vk.source_slots import declarations, registers  # noqa: E402

TRUTH = ROOT / "docs" / "source-slot-truth.json"


def refresh(pairs: list[tuple[Path, list[Path]]]) -> dict:
    shaders: dict[str, dict] = {}
    sources = []
    for catalogue_path, bundles in pairs:
        raw = catalogue_path.read_bytes()
        catalogue = json.loads(raw)
        sources.append({"catalogue": catalogue_path.name, "sha256": hashlib.sha256(raw).hexdigest(),
                        "scenes": len(catalogue.get("sources", [])), "bundles": len(bundles)})
        files = {}
        for bundle in bundles:
            listing = bundle / "shaders.json"
            if listing.exists():
                for s in json.loads(listing.read_text(encoding="utf-8")):
                    if s.get("hash") and s.get("file"):
                        files.setdefault(s["hash"], bundle / s["file"])
        for program in catalogue["programs"].values():
            for stage, vk, dx in (("ps", program["vk_fs"], program["dx_ps"]), ("vs", program["vk_vs"], program["dx_vs"])):
                if not vk or vk not in files:
                    continue
                entry = shaders.get(vk)
                if entry is None:
                    entry = shaders[vk] = {"stage": stage, "dx": set(), "declarations": declarations(files[vk].read_bytes()),
                                           "slots": {}}
                entry["dx"].update(dx)
                for kind in ("textures", "buffers", "constant_buffers"):
                    for slot, item in program[kind].items():
                        if not slot.startswith(stage + "-") or len(item["where"]) != 1:
                            continue
                        where = [item["where"][0]["set"], item["where"][0]["binding"]]
                        known = entry["slots"].setdefault(slot, {"where": where, "evidence": Counter(), "kind": kind})
                        if known["where"] != where:
                            known["conflict"] = True
                        known["evidence"].update(item["evidence"])
    for entry in shaders.values():
        entry["dx"] = sorted(entry["dx"])
        for slot in entry["slots"].values():
            slot["evidence"] = dict(slot["evidence"])
    return {"schema": 1,
            "scope": "Resource declarations of game shaders read from their SPIR-V, and the DX11 slots paired scenes placed in "
                     "their bindings. Metadata only; no shader byte code. Ground truth for the rule in server/efmi_vk/source_slots.py.",
            "sources": sources, "shaders": dict(sorted(shaders.items()))}


def check(truth: dict) -> tuple[Counter, list[str]]:
    stats, problems = Counter(), []
    for shader, entry in truth["shaders"].items():
        regs = registers(entry["declarations"])
        sizes = {(d["set"], d["binding"]): d.get("size") for d in entry["declarations"]}
        for slot, item in entry["slots"].items():
            if item.get("conflict"):
                stats["conflicting observations"] += 1
                continue
            stage, _, reg = slot.partition("-")
            if reg.startswith("t") and int(reg[1:]) >= 100:
                stats["XXMI slots (not the game's)"] += 1
                continue
            got = regs.get(tuple(item["where"]))
            if reg.startswith("t"):
                if got and got["register"] == reg:
                    stats["t registers as observed"] += 1
                else:
                    stats["t registers wrong"] += 1
                    problems.append("%s %s: observed %d.%d, rule gives %s" % (
                        shader, slot, item["where"][0], item["where"][1], got["register"] if got else "no SRV"))
            elif reg.startswith("cb"):
                # Constant buffer windows were paired by size; the rule is compared, not trusted.
                stats["constant buffers as observed" if got and got["register"] == reg else "constant buffers differ"] += 1
        for d in entry["declarations"]:
            if d["kind"] == "cbuffer" and sizes.get((d["set"], d["binding"])) is None:
                stats["constant buffers without size"] += 1
    return stats, problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--refresh", action="store_true")
    ap.add_argument("--catalogue", action="append", default=[])
    ap.add_argument("--bundles", action="append", nargs="+", default=[])
    args = ap.parse_args()
    if args.refresh:
        if not args.catalogue or len(args.catalogue) != len(args.bundles):
            ap.error("--refresh needs one --bundles list per --catalogue")
        truth = refresh([(Path(c), [Path(b) for b in bs]) for c, bs in zip(args.catalogue, args.bundles)])
        # One shader per line keeps the file small and its changes readable.
        items = list(truth["shaders"].items())
        lines = ['{"schema": %d,' % truth["schema"]]
        lines += ['"%s": %s,' % (k, json.dumps(v, separators=(",", ":"))) for k, v in truth.items() if k not in ("schema", "shaders")]
        lines.append('"shaders": {')
        lines += ['"%s": %s%s' % (k, json.dumps(v, separators=(",", ":")), "," if n < len(items) - 1 else "")
                  for n, (k, v) in enumerate(items)]
        lines.append("}}")
        TRUTH.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
        print("written %s: %d shaders" % (TRUTH, len(truth["shaders"])))
    truth = json.loads(TRUTH.read_text(encoding="ascii"))
    stats, problems = check(truth)
    print("source slots: %d shaders; %s" % (len(truth["shaders"]), ", ".join("%s %d" % kv for kv in sorted(stats.items()))))
    for line in problems[:20]:
        print("  " + line)
    if problems or not stats["t registers as observed"]:
        return 1
    print("source slots: %d checks passed" % stats["t registers as observed"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
