# How 3DMigoto (XXMI) does it, and what the layer does instead

Read 2026-10-25 from `F:\vulkan\XXMI-Libs-Package\DirectX11` (HackerContext.cpp, CommandList.cpp,
ResourceHash.cpp, HackerDXGI.cpp) and the installed EFMI Core and d3dx.ini, at the user's request:
design on 3DMigoto's flows instead of finding problems one session at a time. Each section states
the original's flow, the layer's, and what follows for the design.

## 1. The draw

**3DMigoto.** Every `Draw*` call of every context (immediate or deferred) goes through
`BeforeDraw` → original draw unless skipped → `AfterDraw`. `BeforeDraw` looks the bound vertex and
pixel shaders up in `mShaderOverrideMap` (one hash map lookup each) and runs the pre command list of
a match; `AfterDraw` runs the post lists. EFMI's Core makes every vertex shader a match through
`[ShaderRegexEnableTextureOverrides]` (`shader_model = vs_4_0 ... vs_5_1`), whose list is
`if $enable_mods: if DRAW_TYPE % 2 == 0: CheckTextureOverride = ib`.
`CheckTextureOverride = ib` (CommandList.cpp ~10290) prefilters the TextureOverride sections by the
draw's index count (`mTextureOverrideDrawIndexMap`, a hash map from count to candidates); only a
draw with candidates hashes its index region (`GetRegionHash`: an L3 cache per frame keyed by
buffer, offset and size, looked up without a lock; an L2 cache per buffer handle; an L1 copy of the
buffer's bytes in RAM, read back from VRAM once by a GPU stall), then runs the matching sections'
lists on the same context, immediately.

**The layer.** The tracker sees the draw in vkCmdDrawIndexed; the device-wide index (prefiltered by
the sections' counts, as above) hashes the index region with a fingerprint fast path; a match calls
`record()`, which identifies the object, imports bones, runs the Mod's lists through the original's
interpreter into a plan, compiles the plan into a file plan, converts that into the package's draw
list, prepares materials, finds pipeline variants, appends snapshot entries, and emits the draws.

**What follows.** The match is equivalent. The cost is not mainly in the original's interpreter (the
same code; about 0.5 us per command measured offline, LIMIT-037) but in what the layer builds around
it per draw: three
translations of the same result (plan → file plan → draw list), material preparation and variant
lookups repeated every frame for unchanged inputs. 3DMigoto has none of that: a command is a D3D
call. The design target is the same shape: per matched draw, the list's commands turned into
Vulkan state with work proportional to what changed.

## 2. Resources

**3DMigoto.** A `[Resource]` with a file is created and loaded on its first use
(`CustomResource::Substantiate`, CommandList.cpp 6407): synchronously, on the thread that runs the
command, through DirectXTK; `substantiated` is set once and the resource is never unloaded. Video
memory pressure is the driver's and Windows' (WDDM pages resources out and back in). The first frame
that binds a texture waits for its load; no frame ever draws without it, and no texture of a loaded
Mod is ever reloaded.

**The layer.** Textures are loaded asynchronously on first bind, kept within a budget, evicted when
idle or demoted under pressure, and a draw whose texture is not resident is gated: the package's
frame goes to the game's mesh (D-097). That is the source of "the original model first" and "a
loaded Mod turns back into the original" (BUG-047, D-120).

**What follows.** Do as the original: a texture is loaded when a draw first binds it and the draw
waits for it (the bind's wait, D-120); once resident it stays (no idle eviction, no demotion of a
bound image) with pageable device memory letting the system page, as WDDM does for DX11. A budget
remains only as the device's hard cap; beyond it the least recently bound image goes, never one
bound in the last frames. With that there is no frame gate for textures at all.

## 3. One space for all Mods

**3DMigoto.** d3dx.ini, the Core and every Mod's INI are parsed once into one global state:
one variable table (`$\namespace\name`), one set of resources, one `[Present]` list (all Mods'
`[Present]` sections appended), one IniParams texture updated by Map/WRITE_DISCARD when a command
changed a value (`CommandListFlushState`). F10 reloads all of it.

**The layer.** A generation per Mod folder: each parses the Core and its Mod (and the libraries it
calls), holds its own variables, resources and IniParams, and runs its own Present list every frame.
With 22 Mods that is 22 parses of the Core, 565 MiB of host memory (418 MiB of it mesh payloads
kept after upload; ISSUE-011), 22 Present lists per frame (0.5 ms), and an F10 that reloads 22
generations at once (the commit spike of ISSUE-011).

**What follows.** One generation for the whole Mods folder, as the original: one parse of the Core,
one variable space, one Present list, shared resources, one IniParams. The per-Mod isolation bought
nothing the original has (its F10 reloads everything too) and costs memory and a frame-time term
that grows with the number of installed Mods. Mesh payloads are freed once on the device.

## 4. Skinning

**3DMigoto.** The Core's `CommandList\EFMIv1\MergedSkeleton_Apply` (MergedSkeleton.ini) runs in the
draw's list: compute dispatches copy the drawn component's bones into the merged skeleton
(`MergedSkeleton_BoneDataImporter_*.hlsl`, once per component per frame through its
`Pool_MergedSkeleton_Instance_UpdateFrame`), and the instance config is overridden; the draw then
reads the merged skeleton. All of it is GPU work issued from the draw's context, in order.

**The layer.** The bones were imported on the CPU from the game's write-combined bone buffer (17-33 us
per matched draw in the game). 1.8.2 moved the import to compute dispatches (D-117); two sessions
then failed on the GPU because the dispatches crossed the game's two queues (BUG-050, BUG-052). 1.8.4
puts each dispatch in front of the recording that reads it, on that recording's queue: the
original's order (dispatch, then draw, on one context) as far as Vulkan allows outside a render pass.
It still went in a submission of its own, outside the game's order, with per-frame slots instead
of a persistent skeleton; BUG-054's repair then read the game's buffer outside the time the game
keeps it valid (BUG-060). 1.9.3 (D-127, docs/design-review-1.9.md) follows the original's model: one
persistent merged skeleton per object, written only by imports that run in the batch of the
recording that drew the component, at the front of the application's own batch so the game's
waits order them; each recording's draws read a snapshot copied after its batch's imports.

## 5. Threads and contexts

**3DMigoto.** A deferred context runs its command lists itself while it records; resource creation
goes through the device, which DX11 makes thread safe; the IniParams map/unmap happens on the
context that changed them.

**The layer.** The game records on several threads; the layer runs on each, under the generation's
lock for the INI runtime (the original's engine addresses globals, so one list runs at a time). A
single global space (3.) keeps that one lock; the work done under it must then be small (1.).

## 6. What is not in the original's play path

Frame analysis, hunting, region hashing of every buffer, profiling and overlay are off unless the
user turns them on. The layer's probe tracking (`resources`, `image_hash`, `profile_tracker` in
dist/efmi_vk.cfg) runs in every session. What the replacement needs of it (bound ranges, texture
identities for hash conditions) stays; the rest belongs to a diagnostic configuration.

## Order of work

1. Resources as the original (2.): first-bind load with a wait, no eviction of used textures under
   pageable memory, no texture gate. Small, and it ends the visible reverts. Done in 1.8.5 (D-122,
   with the wait of D-120); the gate remains only for a load that failed or outlasted the wait.
2. The GPU skinning of 1.8.4 verified in a session (4.), then measured with `scripts/ab_game.py`.
3. One generation for the Mods folder (3.): the largest change; memory, Present cost and F10.
4. Per-draw work proportional to change (1.): the translated draw list and its materials and
   variants kept per section and input state, rebuilt only when the list's result differs.
5. A play configuration without the probe's diagnostics (6.), measured against the current one. The
   user, 2026-10-25: this waits until everything else is done.

Each step is measured in the game against the one before with the interleaved A/B, not in the
sandbox alone (D-119): the sandbox has the game's queues now, but not its scene.
