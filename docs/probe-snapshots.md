# Buffer contents of a captured frame (layer 0.6.0)

> Verified in the sandbox; first run in the game on 2026-10-12 (last section).

A capture of 0.5.1 says which buffer ranges a draw has bound, and a later `read_buffer` returns what
those ranges hold at the time of the request. For a buffer the application rewrites every frame that is
not what the draw used (LIMIT-008: in the game 1 of 12 reads was still right a moment later). From 0.6.0 a
capture can keep the bytes itself, and it answers a second question at the same time: were the bytes
already there when the draw was recorded?

## Waiting through empty present intervals (0.8.1)

`ProbeClient.capture(wait_draws=True, snapshot=...)` and `collect_probe.py --wait-draws` opt into
waiting for a present interval containing at least one draw command. On the wire this is `wait_draws: 1`.
The layer advertises `capture_wait_draws` in status.features; the client refuses older layers for
this option instead of silently accepting their old behavior. The default remains one interval,
including an empty one. The MCP capture tool does not expose this new option yet; use the CLI/client.

Snapshot recording stays armed across skipped intervals. Draws recorded before an intervening
present but submitted later retain their record-time comparisons. Each skipped interval's capture
contents/budgets are discarded, while normal buffer tracking still applies its writes. The returned
frame, draw list and statistics cover one selected interval, not the sum of skipped intervals.
`wait_draws` and `skipped_empty_intervals` describe selection in the frame and collector summary.
The existing timeout still cancels the request and clears its record-time work; repeated empty
presents are not success. A present interval is not automatically a displayed or complete scene frame.

Motivation and current game evidence: `m2-game-recipes.md`. Native tracker tests simulate two empty
intervals during command recording, including upload/dispatch submission and a buffer write between
record and submit. They check retained comparisons, single-interval statistics, default empty captures
and timeout cleanup. GPU probe tests exercise snapshots with the option through both entries and all
descriptor modes. Full suite on 0.8.1 build/next passes; the game still holds 0.8.0 at this checkpoint.

For the next user-started LOD1 observation, combine `--wait-draws --snapshot mod --follow instance`
with `--require-section '*_LOD1' --require-color --attempts 8 --pipeline-limit 128`. This only requests
observation. It does not change gameplay, enable replacement drawing or infer an on-screen LOD.

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

## First run in the game (2026-10-12)

Data captured in the real game: PID 36420, Vulkan, character display scene with the character of
`arcane fes 1.2`, layer 0.6.0 through the launcher's injection, probe mode, full tracking. 120 frames per
second and a working set of 11.8 GB that did not grow during the measurements. Nothing was drawn or
replaced. Bundles: `captures/second-probe` (the 45 draws of the mod's 12 components, frame 8313) and
`captures/second-probe-all` (all 210 indexed draws and all 117 pipelines of frame 16127); the series are
`series.json`, `palettes.json`, `edge.json` and `edge-component4.json` in the first of them. About 1,100
further captures were taken for the series.

- **Selection at record time works.** No item of any capture is `not_recorded`: the index bytes of every
  draw are readable when it is recorded.
- **Instance entry** (256 bytes at set 2 binding 0): equal at record and at submit in 3,882 of 3,882
  comparisons. Constant 3 is (-300, 300, -300), constant 5 (bone offset, previous bone offset, 258, 1.0f):
  the structure seen under DX11.
- **Uniform ranges** of sets 0 and 2: 29,967 comparisons, none different. The material constants of
  set 1 are in a buffer whose bytes never pass the CPU (`src: none`) and cannot be compared.
- **Bone rows at submit** are final: 240 of 240 windows equal a read taken 1 to 3 ms after the capture.
  A window starts with three rows of unit length and a translation near the character
  (-300, 301, -300).
- **Bone rows at record time**: a component's own palette is complete when its first draw is recorded;
  what lies behind the palette in the buffer is not. Component 4, whose mesh uses bone indices up to 53,
  window at its current offset, 60 frames each:

  | Window | Windows compared | Frames with a difference |
  | --- | --- | --- |
  | 50 bones (2400 bytes) | 488 | 0 |
  | 54 bones | 482 | 0 |
  | 55 bones | 492 | 0 |
  | 56 bones | 488 | 21 |
  | 57, 60, 70 bones | 484, 488, 484 | 13, 26, 23 |

  Every difference is in the first pass that draws the component (the shadow pass) and at the current
  offset, never at the previous frame's. With the preset's 12,288 bytes the windows of the last two
  palettes reach far behind the block and differ in 30 of 40 frames; that is the window, not the data.
- **How many bones to read** is in the mod: `Pool_MergedSkeleton_Component_VertexGroupCounts` of
  `arcane.ini` is 45, 86, 42, 15, 54, 16, 8, 12, 80, 117, 23, 1 for components 0 to 11 (499 in all), and
  the meshes read from the game give exactly these numbers as highest bone index with a nonzero weight
  plus one. The palette in the buffer is at least that long (component 4: 55 against 54).
- **Layout**: the 12 palettes of the character are written next to each other, a new block of about 1782
  elements every frame further along the buffer, which is used as a ring. Their order inside the block
  changes between frames (component 4 is directly behind component 2 in 254 of 420 frames), so the
  distance to the next palette says nothing reliable about a palette's size.

What this settles and what not: for this scene the CPU can take the instance entry and the bones of a
component from mapped memory at the moment its draw is recorded, as long as it reads the number of bones
the mod states and nothing behind them; reading at submit is valid as well. One scene, one character,
standing still; open world, combat, several instances and LOD1 are not covered. The check against a later
read shows that nothing changed after submit within milliseconds, not what the GPU read.
