# Architecture Review and Open Issues (Endfield Dual-Route Scope)

Date: 2026-11-15
Reviewed source version: `3.0.4` (`D-276`, commit `69ea1cc`)
Deployed `dist/` version: `2.1.8` (`D-184`, last real-game session `2.1.7` on 2026-11-12)

## Implementation audit (2026-11-15; remediation in progress)

The findings below are review inputs, not evidence that every proposed remedy is
sufficient. Source inspection confirms the central problems, with these corrections:

- **I-01: confirmed blocker, still open.** The guards exist and conflict with the
  measured two-graphics-queue device. Null no-ops and published immutable file
  descriptors already have exemptions, so the claimed 100% refusal is too broad.
  A per-queue completion timeline protects readback completion; it does not order
  newly introduced accesses against other queues. `queue_signalled` is a bounded
  set of semaphore handles, not a resource-hazard graph. Removing the guards alone
  would be incorrect. Validation must include actual overlapping submissions and
  semaphore stage coverage, not only alternating completed frames (`--two-queues`
  currently drains the previous queue). See the [Vulkan synchronization rules](https://docs.vulkan.org/spec/latest/chapters/synchronization.html).
- **I-02: accepted priority correction.** Freeze expansion of unobserved extension
  matrices. Existing compatibility code stays covered when touched. Historical
  captures establish observed behavior; they cannot prove future engine/device
  configurations will never use other API features. Shared mechanisms remain
  independent of character identity.
- **I-03: shared resource transaction implemented in D-280 (3.0.8).**
  Baseline 3.0.7 ignores an explicit file constant on a matched own mesh (12/13).
  Matched merged, own-mesh, texture-only and application calls now share file,
  target and descriptor preflight, ordered emission and capture binding snapshots.
  Explicit aliases resolve against current shader hashes; 72 new GPU checks cover
  actual colors, ordering, post restoration, mixed uploads and sandbox exchange.
  Geometry-specific mesh/capture work still has its own helpers. This does not
  provide automatic cross-slot mapping or application inputs relocated into the
  private merged/material interface; those remain I-06 work.
- **I-04: confirmed structural limitation.** A classic pass boundary is a valid
  place for barriers, but is not sufficient by itself: allocation aliases between
  different objects, persistent descriptor requests and saved layout restoration
  state must participate in the same transaction. D-275 through D-277 provide
  storage/fallback checks that must survive this consolidation.
- **I-05: reproduced waits fixed in D-278 (3.0.6).** `d278-before.out` on 3.0.5 passes
  17/20: cold merged/texture draws perform three pipeline waits and two texture
  waits; own-mesh draws perform one pipeline wait. These scenes otherwise render
  correctly with clean core/synchronization validation. Contrary to the original
  location summary, the merged path's current await loop also remains inside
  `res_mx`. The remediation queues background work and holds pending replacement
  draws instead of waiting under resource/generation locks. The focused result is
  20/20 with zero pipeline/texture waits; the draw-readiness regression also passes
  real captured opacity/capture scenes and ordered output restoration. Other
  synchronous preparation costs and unmatched native acquisition are not covered
  by this claim. D-280 also makes matched native file acquisition nonblocking and
  fixes the idle image staging arena starving buffer uploads. Live image slices
  remain retained through completion; the draw thread does not wait for budget.
- **I-06: confirmed open compatibility work.** General in-draw application inputs,
  matched cross-slot rebinding and input-layout edits need their shared execution
  adapter. Unmatched Route 2 descriptor support is not evidence for these paths.
- **I-07: private implementation coupling fixed by D-279 (3.0.7).** The roots are in
  `scripts/build.py::ABI_ROOTS`; `scripts/build_character_module.py` does not exist.
  `character_module.h` does not directly include the three headers listed below.
  The host additionally calls image/scope helpers and reads
  `native_target_state->classic`. Old recordings and device caches can survive a
  module exchange, so moving only `native_target_bind` cannot safely remove their
  layouts from the ABI. An opaque boundary must retain the code that owns each
  object's representation. The actual replacement binary is `efmi_vk_backend.dll`.
  D-279 moves all native scope callbacks through the installed module, retains
  private PipelineVariant implementations behind an opaque owner, and stamps the
  surviving protocol explicitly in native_state.h. A sandbox module with 64 extra
  private prefix bytes exchanges into the unchanged host, including classic target,
  image/uniform persistence, own-mesh and merged draws. Private header edits keep
  the ABI; shared protocol edits still change it. No real-game exchange occurred.
- **I-08: legacy remains, but is not wholly dead.** The non-character M2 branch is
  still selectable and has a regression step. Retirement needs explicit removal
  of that configuration/test surface and retention of the host exchange glue.
  File length alone is not a functional bug.
- **I-09: verification gap confirmed, deployment claim clarified.** Verified host,
  injected and backend binaries are built in `.worktrees/fix/dist/` each round;
  the root `dist/` and real-game deployment are separate older artifacts. This
  round does not authorize loading into the game. Preparing a release candidate
  cannot close the missing real-character validation.

D-277 (`7e6eb5c`) completes the already pending uniform fallback fix before this
architecture work. Its 764 CPU checks, 52 persistence checks, existing application
images/uniforms and Vulkan validation pass. It is not a resolution of I-01.

---

## 1. Premise and Scope of This Review

This review evaluates the loader architecture under two explicit project premises established by the project owner:

1. **Dual-route architecture**:
   - **Route 1 (DX11 Compatibility)**: Run existing EFMI and XXMI Mods (`d3dx.ini` grammar, `MergedSkeleton`, `RabbitFX`, `EFMITTL`, HLSL `CustomShader`, DDS textures) on *Arknights: Endfield* running under Vulkan, without modifying the installed Mods.
   - **Route 2 (Vulkan 3DMigoto)**: Provide native 3DMigoto capabilities on Vulkan (`ShaderOverride` / `TextureOverride`, `ShaderFixes` SPIR-V replacement, `vk_binding` for `cb`/`t`/`u` descriptors and `o0`-`o7`/`oD` render targets, interactive hunting overlay, and `F8` GPU frame analysis).
2. **Single-game target (*Arknights: Endfield* only)**:
   - Both Route 1 and Route 2 target **only** *Arknights: Endfield* (`Endfield.exe`, a customized Unity `2021.3.34f5` engine), not arbitrary Vulkan 1.0-1.4 applications.

### Measured Vulkan Profile of *Arknights: Endfield*

From real-game probe captures and sessions (`docs/plan.md` Facts 1-3 and 17-25, `docs/first-probe-session.md`, `docs/third-probe-session.md`, and `KNOWN_ISSUES.md` `BUG-008`, `BUG-032`, `BUG-036`, `BUG-050`, `BUG-052`, `BUG-054`), *Endfield*'s Vulkan renderer has a well-defined, fixed hardware and API profile:

- **Queues**: Requests **two graphics queues** in one unprotected graphics queue family at `vkCreateDevice` (`device->application_queue_count >= 2`, `device->material_queues.size() == 2`) and alternates submissions between those two graphics queues across frames (`BUG-008`, `BUG-050`, `BUG-052`).
- **Command buffers**: Records a single frame across multiple primary command buffers (`4` submits, `2`+ command buffers per frame) and splits a single character's component draws across separate command buffers (`BUG-054`).
- **Render passes**: Uses **classic `VkRenderPass` only** (`vkCmdBeginRenderPass` / `vkCmdEndRenderPass`, `28` passes per frame, subpass `0`, `1` sample) with concrete `VkFramebuffer` objects. Uses **zero** Vulkan 1.3 dynamic rendering (`vkCmdBeginRendering`), **zero** `VK_KHR_imageless_framebuffer`, and **zero** multi-subpass pipelines.
- **Pipelines and shader stages**: All observed graphics pipelines (`936`+ in capture sessions) use **two stages only** (`VK_SHADER_STAGE_VERTEX_BIT` + `VK_SHADER_STAGE_FRAGMENT_BIT`) plus compute (`VK_SHADER_STAGE_COMPUTE_BIT`). Uses **zero** geometry (`GS`) or tessellation (`HS`/`DS`) shaders, **zero** push constants, **zero** specialization constants, and **zero** `VK_EXT_vertex_input_dynamic_state` (dynamic state is limited to viewport, scissor, stencil reference, and depth bias).
- **Descriptors**: Binds `3` or `4` ordinary `VkDescriptorSet` slots via `vkCmdBindDescriptorSets` with dynamic uniform/storage offsets, updated via `vkUpdateDescriptorSets`. Uses **zero** `vkCmdPushDescriptorSetKHR` and **zero** `vkUpdateDescriptorSetWithTemplate` on character and scene draws.
- **Presentation**: Uses frame generation in fullscreen (`BUG-036`, `BUG-092`), producing multiple `vkQueuePresentKHR` calls per rendered scene frame.

---

## 2. Summary of Architectural Issues

| ID | Severity | Route | Area | Summary |
| :--- | :--- | :--- | :--- | :--- |
| **I-01** | **Critical** | Route 2 | Multi-Queue Execution & Analysis | `native_targets`, `native_descriptors`, `native_images`, and `gpu_analysis` hard-refuse when `application_queue_count != 1` or `material_queues.size() != 1`, which is always true in *Endfield*. |
| **I-02** | **High** | Route 2 | Scope Drift vs. Endfield Engine | ~40 versions (`D-195`..`D-276`) built adapters for Vulkan features *Endfield* never uses (Dynamic Rendering, Imageless Framebuffers, GS/HS/DS, Push Descriptors, Dynamic Vertex Input), inflating complexity and test matrices. |
| **I-03** | **High** | Both | Draw Execution Pipeline | Four separate draw executors (`record`, `record_own_mesh`, `record_textures`, `native_calls`) duplicate state restoration and prevent Route 2 `vk_binding` descriptors from working on matched mesh/texture draws. |
| **I-04** | **Medium** | Route 2 | Target & Descriptor Planner | Combinatorial pair whitelisting in `native_targets.cpp` (`independent_descriptor`) instead of a unified render-pass split + barrier + descriptor transaction planner. |
| **I-05** | **High** | Both | Frame Pacing & Locking (`L-04`) | First-use pipeline variant waits (`up to 4000 ms`) and texture binds run under `device->res_mx` (shared) and `mod_mx`, blocking *Endfield*'s resource and mapping threads (`308 ms` spikes in `ISSUE-012`). |
| **I-06** | **Medium** | Route 1 | DX11 Compatibility Gaps | In-draw owned `CustomShader` cannot read the game's `vs-t0`/`vs-cbN` bindings (e.g., Typhoeus physics); game textures cannot be rebound across pixel slots; general `InputLayout` edits beyond blend widening remain open. |
| **I-07** | **Medium** | Both | Hot-Swap ABI Boundary | Backend-internal cache and planner types (`native_targets.h`, `native_images.h`, `pipeline_variant.h`) are included in `ABI_ROOTS`, causing routine backend edits to break `EFVK_ABI` and defeat live hot-swap. |
| **I-08** | **Low** | Both | Codebase Hygiene (`L-09`) | Dead legacy paths (`m2_backend.cpp`, `.bin` round-trip validation, standalone per-Mod generation path) and monolithic source files (`draw_plan.cpp`, `state.cpp`, `layer.cpp`, `character_backend_record.cpp`). |
| **I-09** | **High** | Both | Real-Game Verification Gap | 86 offline versions (`2.1.9` through `3.0.4`) have accumulated without a deployment or real-game verification session in *Endfield*. |

---

## 3. Detailed Findings

### I-01 (Critical): Route 2 Native Execution and Frame Analysis Refuse *Endfield*'s Two-Queue Device

- **Location**:
  - `layer/src/character_backend.cpp:183, 249-258` (`character_device_setup`)
  - `layer/src/native_targets.cpp:256` (`prepare_native_targets`)
  - `layer/src/native_descriptors.cpp:171-178` (`prepare_native_descriptors`)
  - `layer/src/native_images.cpp:176-177` (`native_image_binding`)
  - `layer/src/gpu_analysis.cpp:105` (`GpuAnalysis::begin`)
- **Problem**:
  In `character_device_setup`, `device->application_queue_count` sums `info->pQueueCreateInfos[i].queueCount` from `vkCreateDevice`, and `device->material_queues` collects all queues requested by the game in its graphics queue family.
  As proven in `BUG-008` (2026-10-13, where `0.9.6` refused *Endfield* at startup until `0.9.7` added multi-queue support) and `BUG-050`/`BUG-052` (where *Endfield* alternated submissions across its two graphics queues), *Endfield* creates its `VkDevice` with **two graphics queues** (`application_queue_count >= 2`, `material_queues.size() == 2`).
  However, all four Route 2 runtime subsystems enforce a single-queue guard:
  - `native_targets.cpp:256`: `if (device->application_queue_count != 1 || device->material_queues.size() != 1) return refuse("Native output bindings require one application queue until cross-queue hazards are tracked");`
  - `native_descriptors.cpp:171`: refuses any non-immutable-file resource when `device->application_queue_count != 1`.
  - `native_images.cpp:176`: `if (device->application_queue_count != 1 || device->material_queues.size() != 1) return refuse("Native application images require one application graphics queue");`
  - `gpu_analysis.cpp:105`: `if (device->application_queue_count != 1 || device->material_queues.size() != 1) reason = "Cross-queue application resource hazards are not tracked";`
- **Impact in *Endfield***:
  Because these checks inspect static device queue counts rather than actual cross-queue resource hazards, **100% of Route 2 output bindings (`o0`-`o7`/`oD`), application buffer/image descriptor bindings (`vk_binding`), mutable file resources, and `F8` GPU frame analysis captures will unconditionally refuse in the real *Endfield* game**, even though they pass in single-queue sandbox tests.
- **Required Fix**:
  Replace the static `application_queue_count != 1` / `material_queues.size() != 1` refusal with *Endfield*'s actual per-recording / per-submission queue synchronization model (already proven for Route 1 in `D-091`, `D-117` amended / `BUG-052`, and `D-127`):
  - Track command buffer recording and submission on the queue to which the batch is submitted.
  - For `gpu_analysis.cpp`, signal a per-queue timeline semaphore on the submitting queue (`vkQueueSubmit`) and wait on that queue's timeline counter on the readback worker thread.
  - For `native_targets.cpp`, `native_descriptors.cpp`, and `native_images.cpp`, scope pass splits, layout transitions, and barriers to the command buffer being recorded, and use `Device::queue_signalled` (`D-127`) or per-queue timeline publication when cross-submission visibility is required.

---

### I-02 (High): Route 2 Scope Drift Toward Generic Vulkan 1.3 Features Unused by *Endfield*

- **Location**:
  - `layer/src/native_targets.cpp` (`D-231`..`D-246` dynamic rendering `vkCmdBeginRendering`/`KHR`, suspended/resumed rendering flags, local-read attachment mapping, `D-230`/`D-236`/`D-247` imageless framebuffers)
  - `layer/src/pipeline_variant.cpp` & `layer/src/state.cpp` (`D-195` `GS`/`HS`/`DS` stages, `D-223`..`D-227` `VK_EXT_vertex_input_dynamic_state` and extended dynamic states)
  - `layer/src/native_descriptors.cpp` (push descriptors and template updates)
- **Problem**:
  Between `D-195` (`2.2.5`) and `D-275` (`3.0.3`), roughly 40 releases were spent implementing and verifying Vulkan features that *Arknights: Endfield* does not enable or call:
  1. `VK_KHR_dynamic_rendering` / Vulkan 1.3 `vkCmdBeginRendering` (17 releases, `D-231`..`D-246`).
  2. `VK_KHR_imageless_framebuffer` (`D-230`, `D-236`, `D-247`).
  3. Geometry and Tessellation shader stages (`GS`/`HS`/`DS`, `D-195`).
  4. `VK_EXT_vertex_input_dynamic_state` (`vkCmdSetVertexInputEXT`) and extended dynamic topology/stride/restart states (`D-223`..`D-227`).
  5. Push descriptors (`vkCmdPushDescriptorSetKHR`) and descriptor update templates.
- **Impact**:
  Every new Route 2 feature had to be multiplied across `{classic render pass, classic imageless framebuffer, dynamic rendering core, dynamic rendering KHR} x {ordinary descriptor set, push descriptor} x {primary, secondary}`, turning single architectural steps into 10-15 micro-commits while leaving *Endfield*'s actual two-queue classic `VkRenderPass` architecture blocked by **I-01**.
- **Required Fix**:
  Freeze all non-*Endfield* Vulkan extension matrices (`vkCmdBeginRendering`, `imageless_framebuffer`, `GS`/`HS`/`DS`, `VK_EXT_vertex_input_dynamic_state`, push descriptors). Design, optimize, and test Route 2 strictly against *Endfield*'s actual rendering contract:
  - Classic `VkRenderPass` + concrete `VkFramebuffer` (subpass `0`, `1` sample).
  - `VS` + `FS` graphics pipelines and `CS` compute pipelines.
  - Ordinary `VkDescriptorSet` (`sets 0..3`, plus private set `3` or `4`) with dynamic buffer offsets.
  - Multi-command-buffer, two-graphics-queue submissions.

---

### I-03 (High): Four Split Draw Executors Duplicate State Management and Disconnect Route 1 from Route 2

- **Location**:
  - `layer/src/character_backend_record.cpp:352-1139` (`CharacterGeneration::record` — merged-skeleton character draws)
  - `layer/src/character_backend_record.cpp:1141-1235` (`CharacterGeneration::record_textures` — texture-only overrides)
  - `layer/src/character_backend_record.cpp:1242-1463` (`CharacterGeneration::record_own_mesh` — Mod custom IB/VB meshes without skeleton merge)
  - `layer/src/native_execution.cpp:114-222` (`native_calls` — unmatched `ShaderOverride` / native draw and dispatch lists)
- **Problem**:
  In original 3DMigoto (`DirectX11/HackerContext.cpp`), every draw goes through one unified sequence (`BeforeDraw` -> `CommandList` -> `Draw` -> `AfterDraw`) against one immediate context.
  In this loader, draw execution is split into four separate functions that each manually orchestrate subsets of:
  - `HuntingDrawScope` / `m2_bind`
  - `prepare_shadows` (EFMITTL shadow opacity)
  - `prepare_captures` (EFMITTL translucent layer capture)
  - `prepare_native_targets` and `emit_output_sequence` (`o0`-`o7`/`oD` target switching)
  - `prepare_native_descriptors` (`vk_binding` descriptor rebinding)
  - Private descriptor set (`variant->bone_set`) save and restore (`restore_application_set`)
- **Impact**:
  1. **Functional gap**: `prepare_native_descriptors` is called **only** in `native_calls()` (`native_execution.cpp:199`), and is **missing** from `record()`, `record_own_mesh()`, and `record_textures()`. Consequently, an *Endfield* Mod cannot use `vk_binding` to bind custom constant buffers, structured buffers, or textures on any draw that also replaces character geometry (`vb0`/`ib`) or matches a character `TextureOverride`.
  2. **Maintenance multiplier**: Every cross-cutting change (`D-251`..`D-269` for `oD` / `o0-o7` targets and depth filtering) had to be wired and debugged four separate times across these four functions.
- **Required Fix**:
  Unify the bottom half of draw execution into a single ` DrawTransaction` / `execute_draw_plan()` shared by all four entry points:
  1. **Plan phase (read-only under `res_mx` shared)**: Resolve geometry source (`original`, `merged_skeleton`, or `own_mesh`), shader/variant selection, descriptor writes (`RabbitFX`/`SetTextures` + `vk_binding`), output target sequence (`o0`-`o7`/`oD`), and EFMITTL shadow/translucency hooks.
  2. **Commit phase (lock-free on `CmdBuf`)**: Emit pre-pass barriers/splits, bind pipeline variant + descriptors + vertex/index buffers, emit draw sequence, and restore any overwritten application state (`gfx_bound`, private descriptor set, render pass state) in one place.

---

### I-04 (Medium): Combinatorial Whitelisting in `native_targets.cpp` Instead of Unified Pass-Boundary Planning

- **Location**:
  - `layer/src/native_targets.cpp:64-78` (`independent_descriptor`)
  - `layer/src/native_targets.cpp:251-255` (`prepare_native_targets`)
- **Problem**:
  When a command list combines output target assignments (`o0`-`o7`/`oD`) with descriptor assignments (`vk_binding` for `cb`/`t`/`u`), `prepare_native_targets` originally refused all combinations (`D-270`), and `D-271` through `D-275` incrementally whitelisted specific pairs inside `independent_descriptor()`:
  - `D-271`: Output + Constant Buffer (`Constant`)
  - `D-272`: Output + Buffer SRV
  - `D-273`: Output + Read-only File Image SRV
  - `D-274`: Output + Explicit `null` SRV
  - `D-275`: Output + Disjoint Memory Range
  Meanwhile, `Output + Application Image SRV` and `Output + UAV` are still refused (`native_targets.cpp:253`).
- **Impact**:
  In *Endfield*'s classic `VkRenderPass` architecture, switching `o0`-`o7` or `oD` already ends the current render pass (`vkCmdEndRenderPass`) before beginning the replacement render pass (`vkCmdBeginRenderPass`). That pass boundary is the exact legal point in Vulkan to emit `vkCmdPipelineBarrier` for image layout transitions (`COLOR_ATTACHMENT_OPTIMAL` <-> `SHADER_READ_ONLY_OPTIMAL` / `GENERAL`) before binding the cloned `VkDescriptorSet`. Treating output planning and descriptor planning as mutually suspicious subsystems forces artificial pair-by-pair whitelisting.
- **Required Fix**:
  Merge classic render-pass splitting and `NativeImagePlan` barrier emission at the `vkCmdEndRenderPass` -> `vkCmdPipelineBarrier` -> `vkCmdBeginRenderPass` boundary, and validate only true subresource self-dependency (an image subresource cannot simultaneously be attached as a writable color/depth target and sampled as an SRV in the same draw call unless bound read-only).

---

### I-05 (High): Blocking Variant and Texture Waits Held Under `device->res_mx` and `mod_mx` (`L-04` / `ISSUE-012`)

- **Location**:
  - `layer/src/character_backend_record.cpp:1195` (`record_textures`: `cache->await(..., 4000)` and `textures->bind()` under `std::shared_lock<std::shared_mutex> lock(device->res_mx)` and `std::lock_guard<std::mutex> mod_lock(*mod_mx)`)
  - `layer/src/character_backend_record.cpp:1360-1365` (`record_own_mesh`: `cache->await(..., 4000)` and `textures->bind()` under `res_mx` and `mod_mx`)
  - `layer/src/character_backend_record.cpp:699-705` (`record`: `cache->await(..., 4000)` after releasing `res_mx`, but still holding `mod_mx` while `textures->bind()` at line `621` runs inside `res_mx`)
- **Problem**:
  `device->res_mx` is a device-wide `std::shared_mutex`. Reader locks are taken on every draw, while exclusive writer locks are taken on `vkMapMemory`, `vkUnmapMemory`, `vkCreateBuffer`, `vkDestroyBuffer`, `vkUpdateDescriptorSets`, and pipeline creation/destruction across *Endfield*'s worker threads.
  Holding `res_mx` (even shared) or `mod_mx` while synchronously waiting for background compilation (`cache->await`) or synchronous first-use texture upload (`textures->bind`) blocks writer threads across the entire engine. In real-game measurements (`ISSUE-012`), first-appearance texture waits produced `308 ms` `draw_record` spikes (`321 ms` max).
- **Required Fix**:
  1. Never call `cache->await()` or blocking `textures->bind()` while holding `device->res_mx`. Copy out the needed `PipeInfo` / `DescriptorSetInfo` metadata under `res_mx`, release `res_mx`, and then perform variant/texture waits.
  2. Keep the non-blocking readiness gate (`held_before_shown`, `D-123`) for first appearance so that if a Mod's textures or variants are still uploading/compiling on background workers, the recording thread skips or defers without stalling *Endfield*'s render and streaming threads.

---

### I-06 (Medium): Remaining Route 1 DX11 Compatibility Gaps on *Endfield* (`P1-5` / `LIMIT-035`)

- **Location**:
  - `layer/src/character_program.cpp:600-890` (`CharacterProgram::plan`)
  - `docs/architecture-review-2.1.md:392-397` (`P1-5`)
- **Problem**:
  While Folder Generation (`D-131`..`D-186`) runs the 20 installed character Mods, `RabbitFX`, and `EFMITTL`, three Route 1 compatibility gaps remain for complex DX11 Mods in *Endfield*:
  1. **In-draw `CustomShader` reading game bindings**: Owned GPU work (`CustomShader`) runs at `Present` (e.g., Si NightveilTrancer's shape-key compute shaders and watch GUI), and a draw can bind the resulting owned buffers/images. However, an in-draw `CustomShader` (such as Typhoeus's chest physics `CustomShader` triggered inside a `TextureOverride`) that reads the game's live `vs-t0` or `vs-cbN` bindings cannot yet import those application descriptors into the owned compute/graphics execution scope (`LIMIT-035`).
  2. **Cross-slot application texture rebinding**: `CharacterProgram::plan` can bind Mod DDS textures orowned resources to pixel slots, and can keep an application texture in its original slot, but cannot yet rebind an application texture (`ps-tN = ref ps-tM`) to a different Vulkan descriptor binding.
  3. **General `InputLayout` edits**: Only the `BLENDINDICES` / `BLENDWEIGHTS` widening edit (`R8_UINT` -> `R16_UINT` / `R32_UINT`) is wired in the production character pipeline variant builder; general per-draw `ElementFormat` / byte-offset edits from `LIMIT-032` (`1.2.6`..`1.2.8`) are only in the standalone owned-graphics executor.
- **Required Fix**:
  Bridge the Route 2 descriptor/resource inspection (`native_resources.cpp` / `native_descriptors.cpp`) into Route 1's `CharacterProgram` so that in-draw `CustomShader` dispatches and cross-slot `ps-tN` references can read retained application `VkBuffer` and `VkImageView` bindings.

---

### I-07 (Medium): Hot-Swap `EFVK_ABI` Over-Coupling Prevents In-Game Module Exchange

- **Location**:
  - `layer/src/character_module.h:4-18`
  - `scripts/build_character_module.py` (`ABI_ROOTS`)
- **Problem**:
  By design (`AGENTS.md` and `docs/hot-swap.md`), the replacement logic (`efmi_vk_character.dll`) is a hot-swappable module so that fixes to character replacement, INI planning, or native shader handling can be swapped into a running *Endfield* process without restarting the game.
  In `D-213`, `D-231`, and `D-250` (`BUG-112`), backend headers (`native_images.h`, `native_targets.h`, `pipeline_variant.h`) were pulled directly into `character_module.h` and `ABI_ROOTS` because host functions in `layer.cpp` / `m2_backend.cpp` (`m2_bind` -> `native_target_bind`) directly read backend cache structs (`NativeClassicTarget`, `NativeClassicPipeline`, `PipelineVariant`).
- **Impact**:
  Almost any change to `PipelineVariant`, `NativeTargetState`, or `NativeImageState` now alters the `EFVK_ABI` hash, causing `probe_m2_module` (`m2_backend.cpp:525`) to refuse the new `efmi_vk_character.dll` and forcing a full game restart.
- **Required Fix**:
  Move `native_target_bind` behind the `CharacterModule` function table (`character_module->bind`) so that `NativeClassicTarget`, `NativeClassicPipeline`, and `PipelineVariant` remain private to `efmi_vk_character.dll` and out of `character_module.h` / `EFVK_ABI`.

---

### I-08 (Low): Unremoved Legacy Code Paths and Oversized Source Files (`L-09` / `P4-4`)

- **Location**:
  - `layer/src/m2_backend.cpp:15-223` (`M2Backend` single-component `.bin` prototype)
  - `layer/src/character_package.cpp` (`validate_refreshed` `.bin` round-trip check)
  - `layer/src/character_backend.cpp` (legacy per-Mod standalone generation fallback alongside `FolderGeneration`)
  - `frontend/xxmi/draw_plan.cpp` (`2390` lines), `layer/src/state.cpp` (`2295` lines), `layer/src/layer.cpp` (`2099` lines), `layer/src/character_backend_record.cpp` (`1511` lines), `layer/src/character_program.cpp` (`1467` lines)
- **Problem**:
  1. `M2Backend` (`m2_backend.cpp:15-223`) is the early single-component prototype from `0.9.1`, superseded by `CharacterBackend` and `FolderGeneration` (`2.0.0`+).
  2. `CharacterProgram::load_mod` still serializes and re-parses legacy `.bin` packages inside `validate_refreshed` even though runtime execution runs directly from `CharacterProgram` and `FolderGeneration`.
  3. Several core source files exceed 1,500-2,300 lines, mixing Vulkan hook dispatch, state tracking, and probe JSON serialization.
- **Required Fix**:
  Retire `M2Backend` (keeping only the host module exchange glue in `m2_backend.cpp`), remove the redundant `.bin` round-trip in `load_mod`, and split `draw_plan.cpp` and `state.cpp` by responsibility after **I-01** and **I-03** are addressed.

---

### I-09 (High): 86-Version Verification Gap Between Source (`3.0.4`) and Real Game (`2.1.7`)

- **Location**:
  - `dist/` (`2.1.8` built on 2026-11-12) vs. `HEAD` (`3.0.4`, `D-276`)
- **Problem**:
  Since the last real-game session on 2026-11-12 (`2.1.7`), 86 versions (`2.1.9` through `3.0.4`) have been committed offline:
  - `D-185` (`2.1.9`): EFMITTL translucent shadow opacity (`BUG-094`).
  - `D-186` (`2.2.0`): Dash after-image `cb3`/`cb4` constant-buffer bone imports (`LIMIT-027`).
  - `D-187`..`D-276` (`2.2.1`..`3.0.4`): All of Route 2 (`ShaderOverride`, `ShaderFixes`, hunting overlay, `F8` GPU frame analysis, `vk_binding` descriptors and `o0`-`o7`/`oD` targets).
  None of these 86 versions have been deployed to `dist/` or tested in a real *Arknights: Endfield* session.
- **Required Fix**:
  Before adding any further offline Route 2 surface area:
  1. Fix **I-01** so Route 2 does not immediately refuse *Endfield*'s two-queue device.
  2. Build and deploy a release candidate to `dist/` (while the game is closed) and verify `D-185` (translucent shadows), `D-186` (dash constant-buffer bones), and Route 2 hunting / `ShaderFixes` / `F8` analysis in a user-started *Endfield* session.

---

## 4. Recommended Execution Order

1. **Step 1 — Fix I-01 (Multi-queue support for Route 2 in *Endfield*)**:
   Lift the `application_queue_count != 1` and `material_queues.size() != 1` blocks in `gpu_analysis.cpp`, `native_targets.cpp`, `native_descriptors.cpp`, and `native_images.cpp` using per-queue timeline completion and recording-scoped barriers, verified with the two-queue sandbox (`--queues 2`).
2. **Step 2 — Verify in the Real Game (Close I-09)**:
   Prepare `dist/` and verify `D-185`, `D-186`, and Route 2 (`F8` analysis, hunting, `ShaderFixes`, `vk_binding`) in a live *Endfield* session.
3. **Step 3 — Unify the Draw Execution Pipeline (Fix I-03 & I-04)**:
   Consolidate `record()`, `record_own_mesh()`, `record_textures()`, and `native_calls()` into one draw transaction executor so `vk_binding` works on matched character draws and classic pass-boundary barriers replace `independent_descriptor()` whitelisting.
4. **Step 4 — Eliminate Lock Stalls and ABI Coupling (Fix I-05 & I-07)**:
   Move `cache->await()` and `textures->bind()` outside `device->res_mx`, and move `native_target_bind` behind `CharacterModule` to restore hot-swap ABI stability.
5. **Step 5 — Complete Route 1 In-Draw `CustomShader` Inputs & Clean Up Legacy Paths (Fix I-06 & I-08)**:
   Connect application `vs-t0`/`vs-cbN` reading for in-draw `CustomShader` and remove dead `.bin` / `M2Backend` legacy code.
