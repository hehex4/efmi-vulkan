# M2 first checkpoint: owned pipeline variants (0.8.0)

Date: 2026-10-12. Current-session recipes are now captured (see `m2-game-recipes.md` for scene
corrections, LOD1 snapshot limits and the log-smoke correction). Implemented and verified in the sandbox. The deployed layer still only observes
the game. No real character has been replaced, and M2's animation, texture and performance gates
remain open.

## What this adds

The previous probe retained shaders, vertex input and a few pipeline creation counters. It did
not retain the full rasterization, depth/stencil, blending or render pass state. Shader pairs alone
cannot identify the character's 39 different pipeline objects, and those missing values cannot be
recovered from the historical captures.

`pipeline_recipe.h/.cpp` now owns the core state needed to reconstruct supported graphics pipelines:

- Render pass 1 and 2 attachments, subpasses, input/color/resolve/depth references, preservation lists,
  dependencies and version-2 multiview masks. Creation and destruction are tracked; a pipeline keeps
  its immutable recipe even after the application's render pass handle is destroyed or reused.
- Vertex declarations, assembly, rasterization, multisampling and sample masks, relevant depth/stencil
  and blend state, static viewports/scissors, dynamic-state declarations, entry points and specialization
  maps/data. Shader code shares the existing retention budget rather than making another copy.
- Pointer views are built locally during the driver call. Copying or moving a recipe does not leave
  pointers into the previous object or into application-owned arrays.

Ignored state is not followed: mesh/library/extended-dynamic paths are rejected before recipe reads,
static arrays are not read when viewport/scissor is dynamic, tessellation state is not read for the
supported VS/FS stages, and depth/blend state is not read for absent/unused attachments. This follows
the [graphics pipeline state rules](https://docs.vulkan.org/refpages/latest/refpages/source/VkGraphicsPipelineCreateInfo.html).

Unsupported extensions, flags and retention limits produce `complete=false` with a reason. They
are not silently dropped. A rejected recipe never changes the application's original pipeline.
Current limits per recipe: 256 attachments/subpasses/vertex declarations, 4,096 dependencies or
specialization entries, 64 viewports/dynamic states and 1 MiB specialization data per stage.

## The replacement building block

`spirv_repoint.cpp` implements D-022 in production C++: identify exactly one storage buffer and the
uniform block at set 2 binding 0; move their VS decorations to set 3 bindings 0 and 1. Fragment code
is unchanged. Since 0.9.8 the target is the set right above the pipeline's own, 3 for a three-set
layout and 4 for a four-set one (`docs/character-backend.md`, last section). Ambiguous, malformed, grouped/ID-decorated or colliding inputs are refused.

`PipelineVariant` constructs private compatible layouts, render pass and shader modules from owned
metadata. No application creation handle is borrowed. Temporary modules/pass are destroyed after
pipeline creation, as allowed by their
[pipeline creation lifetimes](https://docs.vulkan.org/refpages/latest/refpages/source/VkGraphicsPipelineCreateInfo.html).
Every Vulkan allocation failure is handled, including a pipeline handle returned with a failing batch.

The initial profile supports three ordinary descriptor sets, a free fourth slot, no immutable
samplers or binding/layout flags, and one bone/instance buffer descriptor each. The original sets
and push constant ranges are reproduced, preserving
[pipeline layout compatibility](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html#descriptorsets-compatibility).
The new set is VS-only storage plus dynamic uniform. Variant vertex input is static, so this
checkpoint does not require enabling a new device feature in the game (D-028).

`emit_variant_draw` first checks readiness. On success it binds the private pipeline, replacement
vertices/indices and the private set, emits one direct indexed draw, then restores the original
pipeline/vertex/index bindings. Original sets and dynamic state are left untouched; both pipelines
have the same dynamic-state declarations. On failure it emits no commands, leaving the caller free
to retain the original draw. Texture patches and indirect draws are outside this helper's scope.

These routines are linked into the layer but are not called by any probe draw hook. The sandbox
calls them explicitly. A worker-thread build is exercised there; the game's asynchronous request
queue, cache and in-flight resource retirement are not implemented yet.

## Probe output

`pipeline` now also returns:

- `recipe`: complete state and render pass description, or an explicit reason. Float fields are
  lossless IEEE-754 uint32 bit patterns, marked by `float_encoding: "ieee754_bits"`.
- `layout_recipe`: set definitions including descriptor type/count/stages, flags and immutable
  sampler counts. Opaque sampler handles are not treated as sufficient information for cloning.
- `variant_preflight`: the CPU check used by the builder, with its reason, device set limit and
  the selected bone/own set bindings when supported. `supported=true` is not a driver compilation
  result, a replacement result or proof of correct animation.

The existing top-level vertex declarations, dynamic states, layout ids and push ranges remain.
`api_usage.pipeline_creation.uses` counts complete and unsupported recipes by reason. With
`resources=0`, recipes and shader code are not retained. Protocol version remains 1.

## Verification

`sandbox/run_all.py` built and tested `build/next/` successfully. Log:
`build/m2-recipes-regression-20261012.txt`.

| Scope | Result |
| --- | --- |
| Existing unit / native tracker checks | 88 / 126 |
| New native recipe checks | 120 |
| New variant runner | 37 checks over CPU tests and 9 headless Vulkan processes |
| Probe configurations | 17 configurations, 514-529 checks each |
| Modes / limits / timing | 40 / 174 / 126 |
| Existing core and synchronization validation | 76 |
| MCP / evidence export | 47 / 78 |

The native checks cover source destruction, recipe copy/move, render-pass handle reuse, specialization
bytes, static arrays, 64-sample masks, ignored poisoned pointers, shader-budget release, unsupported
paths and failure at all nine Vulkan allocation steps. The failed pipeline step deliberately returns
a live handle to check cleanup.

The headless test uses RenderPass 1 and 2 through no layer, the layer entry and the inject entry.
It builds the variant on a worker after destroying the application's creation shader modules and
render pass. It then draws into a separate compatible color/depth framebuffer and reads pixels back:

1. The original vertex layout has 12-byte stride and RGBA8_UINT bone indices; the replacement has
   16-byte stride and RGBA16_UINT indices containing 257. Original indices are uint16 at offset 16;
   replacement indices are uint32 at offset 32.
2. Both VS and FS read bone and instance buffers. Different offsets in the replacement's two buffers
   move its triangle to the opposite side. Both uniform descriptors use a nonzero dynamic offset;
   offset zero deliberately moves geometry out of view.
3. Replacement fragment data would make the output red. Readback is the original green blended with
   the blue clear color: RGBA 0,127,128,127. The original draw immediately after it, with no rebinding
   by the application, produces the same color at the original position. Background stays blue.
4. All six positive runs have zero core/synchronization errors and warnings. Three negative canaries
   confirm validation is active and aborts the known zero-sized buffer call before the driver.

Pixels and stdout are in `build/tests/variant-*/`. There is no window and no game is involved.

`scripts/verify_variant_shaders.py captures/third-probe --dist build/next` also verifies all 21
distinct historical character vertex shaders: C++ output equals the Python reference byte for byte,
and every result passes `spirv-val --target-env vulkan1.1`. Report:
`build/variant-shaders-l0syviji/report.json`. This is offline evidence on historical shaders.

## Deployment and the next game observation

The game was absent before backup and again before replacement. The exact tested files are now in
`dist/`; DLL SHA-256 is `6e95b328e5ef019ad3e6d025cad966f2e3b326b97cd34d2b0270155610d73f3d` for both entries.
Previous package: `build/backups/before-0.8.0-20261012-163359/`. Fingerprints:
`build/deployment-0.8.0.json`. Actual-config inject smokes pass before and after copying:
`build/m2-actual-config-next.json`, `build/m2-actual-config-dist.json`. They confirm full recipes,
five correct draw hashes before/after rewrite. The claimed historical-log equality check was invalid:
the temporary script used a nonexistent filename (see `m2-game-recipes.md`). The first
temporary smoke script used the wrong JSON hash path; fixing it to `draw.ib.hash` was sufficient.

Settings: probe mode, resources on, image hashing off, tracker profiling off, full unmap defaults.
Resource tracking is needed for the new metadata; image hashing remains disabled to avoid its
known loading cost. No launcher setting, installation or system-wide state was changed.

Next requires a user-started Vulkan session with the sample character visible, first in its display
scene and then in the world if available. Verify loaded 0.8.0 and settings before collecting; no
30-minute duration test is required. Use `collect_probe.py` with `--pipeline-limit 128`, the sample
mod and `--snapshot mod --follow instance`. It prioritizes matched pipelines. Preserve and report
any missing pipeline rather than treating it as supported.

Inspect `recipe.complete` and `variant_preflight` for every matched pipeline, including LOD1, plus
actual depth/blend/pass state and the record-time bone/instance evidence. Old captures cannot supply
these missing creation values. This is an observation gate before adapting the builder to real
pipelines, not yet permission to draw replacements in the game. The remaining M2 work includes
bone remapping, texture patches, asset uploads, caching/retirement and real-character animation.
