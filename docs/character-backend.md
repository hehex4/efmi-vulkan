# Whole-character backend (0.9.6, earlier mesh builds 0.10.0 and 0.9.5)

Numbering: 0.10.0 was named before the project's version rule (AGENTS.md); in the sequence it is 0.9.4, and 0.9.5 follows it.

Replaces every draw of one mod's character with the mod's meshes, skinned with one merged skeleton
per character instance, and hides the components the mod deletes. Since 0.9.6, an optional
material envelope also replaces static pixel images through the shared resource and variant
helpers; details and verification are in [character-materials.md](character-materials.md).
The mesh-only package retains the game's material. Verified in the sandbox and saved frames.
Build 0.10.0 ran in the game on 2026-10-13: the mod's outfit is on screen, with a flicker at LOD 0
that 0.9.5 addresses (last section). The included fix and 0.9.6 materials await a new game check.

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
   render pass. Any refusal keeps the original draw and is counted under its own name.
   An application set at slot 3 is no obstacle since 0.9.5: the replacement uses the slot and the
   application's set is bound again afterwards, with the layout and dynamic offsets it had.
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

These paragraphs describe the original mesh-only sandbox checkpoint. The later first game
result is recorded below. 0.9.6 adds validated driver creation of 105 historical material
pipeline combinations and actual-file uploads; appearance, motion and cost of that build
remain questions for the next user-started session.

## Limits

- Mesh-only packages keep the game's textures. The optional 0.9.6 material package uses
  exact-fragment profiles and per-draw images; unknown or unavailable contracts keep the
  complete original component. Runtime Core source-identity queries are still outside it.
- One scenario of the mod's variables, fixed when the package is built.
- A component drawn on a four-set pipeline, instanced or in a secondary command buffer keeps the
  original draw (LIMIT-023 for the first). So does one with pushed descriptors at set 3.
- A stream of the mod shorter than the game's is bound as it is (0.9.5): an element may lie up to
  64 bytes past the file's stride and then reads into the next vertex, as under the source API.
  Farther than that refuses.
- LOD 0 and LOD 1 only.
- Up to 16 objects are followed; one arena holds 256 draws and 64 skeleton copies per command
  buffer.

## First game session and 0.9.5

2026-10-13, user-started, build 0.10.0, open world. The user reports the model roughly right and
a flicker at LOD 0 that comes and goes; a screenshot shows the character in the mod's outfit.
Counters over four seconds (378 frames): matched 23,212, replaced 12,099, hidden 11,030,
`refused_set3` 82, nothing else refused. Each refusal shows the game's own mesh for that draw in
that frame. Present intervals with the replacement active: p50 10.2 ms, p99 15.9 ms, none over
33 ms in the last 2,048.

Why the application has a set 3 there: most pipelines of the game use four sets, and a set bound
at slot 3 stays valid while three-set pipelines are drawn with compatible layouts. The saved
frames used before the session happened to show no such draw among the matched ones.

0.9.5 binds the application's set again after the replacement (`restored_set3`). The sandbox
scene gained the same situation: a four-set pipeline draws, the character follows, the four-set
pipeline draws again without binding set 3; validation would report a set that is missing or
bound with another layout. Against 0.10.0 the scene fails with `refused_set3` 10. Counts with
0.9.5: decoder 40, character scene 53 over four runs. Whether the flicker is gone in the game is
the next session's question.

