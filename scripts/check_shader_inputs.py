"""Build/test the metadata reader and original input-layout bridge; no GPU or game."""
import argparse
import contextlib
import hashlib
import io
from pathlib import Path
import struct
import subprocess

from build import find_vcvars, run_cl
from build_shader_input_index import encode
from build_xxmi_runtime import build_runtime, runtime_flags

ROOT = Path(__file__).resolve().parents[1]


def build(out):
    out.mkdir(parents=True, exist_ok=True)
    libraries = build_runtime(ROOT / 'build/xxmi-runtime', test=False)
    sources = [ROOT / p for p in ['sandbox/shader_input_index_test.cpp', 'layer/src/pipeline_recipe.cpp',
                                 'layer/src/merged_skeleton.cpp']]
    headers = [ROOT / p for p in ['frontend/xxmi/shader_input_index.h', 'frontend/xxmi/vertex_plan.h',
                                 'sandbox/material_fixture.h', 'sandbox/src/spirv.h', 'layer/src/pipeline_recipe.h',
                                 'layer/src/merged_skeleton.h']]
    flags = runtime_flags(out) + ['/DNOMINMAX']
    stamp = hashlib.sha256(repr(flags).encode())
    for path in sources + headers + [Path(__file__)] + [Path(x) for x in libraries if Path(x).is_file()]:
        stamp.update(path.read_bytes())
    exe, cache = out / 'shader_input_test.exe', out / 'stamp.txt'
    key = stamp.hexdigest()
    if not (exe.is_file() and cache.is_file() and cache.read_text() == key + '\n' + hashlib.sha256(exe.read_bytes()).hexdigest()):
        output = io.StringIO()
        try:
            with contextlib.redirect_stdout(output):
                run_cl(find_vcvars(), str(out), flags + [str(p) for p in sources] + libraries +
                       ['/Fe:' + str(exe), '/link', '/NOLOGO', '/OPT:REF', '/OPT:ICF'])
        finally:
            (out / 'build.txt').write_text(output.getvalue(), encoding='utf-8')
        cache.write_text(key + '\n' + hashlib.sha256(exe.read_bytes()).hexdigest(), encoding='ascii')
    return exe


def run(command):
    result = subprocess.run([str(p) for p in command], cwd=ROOT, capture_output=True, text=True, timeout=120)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='')
    if result.returncode:
        raise RuntimeError(f'Input metadata check failed: {result.returncode}')


def fixtures(exe, out):
    root = out / 'fixtures'
    root.mkdir(exist_ok=True)
    module = root / 'fixture.spv'
    run([exe, '--fixture', module])
    code = module.read_bytes()
    names = [(0, 'POSITION', 0), (4, 'TEXCOORD', 4), (5, 'BLENDWEIGHTS', 0),
             (6, 'BLENDINDICES', 0), (3, 'TANGENT', 0)]
    good = encode([(hashlib.sha256(code).digest(), len(code), names)])
    (root / 'good.bin').write_bytes(good)
    bad = dict(truncated=good[:-1], magic=b'UNKNOWN!' + good[8:], checksum=good[:16] + bytes(32) + good[48:],
               trailing=good + b'\0', short=b'EFVIS001')
    def payload(name, data, count=1):
        bad[name] = good[:8] + struct.pack('<2I', count, len(data)) + hashlib.sha256(data).digest() + data
    original = good[48:]
    def changed(offset, value):
        data = bytearray(original);struct.pack_into('<I', data, offset, value);return data
    payload('duplicate-shader', original * 2, 2)
    payload('entry-count', original, 65537)
    payload('module-size', changed(32, 17))
    payload('semantic-count', changed(36, 33))
    payload('location', changed(40, 256))
    payload('semantic-index', changed(44, 256))
    payload('name-length', changed(48, 129))
    data = bytearray(original);data[52] = 0;payload('null-name', data)
    data = bytearray(original);data[52] = 255;payload('unicode-name', data)
    # First name is POSITION (eight bytes), so the next record starts at 60.
    payload('duplicate-location', changed(60, 0))
    first = original[40:60];payload('duplicate-semantic', original[:36] + struct.pack('<I', 2) + first + struct.pack('<I', 1) + first[4:])
    payload('missing-record', original[:38])
    payload('payload-trailing', original + b'\0')
    for name, data in bad.items():
        (root / ('bad-' + name + '.bin')).write_bytes(data)
    run([exe, root])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--index', type=Path)
    parser.add_argument('--modules', type=Path)
    parser.add_argument('--expected', type=int)
    args = parser.parse_args()
    out = ROOT / 'build/shader-input-test'
    exe = build(out)
    if args.index or args.modules or args.expected is not None:
        if args.index is None or args.modules is None or args.expected is None:
            parser.error('Supply --index, --modules and --expected together')
        run([exe, '--corpus', args.index, args.modules, args.expected])
    else:
        fixtures(exe, out)
