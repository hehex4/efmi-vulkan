"""Collect one frame from an explicitly named, already running probe into the project."""

import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))

from efmi_vk.evidence import collect
from efmi_vk.probe_client import ProbeError


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pid", type=int, required=True)
    ap.add_argument("--target", choices=["sandbox", "game"], required=True)
    ap.add_argument("--out", type=Path, required=True, help="new directory inside this project")
    ap.add_argument("--mod", type=Path, help="optional INI file or mod directory")
    ap.add_argument("--pipeline-limit", type=int, default=16)
    args = ap.parse_args()
    try:
        result = collect(args.pid, args.target, args.out, args.mod, args.pipeline_limit)
    except (OSError, ValueError, ProbeError) as exc:
        print(f"collection failed: {exc}")
        return 1
    print(f"Saved {result['collection']} {result['source']['kind']} observation to {args.out.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
