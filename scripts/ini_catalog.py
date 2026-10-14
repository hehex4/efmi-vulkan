"""Read-only INI fingerprint inventory; loading semantics belong to XXMI.

Collect candidate files, including conditionally disabled includes. The native
original parser independently loads the directory in its own order. This audit
does not choose namespaces, evaluate include conditions or execute commands.
"""
from __future__ import annotations

import fnmatch
import hashlib
from pathlib import Path
import re

DEFAULT_EXCLUDES = ("disabled*", "desktop.ini")


def source_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


def sections(text: str):
    name = ""
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        match = re.fullmatch(r"\[([^\]]+)\]", line)
        if match:
            name = match[1]
        else:
            yield name, number, line


def discover(mods: Path, core: Path, excludes=DEFAULT_EXCLUDES) -> dict:
    mods, core = mods.resolve(), core.resolve()
    if not mods.is_dir() or not (core / "main.ini").is_file():
        raise ValueError("Missing Mods directory or Core main.ini")
    files, excluded, seen, missing = [], [], set(), []
    excludes = tuple(x.casefold() for x in excludes)

    def disabled(path: Path, base: Path) -> bool:
        return any(fnmatch.fnmatchcase(part.casefold(), pattern)
                   for part in path.relative_to(base).parts for pattern in excludes)

    def scan(folder: Path):
        for path in sorted(folder.rglob("*"), key=lambda p: p.as_posix().casefold()):
            if path.is_file() and path.suffix.casefold() == ".ini":
                if disabled(path, folder):
                    excluded.append(str(path))
                else:
                    add(path)

    def add(path: Path):
        path = path.resolve()
        key = str(path).casefold()
        if key in seen:
            return
        if len(seen) >= 2048:
            raise ValueError("INI discovery exceeds 2048 inputs")
        if not path.is_file():
            missing.append(str(path))
            return  # The original include condition may disable this input.
        text = source_text(path)
        seen.add(key)
        namespace = re.search(r"(?im)^\s*namespace\s*=\s*(\S+)\s*$", text)
        kinds = {}
        for name in re.findall(r"(?m)^\s*\[([^\]]+)\]\s*$", text):
            prefix = next((p for p in ("TextureOverride", "ShaderOverride", "ShaderRegex", "CustomShader",
                                      "CommandList", "Resource", "Pool", "Key", "Preset", "Include", "Constants", "Present")
                           if name.casefold().startswith(p.casefold())), "Other")
            kinds[prefix] = kinds.get(prefix, 0) + 1
        files.append(dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                          namespace=namespace[1] if namespace else None, sections=kinds))
        for section, number, line in sections(text):
            if not section.casefold().startswith("include") or "=" not in line:
                continue
            key, value = (part.strip() for part in line.split("=", 1))
            if key.casefold() not in ("include", "include_recursive"):
                continue
            target = path.parent / value.replace("\\", "/")
            if key.casefold() == "include_recursive":
                if not target.is_dir():
                    missing.append(str(target.resolve()))
                else:
                    scan(target)
            else:
                add(target)

    add(core / "main.ini")
    scan(mods)
    return dict(schema=1, mods=str(mods), core=str(core), excludes=list(excludes), files=files,
                excluded=excluded, missing_candidates=missing,
                scope="Candidate fingerprints only; original XXMI determines include conditions and actual loading")
