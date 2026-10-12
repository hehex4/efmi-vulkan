# EFMI Vulkan - Current development state

> Last updated: 2026-10-12. Layer 0.5.1 (probe, protocol 1), server 0.1.0. Probe mode ran in the real game for the first time on 2026-10-12: G2 is answered (one recording thread), G3 a passes (all 12 LOD0 components of the sample mod match by hash), G3 b and c are strongly indicated from live reads and still need a snapshot taken at submit. The DX11 baseline of the sample mod was captured on 2026-10-12 with the mods on and off and paired with the Vulkan frame (`docs/dx11-baseline.md`). Long-duration stability is unverified. No mod rendering exists.

## DX11 baseline of the sample mod (2026-10-12, 10:09; game under DX11, started by the user)

Data captured in the real game under DX11, PID 9264 (`Forcing GfxDevice: Direct3D 11` in `Player.log`), with XXMI's `d3d11.dll`, the DX11 probe 0.1.6 of the sibling project and this project's DLL in passthrough (it redirected the exports of `vulkan-1.dll` and saw one instance of another component; nothing else). One full frame, 30344, with constant buffer contents: `captures/dx11-baseline/frame-a.*` (not committed). Taken and summarised with the new `scripts/dx11_baseline.py`, which uses the sibling project's client as a library without writing there, and paired with `captures/first-probe` by `server/efmi_vk/dx11_pair.py`. Full account: `docs/dx11-baseline.md`. A second frame of the same session (`frame-b`, 150191), taken with the script's own `capture` command, has the same XXMI figures (113 calls, 12 and 45 compute runs).

- 346 calls, 113 by XXMI: 56 indexed draws of six mod sections and 57 dispatches of the core's two compute shaders. 18 of the 21 objects both frames contain pair draw for draw (41 draws); component 0 is drawn 7 times under both APIs.
- Instance entry: 16 constants; constant 3 the translation (-300, 300, -300), constant 5 (bone offset, previous bone offset, 258, 1.0f). The game's values (504633, 502851) become (1533, 3030) in the 256-byte copy the mod draw uses. Same structure as read under Vulkan. The second compute step ran 45 times; the Vulkan frame has 45 draws of the 12 original components.
- Under Vulkan the instance window is set 2 binding 0 in 38 of 38 paired draws; under DX11 it is `vs-cb1`, `vs-cb2`, `vs-cb3` or `vs-cb6` depending on the shader. The bone buffer (8,413,184 bytes on both sides) is a storage buffer of set 0 at binding 7, 9, 18 or 19.
- Texture slots: no common order, no names on either side, and the table belongs to the Vulkan fragment shader: one DX11 pixel shader is drawn with two Vulkan fragment shaders that place the same slot at different bindings. Component 1 main pass: `ps-t15` diffuse is set 1 binding 6, `ps-t16` light map binding 2, `ps-t17` normal map binding 5, `ps-t18` binding 3.
- Vertex input: identical except the blend indices (mod RGBA16 uint in a stride of 16, game RGBA8 uint in a stride of 12); the mod's indices are 32-bit.
- Under DX11 only the vertex stage gets the merged skeleton and the patched entry; the pixel shader keeps the game's and reads them. One Vulkan binding serves both stages (T-019).

Mods off and on again (10:29, after the user restored the game window): the DX11 probe pressed EFMI's F11 once, `frame-off` (170064) was saved with EFMI's `Mods Display: Disabled` in it, a second press gave `frame-on` (170102) with `Mods Display: Enabled`. The mods are on as before. No other key was sent.

- With the mods off the game draws the 12 components 7, 4, 4, 5, 4, 2, 3, 2, 2, 4, 4, 4 times; the Vulkan frame has the same numbers. All 45 draws pair; the instance window is set 2 binding 0 in 56 of 56 paired draws.
- The pairing now uses only the slots a shader declares (the declarations are read from the running game and saved as `<label>.shaders.json`), since DX11 leaves earlier bindings in place.
- Slot table over the four frames: 111 entries for 36 Vulkan fragment shaders, no contradiction. Of the slots the mod's draw sections set, 15 are placed, 4 are one of two (T-018), 37 are not declared by the shader of that pass.
- The mod assigns its slots in every pass. For component 0 this puts its diffuse map over a 1x1 volume texture in one pass and its normal map into the diffuse slot of render pass 24; that is how it looks under DX11, so the translation has to be by slot and pass (fact 20).

Scope: one scene, one character, the mod's own key states as found, two sessions (window sizes differ, so screen-sized textures do not pair).

Checks of this round: `sandbox/unit_test.py` 75 of 75 (31 for the pairing), `scripts/check_text.py` clean. The layer was not changed and not rebuilt; the full suite was not run.

## Machine set up for the DX11 baseline (2026-10-12 10:01; game not running)

At the user's request this project switched the launcher's EFMI profile back to DX11: `d3d11_mode_cmd_args` from empty to `-force-d3d11` and `xxmi_dll_inject_mode` from `SKIP` to `DIRECT`, two lines, with the launcher and the game not running and a copy of the file saved in `build/backups/`. `dist/efmi_vk.cfg` is `mode = passthrough` while the launcher keeps injecting this DLL under DX11. Both have to go back before the next Vulkan session (T-008). The launcher log also narrowed BUG-001: XXMI's `d3d11.dll` under Vulkan is the cause, not the DX11 probe.

## First probe session in the real game (2026-10-12, 09:47 to at least 09:55; started by the user)

Launcher configuration of run 3 (Vulkan, XXMI DLL injection mode Skip, only `efmi_vk_inject.dll` injected). `Endfield.exe` PID 8920, layer 0.5.1 in probe mode with full resource tracking. The character of `arcane fes 1.2` was on screen in what looks like a character display scene (objects placed around (-300, 300, -300)). Everything below was read through the probe pipe; nothing was written to the game. Evidence, local and ignored by Git: `captures/first-look/` (status and call counters twice, 5 s apart), `captures/first-probe/` (collector bundle of frame 30630: 84 files, `frame.json` 4.7 MB, 74 shaders, manifest `complete_with_gaps` because 68 pipelines were over the limit of 48), `captures/g3bc/g3bc.json` (two more captures with live buffer reads).

**The probe in the game.** The DLL arrived 4.8 s before `vulkan-1.dll`; 93 of 127 exports redirected; 5 instance creations and 1 device creation seen; no passive call. About 120 frames per second over a 5 s interval with full tracking (possibly the game's cap; no figure without the probe was taken). Process working set 11.5 to 11.6 GB, private 8.4 GB, flat over 7 minutes; the passthrough run of 2026-10-11 had 11.2 GB after 6 minutes. Probe holdings: 22.9 MB of SPIR-V for 1106 shaders (budget 512 MiB, nothing dropped), 2888 live descriptor sets, 6472 images, 22 MB of shadow. No crash, dialog or disconnect was reported by the user in these minutes.

**G2, recording and submission (answered for this scene).** One recording thread. Per frame: 2 command buffers, 4 submits, 28 classic render passes, no secondary command buffers, no dynamic rendering; 238 to 244 draws (215 to 221 indexed, 23 non-indexed, none indirect) and 47 or 48 compute dispatches, all outside render passes. All draws and dispatches of the captured frame are in one command buffer and one submit. Maxima since start (loading included): 3 threads, 8 command buffers, 30 submits. `Player.log` says `jobified=0`. Entry points in use: `vkQueueSubmit`, `vkCmdBeginRenderPass`, `vkCmdBindVertexBuffers`, `vkCmdBindIndexBuffer`, `vkCmdDrawIndexed`; `vkCmdDrawIndexedIndirect` and `vkCmdDispatchIndirect` have been called earlier but not in this scene.

**Descriptor path.** Per frame about 190 `vkAllocateDescriptorSets`, 188 `vkUpdateDescriptorSetWithTemplate`, 2 `vkResetDescriptorPool`, 281 `vkCmdBindDescriptorSets`: sets are allocated and written through core 1.1 templates every frame and released by pool reset. No ordinary `vkUpdateDescriptorSets` and no push descriptors in the interval, although `VK_KHR_push_descriptor` is enabled. All sets of the captured frame were tracked; 1.7 MB of descriptor contents, nothing omitted.

**G3 a, index hashes (passes).** `arcane.ini` has 21 overrides with a hash. All 12 LOD0 entry points (components 0 to 11) match draws of the frame by region hash, first index and index count; the 9 that do not match are the LOD1 entries, which are not drawn with the character close. Every one of the 215 indexed draws was hashed from the application's own mapping (`src: mapped`); no shadow was needed and `unknown_writes` is 0. Geometry lives in 16 MiB pool buffers (usage vertex, index, storage, transfer) in host-visible memory that stays mapped; 5 of them serve the frame, all with 16-bit indices and non-zero bind offsets, as under DX11. Each component is drawn in up to four passes (per pass: 10, 11, 17 and 7 character draws in render passes 5, 8, 21 and 24).

**G3 b, instance config (strongly indicated).** Every character draw binds, as set 2 binding 0, a dynamic uniform buffer of exactly 65536 bytes (4096 constants, as EFMI checks under DX11) in a mapped scratch buffer that alternates between two buffers from frame to frame. A live read right after a capture gave, for 8 of 12 components, floats 12 to 14 = (-300, 300, -300) and entry 5 = (bone offset, second bone offset, 258, 1.0f).

**G3 c, bones (strongly indicated).** The vertex shaders of the character use a storage buffer of 8413184 bytes (buffer 37, mapped) besides the instance config. At `entry5.x * 16` and `entry5.y * 16` bytes it holds rows of four floats that form 3x4 matrices: an orthonormal 3x3 part and a translation of about (-300, 301.4, -300), the same for both offsets up to the fourth decimal. This is the layout EFMI's bone importer reads (`src_offsets.x + 3 + bone * 3`), with x and y as two skeletons (current and previous frame).

Why b and c are not "passed": the reads were taken after the frame, from buffers the engine rewrites every frame. In the first capture 4 of 12 components already read as zeros or other data, in the second capture 11 of 12. The values that were read are consistent with each other, but only a snapshot taken by the layer at submit can show the bytes a draw really used, and nothing yet measures whether they are in place when the draw is recorded. Indirect support: all per-frame data reaches the GPU through mapped memory (no buffer copies and no unknown writes in a steady frame).

**G3 d, textures (open, with one decision forced).** In the main colour pass the material textures of the character are in set 1 (for component 1: six textures, BC7, BC7 sRGB and BC5 at up to 2048 x 2048 plus two small RGBA8 lookups); set 0 holds 31 pass-level textures. The depth-only pass has none in set 1, the pass at render pass 8 has two. None of the 74 exported shaders contains a single `OpName`: the game's SPIR-V is stripped, so mapping by resource name (first choice of plan decision D7) is not possible. The mapping to DX11 registers t12 to t18 needs the modded DX11 baseline (T-008).

**G3 e, vertex streams (as expected, to be compared with DX11).** Four bindings with strides 16, 12, 12, 16 in one pool buffer, binding 3 at the same offset as binding 0. Attributes: position R32G32B32 float plus one float in binding 0; R32G32 float and R8G8B8A8 snorm in binding 1; R16G16B16A16 unorm (weights) and R8G8B8A8 uint (indices) in binding 2. The mod's blend stream has stride 16 with 16-bit indices, so a pipeline variant with other vertex input is needed, as plan decision D8 assumes.

**G3 f, draw shape (passes for this scene).** Direct `vkCmdDrawIndexed` with instance count 1 and first instance 0 for every character draw.

What this session does not establish: long-duration stability; behaviour in the open world or in combat, with several instances, LOD1, or indirect draws; any overhead figure; anything about rendering a mod.

## Review before the first probe session; layer 0.5.1 in `dist/` (2026-10-11; sandbox only)

The 0.4.0 / 0.5.0 work of the sections below was reviewed before any of it goes into the game (session log, D-017). The suite passed as recorded and nothing the tracker reports for the sandbox was found wrong. What was changed is how the probe behaves in a process much larger than the sandbox:

- SPIR-V is kept up to `shader_budget` (512 MiB by default), not without bound; hash and size stay for every module.
- Destroying a module or pipeline, and resetting a descriptor pool, no longer scan every retained shader or every descriptor set.
- A capture keeps at most `descriptor_budget` (64 MiB by default) of descriptor contents and counts what it left out.
- `resources = 0` is a tracking level between full probe and passthrough.
- Mesh pipelines, a collector crash on an unknown shader stage and inline modules under reduced tracking were corrected.

Verification: whole suite against `build/next/`: Python 44, native tracker 78, the 17 probe configurations unchanged, modes 40, limits 100 (new, `sandbox/limits_test.py`), timing 84, validation 76, MCP 46, evidence 52. The tested files were copied to `dist/` (DLL SHA-256 `ce676f264ae4345535fda649766aa23f34a5241a6a9f90123d88ae3a4d009ca8`); the 0.5.0 package is in `build/backups/before-0.5.1-20261011-230051/`. A sandbox run against `dist/` with the settings file as it is (inject entry, push templates, two threads) reported 0.5.1 in probe mode and captured five draws with matching hashes; the historical game log is byte-identical before and after.

Not established: anything about 0.5.1 in the game. The cost of full tracking there is unknown (LIMIT-014); `docs/first-probe-session.md` says what to read first and how to fall back. The numbers of the sections below that mention 0.5.0 describe that version.

## First-probe exporter ready; game-data boundary reached (2026-10-11)

`scripts/collect_probe.py` now saves raw frames, API counters/status before and after, INI fingerprints/matches, G2 summaries and bounded retained shader/pipeline exports. Explicit PID/target labels prevent treating a sandbox capture as game evidence; output cannot overwrite an earlier bundle. Missing post-capture objects and pipeline limits are explicit gaps; a manifest with file SHA-256 values is written last. The collector never launches or injects anything.

Sandbox verification: 52 checks through both entries, push templates and secondary buffers, including failed-query retention, evidence hashes, wrong-scope and overwrite refusal. All current M0/M1 offline prerequisites (T-007/T-012/T-015 and capture preparation) are complete. The next backend decision needs real G2/G3 data; see `docs/first-probe-session.md`. Dist remains 0.5.0 probe with the corrected sandbox; real-game loading of this version and real-character capture remain unverified. Historical run 3 remains accepted initial G1 evidence; no repeated 30-minute run is required.

Final regression against `dist/` passes: Python 44, native tracker 62, all 17 probe configurations, modes 40, timing 84, core/synchronization validation 76, MCP 46 and evidence export 52 (`build/final-offline-regression.txt`). Native tests now write only to their own `build/tests/` log. A test startup line appended to the historical game log was identified and preserved separately, and the exact original bytes were restored; the corrected native run passes 62 checks without changing that log (BUG-002). DLL and sandbox hashes match the tested `build/next/` copies.

## Compatibility inventory complete (2026-10-11; static evidence)

T-007 is complete: docs/compatibility-inventory.md plus the metadata-only compatibility-inputs.json pin 46 Core files, 86 mod files, game binary/content indices and the matching XXMI source. The local 1.2.2.0 DLL exactly matches the official v1.2.2 archive; source commit a7847eae82aaac32175c9a7d199673d5c9323bcd. Game engine version is 2021.3.34.0; no marketing/content version label was recovered, so exact selected-file hashes identify the build. The reproducible inventory check matches all saved metadata.

Corrections: arcane.ini configures 13 slots but has entry points for ids 0-11, and the folder also loads k/1.ini, an interactive HLSL image panel. Complete folder compatibility must include it; it was missing from the original inventory. D-016 retains XXMI engine hosting behind an explicit adapter after static dependency analysis; no compiled host prototype is claimed. Remaining backend decisions require real G2/G3 observations, not more duration-only loading tests.

## Vulkan validation (2026-10-11; sandbox only)

T-015 is complete. Built Khronos VVL 1.4.321 under `F:/vulkan`; the regression suite now requires it. Core/synchronization validation passes 24 positive configurations and three negative canaries: 76 checks. Fixed the sandbox's read-only SPIR-V decoration and shader cleanup; added upload barriers and per-image presentation semaphores. Layer DLL stays 0.5.0, unchanged.

Full suite passes (unit 44, native 62, 17 probe configurations, modes 40, timing 84, MCP 46); final targeted validation passes 76/76. The corrected sandbox is in `dist/`. Repeated 36 benchmark runs: at 2000 draws the median probe increment is 0.386 ms primary / 0.744 ms secondary. These supersede the earlier numbers below. Details: `docs/validation.md`, `docs/probe-performance.md`. An initial VVL run wrote a 36-byte default system temp cache; subsequent child caches are confined to project test directories and that file remains unchanged. No registry/launcher/game change. Next: T-007 inventory, then the real G2/G3 data boundary.

## Many-draw performance measurement (2026-10-11; sandbox only)

T-012 is complete. The sandbox accepts `--draws`, records raw host timings with configurable warmup/sample counts, and `sandbox/benchmark.py` compares no layer, passthrough and probe through the inject entry with rotated trial order. The layer binary is unchanged at 0.5.0. Timing output separates FIFO waiting from recording/submitting. At 2000 draws the median probe increment is 0.424 ms (primary) / 0.553 ms (two secondary recording threads); this does not establish game overhead or the M2 5% target. Full method, limitations and fingerprints: `docs/probe-performance.md`; raw evidence: `build/tests/benchmark-owy1jn92/report.json` (36 runs).

The 37-draw timing/capture regression passes 84 checks. The full suite against `build/next/` passes (Python 44, native 62, existing 17 probe configurations, modes 40, timing 84, MCP 46). Test baselines now clear inherited layer activation; failed startup also cleans up its process. The tested sandbox is copied to `dist/`; the DLL, cfg and historical game log are unchanged. Next: T-015 validation and T-007 inventory.

## Templates and push descriptors (2026-10-11; sandbox only)

The user requested continued development without entering the game. T-011 now decodes descriptor update templates and direct/template push descriptors. Template definitions retain their layouts and copy their entries; offsets/strides, arrays, buffer/image/texel descriptors and immutable samplers share the ordinary-write decoder. Core/KHR and maintenance6 entry points feed the tracker. Unknown templates and unsupported layouts still discard stale knowledge.

Push contents are immutable per recording update, retained by each draw and separate across command buffers and graphics/compute bind points. Partial writes retain compatible elements; disturbance/reset discards them. Immutable samplers must be explicitly pushed. Secondary draws retain their push contents; executing secondaries invalidates primary descriptor bindings. JSON distinguishes `snapshot: record` for pushes from `snapshot: submit` for ordinary sets. Resource metadata and buffer visibility remain submit-time observations (D-015).

- **Built/verified**: 0.5.0 under `build/next/`; full `sandbox/run_all.py` passed. Python 44, native tracker 62, modes 40, MCP 46. Final probe tests pass 17 configurations: ordinary layer 416 / 418 / 418, inject 418 / 420; templates layer 420 / 422, inject 422 / 424; direct pushes and push templates each layer 427 / 429, inject 429 / 431. New paths run with 0 and 2 recording threads through both entries.
- **Prepared in `dist/`**: copied the exact tested DLLs, manifest and sandbox/native executables and checked SHA-256 equality. Old 0.4.0 package/settings/log: `build/backups/before-0.5-20261011-214412-992/`. DLL SHA-256: `c9a8b41db3234ecebce64300d0c1dd34de259b6ebd23827516001df4c89a8005`.
- **Configuration smoke test**: `dist/` inject entry, actual cfg with no mode override, push templates and 2 threads; reported 0.5.0 probe, captured five draws, exited 0 (`build/tests/sandbox-f6c4cx5r/`). The historical game log is unchanged. No game process was running before replacement; no game was launched or injected.
- **Limits**: GPU coverage is core 1.1 templates plus KHR push commands on the existing NVIDIA GPU. Newer aliases and compute/texel pushes have tracker coverage only where described in `docs/probe-resources.md`; no wrapper/GPU coverage is claimed for them. Descriptor buffers, specialized descriptor types, dynamic inline pipeline layouts and full reflection remain open. No validation-layer, visual or overhead result was established.

Next offline work: T-012 overhead measurement, T-015 validation layers, T-007 compatibility inventory. Real-game capture remains a separate user-chosen session; it is not required to continue these tasks.

## Prior game evidence accepted; 0.4.0 probe prepared (historical, 2026-10-11)

The user instructed the project to use the previous run's data. Do not require another 30-minute passthrough run before continuing development or preparing a short probe capture. Run 3 remains historical evidence of loading and scene changes; its exact duration and frame total are unknown. Its passthrough log contains no captured draws or descriptor data.

- **Prepared**: rebuilt layer 0.4.0, both entries and the sandbox in `dist/`, with `mode = probe`. No game process was present before replacing the DLL. The old DLLs, manifest, settings and game log are preserved in `build/backups/before-probe-20261011-212452-288/`.
- **Verified this round, sandbox only**: full `sandbox/run_all.py` against `dist/`; Python 44, native tracker 37, layer probe 385 / 387 / 387, inject probe 387 / 389, modes 40, MCP 46. All passed. A separate inject sandbox read the actual `dist/efmi_vk.cfg` without a mode environment override, reported 0.4.0, captured five draws and exited normally (`build/tests/sandbox-xm23wqvx/`).
- **Evidence preserved**: the historical `dist/efmi_vk_layer.log` is byte-for-byte unchanged. Test logs stayed under `build/`. The two DLL entries have identical bytes; SHA-256 `ab54243ae1db4072169e4587686a7df2c71115c7f4303fe2f116363305e07cf0`.
- **Next game step**: when the user chooses to launch a short probe session, use run 3's launcher configuration, collect API usage and a frame for G2/G3 a with the sample character visible. No game was started this round, and 0.4.0 has not yet loaded in the real game. Long-duration stability is a separate follow-up, not a prerequisite.

## Ordinary descriptors, shader modules and images (2026-10-11; sandbox only)

T-011's ordinary descriptor path is implemented. Draws contain submit-time descriptor contents, dynamic offsets separated by set/binding/array element, buffer ids/ranges/visibility, and image/view/sampler identities, formats and dimensions. Writes, copies, immutable samplers, variable counts, layout disturbance, set/pool lifetimes and handle reuse are tracked. Graphics and compute binding state is separate.

Pipelines retain stage entry points and SPIR-V after module destruction. `efvk_shader` reads or exports the bytes and extracts declared names and direct set/binding decorations. This is limited metadata, not full reflection. Bytecode is reclaimed after its last module/pipeline reference disappears.

- **Built**: layer, sandbox and native tracker test in `build/next/` (0.4.0).
- **Verified during implementation**: full `sandbox/run_all.py`, using `D:/Python312/python.exe` and process-local `EFMI_VK_DIST=F:/efmi vulkan/build/next`; Python 44, native tracker 37, probe 385 / 387 / 387 (layer) and 387 / 389 (inject), modes 40, MCP 46. All passed. Test artifacts now stay under `build/tests/`.
- **Not established**: game loading of 0.4.0, game captures, real-character data, G3 record-time CPU readability, validation-layer cleanliness, visual correctness or overhead.
- **Follow-up**: templates and push descriptors were subsequently implemented in 0.5.0 above. Descriptor buffers, swapchain image metadata and full reflection remain limited; see `docs/probe-resources.md` and LIMIT-006.
- **Deployment follow-up**: the initial implementation left `dist/` at 0.2.0 passthrough. D-014 supersedes that hold: 0.4.0 is now prepared in `dist/` with probe mode, as recorded above. A new game run still requires the user's on-the-spot instruction.

## Call counters, initially staged in `build/next/` (2026-10-11; sandbox only)

Layer 0.3.0 counts every intercepted device function and adds about forty functions that are only counted (descriptor paths, image and shader creation, uploads to images, multi-draw and others), the probe command `api_usage` and the tool `efvk_api_usage`. Purpose: the first probe-mode session in the game then shows which descriptor update path, which submit, copy, map and render pass variants and which draw forms the engine really uses, which decides what T-011 has to track and which untested hooks (LIMIT-011) matter.

- Built into `build\next\` because the running game (run 3) holds `dist\efmi_vk_inject.dll`. `sandbox/run_all.py` with `EFMI_VK_DIST=build\next` passes: unit 36; probe 168 / 170 / 170 (layer entry) and 170 / 172 (inject entry); modes 40; MCP 39.
- **Follow-up completed under D-014**: the user accepts historical run 3 as initial loading evidence. The counters are included in 0.4.0, now rebuilt and verified in `dist/` with probe mode configured. No real-game counter data has been collected yet.

## First runs in the real game (2026-10-11 20:27 and 20:29; started by the user through the launcher)

The user added `efmi_vk_inject.dll` to the launcher's injected libraries and launched twice. The DLL was in passthrough mode (`dist\efmi_vk.cfg`). Evidence: `dist\efmi_vk_layer.log`, the launcher log, `Player.log` / `Player-prev.log`, `F:\EFMI MCP\dist\efmi_probe.log`.

**Run 1, 20:27, DX11** (`cmd_args='-force-d3d11'`, PID 30628; injected: XXMI `d3d11.dll`, `efmi_probe.dll`, `efmi_vk_inject.dll`):

- The DLL loaded, registered the loader notification, and `vulkan-1.dll` was mapped 5.5 s later; 7 of 7 exports redirected. One instance creation was seen (`app=vkFoo`, the engine's probing instance; no device, as expected under DX11).
- The game ran for about 78 s and exited normally (`Player-prev.log` ends with the usual memory statistics, no crash).

**Run 2, 20:29, Vulkan** (`cmd_args=''`, PID 24040; same three libraries injected):

- The DLL loaded 4.6 s before `vulkan-1.dll` was mapped; 7 of 7 exports redirected. Instance creations seen: `vkFoo`, three without application info (API 1.1), then `app=Endfield engine=Unity` (API 1.3). Device creation seen at 20:29:37 on the NVIDIA GPU with 19 extensions.
- About 5 s later the game crashed. Stack from `Player.log`: `unityplayer` -> `vulkan-1` -> `nvoglv64` (four frames inside the NVIDIA Vulkan driver). `efmi_vk_inject.dll` is not on the stack. The last line before the crash is the engine's swapchain attempt (`attempt: 0 useWideColorColorspace: 0 hdrProbe: 0)`). The modules loaded immediately before are D3D ones (`nvwgf2umx.dll`, `d3d11on12.dll`, `dcomp.dll`, `D3DSCache.dll`), which fits the NVIDIA driver setting up DXGI-based presentation for the swapchain.
- In the same process XXMI's `d3d11.dll` (3Dmigoto, which hooks D3D11 / DXGI creation) was loaded, and the DX11 probe had redirected `D3D11CreateDevice` of the system `d3d11.dll` at 20:29:28.

What this establishes:

- **Timing of the launcher's injection is early enough.** Twice the DLL was in the process several seconds before `vulkan-1.dll`. The engine's instance and device creation went through the hooks. This answers the timing and resolution questions of LIMIT-013.
- **The DLL loads and redirects in the real game under DX11 without a visible effect** (one run, 78 s; not the 30 minutes G1 asks for).
- **`jobified=0` under Vulkan as well**: `Player.log` of run 2 says `GfxDevice: creating device client; threaded=1; jobified=0`. A first answer to G2: this run did not use graphics jobs, despite `boot.config`. To be confirmed by counting recording threads once a frame is captured.
- Implicit layers the game listed under Vulkan: `VK_LAYER_NV_optimus` (disabled by the engine), `VK_LAYER_NV_present`, EOS overlay, Steam overlay, Steam Fossilize, Rockstar Social Club.

What it does not establish: the cause of the crash. Suspect, unproven: the D3D11 hooks of XXMI and of the DX11 probe inside a process whose Vulkan driver creates D3D / DXGI objects for presentation. Not excluded: this DLL (absent from the stack, and it hooks only creation and present in passthrough mode), or the game's Vulkan path failing by itself on the current game and driver versions. G1 is therefore neither passed nor failed.

**Run 3, 20:38, Vulkan, only this DLL injected** (`cmd_args=''`, PID 1660; the launcher log lists `efmi_vk_inject.dll` alone; XXMI DLL injection mode Skip, the DX11 probe removed from the list):

- The DLL loaded 4.1 s before `vulkan-1.dll`; 7 of 7 exports redirected; the same sequence of instance creations as in run 2; device creation seen at 20:38:29.
- The game went past the swapchain attempts where run 2 crashed (three `attempt: 0 ...` lines in `Player.log`, no crash) and was still running normally 50 s later, loading assets, when the logs were read. `Player.log` again says `jobified=0`.

Reading of runs 2 and 3: the same Vulkan launch crashes with XXMI's `d3d11.dll` and the DX11 probe injected and does not crash without them. That points at the DX11 hooks, not at this DLL and not at the game's Vulkan path. It is one run each, so "strongly indicated", not proven; which of the two DX11 libraries is responsible is unknown and does not matter for this project. The configuration for every Vulkan session from now on is run 3's.

**Run 3 outcome, reported by the user**: exited before 30 minutes, with scene changes. No `Endfield` process was present when checked. The layer log ends at device creation and has no destruction/frame-count record; `Player.log` has shutdown memory statistics and a JobTempAlloc allocation warning at its end. Exact duration and total frames cannot be inferred from the layer log. The user subsequently accepted this as sufficient initial loading evidence to continue (D-014); it does not establish long-duration stability.

## Inject entry (2026-10-11; sandbox only, game not involved)

The user asked whether the DLL can get into the game by injection, as with d3d11, instead of a registry entry. It can; the entry was added (D-011, architecture in `ARCHITECTURE.md`).

- `dist/efmi_vk_inject.dll` is the same binary as `efmi_vk_layer.dll`; the file name selects the entry. `dist/efmi_vk.cfg` configures a process that cannot be given environment variables.
- `sandbox/run_all.py` passes: unit 36; probe end to end 156 / 158 / 158 with the layer entry (0, 2, 5 threads) and 158 / 160 with the inject entry (0, 2 threads); modes 40; MCP 35. The inject runs establish: with the DLL loaded before Vulkan, everything the layer entry reports is reported identically (draws, hashes, read-back), `vulkan-1.dll` was found after the DLL, at least 40 exports were redirected, no passive call. Loaded after the device exists, the sandbox runs on unaffected, no device is tracked and a capture is refused. Loaded late with the device functions resolved again through the exports, more than 100 calls go through the passive device and the sandbox still runs to its end. Passthrough works in both entries with agreeing frame counts.
- Not established: injection from another process; anything in the game (LIMIT-013).

## Probe, sandbox and MCP server (2026-10-11; sandbox only, game not involved)

- **Built**: `dist/efmi_vk_layer.dll` with manifest, `dist/efmi_vk_sandbox.exe`, both with MSVC from `D:\Microsoft` against the headers in `F:\vulkan\Vulkan-Headers` (tag `vulkan-sdk-1.4.321.0`, commit `2cd90f9d`).
- **Loads in the sandbox**: the loader picks the layer up through `VK_LAYER_PATH` / `VK_INSTANCE_LAYERS` set for the child process only. Device: NVIDIA GeForce RTX 5060 Laptop GPU.
- **Captures in the sandbox**: `sandbox/run_all.py` passed in full when this was written (the current counts are in the section above). What the checks establish:
  - every draw of a frame is reported in order with index buffer id, bind offset, index size, first index, count, instances, dynamic offset, pipeline, recording thread and command buffer;
  - the region hash of each draw equals the value the sandbox computed with an independent CRC-32C, for a buffer read from the application's mapping and for a device-local buffer read from the shadow;
  - a rewrite through a staging buffer that is unmapped before the copy is recorded changes exactly the hash of the rewritten mesh, with no unknown write;
  - draws recorded on several threads into secondary command buffers arrive in execution order with the right thread and command buffer ids;
  - without the layer, and in passthrough mode, no pipe appears; in passthrough mode the layer's frame count equals the sandbox's.
- **Not established**: the rendered picture of the sandbox was not looked at; only that every Vulkan call succeeded on the driver. There is no validation layer on the machine (LIMIT-002). Overhead was not measured.

## Region hash rule checked against real DX11 data (2026-10-11; offline)

`scripts/verify_region_hash.py` on `FrameAnalysis-2026-10-07-123937`: 688 indexed draws in the log, 711 IB files, 336 distinct regions; for all 336 the CRC-32C over `[bind offset + index size * first index, index size * count)` of the dumped buffer equals the region hash in the file name; no mismatch. 23 files had no draw information in the log and were skipped. The dump also shows that the game binds shared index buffers at large byte offsets under DX11 (plan fact 14).

## Workspace (2026-10-11)

`docs/plan.md` is the English v2 plan; `.ai-memory/` and `AGENTS.md` are in place; `scripts/check_text.py` enforces the no-Chinese rule; the Git repository is on branch `main`. The final check found an existing `origin` at `https://github.com/loyal37/efmi-vulkan.git`, superseding the earlier no-remote observation. This round did not configure it or push anything; changes were committed locally. `.mcp.json` registers the MCP server `efmi-vk` for this workspace (a session picks it up when it starts).

## Next

State on 2026-10-12 after the first probe session and the DX11 baseline:

1. T-017: snapshot at submit of the instance entry and the bone rows, with a record-time against submit-time comparison; then the same scene again under Vulkan. The launcher's EFMI profile and `dist/efmi_vk.cfg` have to go back to their Vulkan values first.
2. T-018: content hash of textures at upload, so that slot tables are exact.
3. T-019: decide how the fragment stage stays on the game's data, then M2 (minimal replacement of one component).

Later sessions: open world, combat, several instances, LOD1, indirect draws. M3 must account for the image panel (T-016).

Historical run 3 remains accepted initial G1 loading evidence. A repeated 30-minute passthrough run is not a prerequisite. No real-game launch or injection was performed in this development round.
