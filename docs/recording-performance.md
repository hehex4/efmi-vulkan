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

## Identities that survive dashes, cuts and occlusion (1.7.0, D-100)

Sixth game session (1.6.9 deployed, 2026-10-21 09:45-): 17.5 ms mean frame interval (p95 19.5 ms,
no frame over 33 ms in ten minutes), 5.2 ms of recording work per frame, every texture resident at
full size, no eviction. The user still saw characters at close range revert to the game's mesh for
a moment. The status showed why: 42 identities created in ten minutes for four characters, each
one costing a frame or two of identification and the import of every component's bones, the
character being the game's meanwhile. Of the 42: nine were a second identity for the same object in
the same frame, its passes a cell (12.5 cm) apart; about twenty were identities expired after half
a second without a draw (off screen, occluded, a cut); the rest were jumps of 4 to 7 metres within
one frame (a dash, a camera cut), beyond the radius an identity may move per frame.

1.7.0, exchanged into the running game: within one recording an object's draws may differ by one
cell; an identity expires after 10 seconds without a draw (4 presentations and 10 s), not 0.5 s;
and when no identity lies within the radius but exactly one identified object of the package has
not been drawn since the last presentation, that one is taken to have moved (`identities_adopted`).
In the three minutes after the exchange the four packages created 8 identities in all, 4 of them
the first of each package, the rest after 10 seconds without a draw (a character switched away);
one adoption. `incomplete` draws (a merged draw whose required component had no bones imported
yet, a consequence of every new identity) went from 19392 to 5. The tests set the expiry through
`m2_identity_expiry_ms` (`EFMI_VK_M2_IDENTITY_EXPIRY_MS`).

## Character switches in the menu (1.7.0, D-101)

The user, same session: a switched character shows the game's mesh first, becomes the Mod after
about two seconds, then flickers back two or three times, every time. The log told the story: the
same fragment shader's derived material rules were made again and again for the same pipeline, with
5, then 6, then 8 rules. The rules were derived for the slots the Mod had bound so far; a plan that
bound a new slot (a texture section, a key) grew the set, every pipeline derived its rules again,
the variants' patches changed with them, and every variant compiled again, the frame gate showing
the game's mesh while it did: one flicker per new slot. The two seconds were the first compile of
the twenty or so variants the menu's pipelines need, one builder thread at a time.

1.7.0: the rules of a fragment shader cover every DX11 pixel slot it declares as one image, the
others absent, derived once per shader; a plan binding a new slot changes nothing. Exchanged into
the game: no rule derived twice, the counters of the next switches showing the first compile only.
For the first compile: the variants are created through one VkPipelineCache per device, loaded
from `pipeline-cache-<vendor>-<device>.bin` in the settings folder and written back when the
backend stops, and each package's variant cache builds on several threads (half the cores, 2 to 4).
Both need the game restarted; the saved cache helps from the second session on.

## Resources ready before the draw: the layer's own upload queue (1.7.0, D-103)

With the rules fixed the menu still showed the game's mesh first and flickered: the frame gate
(D-097) closes the package's frame whenever a draw binds a texture that has not arrived or a
pipeline variant still building, and in the menu new textures and pipelines come one after another
as the camera and the animation go on. A placeholder image for the textures on their way was tried
and withdrawn the same hour: grey parts are no better than the game's mesh. The user's requirement
is the original's behaviour: the first frame of a switched character is the Mod, and nothing ever
flickers. The original gets that by loading a resource when a draw first uses it, synchronously.

1.7.0 does the same, and makes the wait short:

- At device creation the layer requests one more queue of its own, from a transfer-capable family
  with a queue the application did not take (a dedicated transfer family first, then compute, then
  graphics; `upload queue:` in the log). Images and mesh buffers are shared between the graphics
  family and that family; the residency's loaders (three of them) read, record and submit on it
  themselves, signalling the device timeline, so an upload never waits for the application's next
  submission and several load at once. Without such a queue everything is as in 1.6.9. A loader
  that finds the staging, source or image memory budget held by the others goes again a moment
  later, behind the rest of the queue, for up to ten seconds; every other failure (a file that is
  no DDS, a size outside its own limit) is final at once, so a reload candidate with a broken
  texture file is refused as before.
- A draw whose texture is on its way waits for it (up to four seconds; `draw_waits`,
  `draw_wait_microseconds` and `draw_wait_max_microseconds` in `textures`), the publication read
  from the timeline's counter. A draw whose pipeline variant is still building waits for it too
  (`variant_waits`, `variant_wait_microseconds`). The frame takes longer; the character is never the
  game's mesh meanwhile, and the frame gate is left for failures and timeouts.
- One variant cache per device: a game pipeline with the same vertex layout and material bindings
  compiles once for every package instead of once per package, built on two to four threads through
  the pipeline cache kept on disk.

Identification is as the Core does it (SpatialIdentification.ini): an object's first frame is
skipped, drawn neither as the game's nor as the Mod's, which the eye does not notice, where 1.6.9
drew the game's mesh for that frame; from the next frame on an object still unidentified is the
game's, as there. A package's draws are skipped the same way while it goes to the GPU (`preparing`).

Verification: The full suite passed on the final source (build/next/run_all-1.7.0.out: 35 steps; character replacement 357/357, live INI rendering 76/76 with the broken-DDS candidate refused, pipeline variants 63/63, Vulkan validation 76/76, both replays clean). The upload queue, the waits, the shared variant cache, the
pipeline cache and the first-frame skip take effect when the game is next started with 1.7.0; the
next session's `textures.draw_waits`, `draw_wait_max_microseconds`, `variant_waits` and
`gated_frames` say whether a switched character ever waited long or was gated.

## Presentations without a draw, and the input sample (1.7.1, D-104)

Seventh game session (1.7.0): in fullscreen the game presented four to ten times per rendered
frame (frame generation; a frame presented again), nothing drawn between two presentations, and
the layer ran the Present lists and the owned Present work only after a rendered frame: a Mod's
key panel was in one presented image of four and flickered. 1.7.1 runs the Present lists once per
rendered frame, as the original runs them once per frame, and gives a presentation without a draw
a replay of the last Present's drawing when that Present drew into the back buffer: the lists run
again from the variable state the Present started from, into the new image, and every variable
and parameter is restored. A Present that did not draw into the back buffer (Typhoeus' per-frame
GPU work) is not replayed: the generated frames cost nothing there.

The keys, the window and the cursor were sampled per package per presentation (256 key states
each); once per presentation now, the Present work went from about 1.7 ms to about 0.5 ms per
presentation with 22 packages. What remains of the frame cost, measured in the session: 91 fps
with the replacement against 120 without; per rendered frame 3.9 ms of recording on the
rendering thread, 2.0 ms of it the INI plan (67 us per matched draw), 0.4 ms bones, 0.65 ms
index hashing, 0.16 ms variants, 0.3 ms snapshot and emit.

## The INI plan cut offline (1.7.2, D-106)

The user's bar after the seventh session: 120 fps, the frame rate without the replacement.
Measured there: 91 fps with it, 3.9 ms of recording work per rendered frame on the rendering
thread, 2.0 ms of it the INI plan of about 30 matched draws (67 us each, about a hundred commands
of the Core's lists per draw), 0.4 ms bones, 0.65 ms index hashing, 0.16 ms variants.

Measured offline with the perlica bundle (vk14) through two profiling builds that the build
scripts now offer (`EFMI_VK_COMMAND_TIMING=1`: every command class timed, exclusive of its
children, through a slot found once per bridge command; `EFMI_VK_PHASE_TIMING=1`: a dozen phase
timers around the plan and the Present, nearly free), the commands themselves took about 6 us of
a matched draw's 33 us of list execution: the rest was the planner's own frame around them.

What 1.7.2 changed, each measured in the replay:

- The command kinds decided once at adaptation (a word of bits per bridge command) instead of a
  chain of up to fifteen `dynamic_cast`s per command per draw: list execution 33 to 21 us per
  matched draw, a package's Present 76 to 36 us per presentation.
- The Core extension's native handlers read the Core's variables, lists and resources through
  pointers found once per generation instead of a string built and hashed per access.
- The variant of a (pipeline, draw, material plan) is remembered per generation; the variant
  cache's key building and lookup cost 5 us per replaced draw.
- The plan item's stream or image index is found by the resource object the compiler hands out
  for a declaration, not by a path normalized and lowered per item (10 us per matched draw).
- A region a section acts on has its edge fingerprint read every draw until it matched 120 times
  in a row, then every 16 frames like the others; a mismatch starts over.
- A native command (an if, a variable assignment, a pool variable, abort, preset) whose arguments
  the CPU evaluates stays the original's own object in the list: the bridge only budgeted it.

perlica replay (vk14, 120 frames), per matched draw: list execution 33.2 to 14.5 us, the whole plan 41.5 to 22.4 us; a package's Present 76 to 36 us per presentation; the 22 packages' Present lists 0.48 ms per presentation (with the Core's GUI text of the first minute).

Still there: the index hashing of the draws that match no section; the Present lists of every
package (the Core's GUI list runs for each).

## The bones on the GPU (1.8.2, D-117)

In the 1.8.1 game session the bones were the largest single item: 17-33 us per matched draw, the
CPU reading each component's two poses (25-60 KB) out of the game's write-combined bone buffer with
streaming loads, then copying the master into every recording's arena; offline, with the same data
in cached memory, the read is 1 us, so the bus is the cost and no CPU change removes it. The Core
never reads bones on the CPU: its MergedSkeleton_BoneDataImporter compute shaders copy them on the
GPU. The layer now does the same (layer/src/bone_merge.cpp, layer/shaders/bone_merge.hlsl): per
identity a ring of eight merged skeletons in device memory, a slot per frame, one job per drawn
component (the game's bone buffer as bound, the entry's row offsets, the component's range and LOD
remap), all jobs dispatched in one submission of ours on the application's queue before its own
(character_submit), a barrier to the vertex shaders, a timeline for completion and for the slot's
reuse, a wait when the other graphics queue drew. The draws bind the ring slot
(DrawSnapshots::append_external): no copy of the skeleton per recording any more. The CPU import
stays as the fallback and for `m2_gpu_bones = 0`; the probe's `set gpu_bones=0/1` switches at run
time, so one scene can be measured both ways (scratch ab_interleaved.py). Offline the pictures of
vk03, vk04 and vk01 are identical either way; the gain is a game measurement.

## A sampling profiler, and what it found (1.7.4, D-108)

`scripts/sample_profile.py` samples a process of the project (the replay, a sandbox program) about
a thousand times a second: it suspends each thread that ran in the last half second, reads its
context, walks its stack with dbghelp and names the frames through the PDBs, then prints the
functions by self and inclusive samples and, with `--dump`, writes every stack for later queries.
`--delay` skips the start-up and the Core's notifications of the first minute. The replay runs
without the validation layer with `EFMI_VK_REPLAY_NO_VALIDATION=1` (the validation layer's calls
dominated a profile otherwise), and the profiling builds (`EFMI_VK_PHASE_TIMING=1`) compile the
INI runtime with debug information so its frames are named. The replay's timing is only a guide
for the CPU work; the game's numbers come from the probe in a session.

What the profile of the perlica bundle showed after D-106, and what 1.7.4 does about it:

- The replay snapshot of D-104 (every variable's value before the Present lists, for a
  presentation without a draw) was taken for every package at every presentation, a pair per
  variable through the hash table: 5% of the busy samples. It is a flat copy over a variable list
  made once, and only for a package whose last Present drew into the back buffer.
- Allocations were a sixth of the recording: the plan, the file plan, the program's draw list,
  material and signature, a dozen containers of `record()`, the bone ranges. They are kept per
  generation (or per planner) for their capacity and swapped with the caller's; the pixel slots
  of a plan are a small sorted vector instead of a std::map; an external list's name is copied
  only for a plan item that is kept.
- A draw's twenty pixel slot queries looked up its pipeline, the fragment module's registers and
  its descriptor set each time, and the same slots were asked again by the Core and RabbitFX: the
  draw's pipeline, registers and last set are found once, and each slot's answer is kept for the
  rest of the draw.
- A Mod with GPU work at every Present (Typhoeus) created and destroyed a command pool and a fence
  per submission (about 50 us per frame): completed submissions give theirs back to the next ones.

Interleaved A/B against 1.7.3 on the perlica replay without validation, three rounds each, medians per frame (the machine under another game's load, so the ratios count): recording 1977 to 1621 us (0.82), the INI plan 1224 to 972 us (0.79), the 22 packages' INI Present 806 to 567 us (0.70), Typhoeus' Present GPU submission 107 to 64 us (0.60), the replay's frame interval 0.94.

What the profile leaves: the INI lists themselves (each Mod's key panel runs about two hundred lines
at every Present, as under the original; 14 us per package), the original interpreter's commands
in the matched draws' lists, and the driver calls of the replaced draws.
