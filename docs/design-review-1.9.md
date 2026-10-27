# Design review after 1.9.2 (2026-10-26)

The user, after the 1.9.2 session: the designs still have many problems; think through how to solve
them properly instead of finding them one session at a time. This document names what the session
showed, the causes those symptoms share, and the changes that remove the causes. Each change is
made test first: a check that fails on 1.9.2 for the reason given, then the change, then the suite.

## What the 1.9.2 session showed (PID 13144, user-started, DLSS Frame Generation on)

1. Typhoeus (Nya Tech & GUI): the wings lay on the ground in some frames and were in place in others,
   with the bones imported on the GPU; with the CPU import (the probe's `gpu_bones=0`, 18:45:23,
   diagnosis only) they stayed in place (BUG-060).
2. After the user turned Frame Generation off and on, the game became very slow. The log shows the
   two swap chain recreations (18:46:41, 18:47:16) and then an F10 exchange at 18:47:33: every
   package that had ever been active brought its successor up at once, with the images the live one
   showed loaded again; 2.45 GB of textures, 141 evictions, about 5 to 12 frames per second (BUG-061).
3. Switching characters and a Mod's Ctrl+K options stuttered: matched draws waited for textures'
   first loads, 380 to 570 ms of waiting in a 2 s window; reading one 64 MiB 8192x8192 BC7 map took
   30 to 70 ms although the file cache held it.
4. The layer's work took 59% of the render thread's busy time.

## Cause A: the layer's GPU work keeps an order of its own instead of the game's

Under DX11 the original issues its work (the merged skeleton's import, a Mod's compute shaders) on
the context of the draw that needs it, so the work runs in the game's order by construction, whatever
queues the driver uses below. The layer cannot put a dispatch inside the game's render pass, so it
submitted its work in submissions of its own in front of the application's: the bone dispatches
(1.8.4), the owned work of replaced draws (bound to one queue, D-087), the Present work (on the
presentation's queue). A submission of ours does not wait for what the application's batch waits
for, so on the game's two queues our work ran ahead of or beside work it depends on. Every attempt
to fix that with waits of our own deadlocked the GPU (BUG-050, BUG-052), and the rule became that
nothing crosses queues (D-091). That rule forced the bone import into per-frame ring slots, which
lost the original's persistent merged skeleton, and BUG-054's repair of that read the game's bone
buffer again at a later submission, for a component another recording had imported, possibly in
the frame before.

The game keeps a bone region valid from its CPU write (before the draw is recorded: the CPU import
reads it at the draw) until the GPU finished that frame; while the GPU runs frame N the CPU already
writes frame N+1 into regions frame N-1 used. A read of an earlier frame's region at a later
submission sees another frame's data (BUG-060). The CPU import reads at the draw and is not affected.

**Change (D-127).** Our GPU work rides in the game's own batches: a command buffer of ours goes at
the front of the batch that carries the recording needing it, and the batch's waits on semaphores
that a queue signals include our stages, so the game's own semaphores order our work across its
queues exactly as they order the game's. We add no wait and no semaphore between queues; a
timeline of ours, signalled after the application's batches in the same call, only tells when our
command buffers can be reused (D-091 stands: nothing of ours can block a queue).

For the bones this restores the original's model:

- One persistent merged skeleton per object on the device (the Core's
  `ResourceMergedSkeletonDataRW`), written only by imports. An import is dispatched with the
  recording that drew the component, when the game's data is valid; the game's bone buffer is never
  read at any other time.
- Each recording's draws read a snapshot of it, copied in that recording's batch after the batch's
  imports: the GPU form of the CPU path's master and per-draw snapshots, so a later batch's import
  cannot change what an earlier batch's draws read.
- A component a recording reads but does not import comes from the persistent skeleton: its latest
  import in the game's order. Only when the object's skeleton never had the component submitted, the
  recording takes the import of a recording that is recorded and not yet submitted (its data valid
  until that recording completes); otherwise a draw that would read a component no import ever
  wrote is refused rather than drawn with zeros.

## Cause B: state kept per generation instead of per file

3DMigoto's F10 releases its resources and loads them lazily again. The layer builds the new
generations beside the live ones and declares every image under the new generation's tag, so a
reload reads the same files a second time and the old copies stay until the old generation's
recordings went: the device holds the textures twice and then evicts under the cap.

**Change.** Images and mesh files belong to the device, keyed by their file's identity (normalized
path, size, write time); generations hold references. A reload shares every file that did not
change; one that changed on disk is a new image, loaded on first bind as before; the old
generation's references go when it retires. F10 then costs no texture loads unless a file changed,
and memory does not double.

## Cause C: the texture load path is slower than the original's

The original loads at first use and waits (CreateDDSTextureFromFileEx: one read, the driver's copy).
The layer's loader (frontend/assets/resource_cache.cpp, image_upload.cpp), per texture: a vector of
the file's size, zero-filled (64 MiB written and faulted in), ReadFile into it, then a new
host-visible staging allocation of the same size (the system zeroes it), a copy into that
write-combined memory, a new image allocation, a command pool, a command buffer and a fence, all
freed again when the copy completed; publication through the timeline a frame or two later.
**Change:** measure each step offline on the installed Mods' largest maps, then keep staging
memory and command buffers between loads and read the file straight into the mapped staging
memory (one copy), load in parallel and publish when the upload completed; the target is a
first-use wait no longer than the original's synchronous load.

## Cause D: per-draw work that repeats unchanged results

As docs/3dmigoto-flows.md section 1 and 6: three translations of each matched draw's result,
material preparation and variant lookups repeated for unchanged inputs, and the probe's diagnostics
in the play path. **Change:** keep a list's translated result per section and input state, rebuilt
only when the list's result differs; one generation for the Mods folder; a play configuration.

Measured 2026-10-27 on 2.0.3 (vk03 replay, arcane, 29 matched draws a frame). Normal build, 6000
frames, 173,913 matched draws: about 16.6 us per matched draw, of which plan 10.5 (the original's
interpreter running the TextureOverride lists, then the compile to a file plan and the draw list,
whose result is already cached by its signature), emit 1.4, bones 1.0, materials 0.9, snapshot 0.9,
variants 0.7 (already looked up by pipeline, draw and material plan), append 0.6, identify 0.55.
A phase-timing build (build/phase-profile in the worktree) splits the plan: interpreter about 5.2,
compile 0.6, list 0.8, the rest setup; its timers inflate materials and variants to 2.5 and 1.9.
The interpreter is the original's own per-draw work, which 3DMigoto runs too; what the layer adds
is about 6 us spread over several sub-microsecond steps, with no single repeated result left to
keep. Before more work here: the same figures from the game (the profile endpoint of the probe),
where draws per frame and threads differ.

## Cause E: tests that do not model what failed

BUG-050, BUG-052, BUG-054 and BUG-060 each passed the replay, because the replay kept its data
still and valid forever and ran its own order. The replay gets the game's guarantees and nothing
more: frame data valid only until that frame completed (`rolling-bones`: two copies of every bone
region, the previous frame's overwritten before the next frame is submitted), two queues with
semaphores between them, frames in flight.

## Order of work

1. D-127 for the bones, with the rolling-bones replay failing on 1.9.2 first (1.9.3).
2. Images and meshes shared by file identity; F10 loads only what changed (BUG-061).
3. The texture load path measured and shortened (1.9.9, D-130: one map 20 ms against the original's 28 ms).
4. The owned work of replaced draws and the Present work ride in the game's batches (D-127). The
   owned work of replaced draws goes only to the queue the generation's owned runtime is bound to,
   so a recording submitted on the other queue runs before it; the Present work (Typhoeus' and Si
   NightveilTrancer's shape-key compute) waits for the game's frame but nothing orders the next
   frame's batches on the other queue after it, so the Mod's draws may read positions while the
   compute writes them.
5. Per-draw work proportional to change; one generation for the Mods folder; the play configuration.
