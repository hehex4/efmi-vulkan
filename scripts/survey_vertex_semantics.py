"""Inventory retained vertex input names/semantics without changing captured shaders."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def survey(root):
    seen = set()
    result = dict(files=0, unique_modules=0, vertex_modules=0, located_vertex_inputs=0,
                  named_vertex_inputs=0, semantic_vertex_inputs=0,
                  vertex_modules_with_semantic_decorations=0, examples=[])
    for path in sorted(root.rglob("*.spv")):
        data = path.read_bytes()
        result["files"] += 1
        digest = hashlib.sha256(data).hexdigest()
        if digest in seen: continue
        seen.add(digest)
        if len(data) < 20 or len(data) % 4: raise ValueError(f"Invalid SPIR-V size: {path}")
        words = struct.unpack(f"<{len(data)//4}I", data)
        if words[0] != 0x07230203: raise ValueError(f"Invalid SPIR-V magic: {path}")
        result["unique_modules"] += 1
        vertex = False
        inputs, names, locations, semantics = set(), {}, {}, {}
        any_semantic = False
        at = 5
        while at < len(words):
            count, opcode = words[at] >> 16, words[at] & 65535
            if not count or at + count > len(words): raise ValueError(f"Invalid SPIR-V instruction: {path}:{at}")
            args = words[at+1:at+count]
            def string(values):
                return struct.pack(f"<{len(values)}I", *values).split(b"\0")[0].decode("utf-8")
            if opcode == 15 and args[0] == 0: vertex = True
            if opcode == 5: names[args[0]] = string(args[1:])
            if opcode == 59 and args[2] == 1: inputs.add(args[1])
            if opcode == 71 and args[1] == 30: locations[args[0]] = args[2]
            if opcode == 5632 and args[1] == 5635:
                semantics[args[0]] = string(args[2:]); any_semantic = True
            if opcode == 5633 and args[2] == 5635: any_semantic = True
            at += count
        if not vertex: continue
        entries = sorted((locations[i], names.get(i, ""), semantics.get(i, "")) for i in inputs if i in locations)
        result["vertex_modules"] += 1
        result["located_vertex_inputs"] += len(entries)
        result["named_vertex_inputs"] += sum(bool(name) for _, name, _ in entries)
        result["semantic_vertex_inputs"] += sum(bool(semantic) for _, _, semantic in entries)
        result["vertex_modules_with_semantic_decorations"] += any_semantic
        if len(result["examples"]) < 12:
            result["examples"].append(dict(path=path.relative_to(root).as_posix(), sha256=digest, inputs=entries))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path)
    args = parser.parse_args()
    if not args.captures.is_dir(): parser.error("capture directory does not exist")
    print(json.dumps(survey(args.captures), indent=2, ensure_ascii=True))
