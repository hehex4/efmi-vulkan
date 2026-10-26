"""Callers of named functions in a saved profile of scripts/profile_game.py, and the inclusive layer
functions of one thread.

    python scripts/profile_callers.py PROFILE.json NAME [NAME...] [--thread ID] [--pdb DIR ...]

For each NAME (a substring of a frame's name) the samples whose stack holds it and the three frames
above it most often; with --thread only that thread's samples."""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from profile_game import ROOT, Symbols, in_layer  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("profile", type=Path)
    ap.add_argument("names", nargs="*")
    ap.add_argument("--thread", type=int)
    ap.add_argument("--pdb", type=Path, action="append", default=[])
    ap.add_argument("--top", type=int, default=8)
    args = ap.parse_args()
    result = json.loads(args.profile.read_text(encoding="utf-8"))
    symbols = Symbols(result["modules"], args.pdb or [ROOT / "layer" / "build", ROOT / "layer" / "build" / "module", ROOT / "dist"])
    stacks = [(s["n"], s["thread"], [symbols.name(f) for f in s["frames"]]) for s in result["stacks"]
              if args.thread is None or s["thread"] == args.thread]
    total = sum(n for n, _, _ in stacks)
    for target in args.names:
        hits, callers = 0, Counter()
        for n, _, names in stacks:
            for i, x in enumerate(names):
                if target in x:
                    hits += n
                    callers[" < ".join(names[i + 1:i + 4])] += n
                    break
        print("== %s: %d of %d samples (%.1f%%)" % (target, hits, total, 100.0 * hits / max(total, 1)))
        for c, n in callers.most_common(args.top):
            print("   %5d  %s" % (n, c[:300]))
    if args.thread is not None:
        inclusive = Counter()
        for n, _, names in stacks:
            for x in set(names):
                if in_layer(x):
                    inclusive[x] += n
        print("== inclusive layer functions of thread %d (%d samples)" % (args.thread, total))
        for x, n in inclusive.most_common(40):
            print("   %5.1f%%  %s" % (100.0 * n / max(total, 1), x[:160]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
