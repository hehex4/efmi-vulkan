"""Checks the region hash rule against an XXMI FrameAnalysis dump.

For every indexed draw of the dump this reads the bound index buffer from the
dumped ``.buf`` file, takes the byte range the draw uses and compares its
CRC-32C with the region hash XXMI put into the file name
(``NNNNNN-ib=<region>(<resource>)-...``).

The dump is only read. Usage:

    python scripts/verify_region_hash.py <FrameAnalysis directory> [--limit N]
"""

from __future__ import annotations

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "server"))

from efmi_vk.hashes import crc32c, format_hash, index_region  # noqa: E402

RE_SET_IB = re.compile(r"^(\d+) IASetIndexBuffer\(pIndexBuffer:0x([0-9A-Fa-f]+), Format:(\d+), Offset:(\d+)\)")
RE_DRAW = re.compile(
    r"^(\d+) DrawIndexed(?:Instanced)?\((?:IndexCount|IndexCountPerInstance):(\d+),"
    r"(?: InstanceCount:\d+,)? StartIndexLocation:(\d+)"
)
RE_IB_FILE = re.compile(r"^(\d{6})-ib=([0-9a-f]{8})(?:\(([0-9a-f]{8})\))?-.*\.buf$")

DXGI_FORMAT_R16_UINT = 57
DXGI_FORMAT_R32_UINT = 42


def parse_log(path):
    """Returns {draw number: (format, bind offset, first index, index count)}."""
    draws = {}
    fmt = offset = None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = RE_SET_IB.match(line)
            if m:
                fmt, offset = int(m.group(3)), int(m.group(4))
                continue
            m = RE_DRAW.match(line)
            if m and fmt is not None:
                draws[int(m.group(1))] = (fmt, offset, int(m.group(3)), int(m.group(2)))
    return draws


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dump")
    ap.add_argument("--limit", type=int, default=0, help="check at most this many distinct regions")
    args = ap.parse_args()

    draws = parse_log(os.path.join(args.dump, "log.txt"))
    files = {}
    for name in os.listdir(args.dump):
        m = RE_IB_FILE.match(name)
        if m:
            files[int(m.group(1))] = (name, m.group(2), m.group(3))

    seen = {}
    counts = {"whole_buffer": 0, "region_only": 0, "mismatch": 0, "no_draw_info": 0, "unreadable": 0}
    mismatches = []
    cache = {}
    for number in sorted(files):
        name, region_hash, resource_hash = files[number]
        info = draws.get(number)
        if info is None:
            counts["no_draw_info"] += 1
            continue
        fmt, bind_offset, first_index, index_count = info
        index_size = 4 if fmt == DXGI_FORMAT_R32_UINT else 2
        real = os.path.realpath(os.path.join(args.dump, name))
        key = (real, bind_offset, first_index, index_count)
        if key in seen:
            continue
        if args.limit and len(seen) >= args.limit:
            break
        seen[key] = number
        try:
            data = cache.get(real)
            if data is None:
                with open(real, "rb") as f:
                    data = f.read()
                cache[real] = data
        except OSError:
            counts["unreadable"] += 1
            continue
        off, size = index_region(bind_offset, index_size, first_index, index_count)
        whole = format_hash(crc32c(data[off:off + size])) if off + size <= len(data) else None
        only = format_hash(crc32c(data[:size])) if size <= len(data) else None
        if whole == region_hash:
            counts["whole_buffer"] += 1
        elif only == region_hash:
            counts["region_only"] += 1
        else:
            counts["mismatch"] += 1
            if len(mismatches) < 10:
                mismatches.append(
                    f"draw {number:06d}: file hash {region_hash}, computed whole={whole} region_only={only}, "
                    f"fmt={fmt} offset={bind_offset} first={first_index} count={index_count} file_size={len(data)}"
                )

    print(f"dump: {args.dump}")
    print(f"indexed draws in log: {len(draws)}; ib files: {len(files)}; distinct regions checked: {len(seen)}")
    print("matches where the file holds the whole buffer : %d" % counts["whole_buffer"])
    print("matches where the file holds only the region  : %d" % counts["region_only"])
    print("mismatches: %d; without draw info: %d; unreadable: %d"
          % (counts["mismatch"], counts["no_draw_info"], counts["unreadable"]))
    for line in mismatches:
        print("  " + line)
    return 0 if counts["mismatch"] == 0 and (counts["whole_buffer"] + counts["region_only"]) > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
