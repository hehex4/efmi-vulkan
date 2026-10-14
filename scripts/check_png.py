"""Validate shared PNG decoding against independent pixels and installed assets."""
from __future__ import annotations
import contextlib
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import zlib

from build import COMMON, find_vcvars, find_vulkan_include, run_cl

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/png"


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def png(width, height, color, depth, scanlines, srgb=False, extra=b""):
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, color, 0, 0, 0))
            + (chunk(b"sRGB", b"\0") if srgb else b"") + extra
            + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "png_test.exe"
    sources = [ROOT / "frontend/assets/png.cpp", ROOT / "frontend/assets/png.h",
               ROOT / "frontend/assets/dds.h", ROOT / "frontend/assets/allocation_budget.h",
               ROOT / "sandbox/png_test.cpp", Path(__file__)]
    log = io.StringIO()
    try:
        with contextlib.redirect_stdout(log):
            run_cl(find_vcvars(), str(OUT), COMMON + ["/I" + find_vulkan_include(), "/Fo" + str(OUT) + "/",
                "/Fd" + str(OUT / "png.pdb"), str(sources[0]), str(sources[4]), "/Fe:" + str(exe), "/link", "/NOLOGO"])
    finally:
        (OUT / "build.txt").write_text(log.getvalue().encode("ascii", "backslashreplace").decode(), encoding="ascii")
    results = []

    def decode(path, name, expected=None, formats=(), valid=True, budget=None):
        raw = OUT / (name + ".pixels")
        before = hashlib.sha256(path.read_bytes()).hexdigest()
        call = [str(exe), str(path), str(raw)] + ([str(budget)] if budget is not None else [])
        run = subprocess.run(call, cwd=OUT, capture_output=True, text=True, timeout=30)
        assert run.returncode == (0 if valid else 4), run.stdout + run.stderr
        row = dict(input=str(path), sha256=before, expected_valid=valid, passed=True)
        if valid:
            row.update(json.loads(run.stdout))
            pixels = raw.read_bytes()
            if row["format"] in (44, 50):  # Original WIC may preserve BGRA byte order.
                pixels = b"".join(bytes((pixels[i+2], pixels[i+1], pixels[i], pixels[i+3])) for i in range(0, len(pixels), 4))
            assert expected is None or pixels == expected, name + ": decoded pixels differ"
            assert not formats or row["format"] in formats, name + ": wrong typed/color-space format"
            row["pixel_sha256"] = hashlib.sha256(pixels).hexdigest()
            row["raw"] = str(raw)
        assert hashlib.sha256(path.read_bytes()).hexdigest() == before, "Input image was modified"
        results.append(row)

    rgba = bytes((128, 64, 32, 127, 255, 0, 100, 0))
    fixtures = [
        ("rgba-linear", png(2, 1, 6, 8, b"\0" + rgba), rgba, (37, 44)),
        ("rgba-srgb", png(2, 1, 6, 8, b"\0" + rgba, True), rgba, (43, 50)),
        ("rgb", png(2, 1, 2, 8, b"\0\x80\x40\x20\xff\0\x64"), bytes((128, 64, 32, 255, 255, 0, 100, 255)), (37, 44)),
        ("gray8", png(2, 1, 0, 8, b"\0\x40\x80", True), b"\x40\x80", (9,)),
        ("gray16", png(2, 1, 0, 16, b"\0\x01\x23\x45\x67"), b"\x23\x01\x67\x45", (70,)),
        ("rgba16", png(1, 1, 6, 16, b"\0\x01\x23\x45\x67\x89\xab\xcd\xef"), b"\x23\x01\x67\x45\xab\x89\xef\xcd", (91,)),
        ("palette", png(2, 1, 3, 8, b"\0\0\1", extra=chunk(b"PLTE", b"\xff\0\0\0\xff\0") + chunk(b"tRNS", b"\xff\x40")), bytes((255, 0, 0, 255, 0, 255, 0, 64)), (37, 44)),
    ]
    for name, data, pixels, formats in fixtures:
        path = OUT / (name + ".png"); path.write_bytes(data); decode(path, name, pixels, formats)
    for name, data in [("bad-signature", b"bad"), ("truncated", fixtures[0][1][:35]),
                       ("oversized", png(16385, 1, 6, 8, b"\0")),
                       ("expanded-budget", png(10000, 10000, 6, 8, b"\0"))]:
        path = OUT / (name + ".png"); path.write_bytes(data); decode(path, name, valid=False)
    decode(OUT / "rgba-linear.png", "aggregate-budget", valid=False, budget=7)
    asset_report = ROOT / "build/folder-assets/report.json"
    if asset_report.exists():
        from PIL import Image
        for i, row in enumerate(json.loads(asset_report.read_text())["assets"]):
            path = Path(row["path"])
            if path.suffix.lower() != ".png" or not path.is_file():
                continue
            with Image.open(path) as image:
                expected = image.convert("RGBA").tobytes()
                srgb = "srgb" in image.info
            decode(path, f"asset-{i}", expected, (43, 50) if srgb else (37, 44))
    report = dict(scope="CPU PNG pixels and typed formats; not panel drawing", passed=True, results=results,
                  source_sha256={str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources},
                  executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest())
    (OUT / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="ascii")
    print(f"PNG decoding: {len(results)} checks passed")


if __name__ == "__main__":
    main()
