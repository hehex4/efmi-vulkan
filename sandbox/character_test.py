"""Offline historical package inspection and fixtures for capture migration."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

from harness import Checks, DIST, test_dir
from efmi_vk.hashes import crc32c
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from character_materials import envelope

NONE = 0xFFFFFFFF


def fixture_package(path, salt=0):
    """Three components. A (bones 0-1, LOD 1 remap [1, 0]), B (bones 2-3), C (bone 4, hidden).

    Must agree with sandbox/character_gpu_case.h: index regions of the application's draws,
    and where each replacement lands on screen. A nonzero `salt` changes every section hash: the
    package of another character, which matches none of the scene's draws.
    """
    base = ((-.22, -.2), (.22, -.2), (0, .22))

    def positions(dx, dy):
        return b"".join(struct.pack("<4f", x + dx, y + dy, 0, 0) for x, y in base)

    def blend(bone):
        return struct.pack("<8H", 65535, 0, 0, 0, bone, 0, 0, 0) * 3

    streams = []  # kind, stride, bytes, bone index offset, bone weight offset

    def add(kind, stride, data, index_offset=NONE, weight_offset=NONE):
        streams.append((kind, stride, data, index_offset, weight_offset))
        return len(streams) - 1

    remap = add(2, 2, struct.pack("<2H", 1, 0))
    index = add(1, 4, struct.pack("<3I", 0, 1, 2))
    texcoord = add(0, 8, bytes(24))
    a_position, a_blend = add(0, 16, positions(0, -.3)), add(0, 16, blend(1), 8, 0)
    b1_position, b1_blend = add(0, 16, positions(0, 0)), add(0, 16, blend(3), 8, 0)
    b2_position, b2_blend = add(0, 16, positions(1.2, .9)), add(0, 16, blend(0), 8, 0)

    def region(first, count):
        values = [0] * count
        values[0:3] = [0, 1, 2]
        return crc32c(struct.pack("<%dH" % count, *values)) ^ salt

    draws = [(index, 3, 0, 0, (a_position, texcoord, a_blend, a_position)),
             (index, 3, 0, 0, (b1_position, texcoord, b1_blend, b1_position)),
             (index, 3, 0, 0, (b2_position, texcoord, b2_blend, b2_position))]
    lists = [(0, 1), (0, 1), (1, 2), (1, 2), (3, 0), (3, 0)]
    components = [(0, 2, remap, 0, 1), (2, 2, NONE, 2, 3), (4, 1, NONE, 4, 5)]
    sections = [(region(0, 3), 3, 0, 0, 0), (region(3, 6), 6, 3, 1, 0), (region(9, 9), 9, 9, 2, NONE),
                (region(18, 12), 12, NONE, 0, 1)]
    payload, table = bytearray(), []
    for kind, stride, data, index_offset, weight_offset in streams:
        payload += b"\0" * (-len(payload) % 16)
        table.append(struct.pack("<2I2Q2I", kind, stride, len(payload), len(data), index_offset, weight_offset))
        payload += data
    body = b"".join(struct.pack("<6I", *c, 0) for c in components) + b"".join(table)
    body += b"".join(struct.pack("<5I", *s) for s in sections) + b"".join(struct.pack("<2I", *l) for l in lists)
    body += b"".join(struct.pack("<3Ii4I", d[0], d[1], d[2], d[3], *d[4]) for d in draws)
    body += struct.pack("<3I", 2, 41, 95)
    header = b"EFMICH01" + struct.pack("<8IQ", 5, len(components), len(streams), len(sections), len(lists), len(draws), 1,
                                       crc32c(body + bytes(payload)), len(payload))
    path.write_bytes(header + body + bytes(payload))


def material_package(path, reject_late=False):
    paths = []
    for name, rgba in (("red", (255, 0, 0, 255)), ("green", (0, 255, 0, 255))):
        data = bytearray(152)
        for offset, value in ((0, 0x20534444), (4, 124), (8, 0x1007), (12, 1), (16, 1),
                              (28, 1), (76, 32), (80, 4), (84, 0x30315844), (108, 0x1000),
                              (128, 28), (132, 3), (140, 1)):
            struct.pack_into("<I", data, offset, value)
        data[148:] = bytes(rgba)
        file = path.with_name(name + ".dds")
        file.write_bytes(data)
        paths.append(str(file))
    shader = path.with_suffix(".spv")
    subprocess.run([str(Path(DIST) / "efmi_vk_character_test.exe"), "--material-shader", str(shader)],
                   cwd=path.parent, check=True, capture_output=True)
    # The fixture module's image is t1 by the DX11 register rule (its read-only bone block is t0), so
    # the package binds slot 1; slot 18 is not declared there. 42 is a slot no profile knows.
    profiles = [dict(code=shader.read_bytes(), rules=[(1, 2, 1, 6), (18, 1, 0, 0)])]
    pixels = [{1: 0, 18: 1}, {1: 1}, {42 if reject_late else 1: NONE}]
    path.write_bytes(envelope(path.read_bytes(), paths, profiles, pixels))


def main():
    checks = Checks("historical package inspection")
    native = Path(test_dir("character-package-"))
    proc = subprocess.run([os.path.join(DIST, "efmi_vk_character_test.exe")], cwd=native, capture_output=True, text=True, timeout=30)
    (native / "stdout.txt").write_text(proc.stdout + proc.stderr)
    print(proc.stdout, end="")
    checks.equal(proc.returncode, 0, "native package decoding and declaration tests")
    return checks.finish()


if __name__ == "__main__":
    raise SystemExit(main())
