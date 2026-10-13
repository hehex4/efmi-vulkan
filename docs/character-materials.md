# Static whole-character materials (0.9.6)

The static character backend now replaces meshes, shared bones and pixel images for
one fixed scenario, at LOD0 and LOD1. It uses the original hosted callback plans,
verified exact-fragment correspondences and the existing shared resource, variant
and snapshot helpers. There are no component-specific material branches. This
extends D-035; it does not complete the hosted runtime in T-027.

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
never submits concurrently to the application's queue. This path currently
requires exactly one enabled unprotected graphics queue. Format support and BC
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

## Verification and deployment

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

The deployed settings are probe mode, resources on, image hashing and tracker
profiling off, `m2_enabled = 1`, and the new material package. No game was started
by the agent. Next, the user starts Vulkan and shows the character: verify loaded
0.9.6, image readiness, growing replacement/material counters, settled pending
counts and any refusal reason. Check whole-outfit appearance and motion at LOD0
and LOD1, including the earlier flicker fix. Static materials and the 0.9.5 fix
have no new real-game acceptance yet. No long-duration or LOD2+ test is required.
