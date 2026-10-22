# Captured frames replayed through the layer (candidate 1.5.6)

`scripts/replay_capture.py` takes a catalogue bundle of the Vulkan session
(`captures/catalogue-20261019/vulkan/vkNN`) and replays the draws an installed Mod's sections
match, with the game's own pipelines, shaders, descriptor layouts and uniform data, in a child
process that loads the layer with the Mods folder (directory generation, D-078). It is the first
check of the live path (match, identification, bone import, INI plan, variant pipelines, materials,
owned Present) against real game pipelines and real Mods without starting the game. Sandbox only:
it is not a result of the game.

    py -B scripts/replay_capture.py --bundle "F:/efmi vulkan/captures/catalogue-20261019/vulkan/vk03"

Options: `--settings DIR` (a folder of persistent INI variable files the Mods start from, as the game's settings folder: a variant key's state, BUG-038), `--frames N` (default 90), `--rp N` (only the draws of these recorded render pass
instances), and two diagnoses: `--open-tests` (no depth, stencil or culling in any replayed
pipeline) and `--probe-vertices` (the original draws get a small known shape). Output under
`build/replay/<bundle>[-rp...][-open][-probe]/{disabled,enabled}`: the layer log, the status, the
read-back targets (`target-G-A.raw`), pictures (`groupG-attachmentA.png`) and `summary.json`.

## What the replay makes up

The bundle keeps pipeline recipes, SPIR-V, the descriptor contents of every draw and the uniform
buffers they read, not the index or vertex bytes, storage buffers or images. What is missing is a
stand-in, never guessed game data:

| Missing | Stand-in |
| --- | --- |
| index bytes (only the region hash is kept) | forged to the same CRC-32C with small indices (GF(2) solve over the low bytes of the first eight indices) |
| vertex buffers | zero, one buffer per binding the pipeline declares; the original draws collapse to nothing |
| storage buffers | zero, except the bone buffer |
| bones | at each instance entry's two rows (current, previous), 1024 copies of an upright bone: the character meshes are stored Z-up and their skeletons turn them upright (-90 degrees about X), so the Mod's mesh stands in bind pose |
| images | 4x4 grey of the recorded format and view type |
| framebuffers | one 960x600 target set per render pass instance, cleared; passes of one depth format share their depth (the first clears, later ones keep it), as a deferred renderer's G-buffer and forward passes do; depth starts at 0 where a pipeline of the pass tests GREATER or GREATER_OR_EQUAL |
| viewport, scissor, stencil reference, depth bias | the whole target, 0, 0 |

Command buffers are three, reset (vkResetCommandPool) and recorded again in turn as a game does;
each frame is presented to a real swap chain (owned GPU work of the INI runs at presentation). The
window is on the secondary screen and does not take the focus. The validation layer is on in both
runs. Pictures are turned the right way up: the game's vertex shaders negate y, so its targets hold
the picture upside down until the final blit.

## What it found in the live path (fixed in 1.5.6)

The first replays of arcane (vk03) never replaced a draw. Three defects, none visible to the
sandbox scene or the offline dry run (BUG-028, D-080):

1. Identification never completed. The layer required four components seen at one place (the
   Core's default `identification_min_components`), but EFMI exports set 3 (some 2) inside their
   DrawInstances list, and adapted command lists hid that assignment from the reader. The threshold
   is now the Mod's own constant, read through the adapter (`DrawPlanAdapter::original`).
2. The Mod's images stayed pending. The resident image budget was 512 MiB; arcane's nine images
   are 576 MiB. The budget is now a quarter of the largest device-local heap, between 512 MiB and
   2 GiB.
3. Material slots were refused ("File image is incompatible with the mapped shader image") in
   directory mode: a 2D file at a 3D shader image needs the DX11 dimension policy (reads zero on
   NVIDIA), which only offline packages carried. The policy is configuration now
   (`m2_dimension_zero = vendor:device`, from build/dx11-dimensions evidence) and applies to every
   package on that device.

A fourth finding was the replay's own: with identity bones the Mod's mesh lies along Z and leaves
the view, which looked like an empty G-buffer. `--probe-vertices` showed the replay's G-buffer
pass draws, the diagnosis with identity everywhere in the bone buffer showed the Mod's draws do,
and the upright stand-in replaced identity.

Also confirmed: the G-buffer fragment shaders read DX11 t0 and t1 only, so a Mod's textures at
t16 to t19 do not reach that pass (as under DX11); the forward pass reads t0 to t22 and shows the
Mod's textures.

## Results

All 16 bundles of the catalogue, 2026-10-20, layer 1.5.6 (build/next), the installed Mods folder,
NVIDIA RTX 5060 Laptop. 120 frames, and 360 for the six whose images were still loading from a cold
disk cache at 120 (about 600 MiB of DDS per Mod; a character's materials are published once all its
images are resident, until then the game's own draw stays). "Passes" counts the render pass
instances with a component draw of the Mod that show covered texels with the layer; without the
layer every replay covers nothing. Validation: 0 errors and 0 warnings in all 32 runs (after the
replay device enabled the features the game's shaders use, see below).

| Bundle | Mod | Replaced draws | Passes | Note |
| --- | --- | --- | --- | --- |
| vk01 | Yvonne | 0 | 0/5 | package refused: `EFMITTL/translucency.ini` acts on game shaders (ShaderOverride); 1.6.4 (D-089): loads, 8071 replaced at 360 frames, no refusal |
| vk02 | TangTang | 497 | 5/5 | |
| vk03 | arcane | 528 | 3/3 | |
| vk04 | Si Lunabelle | 0 | 0/4 | LIMIT-034: `Si_BossGuard.ini` replaces the Core's identification (1.6.2, D-086: 10334 replaced, 4/4, 360 frames) |
| vk05 | Liino | 5372 | 4/4 | 360 frames |
| vk06 | Typhoeus | 5995 | 5/5 | 360 frames; the chest component's per-draw compute deformation (copy, CustomShader, owned vb0) is refused (P2), the game draws that part (1.6.3, D-087: 14130 replaced, body and clothes the Mod's, docs/draw-gpu-work.md) |
| vk07 | mifu | 4843 | 4/4 | 360 frames |
| vk08 | zhuangfangyi | 2022 | 3/3 | 360 frames |
| vk09 | rossiy | 314 | 4/4 | |
| vk10 | last rite | 409 | 4/4 | |
| vk11 | ardelia | 737 | 4/4 | |
| vk12 | gilberta | 347 | 4/4 | |
| vk13 | laevatain | 224 | 3/3 | |
| vk14 | perlica | 6956 | 5/5 | 360 frames |
| vk15 | arclight | 2573 | 4/4 | 360 frames |
| vk16 | Xaihi | 0 | 0/5 | package refused: `ps skip.ini` acts on game shaders (ShaderOverride); 1.6.4 (D-089): loads, 10072 replaced at 360 frames, no refusal |

13 of 16 characters show the Mod's mesh in every pass their components draw in: the depth
prepass, the G-buffer and, with the Mod's textures through the game's forward shaders, the forward
pass (`build/replay/characters.png`, a sheet of the lit forward targets, or the G-buffer where the
stand-in lighting leaves the forward target black). The three others are the known limits named
above; there the game keeps drawing its own character.

The first arclight run had two validation errors in both runs, the replay's own: a game fragment
shader writes a storage resource and the replay's device lacked `fragmentStoresAndAtomics`. The
replay device now enables what the device offers of the features the game's shaders use.

## In the regression suite

`sandbox/run_all.py` step "captured frame replayed with the Mods folder" replays arcane (vk03) for
300 frames with `--check`: both runs exit cleanly without validation messages, the original draws
cover nothing, at least one draw is replaced, and every render pass instance with a component
draw of the Mod covers texels with the layer. It needs the catalogue bundle and the installed Mods
folder, as the dry-run step needs the Mods.

## What this does not show

- Real bones, animation, lighting and textures of the game: all are stand-ins. Whether the Mod's
  mesh follows the game's skeleton is the bone import's question, checked in the game earlier
  (the arcane slice) and not here.
- Timing in the game: the replay's frame is 29 to 59 draws.
- The game itself: nothing here was loaded into the game.

## A Mod's own identification (candidate 1.6.2, D-086)

Si Lunabelle's optional `Si_BossGuard.ini` replaces the Core's identification call with its own
list over the Core's internals (spatial pools, `Original_VS_CB->SpatialHash`, the Core's output
lists), so that only an object whose body was drawn at that place this frame or the last is the
Mod's. vk04 now replays with it: 10334 draws replaced at 360 frames (3549 at 120), every pass,
the guard's own pool and the Core's spatial pools filled; the first draws of the first identified
frame keep the game's draw (`kept_original` 44), as the original Core draws the original
instance while it identifies. A project-owned copy of the Mod whose guard never sees a body
(`build/replay-mods/si-guard-rejects`, the witness component changed to 99) replaces nothing and
keeps the game's draw of every identified draw (4998 of 5039 matched, the rest still
identifying): what a boss with the same parts gets. Suite step "captured frame with a Mod's own
identification" (vk04, 120 frames, --check).

## A fatal exception in a replay (candidate 1.6.3)

`replay_gpu.exe` writes a fatal exception (breakpoint, access violation, fast fail, heap
corruption) to its output with the faulting stack, symbolized from the modules' own debug
information, before the process ends (`stdout.txt` of the run). It found BUG-031: NVIDIA's shader
compiler ending the process on a translated division (D-088).

## All 16 bundles after 1.6.4 (2026-10-20)

Every catalogue character loads and replaces, none with a refused draw (360 frames; vk03 300 and
vk04 120 from the suite): Yvonne 8071, TangTang 5944, arcane 3368, Si 3517, Liino 7404, Typhoeus
14110, mifu 6259, zhuangfangyi 2732, rossiy 3520, last rite 6919, ardelia 9125, gilberta 4180,
laevatain 4132, perlica 10121, arclight 4533, Xaihi 10072 replaced draws; validation clean in all
runs. What stays the game's: Typhoeus' chest physics (LIMIT-035), the shader-keyed parts of Yvonne
and Xaihi (LIMIT-036). `exhausted` at the start of a replay is the snapshot pool filling while the
first frames run (the same count in 1.6.2 and 1.6.3 for vk03). Sandbox only.
