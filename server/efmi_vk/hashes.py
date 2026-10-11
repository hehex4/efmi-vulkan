"""CRC-32C (Castagnoli) as XXMI uses it for region hashes.

XXMI calls ``crc32c_hw(0, ptr, size)`` over the bytes of the index buffer
region a draw uses. That function inverts the running value on entry and on
exit, so with a seed of 0 it is the standard CRC-32C of the data.
"""

from __future__ import annotations

_POLY = 0x82F63B78


def _make_tables():
    base = []
    for n in range(256):
        c = n
        for _ in range(8):
            c = (c >> 1) ^ _POLY if c & 1 else c >> 1
        base.append(c)
    tables = [base]
    for k in range(1, 8):
        prev = tables[k - 1]
        tables.append([(prev[n] >> 8) ^ base[prev[n] & 0xFF] for n in range(256)])
    return tables


_T = _make_tables()


def crc32c(data, seed: int = 0) -> int:
    """CRC-32C of ``data`` continuing from ``seed`` (a previous result, or 0)."""
    t0, t1, t2, t3, t4, t5, t6, t7 = _T
    mv = memoryview(data).cast("B")
    crc = seed ^ 0xFFFFFFFF
    n = len(mv)
    i = 0
    end8 = n - (n % 8)
    while i < end8:
        a = crc ^ (mv[i] | (mv[i + 1] << 8) | (mv[i + 2] << 16) | (mv[i + 3] << 24))
        crc = (
            t7[a & 0xFF]
            ^ t6[(a >> 8) & 0xFF]
            ^ t5[(a >> 16) & 0xFF]
            ^ t4[a >> 24]
            ^ t3[mv[i + 4]]
            ^ t2[mv[i + 5]]
            ^ t1[mv[i + 6]]
            ^ t0[mv[i + 7]]
        )
        i += 8
    while i < n:
        crc = (crc >> 8) ^ t0[(crc ^ mv[i]) & 0xFF]
        i += 1
    return crc ^ 0xFFFFFFFF


def index_region(bind_offset: int, index_size: int, first_index: int, index_count: int):
    """Byte range of an index buffer that one indexed draw reads.

    Mirrors GetIndexBufferRegionOffset / GetIndexBufferRegionSize of XXMI:
    the bind offset plus ``first_index`` indices, ``index_count`` indices long.
    """
    return bind_offset + index_size * first_index, index_size * index_count


def format_hash(value: int) -> str:
    return f"{value & 0xFFFFFFFF:08x}"
