"""Derive a visibly altered component-7 package for an in-game check of the replacement path.

Component 7 of the sample has the positions of the game's own mesh, so a correct replacement
cannot be told from the original on screen. This tool takes the project-owned package made by
package_m2_component.py and changes vertex positions only: the strip on one side (x < 0) is
scaled about its own centroid and moved forward (towards -y, the side the face is convex to).
The other strip stays as it is, so one frame shows the altered and the unaltered shape side
by side. Counts, texture coordinates, blend data, indices and the match profile are unchanged;
the checksum in the header is recomputed.

The mod's files are not read or written. Input and output stay in this project's build folder.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "server"))
from efmi_vk.hashes import crc32c

SOURCE_SHA256 = "318e8354297fcdfd4b352d0ec031a6b2ba6472335fefe0be2c1b16f3eed52167"
HEADER = struct.Struct("<8s12I")


def bounds(points):
    return [[min(p[i] for p in points), max(p[i] for p in points)] for i in range(3)]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", type=Path, default=ROOT / "build/m2-component7.bin")
    ap.add_argument("--out", type=Path, default=ROOT / "build/m2-component7-visible.bin")
    ap.add_argument("--scale", type=float, default=3.5, help="scale of the altered strip about its centroid")
    ap.add_argument("--forward", type=float, default=0.01, help="shift of the altered strip towards -y, in mesh units")
    args = ap.parse_args()
    source, out = args.source.resolve(), args.out.resolve()
    if not source.is_relative_to(ROOT) or not out.is_relative_to(ROOT):
        raise ValueError("input and output must stay in this project")
    if not 1.0 < args.scale <= 8.0 or not 0.0 <= args.forward <= 0.05:
        raise ValueError("scale must be in (1, 8] and forward in [0, 0.05]")
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise ValueError("source package differs from the reviewed component-7 package")
    magic, *words = HEADER.unpack_from(data)
    vertices, indices = words[6], words[7]
    if magic != b"EFMIM201" or (vertices, indices, words[8], words[9], words[10]) != (40, 108, 16, 8, 16):
        raise ValueError("unexpected package layout")
    payload = bytearray(data[HEADER.size:])
    if len(payload) != vertices * 40 + indices * 4 or crc32c(bytes(payload)) != words[11]:
        raise ValueError("source payload size or checksum mismatch")
    before = [struct.unpack_from("<3f", payload, v * 16) for v in range(vertices)]
    chosen = [v for v in range(vertices) if before[v][0] < 0]
    if len(chosen) != vertices // 2:
        raise ValueError("the two strips are not separated by the sign of x as expected")
    centre = [sum(before[v][i] for v in chosen) / len(chosen) for i in range(3)]
    for v in chosen:
        moved = [centre[i] + args.scale * (before[v][i] - centre[i]) for i in range(3)]
        moved[1] -= args.forward
        struct.pack_into("<3f", payload, v * 16, *moved)  # the fourth word of the element stays
    after = [struct.unpack_from("<3f", payload, v * 16) for v in range(vertices)]
    words[11] = crc32c(bytes(payload))
    result = HEADER.pack(magic, *words) + bytes(payload)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(result)
    report = {
        "scope": "visible in-game check of the component-7 replacement path; positions of one strip only",
        "source": {"path": str(source), "sha256": SOURCE_SHA256},
        "scale": args.scale,
        "forward": args.forward,
        "altered_vertices": chosen,
        "centre": centre,
        "bounds_before": {"altered": bounds([before[v] for v in chosen]),
                          "other": bounds([before[v] for v in range(vertices) if v not in chosen])},
        "bounds_after": {"altered": bounds([after[v] for v in chosen]),
                         "other": bounds([after[v] for v in range(vertices) if v not in chosen])},
        "unchanged_bytes_after_positions": bytes(payload[vertices * 16:]) == data[HEADER.size + vertices * 16:],
        "package_sha256": hashlib.sha256(result).hexdigest(),
    }
    out.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
