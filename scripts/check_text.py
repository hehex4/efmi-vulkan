"""Checks that tracked workspace files and file names contain no CJK characters.

The rule is in AGENTS.md: everything in the workspace is written in English.
Text that has to match another script is spelled with \\u escapes instead.

Exit code 0 when clean, 1 when something was found.
"""

from __future__ import annotations

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Directories that are never part of the repository content.
SKIP_DIRS = {".git", "dist", "build", "captures", "__pycache__", ".claude", ".vs"}

# Files that are binary by nature; their names are still checked.
BINARY_EXT = {
    ".dll", ".exe", ".obj", ".pdb", ".lib", ".exp", ".ilk", ".bin", ".buf",
    ".dds", ".png", ".jpg", ".spv", ".zip", ".pyc",
}

# CJK ideographs, kana, hangul, CJK punctuation and full-width forms.
RANGES = (
    (0x2E80, 0x2FDF),
    (0x3000, 0x30FF),
    (0x3100, 0x318F),
    (0x31A0, 0x31FF),
    (0x3400, 0x4DBF),
    (0x4E00, 0x9FFF),
    (0xAC00, 0xD7AF),
    (0xF900, 0xFAFF),
    (0xFE30, 0xFE4F),
    (0xFF00, 0xFFEF),
    (0x20000, 0x2FA1F),
)


def is_cjk(ch: str) -> bool:
    cp = ord(ch)
    for lo, hi in RANGES:
        if lo <= cp <= hi:
            return True
    return False


def first_cjk(text: str):
    for i, ch in enumerate(text):
        if is_cjk(ch):
            return i, ch
    return None


def tracked_and_untracked():
    """Files Git knows about plus untracked ones that are not ignored."""
    try:
        out = subprocess.run(
            ["git", "-C", ROOT, "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
            check=True, capture_output=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return [p for p in out.decode("utf-8", "surrogateescape").split("\0") if p]


def walk_all():
    found = []
    for base, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            found.append(os.path.relpath(os.path.join(base, name), ROOT).replace("\\", "/"))
    return found


def main() -> int:
    files = tracked_and_untracked()
    if files is None:
        files = walk_all()
    problems = []
    for rel in files:
        hit = first_cjk(rel)
        if hit:
            problems.append(f"{rel!a}: file name contains U+{ord(hit[1]):04X}")
        path = os.path.join(ROOT, rel)
        if not os.path.isfile(path):
            continue
        if os.path.splitext(rel)[1].lower() in BINARY_EXT:
            continue
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError as e:
            problems.append(f"{rel!a}: cannot read ({e})")
            continue
        if b"\0" in data[:4096]:
            continue
        text = data.decode("utf-8", "replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            hit = first_cjk(line)
            if hit:
                problems.append(f"{rel!a}:{lineno}: contains U+{ord(hit[1]):04X}")
                break
    if problems:
        print("check_text: CJK characters found")
        for p in problems:
            print("  " + p)
        return 1
    print(f"check_text: {len(files)} files clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
