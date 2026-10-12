# Buffer contents of a captured frame (layer 0.6.0)

> Sandbox only so far. Nothing here has run in the game.

A capture of 0.5.1 says which buffer ranges a draw has bound, and a later `read_buffer` returns what
those ranges hold at the time of the request. For a buffer the application rewrites every frame that is
not what the draw used (LIMIT-008: in the game 1 of 12 reads was still right a moment later). From 0.6.0 a
capture can keep the bytes itself, and it answers a second question at the same time: were the bytes
already there when the draw was recorded?

## What a request asks for

Fields of `ProbeClient.capture(snapshot=...)`; on the pipe they are flat keys of the `capture` command.

| Field | Meaning | Default, bound |
| --- | --- | --- |
| `hashes` | region hashes; the indexed draws with one of them are selected | at most 4096 |
| `all` | select every indexed draw instead | off |
| `max_range` | a uniform range above this size is listed but not read | 64 KiB, at most 1 MiB |
| `budget` | bytes one capture may keep inside the observed process | 32 MiB, at most 256 MiB |
| `follow` | `set`, `binding`, `offset`, `count`, `stride`, `bytes`, `entry`, see below | off |

For a selected draw the layer reads every uniform buffer range of sets 0 to 3 (dynamic offsets applied).
With `follow` it also reads `count` 32-bit values at byte `offset` of the uniform range at (`set`,
`binding`) and, for each value, `bytes` at `value * stride` of every storage range of the draw that such
a window fits in. A value whose window fits nowhere is listed with `no_storage_range_fits`. `entry` bytes
at the start of that uniform range are read as an item of their own: a range of 64 KiB holds the entries
of many draws, and whether the whole range changed says little about the entry of this one.

For the game this is `set 2, binding 0, offset 80, count 2, stride 16`: the instance entry and the two
bone offsets in its constant 5 (`docs/dx11-baseline.md`). `scripts/collect_probe.py --snapshot mod
--follow instance` uses it with 256 bytes of entry and 12,288 bytes per offset (256 bones of three rows;
the number of bones of a component is not in the entry, so this is a window, not the palette).

## What comes back

The capture has a `snapshot` summary (`draws`, `blobs`, `bytes`, `omitted`, `record` counts), and each
selected draw a `snapshot` list in this order: uniform ranges by set and binding, each followed by its
`entry` item if it is the follow source, then the windows. An item names `set`, `binding`, `element`,
`buf`, `offset`, `size`, `src` (`mapped`, `shadow`, `none`), `crc` (CRC-32C of the bytes at submit),
`id` and `record`.

`id` is the number of the kept bytes; `ProbeClient.snapshot(seq, id)` or the `snapshot` command returns
them while that capture is the layer's latest one. Equal ranges of several draws share one id. `id` is
null when the budget was exhausted: the range was still read, checksummed and compared.

`record.state` compares with the read taken inside `vkCmdDrawIndexed`:

| State | Meaning |
| --- | --- |
| `equal` | same range, same checksum at record and at submit |
| `different` | same range, the bytes changed after the draw was recorded |
| `other_range` | the slot held another buffer range when the draw was recorded |
| `unreadable` | not CPU-visible at one of the two times |
| `not_recorded` | no read at record time: the draw was recorded before the captured frame began, or its index data was not readable or had another hash then |

Only a checksum is kept from record time, so `different` says that bytes changed, not which.

## Timing and cost

- Submit: all draws of a command buffer are read when `vkQueueSubmit` is called, under the shared
  resource lock, after the recorded buffer copies of that command buffer were applied to the shadows.
- Record: only while a capture with a snapshot is in its frame. The draw path tests one atomic flag
  otherwise. In that frame every indexed draw has its index range hashed once more (to know whether it is
  selected), and a selected draw has its ranges read and checksummed.
- Secondary command buffers: record-time reads move with the draws into the primary.
- Memory: the kept bytes stay until the next capture replaces them; `budget` bounds them.

## Limits

- Indexed draws only; sets 0 to 3; uniform and storage buffers. Texel buffers, push constants and
  indirect draws are not covered.
- With resource tracking off (`resources = 0`) a draw is selected but has no ranges.
- The submit-time bytes are what the CPU side holds when the command buffer is handed over. Writes the
  application makes to mapped memory between submit and execution, and anything a compute pass
  produces, are not seen.
- A selection by hash needs the index bytes at record time. The frame in which an index buffer is
  rewritten by a copy of the same command buffer has `not_recorded` for the draws concerned.
- `follow` reads values of 32 bits in the byte order of the machine and knows one rule per capture.

## Verification on 2026-10-12 (sandbox)

The sandbox gives every uniform slot 96 bytes: a frame number written before recording (byte 64), for
odd slots the same number written again after recording and before submit (byte 72), and two element
numbers into its storage ranges (byte 80). `probe_test.py` checks for every entry, thread count and
descriptor mode that the kept bytes are those of the submit (the late value is in them), that exactly
the odd slots report `different` for the whole range while the entry in front of the late bytes reports
`equal`, that the windows are located by the values and equal the buffer, that ids are shared and that
the bytes of an earlier capture are refused. `limits_test.py` checks the budget, the size limit, a window
that fits nowhere, clamping, malformed hash lists and the reduced tracking mode; `evidence_test.py` the
packed file written by the collector.
