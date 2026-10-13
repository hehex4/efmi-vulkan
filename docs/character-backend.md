# Whole-character backend (layer 0.10.0)

Replaces every draw of one mod's character with the mod's meshes, skinned with one merged skeleton
per character instance, and hides the components the mod deletes. The game's own material is
kept: textures are not part of this step. Verified in the sandbox and against saved game frames;
no game process has loaded 0.10.0 yet.

It is a fixed snapshot of the mod in one state of its variables, not an INI interpreter: no keys,
conditions or reloads at run time. The general route (hosting the original XXMI engine in the
layer, T-027) remains the way to those; this backend runs the same shared pipeline, bone and
command code with a static input, and exists to put the whole character on screen first.

## Package

`scripts/package_character.py --mod <ini>` writes `build/character/<name>.bin` and a manifest.
It does not interpret the INI's logic itself. What each component callback binds and draws comes
from `build/xxmi-host/mod-plans.json`, the plans the original XXMI engine produces
(`scripts/check_mod_draw_plans.py`); bone ranges and LOD 1 remaps come from
`build/xxmi-host/core-skeleton-plan.json`, the original core initialization
(`scripts/check_core_skeleton.py`). From the INI text it reads only the entry sections (index
hash, match range, component, LOD assignment) and the layout edit
`vb2->ElementFormat(BLENDINDICES, 0) = R16G16B16A16_UINT`; any other layout edit refuses.

The sample (`arcane.ini`, scenario 0, which equals the user's persisted toggles except a texture
switch): 499 bones, 12 components, 21 sections, 28 draws, 36 streams, 31.95 MB. Components 3, 5, 8,
9, 10 and 11 have empty draw lists and are hidden. Component 1 has nine draws per LOD.

The layer checks everything again at load (`character_package.cpp`): sizes, ranges, the index
bounds of every draw against its vertex streams, every bone index against the bone count, weight
sums. It computes, per draw list, which components' bones the list's vertices use.

## What happens to a draw

`vkCmdDrawIndexed` with an index count no section has costs one hash-table lookup. Otherwise,
under one lock (the mod's state is global, as in the core):

1. The index bytes of the draw are hashed (CRC-32C). The hash is cached per buffer range and
   recognized afterwards by a 128-byte fingerprint; the full hash is repeated every few hundred
   frames, staggered, because the game's mapped pools change without the layer seeing it.
2. The section's component and LOD follow. A section without a LOD assignment leaves the mod's
   LOD variable as the last section set it, as the INI does.
3. The instance entry is read at set 2 binding 0. Its translation (constant 3) names the object:
   every component of one character has the same translation within a frame (one distinct value
   among 45 to 61 matched draws in each of four saved frames). Across frames an identity follows
   the object within the core's radius; within a frame only the very same position is the same
   object, so a neighbour that shares a mesh gets an identity of its own.
4. The component's poses are imported into the identity's merged skeleton, once per frame and
   component, the lower LOD winning, through the LOD 1 remap where the section is LOD 1
   (`MergedSkeleton`, the importer of 0.8.2 with the offsets corrected in 0.9.1). The bone buffer
   is found from the vertex shader, also for pipelines with four sets.
5. The object becomes the mod's once four distinct components were seen at its place and a frame
   has passed, the core's rule. Until then the game draws it. The core skips the object in its
   first frame; this backend lets the game draw it instead.
6. An empty draw list hides the draw. Otherwise every component whose bones the list uses must
   have been imported, a variant of the current pipeline must be ready for each draw of the list
   (built off the draw thread, D8), and the draw must be a single instance in a primary classic
   render pass without an application set 3. Any refusal keeps the original draw and is counted
   under its own name.
7. The replacement is one transaction: the patched entry and the skeleton go into the command
   buffer's arena, the mod's draws are emitted with the variant, the application's pipeline,
   vertex and index bindings are restored. Draws that read the same skeleton bytes share one copy
   in the arena (`DrawSnapshots::create_shared`); the bytes change only at an import.

The frame is the present, except that a present after which nothing was drawn does not count:
the game presents intervals without scene draws in the LOD 1 world (ISSUE in
`docs/m2-game-recipes.md`).

The mesh is written once into device memory that the host can map, the kind the game uses for
its own mesh pools (memory type 4 on this machine); host memory is the fallback and the status
says which. A whole character read from host memory in every pass would be slow.

## Vertex declaration

A stream the mod binds takes the file's stride. The edit widens `R8G8B8A8_UINT` elements of
binding 2 to `R16G16B16A16_UINT` in place. Streams the mod does not bind, the default tangent at
binding 4 among them, stay. For all 426 replaced draw items of six saved frames this equals the
semantic reference of `scripts/vertex_profiles.py` (which names `BLENDINDICES` through the
paired DX11 layouts), so no per-shader profile is needed for this mod's one edit.

## Settings and status

`m2_package` names either kind of package; the file's magic selects the backend. `m2_enabled`
and the pipe command `{"cmd":"m2","enabled":0|1}` work as before. `status.replacement` has
`kind: "character"`, the totals the single-component backend reports, and `counters` with one
entry per outcome: `matched`, `replaced`, `items`, `hidden`, `identifying`, `incomplete`,
`pending`, `refused_state`, `refused_instances`, `refused_set3`, `refused_pipeline`,
`refused_vertex`, `refused_variant`, `refused_bindings`, `unreadable_entry`, `unreadable_bones`,
`import_rejected`, `exhausted`, `identities_full`; also `identities`, `identities_replaced`,
`mesh_in_device_memory`.

## Verification

Sandbox, layer 0.10.0, full suite in `build/next` and again on `dist/` without building: the
earlier counts, draw snapshots 47 (13 new for shared slices), and the new step "character
replacement": 37 native decoder checks (25 kinds of damaged package refused without changing the
accepted one; declaration rule) and 49 checks over four GPU runs through the real hooks (layer
and inject entry, render pass 1 and 2) under core and synchronization validation. The scene has
three components and a neighbour. Pixels prove: the first frame is the game's; then one
component drawn on its own bone, one whose second part sits where another component's bone puts
it and follows that bone through 20 pose changes in the same frame, one hidden; the next
unmatched draw at its place without rebinding; a LOD 1 section imported through its remap; the
neighbour that draws a shared mesh a quarter unit away never replaced; disable, enable and pool
reset. No refusal of any kind is counted.

Saved game frames, offline: the package finds the section the Python matcher found for all 329
matched draws of six frames; a model of the rules above gives 24 to 32 replaced draws and 21 to 29
hidden ones per frame with 56 to 80 arena entries, no refusal. In those frames every matched draw
has all four tracked vertex slots bound, a zero vertex offset and a pipeline with three sets,
except component 10's draw in a depth-only pass on a four-set pipeline, which the mod hides.

Smoke with the deployed settings and the real package: 0.10.0, backend ready, 31,950,446 bytes
in device memory, validation clean (`build/character-actual-config-dist.json`).

Not verified: anything in the game. Variant creation by the game's driver for these
declarations, appearance, animation and cost are the next session's questions.

## Limits

- No textures. The mod's meshes carry the mod's texture coordinates and are shaded with the
  game's textures, so colors are wrong wherever the layouts differ. Shape, animation and the
  hidden components are what this step shows.
- One scenario of the mod's variables, fixed when the package is built.
- A component drawn on a four-set pipeline, instanced, in a secondary command buffer or with an
  application set 3 bound keeps the original draw (LIMIT-023 for the first).
- A texture coordinate stream whose stride differs from the game's is accepted only when every
  element still fits; otherwise the draw is refused. The source API would read into the next
  vertex there.
- LOD 0 and LOD 1 only.
- Up to 16 objects are followed; one arena holds 256 draws and 64 skeleton copies per command
  buffer.
