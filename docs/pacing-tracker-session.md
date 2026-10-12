# Probe tracker stalls and mapped-read candidate (2026-10-12)

## Real-game observation: 0.7.2 still stutters

The user reports renewed stutter after the smooth 0.7.1 passthrough comparison.
User-started Endfield PID 41044 began at 14:22:01 local; the log confirms 0.7.2
probe/inject at 14:22:02.864, resources and image hashing off, tracker profiling on.
Unmap snapshots remain 16 MiB and the shadow mask remains 0x40. No frames were captured.

The first read was background and its presentation history was stale relative to the
ongoing upload work. An automatic wait found no stable foreground within 80 seconds.
Neither is a gameplay pacing sample. After the user explicitly reported readiness,
13 status reads two seconds apart cover 14:44:14.809560-14:44:38.839882 local:

| Observation | Result |
| --- | --- |
| Elapsed time | 24.030379 seconds |
| Foreground | 240/240 polls, all 13 endpoints before and after querying |
| Present interval frames | 99455 through 104547 (5,093 intervals) |
| Intervals over 16 / 33 / 50 ms | 203 / 51 / 17 |
| Largest retained interval | 129.125 ms, frame 101592 |
| Final 2,048 intervals | p50 3.233 ms, p99 39.976 ms, max 106.326 ms |
| Hash time / captures | Both zero at every endpoint |
| Largest status round trip | 30.455 ms, including waits, not an exact render-lock duration |

These are host present boundaries, not GPU/display frame times. Percentiles describe
the final rolling window only. The script neither captures frames nor sends input or focus.

## The snapshot path overlaps the largest spikes

During those 24 seconds, buffer-unmap tracking reports 5,141 calls, 3,712,749,314 copied
bytes (3.46 GiB) and 11,625,362 accumulated wall microseconds. Buffer-copy recording adds
3,257,917 microseconds while copying only 2,076,320 bytes. Submit assembly adds 222,594
microseconds, including 175,476 from buffer-submit work; draw recording adds 133,627.
The totals include waiting and descheduling, can overlap across threads, and are not additive.

Frame 101592's 129.125 ms interval overlaps three consecutive unmap operations of
43.633, 43.805 and 44.007 ms. Their nonoverlapping portions inside the present interval
cover 128.807 ms. A buffer-copy record operation taking 43.730 ms with zero copied bytes
overlaps the middle unmap. Frame 104497 spans 86.423 ms and overlaps a zero-byte copy-record
operation lasting 96.087 ms across two unmaps. The code holds res_mx exclusively through
snapshot reads, while copy recording requires a shared lock. This is strong evidence for
expensive snapshot reads and associated global-lock contention as a major contributor.
It does not establish the cause of every spike or a post-fix game result.

Of 41 unique retained present peaks in the measured frame range, 31 overlap retained slow
work. The 64-event history adds 1,112 entries; 11 of 12 query gaps add more than its capacity.
Missing overlap therefore cannot exclude work lost to rollover. QPC timestamps are not UTC.

Evidence: captures/pacing-tracker-work-20261012/, with 21 verified fingerprints in its
manifest. world-144412/ contains every raw sample, foreground polls and analysis.json.
The separate buffer inventory contains 76 transfer-source-only buffers on memory type 2;
the game probe does not expose those pages' cache policy in 0.7.2.

## 0.7.3: preserve the bytes and accelerate WC reads

At successful map tracking, VirtualQuery classifies the complete mapped range. A range
wholly inside one committed PAGE_WRITECOMBINE region is marked WC; unknown, mixed and
ordinary cached mappings retain memmove. Classification is recomputed on remap and cleared
on unmap. On CPUs supporting SSE4.1, WC reads of at least 64 bytes use aligned MOVNTDQA
loads and ordinary unaligned stores into the CPU destination. Prefixes and tails stay inside
the exact requested range, with MFENCE before and after the streaming copy. CPU capability
is checked independently of CRC's SSE4.2 support.

This applies to unmap snapshots, record-time fallbacks, submit reads and direct probe reads.
No snapshots, bytes, hashing capabilities or fallback semantics were removed; shadow
self-copies keep memmove. Resource-lock ordering is unchanged. status.mapped_read reports
the available strategy; buffers[].mapped_write_combined reports the current full-buffer map
classification. The capability string alone does not establish that a particular copy used it.

Instruction constraints and ordering follow the [Intel SSE4 reference](https://www.intel.com/content/dam/develop/external/us/en/documents/d9156103-705230.pdf)
and the [MSVC intrinsic list](https://learn.microsoft.com/en-us/cpp/intrinsics/x86-intrinsics-list).
CPU fences do not replace the application's Vulkan synchronization or noncoherent cache handling.

## Local verification, separate from game performance

The production helper was measured in a separate headless Vulkan process on the same
NVIDIA GeForce RTX 5060 Laptop GPU, without this layer injected. Every method copies 8 MiB;
eight measured iterations per method follow warmup, alternating order. Every output byte is
checked against the independently generated source pattern. Medians:

| Vulkan memory type | Flags / OS policy | Ordinary copy | Production helper |
| --- | --- | --- | --- |
| 2 | 0x6 / PAGE_WRITECOMBINE | 24.023 ms | 1.270 ms |
| 3 | 0xe / cached | 0.338 ms | 0.334 ms (same ordinary path) |
| 4 | 0x7 / PAGE_WRITECOMBINE | 226.258 ms | 150.952 ms |

Sources/output: build/vulkan-mapped-benchmark.cpp and .txt, linking layer/src/util.cpp.
A separate VirtualAlloc WC test of the production helper gives 24.740 versus 1.373 ms
(build/production-wc-benchmark.cpp and .txt). These are local copy experiments, not an
end-to-end speedup. Device-local mapped reads remain expensive, as type 4 demonstrates.
The type-2 result supports the suspected mechanism; it does not inspect the old game's pages.

The first build caught a mistyped JSON writer method and produced no candidate. After fixing
that call, build/mapped-read-regression-final-20261012.txt passes the complete suite:
unit 88, native 118, 17 probe configurations 503-518 each, modes 40, limits 148, timing 126,
Vulkan validation 76, MCP 47, evidence 78. New native coverage uses actual WC pages bounded
by inaccessible pages, every source alignment, unaligned destinations, short/tail reads,
ordinary fallback, self-copy, map offsets, remap policy, submit priority and unmap-before-copy.

The game had exited before replacement, confirmed again before backup and copying. Old dist/
is in build/backups/before-0.7.3-20261012-150100/. Exact candidate files from
build/candidate-0.7.3-20261012/ are now in dist/. Both DLLs have SHA-256
4cb8fdc1c111530cdbc8d76c9c2924c1cf61ec6fa701ba64a18edc5b36548c85.
Actual-config inject smoke passes for the candidate (build/tests/sandbox-wcxt2ysn/) and
deployment (build/tests/sandbox-zg_0n_36/): 0.7.3, WC strategy and mapped WC buffers reported,
five correct draw hashes, probe/resources off/hash off/profile on, default buffer evidence.
The historical game log is unchanged. No launcher edit or automatic game launch/stop occurred.

T-020 / ISSUE-005 remains open. Next: a short user-started same-route foreground comparison
with 0.7.3, confirming its loaded version/settings and collecting status-only timing. No
30-minute requirement. Keep profiling enabled for this comparison; test its disabled setting
later if needed. Passthrough remains the user-confirmed smooth fallback.
