# The copy at unmap: a wrong attribution, and what the game showed

> 2026-10-12. This document first claimed that the stutter came from the game's mesh pools being
> copied at unmap and that layer 0.7.3 could not fix it. Both claims were wrong. The game session with
> 0.7.4 showed it within minutes, through the counters 0.7.4 had added. The first two sections say what
> was measured; the third keeps the wrong reasoning on record, with the reason it failed.

## What the game showed (PID 27188, layer 0.7.4, open world)

Data captured in the real game, started by the user at 15:28 under Vulkan: probe, inject, resources and
image hashing off, tracker timings on, `unmap_snapshot_all` off (the 0.7.4 default). Evidence:
`captures/pacing-0.7.4-20261012/` (two status reads and a buffer inventory) and
`captures/open-world-0.7.4/` (three frames).

| | 0.7.2, PID 41044 | 0.7.4, PID 27188 |
| --- | --- | --- |
| Copied at unmap | 69.7 GB in 213.8 s over the session: 0.33 GB/s | 27.9 GB in 7.7 s between the two reads: 3.6 GB/s |
| Buffers passed over by the staging-only rule | not counted | 0 of 144,103 |
| Present intervals over 16 / 33 / 50 ms | 203 / 51 / 17 in 5,093 (24 s in the foreground) | 24 / 3 / 2 in 87,005 (about 12 minutes) |
| Last 2,048 intervals | p50 3.2 ms, p99 40.0 ms, max 106 ms | p50 8.4 ms, p99 10.6 ms, max 14.5 ms |

- The user reports that the stutter is gone, after running the open-world route.
- The copies at unmap are as many as before; they are eleven times faster. That is the streaming-load
  path for write-combined mappings that 0.7.3 introduced and 0.7.4 contains.
- `passed_over` stayed 0: no buffer with further usage was ever inside an unmapped range. The mesh pools
  are mapped the whole time (`mapped: true` on memory type 4 in the inventories of both sessions, 22 and
  21 of them; write-combined where 0.7.4 reports it).
  The rule 0.7.4 added skipped nothing and contributed nothing to the result.
- A status read between the two shows the 64 most recent slow tracker operations: 52 of them are unmaps
  of 11 to 16 MiB taking 3 to 7.5 ms. The longest unmap of the session, 390 ms, predates the first read
  (start-up).

The two columns are not the same conditions: the 0.7.2 figures are a foreground-only window; the 0.7.4
figures span everything between two reads, with the game partly in the background (two attempts to find
a foreground window before the run failed, then the user ran). The size of the difference does not hang
on that, but a foreground-only window of 0.7.4 was not taken.

## Open world: every hash comes from the mapping

Three captures at frames 123798, 123930 and 124064 of the same session, the first of the open world:

- 1,182 to 1,213 draws, 1,155 to 1,186 indexed, one indexed indirect draw, 59 to 60 dispatches (three
  indirect), 35 render passes, 4 submits, 2 command buffers, one recording thread.
- Hash source `mapped` for every direct indexed draw (1,154, 1,164, 1,185); none without a hash. 18
  index buffers, 16-bit indices.
- 428 draws with more than one instance in the first frame, 13,294 instances, 675 at most. The character's
  draws have one instance.
- 19 of the mod's 21 sections match: the 12 LOD0 components in the passes known from the display scene,
  and 7 LOD1 sections with two draws each in render pass 5 (518 draws; the cascaded shadow pass by its
  look). Components 5 and 8 have no LOD1 draw in these frames.
- A capture takes about 208 ms round trip with resources off.

So G2 looks the same in the open world, G3 a holds there including LOD1, and the question of what to do
when a pool is unmapped does not arise.

## What this document claimed before, and why it was wrong

The buffer inventory of the 0.7.2 session lists a `generation` per buffer. Three 16 MiB pools had 1,491,
573 and 511; the 76 staging buffers 87 in all. From that the first version concluded that the pools were
what was copied at unmap (about 40 GiB), that the 0.7.3 benchmark had measured the wrong memory type,
and that the copy rule had to exclude buffers with further usage.

Two things were overlooked:

- `generation` is incremented by a copy at unmap and by every tracked buffer copy into the buffer. The
  pools receive thousands of tracked copies; that is what their counters show.
- Staging buffers are short-lived. The session created about 280,000 buffers and 251 were alive when the
  inventory was taken. An inventory cannot show where a volume went that was spread over buffers that no
  longer exist.

The inference was not checked against a direct count before the layer was changed, the documents were
rewritten and D-026 was recorded. The counters that would have checked it were added in the same change
and refuted it in the next session. The earlier diagnosis (`docs/pacing-tracker-session.md`, BUG-004,
D-025) was right as it stood.

## What stays in the code and what was reverted (0.7.5)

- Reverted: the default. Every whole transfer source within the limit is copied at unmap again
  (`unmap_snapshot_all = 1`). `unmap_snapshot_all = 0` restricts the copy to transfer-only buffers; it
  has no known use in this game.
- Kept: a buffer the mapping touched that is not copied loses its shadow knowledge at unmap. Before, a
  buffer above the limit or without the transfer-source bit could keep bytes from earlier buffer copies
  although the application had been able to overwrite them unseen.
- Kept: `counts.unmap` (copied, passed over, shadows dropped) and the `set` command for the two unmap
  settings in a running process.
- Kept: the test harness pins its settings against `dist/efmi_vk.cfg`.

0.7.5 passed the full suite in build/next and was deployed after observing game exit at 15:52.
The copied dist/ package passes the full suite and an actual-config inject smoke test; both DLLs
have SHA-256 f331ff7c3a390affbc7adfe617a1e1364f9d5441821b86f2ecdabf4ff39c7475.
No 0.7.5 game run is claimed. In the observed 0.7.4 session the two copy rules behaved identically.

## What remains

The volume is unchanged: about 28 GB of staging memory copied in twelve minutes, which nothing reads in
this game (index data comes from the mapping, image hashing is off). It costs about one percent of the
time and single unmaps of 3 to 7.5 ms, with the resource lock held. Not copying it would need a way to
know which staging buffer feeds a tracked destination before it is unmapped; that is not attempted. A
layer that replaces draws during normal play does not need the copy at all.
