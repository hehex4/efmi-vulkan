# Static whole-character materials (0.9.6; queue fix 0.9.7)

The static character backend now replaces meshes, shared bones and pixel images for
one fixed scenario, at LOD0 and LOD1. It uses the original hosted callback plans,
verified exact-fragment correspondences and the existing shared resource, variant
and snapshot helpers. There are no component-specific material branches. This
extends D-035; it does not complete the hosted runtime in T-027.

## Game initialization refusal and 0.9.7

The user-started Vulkan session of 2026-10-13, PID 18868, loaded 0.9.6 at 15:56
but displayed the original character. The actual layer log says
`character package refused: materials require one unprotected graphics queue`.
Live status confirms configured/enabled/ready false, replaced zero and image_count
zero. Initialization refused before either the mesh or textures prepared; this
was not an unknown shader or incorrect asset path. Evidence:
`captures/materials-0.9.6-20261013/`. The old status did not retain the actual
requested queue counts, so no exact count is claimed for that session.

0.9.7 accepts up to 32 enabled queues in one unprotected graphics family. It
retrieves all their actual handles and uses the first active queue as the single
uploader. No worker submits to an application's queue. Once all immutable images
are Ready, that queue submits one publication signal per graphics queue. Each
consumer queue inserts its own one-time wait before forwarding application work.
Only after the signals have been submitted can recording retain image views.
Thus the replacement does not depend on queue 0 being used or on an application
semaphore accidentally ordering the layer's uploads. Images stay in the same
queue family; multiple graphics families and protected queues still refuse.

The signal scope includes earlier queue work and the wait scope includes later
work, with ALL_COMMANDS as its destination stage; see the primary
[vkQueueSubmit synchronization rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit.html).
Submission failures propagate through all three hooked submit entries. Semaphore
and fence storage is allocated at setup and drained only at device shutdown.
The draw and submit paths do not wait for a fence or queue idle. The chosen
uploader must continue submitting while files are being prepared; the backend
does not move uploads to another queue midway through initialization.

New status fields are material_graphics_queues, material_graphics_family,
image_upload_queue_index, images_published and image_queue_waits. Initialization
also logs each requested graphics family's queue count and flags.

Verification of 0.9.7 is sandbox only so far:

- Complete rebuilt suite passes, `build/material-0.9.7-regression.txt`. Character
  hook coverage increases from 127 to 261 checks across eighteen runs. The added
  cases use only queue 1 while queue 0 stays unused, and upload on queue 0 then
  create/draw the application scene on queue 1. Both DLL entries pass with legacy
  QueueSubmit, core QueueSubmit2 and QueueSubmit2KHR, clean core/sync validation,
  material pixels, original state restoration and sealed resubmission.
- The same two-queue scene against both old 0.9.6 DLL entries reproduces the game's
  initialization refusal, with zero replacement and zero textures. The original
  scene is still drawn, validation is clean. Control evidence:
  `build/material-queues-0.9.6-control.json`.
- Actual candidate configuration and nine real DDS files pass through both 0.9.7
  entries with two enabled queues, only queue 1 active: nine reads/submissions,
  nine Ready, published true, one queue wait, zero failures and 216,268,800
  resident image bytes. Report: `build/character-assets-config-next-0.9.7.json`.
- Both candidate DLLs SHA-256:
  `3ff4517d29ac23cdd877b45f34f777f2146391488955d91f6eb181b40e02995d`.
  No package, installed mod, launcher value or system setting changed in the fix.

The user then exited normally. Repeated complete process checks found neither
game nor launcher. At 16:23, thirteen verified files were backed up to
`build/backups/before-0.9.7-20261013-162318/` and copied to dist with exact hash
comparison. Actual-config two-queue smoke passes again through both deployed
entries (`build/character-assets-config-dist-0.9.7.json`). The historical game log
and launcher bytes are unchanged; launcher stays Vulkan. Manifest:
`build/deployment-0.9.7.json`. The full suite ran on identical candidate bytes.
The user starts again; image publication and actual character appearance must be
checked in that new process. A queue that is enabled but never used need not
increment image_queue_waits.

## Images published in batches (1.5.7, D-081)

Until 1.5.6 the uploader submitted one publication when every image of the package was Ready, so
a single file that failed (a truncated DDS, an unsupported format, the resident budget) kept the
whole character the game's for good, and a Mod's colour variants it never draws (arcane ships six
diffuse maps) delayed its first appearance by their loading time. From 1.5.7 the uploader queue
publishes in batches: at each of its submissions the images that became Ready since the last batch
get one signal per graphics queue, at most 16 batches (the last one waits until every image is
Ready or failed). Each consumer queue waits once per batch before forwarding application work, as
before for the single signal. A recording retains an image's view only after its own batch was
enqueued; a draw waiting for an image moves it to the front of the load queue
(`ResourceCache::prioritize`); a draw needing a failed image is refused (`refused_image`) and the
game draws it, while the character's other draws replace. All semaphores and fences are made at
setup and drained only at shutdown. `images_published` still means every image (an F10 candidate
takes over only then); new status fields `image_publications` and `images_published_count`.

Checked: resource cache 257 checks including the reorder; character replacement 357 (queue waits
equal batches times used queues); the capture replay of arcane with one diffuse map truncated in a
project-owned copy of the Mod (`replay_capture.py --mods build/broken-mods`): 8 of 9 images
published in 8 batches, 2352 draws replaced in the prepass, G-buffer and forward pass, 1192 forward
draws that need the broken map refused and left to the game. With 1.5.6 nothing would have been
replaced.

## Package and contracts

`scripts/package_character.py --textures` snapshots each pixel binding immediately
before each hosted draw. An omitted slot inherits the application's resource; an
explicit null remains a null. The known Core OverrideTextures callback is outside
this static scenario: runtime source-identity queries are not executed. Unknown
external callbacks and unresolved explicit bindings refuse packaging.

The EFMICH02 envelope contains an unchanged EFMICH01 mesh package, absolute UTF-8
DDS paths, exact fragment words, logical-to-physical rules and per-draw image
references. Its length and CRC cover the mesh and material table. The decoder
checks budgets, references, duplicate profiles/rules, policy consistency and exact
consumption before replacing any accepted state. Nested envelopes are forbidden;
legacy mesh-only packages still load. DDS bytes are read by ResourceCache, not
copied into the mesh allocation.

`prepare_character_materials` is the production helper shared by the backend and
offline verification. It prepares all profile/draw contracts at setup. A pipeline
generation finds its profile by exact fragment bytes, once. Image moves affect
only the fragment stage; original buffers and samplers retain their meanings.
Unknown code, an unresolved slot or an unavailable image retains the entire
original component draw. A late failure cannot emit half its replacement list.

The packaged sample is `build/character/arcane-materials.bin`: 32,988,541 bytes,
SHA-256 `5bcae691949fcc0a8f3ccabf3719a349de1e17a316030521ad68c3d705e10218`.
It holds 499 bones, 12 components, 21 sections, 28 draws, 36 mesh streams,
20 exact fragment profiles and 9 DDS paths. Its mesh payload remains 31,950,446
bytes. The adjacent manifest fingerprints all source files and evidence. The old
mesh-only `arcane.bin` remains available. Both installed mod and Core are read-only.

## Explicit dimension compatibility

LIMIT-021 still describes invalid source-API bindings, not portable null semantics.
Three exact LOD0 profiles assign a 2D file to a 3D shader slot. The existing
headless DX11 experiment returned zero for those mismatched samples on this
NVIDIA device, while WARP returned color; valid control cases passed on both.

The optional `--dx11-dimension-zero build/dx11-dimensions/report.json` selects
`dx11-invalid-2d-to-3d-zero`. Packaging validates that evidence and marks only the
annotated profile slots as ZeroFile. The production contract treats their file
bindings as explicit null results. The package is bound to vendor 4318 and device
11545, and the backend refuses it on other devices. Default material selection
still refuses the dimensional mismatch. This policy is explicitly present in the
deployed sample and reported by status; it is not inferred from arbitrary errors.

The evidence SHA-256 is
`cafa551dae9def04526e83e630093e1a1cac550882f945e9d3b49a2189b30c08`.
Eleven of the 370 historical draw contracts use this policy. Neither that test nor
successful Vulkan pipeline construction establishes the actual game's pixels.

## GPU ownership and queue service

ResourceCache reads and prepares the files off the recording thread. Only the
application's graphics-queue submission hook calls `submit_prepared`; the worker
never submits concurrently to the application's queue. 0.9.6 required exactly one
enabled graphics queue; 0.9.7 adds the family-wide publication described above.
Format support and BC
compression use the physical device's properties and the features actually
enabled at device creation.

Internally allocated dispatchable objects are initialized with the layer loader's
device-data callback when present. Missing this for upload command buffers caused
the initial layer-entry sandbox failure; both entries pass after the fix (BUG-007).

Each command recording owns shared-bone snapshot arenas for its required image
counts. Per-draw views retain their image owners, while draws reading the same
bone generation share a bone copy within an arena. All arenas seal at submit and
stay intact on resubmission, including after runtime disable. Only legal command
reuse recycles them. Prepared batches remain bounded: two waiting, eight total,
64 MiB maximum snapshot storage per batch, 256 entries and 64 bone slices per
arena. Multi-mod residency optimization is outside this milestone.

Status adds `materials`, `material_profiles`, `image_count`, `images_ready`,
`images_failed`, `image_file_reads`, `image_submissions`, `image_resident_bytes`
and `dimension_zero_policy`. New counters are `refused_material`, `refused_image`,
`image_pending`, `material_items` and `dimension_zero_items`.

## Historical 0.9.6 verification and deployment

- Full rebuilt suite in `build/next` passes:
  `build/material-0.9.6-regression-final.txt`. Character decoder/preparation 67,
  shared snapshots 52, character-hook checks 127 across ten runs through layer
  and inject entries, plus all existing unit/tracker/variant/probe/mode/timing/
  validation/MCP/evidence steps.
- GPU pixels verify per-draw red/green/null materials, original FS buffers and
  application set 3 restoration, shared bones and LOD1 remaps, twenty poses,
  hiding, neighbours, disable/enable, pool reset and sealed resubmission. A late
  unknown slot keeps that entire component original while another still replaces.
- `scripts/check_historical_variants.py --character-package
  build/character/arcane-materials.bin` verifies native production contracts
  against hosted pixel plans and saved shader bytes: 370 accepted draws, eleven
  explicit dimension-zero uses, zero omissions. All 105 distinct saved pipeline
  combinations create, 211 checks pass, validation reports zero errors/warnings.
  Evidence: `build/character-variants/report.json`. This constructs pipelines;
  it does not draw a real character.
- Rebuilt standalone assets: 253 checks, all ten inventory DDS variants upload
  and read back with clean validation. The rebuilt pinned host suites also pass.
- The actual static package and actual candidate settings load through both DLL
  entries: nine file reads, nine submissions, nine Ready images, zero failures.
  Image allocation totals 216,268,800 bytes. This is sandbox allocation, not
  measured game VRAM or a multi-mod estimate.
- At 15:47 on 2026-10-13, with repeated complete checks finding neither game nor
  launcher, thirteen tested files were copied to `dist` and compared byte-for-byte.
  Both DLLs have SHA-256
  `21b9be301fb8c6b80a268e4434c352a3a5d9e40e78b21a07f3609052e37dba83`.
  Backup: `build/backups/before-0.9.6-20261013-154745/`.
  Manifest: `build/deployment-0.9.6.json`.
- The user explicitly requested the switch back to Vulkan. Only the launcher's
  EFMI `d3d11_mode_cmd_args` changed from `-force-d3d11` to empty, and
  `xxmi_dll_inject_mode` from `DIRECT` to `SKIP`. Whole-JSON comparison and exact
  readback preserve every other byte, including signed values. Launcher JSON
  before/after SHA-256: `edbc7276796c77bfe5967718e21972641c4235675802d7c56c29f98b93f3c0cd`
  / `8638301e0fe19210713f043dac482884457b22eb1b8895982bd1838d0d3a3fc4`.
- Actual-config smoke also passes on `dist` through both entries. Reports:
  `build/character-assets-config-next-0.9.6.json` and
  `build/character-assets-config-dist-0.9.6.json`. The historical game log is
  unchanged. The full suite was not repeated on identical deployed bytes.

The settings are probe mode, resources on, image hashing and tracker
profiling off, `m2_enabled = 1`, and the material package. The first user-started
0.9.6 session refused initialization as recorded above. After the 0.9.7 update,
verify its loaded version, image readiness/publication, replacement/material
counters, settled pending counts and any refusal reason. Check whole-outfit
appearance and motion at LOD0 and LOD1, including the earlier flicker fix. Neither
static materials nor that fix has visual game acceptance yet. No long-duration
or LOD2+ test is required.

## Fragment shaders without a profile (0.9.9)

A profile is tied to the exact bytes of a fragment shader, and its rules say at which Vulkan
binding each of the mod's fixed source slots lands. The game has more shaders per component than
were captured: the dithered fade alone gives every pass of every component a second fragment
shader. 0.9.6 to 0.9.8 refused a draw with such a shader and kept the whole original component.
In the game that mixes the original model with the mod's (session of 2026-10-13 with 0.9.8).

What the captured frames show for all 25 such shaders on three-set pipelines: the variant has
the images of one profiled shader of the same component and kind of pass, bound to the same
textures, plus one image (a 256 x 256 noise texture), at bindings moved by one or two. Example,
component 0, lit pass: profile 09e0be38 has slot 15 at (1,6), 16 at (1,3), 17 at (1,1); the
variant f1ded4ae holds the same three textures at (1,8), (1,4) and (1,2).

0.9.9 took the rules of a profiled shader, the sibling, to such a variant through the images the
application had bound (scripts/sibling_replay.py and sibling_check.py checked 25 captured
variants). That was a heuristic: it kept a variant looking like its sibling, not the source API's
slots, and it carried over rules that put a texture into an unrelated slot.

Since 2026-10-19 the rules of every fragment shader without a profile come from its own module
by the DX11 register rule (D-076, docs/source-slots.md): for each slot the package binds, the
module's t register of that number is looked up (shader resource views ranked in declaration
order, read-only buffers first). No register there: the slot is absent, as an unread slot is
under DX11. One image there: the slot binds it (or, under the package's device-bound dimension
policy, the zero file at a 3D image). Anything else, or a module the rule cannot read: the
application's images stay on the mod's mesh. The sibling search, its signatures and
`material_sibling.cpp` are gone.

For the example above the rule gives the variant f1ded4ae t15 (1,8), t16 (1,4), t17 (1,2), the
same textures the sibling search found, and the profile 09e0be38 its captured (1,6), (1,3),
(1,1). At setup the backend checks every profile of the package against the rule and reports a
difference (`material_rule_mismatches`, a log line); the profile stays in use.

`material_derived` and `material_inherited` count replaced draws of the two kinds;
`material_derived_profiles` is the number of prepared rule sets, and the log names each.
Sandbox: the fixture's module has its read-only bone block at t0 and its image at t1, so the
fixture binds slot 1; the variant with the image at binding 8 shows the mod's texture through the
rule, and the four-set pipeline's shader, which declares no image, gets every slot absent.

The game result is open.
