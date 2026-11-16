"""Read version-5 VFS indices and extract selected content into the project build directory.

Selection is by container file type (initial bundles by default), never character
identity. Indices require CRC32; payload ranges require their recorded MD5. The
source installation is opened only for reading. Bundle decoding is separate.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

from unity_shader_metadata import require, span

ROOT = Path(__file__).resolve().parents[1]
# Installed content-container format constant; not an account credential.
CONTAINER_KEY = bytes.fromhex('E95B317AC4F828569D23A86BF271DCB53E846FA75C924D671DBA8E38F4CA52E1')


def decode(data, nonce):
    from Crypto.Cipher import ChaCha20
    require(len(nonce) == 12, 'Invalid container nonce')
    cipher = ChaCha20.new(key=CONTAINER_KEY, nonce=nonce)
    cipher.seek(64)
    return cipher.decrypt(data)


def parse_plain_index(data):
    require(len(data) >= 4 and zlib.crc32(data[:-4]) == struct.unpack('<I', data[-4:])[0], 'Index CRC32 mismatch')
    body, pos = data[:-4], 0

    def take(size):
        nonlocal pos
        result = span(body, pos, size)
        pos += size
        return result

    def number(fmt):
        return struct.unpack('<' + fmt, take(struct.calcsize('<' + fmt)))[0]

    def text():
        return take(number('H')).decode('ascii')

    def count():
        value = number('i')
        require(0 <= value <= 2_000_000, 'Invalid container count')
        return value

    version = number('i')
    require(version == 5, 'Only the observed version-5 index schema is supported')
    build = number('i')
    label = text()
    number('q'); number('i'); number('q'); number('B')
    entries = []
    for _ in range(count()):
        chunk = take(16).hex().upper() + '.chk'
        take(16); number('q'); number('B')
        # Version 5 uses eight bytes here and after each file, not version 4's four.
        number('q')
        for _ in range(count()):
            name, name_hash = text(), number('q')
            take(16)
            digest = take(16).hex()
            offset, length, kind, encrypted = number('q'), number('q'), number('B'), number('B')
            require(offset >= 0 and length > 0 and encrypted in (0, 1), 'Invalid indexed payload range/flags')
            iv = number('q') if encrypted else 0
            number('q')
            entries.append(dict(name=name, name_hash=name_hash, md5=digest, offset=offset,
                                length=length, file_type=kind, encrypted=bool(encrypted), iv=iv, chunk=chunk))
    # Retain the observed extension, whose semantics this reader does not use.
    # Its bytes are covered by the CRC; do not silently allow an arbitrary tail.
    require(len(body) - pos == 10, 'Unknown index trailer schema')
    return dict(version=version, build=build, label=label, trailer_hex=take(10).hex(), entries=entries)


def read_index(path):
    raw = path.read_bytes()
    require(len(raw) >= 16 and struct.unpack('<I', raw[:4])[0] == 3, 'Unknown index envelope protocol')
    result = parse_plain_index(decode(raw[12:], raw[:12]))
    result.update(path=str(path), sha256=hashlib.sha256(raw).hexdigest())
    return result


def read_payload(index_path, entry):
    path = index_path.parent / entry['chunk']
    before = path.stat()
    require(0 <= entry['offset'] <= before.st_size and 0 < entry['length'] <= before.st_size - entry['offset'],
            'Payload range exceeds its chunk')
    require(entry['length'] <= 512 * 1024 * 1024, 'Payload exceeds the offline reader limit')
    with path.open('rb') as stream:
        stream.seek(entry['offset'])
        payload = stream.read(entry['length'])
    require(len(payload) == entry['length'], 'Truncated payload read')
    if entry['encrypted']:
        payload = decode(payload, struct.pack('<Iq', 3, entry['iv']))
    require(hashlib.md5(payload).hexdigest() == entry['md5'], 'Payload content MD5 differs from its index')
    after = path.stat()
    require((before.st_size, before.st_mtime_ns) == (after.st_size, after.st_mtime_ns), 'Chunk changed during read')
    return payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vfs', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--file-type', type=int, action='append', help='Container type; default 2, initial bundles')
    parser.add_argument('--inventory-only', action='store_true')
    parser.add_argument('--packages', type=Path, default=Path('F:/vulkan/unity-analysis/packages'))
    args = parser.parse_args()
    sys.path.insert(0, str(args.packages.resolve()))
    output = args.output.resolve()
    require(output.is_relative_to(ROOT / 'build'), 'Output must stay in the project build directory')
    selected = set(args.file_type or [2])
    totals, indices, entries = Counter(), [], []
    targets = {}
    for path in sorted(args.vfs.resolve().rglob('*.blc')):
        index = read_index(path)
        counts = Counter(row['file_type'] for row in index['entries'])
        totals.update(counts)
        indices.append({key: value for key, value in index.items() if key != 'entries'})
        indices[-1]['file_types'] = dict(counts)
        for row in index['entries']:
            if row['file_type'] not in selected:
                continue
            identifier = hashlib.sha256(row['name'].encode('ascii')).hexdigest()
            require(identifier not in targets, 'Duplicate logical payload; version precedence is not inferred')
            targets[identifier] = row['name']
            record = dict(row, index=str(path))
            if not args.inventory_only:
                payload = read_payload(path, row)
                destination = output.parent / 'bundles' / (identifier + '.ab')
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(payload)
                record.update(output=str(destination), sha256=hashlib.sha256(payload).hexdigest())
            entries.append(record)
        require(hashlib.sha256(path.read_bytes()).hexdigest() == index['sha256'], 'Index changed during read')
        print(index['label'], sum(counts.values()), 'entries; CRC and source identity checked', flush=True)
    require(indices, 'No VFS indices found')
    report = dict(indices=indices, file_types=dict(totals), total_entries=sum(totals.values()),
                  selected_types=sorted(selected), entries=entries, payloads_checked=not args.inventory_only)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, ensure_ascii=True) + '\n', encoding='ascii')
    print('Selected', len(entries), 'of', sum(totals.values()), 'indexed payloads')


if __name__ == '__main__':
    main()
