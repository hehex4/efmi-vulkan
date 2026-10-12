"""Collect one frame from an explicitly named, already running probe into the project."""

import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))

from efmi_vk.evidence import collect
from efmi_vk.probe_client import ProbeError

# Set 2 binding 0 holds the instance entries; constant 5 (byte 80) of an entry has two element
# numbers into the bone buffer, 16 bytes per element. 12288 bytes are 256 bones of three rows;
# the first 256 bytes of the range are the draw's own entry (docs/dx11-baseline.md).
INSTANCE_FOLLOW = "2:0:80:2:16:12288:256"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pid", type=int, required=True)
    ap.add_argument("--target", choices=["sandbox", "game"], required=True)
    ap.add_argument("--out", type=Path, required=True, help="new directory inside this project")
    ap.add_argument("--mod", type=Path, help="optional INI file or mod directory")
    ap.add_argument("--pipeline-limit", type=int, default=16)
    ap.add_argument("--snapshot", choices=["mod", "all"],
                    help="keep the buffer contents of the draws the mod's hashes match, or of all indexed draws")
    ap.add_argument("--follow", default=None,
                    help="SET:BINDING:OFFSET:COUNT:STRIDE:BYTES[:ENTRY] or the preset 'instance' "
                         "(the game's instance entry: " + INSTANCE_FOLLOW + ")")
    ap.add_argument("--snapshot-budget", type=int, default=None, help="bytes the layer may keep (its default: 32 MiB)")
    args = ap.parse_args()
    snapshot = None
    if args.snapshot:
        snapshot = {"all": True} if args.snapshot == "all" else {}
        if args.snapshot_budget is not None:
            snapshot["budget"] = args.snapshot_budget
        if args.follow:
            parts = (INSTANCE_FOLLOW if args.follow == "instance" else args.follow).split(":")
            if len(parts) not in (6, 7) or not all(p.isdigit() for p in parts):
                ap.error("--follow takes six or seven numbers separated by colons, or 'instance'")
            snapshot["follow"] = dict(zip(("set", "binding", "offset", "count", "stride", "bytes", "entry"), map(int, parts)))
    elif args.follow or args.snapshot_budget is not None:
        ap.error("--follow and --snapshot-budget need --snapshot")
    try:
        result = collect(args.pid, args.target, args.out, args.mod, args.pipeline_limit, snapshot)
    except (OSError, ValueError, ProbeError) as exc:
        print(f"collection failed: {exc}")
        return 1
    print(f"Saved {result['collection']} {result['source']['kind']} observation to {args.out.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
