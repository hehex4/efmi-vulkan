"""A Mod whose files are broken degrades as the original does, not as a whole (BUG-077, D-137).

Copies the smallest installed character Mod (read-only source) into folders of its own and breaks one
thing in each, then runs the every-key-state check (check_mod_states.py) on that folder with the
installed Core:

- an index buffer file deleted: the original's resource is null and that draw shows nothing;
- a vertex buffer file one byte short of whole elements: the original's buffer holds it, the last
  partial element is never read;
- a vertex buffer file declared a second time with another stride: the original makes two buffers;
- more distinct draw lists than the layer's cache holds;
- a section matched by a texture's description (match_type, match_width ...): the original runs it at
  a CheckTextureOverride of a pixel slot (D-138);
- a section matched by a buffer's description and a section that changes the game's resources when they
  are created: the layer does not do these, the rest of the Mod loads and the layer says so;
- a texture the Mod binds given as a PNG: the original loads every file not named .dds with WIC (D-139);
- a texture file the loader makes nothing of: the original's resource is null and the draw goes on;
- a texture declared with a colour space and a size: the loaders take the first and ignore the second;
  one whose declared format its file is not: the original's view fails and the slot binds nothing (D-140).

Each must load, and every section must plan in every state. Writes build/mod-degradation/.
"""
from __future__ import annotations

import os
import re
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODS = Path(r"D:\XXMI-Launcher-Portable-v2.1.6\EFMI\Mods")
CORE = Path(r"D:\XXMI-Launcher-Portable-v2.1.6\EFMI\Core\EFMI")


def resources(ini: str) -> list[tuple[str, dict]]:
    out, name, keys = [], None, {}
    for line in ini.splitlines():
        line = line.split(";", 1)[0].strip()
        if line.startswith("["):
            if name: out.append((name, keys))
            name, keys = line.strip("[]"), {}
        elif "=" in line and name:
            k, v = line.split("=", 1)
            keys[k.strip().lower()] = v.strip()
    if name: out.append((name, keys))
    return [(n, k) for n, k in out if n.lower().startswith("resource") and "filename" in k]


def png_bytes(width: int, height: int) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    # Filter byte 0, then RGBA8 samples.
    rows = b"".join(b"\x00" + b"".join(bytes((x * 40 & 255, y * 40 & 255, 128, 255)) for x in range(width)) for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def set_key(lines: list[str], section: str, name: str, value: str) -> None:
    """`name = value` in [section], in place of the line that sets it."""
    inside = False
    for i, line in enumerate(lines):
        stripped = line.split(";", 1)[0].strip()
        if stripped.startswith("["):
            inside = stripped.strip("[]").lower() == section.lower()
        elif inside and "=" in stripped and stripped.split("=", 1)[0].strip().lower() == name:
            lines[i] = "%s = %s" % (name, value)
            return
    raise RuntimeError("[%s] sets no %s" % (section, name))


def smallest_character_mod() -> Path:
    candidates = []
    for folder in MODS.iterdir():
        if not folder.is_dir() or folder.name.upper().startswith("DISABLED"):
            continue
        inis = [p for p in folder.glob("*.ini")]
        if not any(re.search(r"(?im)^\s*\[TextureOverride", p.read_text(encoding="utf-8", errors="replace")) for p in inis):
            continue
        if not list(folder.rglob("*.buf")):
            continue
        candidates.append((sum(f.stat().st_size for f in folder.rglob("*") if f.is_file()), folder))
    return min(candidates)[1]


def main() -> int:
    source = smallest_character_mod()
    out = ROOT / "build" / "mod-degradation"
    if out.exists():
        shutil.rmtree(out)
    failed = False
    ini_name = next(p.name for p in source.glob("*.ini") if re.search(r"(?im)^\s*\[TextureOverride", p.read_text(encoding="utf-8", errors="replace")))
    text = (source / ini_name).read_text(encoding="utf-8", errors="replace")
    declared = resources(text)
    fmt = lambda k: k.get("format", "").upper().replace("DXGI_FORMAT_", "")
    index = next((k["filename"] for n, k in declared if fmt(k) in ("R32_UINT", "R16_UINT") and k["filename"].lower().endswith(".buf")), None)
    vertex = next(((n, k) for n, k in declared if "stride" in k and k["filename"].lower().endswith(".buf") and fmt(k) not in ("R32_UINT", "R16_UINT")), None)
    if not index or not vertex:
        print("mod degradation: %s has no index or vertex buffer declaration to break" % source.name)
        return 1

    def variant(label: str, change, expect: tuple[str, ...] = ()) -> None:
        nonlocal failed
        mods = out / label / "Mods"
        mod = mods / source.name
        shutil.copytree(source, mod)
        change(mod)
        env = dict(os.environ, EFMI_VK_CHECK_MODS=str(mods), EFMI_VK_CHECK_CORE=str(CORE))
        result = subprocess.run([sys.executable, str(ROOT / "scripts" / "check_mod_states.py")], env=env, capture_output=True,
                                text=True, errors="replace", timeout=1800)
        (out / label / "check.txt").write_text(result.stdout + result.stderr, encoding="utf-8")
        ok = result.returncode == 0 and all(e in result.stdout for e in expect)
        failed |= not ok
        print("%s %s: %s" % ("ok  " if ok else "FAIL", label, (result.stdout.strip().splitlines() or ["(no output)"])[-1]))
        if not ok:
            print("\n".join("     " + line for line in result.stdout.strip().splitlines()[-8:]))

    variant("index buffer file deleted", lambda mod: (mod / index.replace("\\", "/")).unlink())

    def short(mod: Path) -> None:
        path = mod / vertex[1]["filename"].replace("\\", "/")
        path.write_bytes(path.read_bytes()[:-1])
    variant("vertex buffer one byte short", short)

    def twice(mod: Path) -> None:
        name, keys = vertex
        stride = int(keys["stride"])
        extra = "\n[ResourceEfvkSecondMeaning]\ntype = Buffer\nstride = %d\nfilename = %s\n" % (stride * 2, keys["filename"])
        # Bound after a list's last draw, so the generation declares it in use without a draw reading it.
        lines = text.splitlines()
        first = next(i for i, line in enumerate(lines) if line.strip().lower().startswith("drawindexed"))
        end = next((i for i in range(first + 1, len(lines)) if lines[i].strip().startswith("[")), len(lines))
        last = max(i for i in range(first, end) if lines[i].strip().lower().startswith("drawindexed"))
        lines.insert(last + 1, "vb1 = ResourceEfvkSecondMeaning")
        (mod / ini_name).write_text("\n".join(lines) + "\n" + extra, encoding="utf-8")
    variant("vertex buffer file declared with a second stride", twice)

    def many_lists(mod: Path) -> None:
        # A draw whose first index follows a counter the Mod's Present advances every frame: 1100 binding
        # states, more than the layer's cache of distinct draw lists holds (1024).
        lines = text.splitlines()
        at = next(i for i, line in enumerate(lines) if line.strip().lower().startswith("drawindexed"))
        lines[at] = "drawindexed = 3, ($efvk_frame % 1100) * 3, 0"
        def insert_after(header: str, line: str) -> None:
            where = next((i for i, l in enumerate(lines) if l.strip().lower() == header.lower()), None)
            if where is None:
                lines.extend(["", header, line])
            else:
                lines.insert(where + 1, line)
        insert_after("[Constants]", "global $efvk_frame = 0")
        insert_after("[Present]", "$efvk_frame = $efvk_frame + 1")
        (mod / ini_name).write_text("\n".join(lines) + "\n", encoding="utf-8")
    variant("more distinct draw lists than the cache holds", many_lists)

    texture = next((n for n, k in declared if k["filename"].lower().endswith(".dds")), None)

    def by_description(mod: Path) -> None:
        bind = "ps-t0 = %s" % texture if texture else "ps-t0 = null"
        extra = "\n[TextureOverrideEfvkBySize]\nmatch_type = Texture2D\nmatch_width = 1024\nmatch_height = width\n%s\n" % bind
        (mod / ini_name).write_text(text + extra, encoding="utf-8")
    variant("texture section matched by its description", by_description)

    def not_for_vulkan(mod: Path) -> None:
        extra = "\n[TextureOverrideEfvkByteWidth]\nmatch_type = Buffer\nmatch_byte_width = 1024\nhandling = skip\n"
        lines = text.splitlines()
        first = next(i for i, line in enumerate(lines) if line.strip().lower().startswith("[textureoverride"))
        lines.insert(first + 1, "deny_cpu_read = 1")
        (mod / ini_name).write_text("\n".join(lines) + "\n" + extra, encoding="utf-8")
    variant("buffer section by description, creation-time key", not_for_vulkan,
            ("matches buffers by their description", "changes how the game creates"))

    # A texture a draw of the Mod binds (`ps-tN = Resource...`) from a .dds file.
    files = {n.lower(): k["filename"] for n, k in declared}
    bound = [b for b in re.findall(r"(?im)^\s*ps-t\d+\s*=\s*(?:ref\s+|reference\s+)?(Resource[^\s;]+)", text)
             if files.get(b.lower(), "").lower().endswith(".dds")]
    if not bound:
        print("mod degradation: %s binds no DDS texture" % source.name)
        return 1

    def as_png(mod: Path) -> None:
        (mod / "efvk_texture.png").write_bytes(png_bytes(8, 4))
        lines = text.splitlines()
        set_key(lines, bound[0], "filename", "efvk_texture.png")
        (mod / ini_name).write_text("\n".join(lines) + "\n", encoding="utf-8")
    variant("texture given as a PNG", as_png)

    def unloadable(mod: Path) -> None:
        (mod / files[bound[0].lower()].replace("\\", "/")).write_bytes(b"DDS " + bytes(range(60)))
    variant("texture file the loader makes nothing of", unloadable, ("does not load",))

    def insert_key(mod: Path, lines_to_add: list[str]) -> None:
        lines = text.splitlines()
        header = next(i for i, line in enumerate(lines) if line.split(";", 1)[0].strip().strip("[]").lower() == bound[0].lower()
                      and line.strip().startswith("["))
        lines[header + 1:header + 1] = lines_to_add
        (mod / ini_name).write_text("\n".join(lines) + "\n", encoding="utf-8")
    variant("texture declared with a colour space and a size", lambda mod: insert_key(mod, ["color_space = sRGB", "width = 64"]))
    variant("texture declared with a format its file is not", lambda mod: insert_key(mod, ["format = R32_FLOAT"]))
    print("mod degradation: %s (%s)" % ("FAILED" if failed else "passed", source.name))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
