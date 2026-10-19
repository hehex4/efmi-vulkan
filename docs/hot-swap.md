# Exchanging the replacement code while the process runs (1.0.1)

Requested by the user on 2026-10-13, after four restarts of the game in one afternoon: write the
replacement logic as a module of its own, and let the agent swap it into the running game when a
change of that logic is to be verified. The rule is in `AGENTS.md`.

## Host and module

The DLL that is injected into the game does two jobs. It hooks the Vulkan calls and keeps the
tracker, the record of every object the game has created since it started; and it decides which
draws to replace and records the replacements. A loaded DLL cannot be replaced, and the tracker
cannot be rebuilt by a second copy (objects created before it arrived are unknown to it). The
second job has no such state: everything it knows it reads from the tracker or learns again
within a few frames.

So the code of the second job exists twice. The layer DLL has it built in, as before, and uses
that copy unless told otherwise. `efmi_vk_backend.dll`, the module, holds the same code alone:

| | layer DLL (host) | module |
|---|---|---|
| hooks, tracker, probe pipe, captures | yes | no |
| character backend, its package and materials, derived material rules, merged skeleton, pipeline variants and their cache, draw snapshots, texture upload | built in | yes |
| single-component backend of 0.9.x | yes | no |

`layer/src/character_module.h` is the boundary: a table of the backend's functions
(`CharacterModule`: create, usable, ready, stop, record, retire, frame, status, enable, submit)
and a table of what a module needs from the host (`HostApi`: the log, CRC-32C, and the tracker's
two readers of buffers). A module's one export, `efmi_vk_character_module`, takes the second and
returns the first. `layer/module/module_main.cpp` is the only source the module has for itself.

What a command buffer keeps of a backend's work is a `CommandOwner` (`command_owner.h`): the
tracker resets and seals it through virtual calls and knows nothing of its contents. It names
the backend that made it; a backend leaves another's alone while it is in use and takes its
place once a recording has been reset.

## The stamp

Host and module exchange tracker structures, strings and vectors as they are. That is only
sound when both were built from the same definitions with the same compiler settings. The build
computes a 64-bit stamp over the headers that define what crosses (`state.h`,
`character_module.h`, `command_owner.h`, `json.h` and everything they include from `layer/src`;
at present ten files) and the compiler flags, and compiles it into both sides as `EFVK_ABI`. The
version text is left out of it. A module with another stamp is refused before any of its code
beyond the entry has run, and unloaded again. `scripts/hot_swap.py` compares the stamp of the
working tree with the running layer's before it builds anything.

Both sides link the C runtime statically; since the universal runtime that means both allocate
from the process heap, so an object may be freed by the side that did not allocate it.

A change inside the backend can be swapped. A change to a shared header, to the hooks or to the
tracker cannot; it needs the layer replaced with the process closed.

## The exchange

Probe command `{"cmd":"module","path":...,"package":...}` (`ProbeClient.swap_module`):

1. The layer loads the file and checks the stamp.
2. It has the module make a backend for the device (package read, mesh uploaded, arenas made, as
   at start-up) and stages it. A package that is refused ends the attempt; the backend in use
   is not touched.
3. The staged backend is served by the same submit hook as the one in use, so its textures are
   uploaded and published to the queues while the other still replaces every draw.
4. At the end of a frame in which the staged backend reports ready, it takes over: every call
   into the backend in use holds `Device::character_mx` shared, the exchange takes it
   exclusively, so it happens between two calls. A staged backend that is not ready after 30
   seconds is given up.
5. The backend that was in use is stopped and destroyed on a thread of its own. What command
   buffers still hold of it lives as long as they do.

A module that has been used is never unloaded: objects it made, and their code, may outlive its
backend. Each exchange therefore needs a file name of its own; `scripts/hot_swap.py` copies the
built module to `build/modules/` under a name with time and hash and deletes older copies that
no process holds any more.

What is visible in the picture at an exchange: the new backend knows no object and has no
pipeline variants yet. The character is the game's for the frame of identification and for the
frames until its variants are built, as at start-up or after the first use of a shader.

`status.replacement.module` reports `generation` (exchanges completed), `builtin`, `version`,
`path`, `staged`, `last_error` and `abi`. Counters start at zero with every backend.

## Doing it

```
python scripts/hot_swap.py --note "what the change is"
```

builds the module from the working tree (`scripts/build.py module`), exchanges it in the one
`Endfield.exe` process, waits until it is in use and appends a record to `build/hot-swaps.json`
(time, process, layer version, module hash and version, commit and whether the tree was dirty,
seconds, outcome). `--pid`, `--target`, `--package`, `--no-build` and `--timeout` do what they
say. Exit status 3 means the shared headers changed and the layer has to be replaced instead.

## Sandbox evidence

- Character scene through the real hooks, all sixteen full runs (layer and inject entry, render
  pass 1 and 2, mesh-only and materials, one and two queues, three submit calls): after
  everything was counted on the built-in code, the module's code is exchanged in twice while
  frames are drawn; the backend in use keeps replacing until the staged one takes over; the new
  one identifies the character and replaces it; a module built with another stamp is refused
  and changes nothing; disable, enable and command-pool reset work on the module's backend. Core
  and synchronization validation report nothing, also not at device destruction, where a leaked
  object of a retired backend would show. 357 checks.
- `scripts/hot_swap.py` against the windowed sandbox with the candidate's real settings and the
  real mesh package: two exchanges, 0.2 s each, the process ends by itself with clean validation.
- The headless texture program with the actual settings, the actual material package and its
  nine texture files, two graphics queues with only the second submitting (`efmi_vk_variant_gpu
  --character-assets 9 --module ...`): the module's backend is staged while the first has all nine
  images in use, uploads the nine again and takes over 0.27 s after staging; nine ready, none
  failed, 216,268,800 bytes resident for the backend in use, validation clean at exit through
  both entries (`build/hot-swap-real-package-1.0.1.json`).

## First game exchange (2026-10-13, 1.0.1)

User-started Endfield PID 19452 runs the 1.0.1 layer. After clean sampled LOD0 and
LOD1 intervals, the agent uses the standing authorization in AGENTS.md and runs
`scripts/hot_swap.py --pid 19452 --out dist --no-build --timeout 35` with the
unchanged, previously regression-verified module. Its SHA-256 matches both the
candidate and deployment manifest:
`e965c0e45ccf5ec6a3260016e65f1cdd5d0b1c4d91f8d14f0398f48b36804a39`.

At 20:49:41 the module takes over in 0.8 seconds, generation 0 to 1, nine images
Ready and no module error. The user confirms normal appearance without a
persistent anomaly. A subsequent 710-frame LOD0 interval has zero new refusal,
pending work or identification; the later requested dash interval also remains
clean. Counters reset on exchange, so each delta uses one generation only.

Evidence: `build/hot-swaps.json` and
`captures/stability-1.0.1-20261013-204259-19452/`. The loaded copy is
`build/modules/efmi_vk_backend-20261013-204941-e965c0e4.dll`. No source, deployed
file, package or launcher setting was changed. This verifies one functional
exchange in the real game, not repeated-exchange lifetime/performance or the
absence of every transient frame during takeover.

## Limits

- One exchange at a time; a second request while one is staged is refused.
- Each exchange leaves the module's code mapped and, in the worst case, one command owner of the
  old backend (with the mesh it keeps alive) in a command buffer that never draws the character
  again.
- A fault in a module is a fault in the game process.
- The single-component backend is not exchangeable.
