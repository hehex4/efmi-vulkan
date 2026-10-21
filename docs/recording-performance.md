# The recording path's cost in the game, measured and cut (1.6.9, D-099)

2026-10-21, fifth game session (1.6.8 deployed, PID 31884, one session of 70 minutes). The user
reported characters taking two to three seconds to show after a switch in the character menu,
characters within LOD1 range reverting to the game's mesh now and then, and performance better than
1.6.7 but not acceptable. Everything below was measured in that session through the probe and
changed through eight exchanges of the replacement module (`build/hot-swaps.json`); the layer was not
touched until the end.

## Baseline

Replacement switched off through the probe for 75 seconds in the open world, then on again, same
scene: frames of 20.9 ms mean (p50 20.4) without it, 41-42 ms with it. The game on Vulkan runs the
scene at about 48 fps on its own; the replacement halved that.

## Where the time went (1.6.8)

| Item | Measured | Cause |
|---|---|---|
| Texture target at the 64 MiB floor | 889 MiB free below the 1 GiB reserve; 164 evictions in 16 minutes, `pressure` always on | Every image not bound for 2 seconds was evicted: a LOD change or a menu switch reloaded the character from disk, and the frame gate showed the game's mesh meanwhile |
| Index region hashing | 12.5 us per indexed draw, 13 ms per frame at 1050 draws (two recording threads) | The edge fingerprint is two uncached 64-byte reads over the bus for every draw. Full hashes were only 2.4% of calls (98% regions seen first, 1.8 KB on average; a changed buffer generation: 0) |
| Hash table | 20 ms per frame at first | 4096 entries emptied when full; the open world draws more regions than that per frame |
| `owner` stage (arcane) | 142 us per matched draw | `owner_for` drops another package's owner that a reset left unused: every reset clears `used`, so the first package to record destroyed the others' 64 MiB arenas and their workers made new ones, every recording |
| INI plan | 50-63 us per matched draw | About a hundred commands per draw (Si: 21 `run`, 19 `if`, 23 `checktextureoverride`, 31 assignments, 6 bindings); leaf commands cost 0.05-0.1 us, the rest is the adapter's dispatch (a chain of `dynamic_cast`s per command) and list execution overhead |
| Bones | 18-25 us per matched draw | Two reads of about 30 KB of bone matrices over the bus per import, and the import |
| Owned Present | 1.3-1.6 ms per frame | 22 packages' Present lists and the owned submissions |
| Hot-swapped module | owned GPU work unavailable | The tools were looked up beside the module, not the layer |

About 150-180 matched draws per frame with four characters on screen (every pass of every
component), 1050-1150 draws per frame in all.

## What changed

Module (exchanged into the running game, verified by the next reading):

- Reserve default 512 MiB (a sixteenth of the heap), images evicted only after 600 frames idle:
  target 1.2 GiB, 0 evictions for the rest of the session, 1.3 GiB still free.
- Hash table 65536 entries per thread, trimmed of entries idle for 8 frames instead of emptied;
  regions hashed again every 1024 frames.
- The fingerprint is read every draw only for a region whose hash a package acts on (a stale hash
  there would replace the wrong object); for the others every 16 frames (a stale hash there delays a
  match by that much at most). Hashing: 12.5 to 3.9 us per draw.
- Another package's command owner stays in a recording until 8 resets found it unused: `owner`
  stage 142 to 0.3 us per draw.
- Snapshot bone copies reused by key in any order; arenas in cached host memory.
- A backend disabled through the probe hashes nothing (the baseline measurement).
- The owned GPU tools are looked up beside the layer DLL when the module lives elsewhere.
- Status: `record` (recording calls and microseconds, hashing, full hashes by cause, fingerprint
  reads and skips, snapshot timing), per package `stage_us` (lock, identify, bones, plan,
  materials, variants, owner, append, snapshot, emit), `ini.draw_execute_us` and
  `ini.draw_compile_us`, `snapshot_copies`; per-command-class timing with `EFMI_COMMAND_TIMING`.

Layer (needs the game restarted): a buffer remembers its last 32 writes by range, and a region's
generation is the newest write overlapping it, so a copy into one part of a pool buffer no longer
makes every region in it hash again (this session showed no such copies; the open world of other
areas may).

## Result

Same scene, 4513 frames at the seventh exchange: 24.5 ms mean (p50 24.0, p95 29.0), 11 frames over
33 ms in 4513; the layer's recording work 15.8 ms per frame over two threads (33 before), almost all
of it the matched draws: plan 50 us, bones 18 us, identify 3.6, variants 3.4, snapshot 3.3, append
2.7, emit 1.9, owner 0.3. Against the 20.9 ms baseline, 3.6 ms remain.

## Next

- The INI dispatch: a command's kind decided once when it is adapted, not by a chain of
  `dynamic_cast`s per execution; the list execution overhead. Measurable offline: the replay runs the
  same code with the same `stage_us`.
- Bones: a GPU-side remap (the variant's vertex shader reading the game's bone buffer through a
  per-component index table) would remove the import and the snapshot copies altogether.
- One pipeline variant cache per device instead of one per package, and a VkPipelineCache on disk:
  a character switch waits for its variants to compile (2-3 seconds in the menu).
- Owned Present: 22 packages' Present lists per frame.
