# EFMI Vulkan - Tasks

> Last updated: 2026-10-12.

Milestones and gates refer to `docs/plan.md`.

## Current state

T-001 to T-008, T-012, T-014, T-015, T-017, T-020 and T-023 are complete within their recorded scope. T-011's standard descriptor/resource paths are implemented. T-018 is complete for the display scene. T-019 has a GPU sandbox proof (D-022, D-028), not a real-character result. The stutter fix remains 0.7.3's streaming loads; T-021 is withdrawn. Source/dist/ are 0.8.1 and user-started PID 34644 supplies two actual LOD1 snapshot bundles. Full recipes and bone availability now cover the observed display, world LOD0 and actual LOD1 scenes. Continue M2 offline; no further collection is needed for bone import/remapping. See docs/m2-game-recipes.md for scope and earlier corrections.

## Tasks and follow-ups

### T-022: Owned pipeline variants and state restoration (M2; recipes and observed LOD1 bone timing captured)

0.8.0 adds complete core graphics/render-pass recipes with independent lifetimes, a C++ VS-only rewriter, private pipeline/layout construction and transactional direct indexed draw/restoration. Native ownership/error tests, real GPU pixel readback and core/sync validation pass. All 21 historical VS rewrites equal the Python reference and pass spirv-val. New `pipeline.recipe`, `layout_recipe` and `variant_preflight` make gaps explicit. Evidence and supported scope: `docs/m2-pipeline-variants.md`. The builder uses static vertex input (D-028), not a newly enabled game feature.

The user-started 0.8.0 session supplies complete supported recipes for all matched pipelines: display 39, world LOD0 45, actual LOD1 36. The new 0.8.1 actual LOD1 session adds two full snapshot frames: all 58 matched entries and both VS bone windows agree at record/submit, including all nine LOD1 sections in color subpasses. This closes the observed LOD1 timing gap (T-023). See docs/m2-game-recipes.md. Continue sample assets/bone remapping/texture patches plus the asynchronous cache and in-flight lifetime system. Probe hooks still never call the builder/emitter; no complete M2 result exists.

Review note (2026-10-12, from `captures/third-probe`): 33 of the 45 matched draws use pipelines that declare five vertex bindings, (0,16) (1,12|20|8) (2,12|4) (3,16) (4,0); binding 4 with stride 0 is the game's default tangent buffer, which the mod does not replace. The tracker keeps four slots (`kMaxVb = 4`), so a capture does not show what is bound at slot 4. The replacement vertex declaration therefore has to carry binding 4 over from the recipe unchanged, and the replaced draw rebinds and restores bindings 0 to 3 only. Raising `kMaxVb` is needed only if a later mod replaces that slot.

### T-023: Capture an interval containing graphics while keeping snapshot recording armed (done in 0.8.1; actual LOD1 verified)

The user confirmed normal LOD1 display/movement, but snapshot captures repeatedly returned empty intervals while no-snapshot frames contain graphics. 0.8.1 implements the opt-in server wait (D-029): skip empty present intervals without merging data or stopping record-time reads until a drawn interval or timeout; report skipped intervals. Native 144, evidence 113 and the full suite pass, as do candidate/deployed actual-config smokes. User-started PID 34644 now supplies two actual LOD1 frames on the first collection attempt, after 8/7 skipped intervals, with all 58 relevant entries/current/previous bone windows equal at record/submit. Evidence: captures/m2-0.8.1-20261012/ and docs/m2-game-recipes.md. No attribution to anti-cheat, frame generation or LOD rendering failure is established. The MCP wrapper's new-option exposure is a later tooling follow-up; client/CLI are available.

### T-007: M0 inventory (done; static assessment)

`docs/compatibility-inventory.md` and `docs/compatibility-inputs.json` cover every lexical construct/command and direct EFMIv1 item used by the main mod and Core, plus the previously missed image panel. Pinned the exact release DLL and source commit; saved Core/mod tree and game binary/content-index fingerprints. Engine version is known; the game marketing label is not inferred. D-016 retains the hosted XXMI engine approach, with explicit adapter boundaries and a later compiled prototype. Reproducible snapshot check passes. No runtime compatibility is claimed.

### T-008: DX11 baseline capture with the sample mod (done on 2026-10-12)

State of the machine for this session: at the user's request the launcher's EFMI profile was set back to DX11 by this project (`d3d11_mode_cmd_args` = `-force-d3d11`, `xxmi_dll_inject_mode` = `DIRECT`; copy of the file before the change in `build/backups/launcher-config-before-dx11-20261012-100111.json`). The injected libraries are still `efmi_probe.dll` and `efmi_vk_inject.dll`; that value is signed and was not touched, so `dist/efmi_vk.cfg` was set to `mode = passthrough` for the DX11 session. Reverted at 10:43 the same day, after the user had closed the game and the launcher and asked for it: both launcher values back to empty and `SKIP` (copy before the change: `build/backups/launcher-config-before-vulkan-20261012-104327.json`), `dist/efmi_vk.cfg` back to `mode = probe`.

Done on 2026-10-12 in the user's DX11 session (PID 9264): one full frame with the mod on screen and constant buffer contents, `captures/dx11-baseline/frame-a.*`, taken with `scripts/dx11_baseline.py` through the DX11 probe of the sibling project, and paired with `captures/first-probe`. Results and tables are in `docs/dx11-baseline.md`; facts 15 to 19 of the plan and D-019 follow from it. G3 d is answered in kind (a table per fragment shader), G3 b and c are confirmed in structure from the DX11 side (instance entry, constant 5, bone buffer), G3 e in detail (only the blend index attribute and its stride differ, plus 32-bit indices).

The frame with the mods off followed the same morning, once the user had restored the game window: the DX11 probe pressed EFMI's F11, the frame after it (`frame-off`, EFMI printed `Mods Display: Disabled`) was saved, and a second press switched the mods on again (`frame-on`, `Mods Display: Enabled`). With it all 12 original components pair draw for draw with the Vulkan frame (45 draws each side), and each slot the mod's draw sections set was looked up per pass: 15 placed, 4 one of two (T-018), 37 not declared by the shader of that pass. The table is keyed by the Vulkan fragment shader (111 entries for 36 fragment shaders over the four frames, no contradiction). The mod's own toggle keys only select index ranges and were not exercised.

### T-009: Gate G1 in the real game (initial loading accepted; long-duration observation deferred)

Route: the inject entry through the launcher, no registry (D-011). Historical G1 runs used passthrough. Later capture sessions used probe (D-014); `dist/efmi_vk.cfg` now prepares a 0.7.2 diagnostic probe comparison for T-020.

Done on 2026-10-11 (details in CURRENT_STATE): one DX11 launch with the DLL present, 78 s, clean exit; one Vulkan launch that crashed in the NVIDIA driver about 5 s after device creation, with XXMI's `d3d11.dll` and the DX11 probe also injected (BUG-001). Timing of the injection is confirmed to be early enough.

Run 3 (20:38, only this DLL injected) passed the previous crash point. The user reports exiting before 30 minutes, with scene changes, and explicitly instructs the project to use that run's data. No game process remains; the layer log contains no device-destruction/frame-count record. This is accepted initial loading evidence, with long-duration stability still unverified. Do not require a repeat 30-minute run as a prerequisite. `dist/` now holds sandbox-verified 0.5.1 probe, ready for a user-initiated session; this version has not loaded in the real game.

What the user set in the launcher for run 3: the injection mode of the XXMI DLL to Skip (the Chinese label of that option is the one translated from "Skip"), leave only `F:\efmi vulkan\dist\efmi_vk_inject.dll` in the injected libraries, keep `d3d11_mode_cmd_args` empty, start. If the game still crashes: start it from the official launcher with nothing injected, to see whether Vulkan works by itself today. After each run read `dist\efmi_vk_layer.log`, `Player.log` and the launcher log.

To restore normal EFMI afterwards: `d3d11_mode_cmd_args` back to `-force-d3d11`, injection mode back to the first option (Direct), `efmi_probe.dll` back in the list if wanted.

The two launcher settings are independent (the user asked on 2026-10-11 whether the JSON value can go back once the injection mode is Skip; it cannot for a Vulkan session). `d3d11_mode_cmd_args` selects the game's renderer: with `-force-d3d11` the game runs Direct3D 11 and this DLL sees only the engine's probing instance. The injection mode decides whether XXMI's `d3d11.dll` is put into the process. A Vulkan session needs the value empty and the mode Skip; a normal EFMI session needs `-force-d3d11` and the mode Direct, preferably without this DLL in the injected libraries.

Where the argument lives: `d3d11_mode_cmd_args` under `Importers.EFMI.Importer` in `XXMI Launcher Config.json`; no control in the settings window, not a signed field (launcher source `core/game_launcher.py`, `build_cmd_args`).

### T-017: Snapshot at submit of the buffer ranges a matched draw uses (done on 2026-10-12: layer 0.6.0, sandbox and game)

Result in the game (CURRENT_STATE, `docs/probe-snapshots.md`, facts 21 and 22, D-021): the instance entry and a component's own palette are complete when its draw is recorded and final at submit; the bone count comes from the mod. Not done: MCP tools for the snapshot; other scenes.

Done: request, record-time checksum, submit-time bytes, comparison states, budget, `snapshot` command, client, collector options, sandbox scenario and tests (`docs/probe-snapshots.md`, CURRENT_STATE). Open: run it in the game on the scene of the first session (`docs/first-probe-session.md` has the command) and read, for the 45 draws of the 12 components, the entry item's record state (is the instance entry in place when the draw is recorded), the follow items' record state (are the bone rows), and the bytes themselves against the DX11 values (constant 3, constant 5). Then decide D3. Not done: MCP tools for the snapshot.

Original task text:

The first probe session read the instance config and the bones live, after the frame, and most reads were already overwritten (CURRENT_STATE). The layer should keep, for the draws of a captured frame whose region hash is in a list given with the request, the bytes of each uniform buffer range of at most 64 KiB as they are at submit, and for the instance config layout the bone rows at the two offsets of entry 5. With that, G3 b and c can be checked for all 12 components in one frame and across passes, and the same code path is what M2's CPU bone import reads. Also measure whether the bytes are already in place when the draw is recorded (compare a record-time and a submit-time read). Sandbox test with a mapped buffer that is rewritten every frame. The DX11 baseline fixes what to keep: the 256 bytes of the instance entry at the dynamic offset of set 2 binding 0 (16 constants; constant 5 holds the two bone offsets in 16-byte elements) and the bone rows from there.

### T-018: Tell textures apart by content (done for the display scene on 2026-10-12)

The third Vulkan session with 0.7.0 resolves all four former ambiguities by content: 19 placed mod slots, 37 undeclared, none open. Pipeline inventory is captured: 936 created graphics pipelines; the character uses 39 objects, 26 shader pairs, three sets and no specialization/push constants. Exact mappings and evidence: docs/third-probe-session.md and docs/arcane-texture-slots.json. Upload processing accumulated about 8.1 seconds; hashing is disabled for the next session. Intermittent stutter is tracked separately as T-020, not considered solved. Reproducing XXMI's texture resource hash remains T-016 work.

Original task text:

Two textures of the same size, mip count and format cannot be paired between a DX11 and a Vulkan frame, and a slot table built from looks alone stays ambiguous there. Four such entries exist for the sample mod: component 0 main pass `ps-t16` and `ps-t17` (set 1 bindings 1 and 3), component 0 render pass 24 `ps-t16`, component 4 main pass `ps-t17`. The DX11 half exists since 2026-10-12: `captures/dx11-baseline/frame-off-tex.textures.json` has CRC-32C and SHA-256 of mip 0 (rows without padding) of the 65 file-backed textures the scene reads with the mods off, keyed by DX11 resource hash, all distinct. For the open entries: component 0 light map `9a6c6973` is `7309a055` and normal map `733efa0d` is `12076a3f`; in render pass 24 `ps-t16` `908140d5` is `91bc27d4` and `ps-t13` `151c9982` is `8fcf3b42`; component 4 `ps-t17` `0e43c0bf` is `7ebbf40f` and `ps-t12` `9750205d` is `db2f1ff8`. What is missing is the Vulkan half. The layer sees every upload from process start: hash the bytes of mip 0 when they are copied from a buffer into an image (`vkCmdCopyBufferToImage`, source mapped or shadowed), keep the hash with the image and report it with the descriptor contents. On the DX11 side the sibling probe can read mip 0 of a bound texture; `dx11_baseline.py` then keys on the content hash. Check whether XXMI's own texture hash can be reproduced from the same bytes: the image panel mod (T-016) matches by texture hash.

### T-019: Keep the fragment stage on the game's data in a replaced draw (D-022; GPU sandbox proof done, real character open)

Decision: option (a). The vertex module of the pipeline variant is re-pointed to a set of the layer's own (set 3 for all 45 draws of the sample character); the fragment module is untouched. Evidence and the rule for finding the two variables: fact 23, `scripts/stage_usage.py`, `docs/dx11-baseline.md`. First GPU check now passes in 0.8.0's synthetic sandbox: both stages read the buffers, VS uses the replacement and FS keeps the original, verified by pixel readback. C++ also reproduces all 21 historical Python rewrites exactly. Real-character rendering remains open in M2/T-022.

Original question:

Under DX11 the core replaces the bone buffer and the instance window for the vertex shader only; the pixel shader keeps the game's and reads them (fact 19). Under Vulkan one binding serves both stages. Options: (a) in the pipeline variant, re-point the vertex module's two variables to bindings of an extra set the layer owns (two decorations each; the set layouts the game binds stay compatible because sets 0 to 2 are unchanged); (b) replace the shared bindings and show from the listings and on screen that what the fragment shader reads there (two translation terms of the component's first bone when a flag is set) stays right or does not matter. Decide with the SPIR-V of the paired fragment shaders in `captures/first-probe`.

### T-020: Diagnose intermittent frame-time spikes (closed on 2026-10-12: slow reads of write-combined staging memory at unmap, fixed by the streaming loads of 0.7.3)

Result in the game with 0.7.4 (CURRENT_STATE, `docs/unmap-snapshots.md`): 3 of 87,005 present intervals over 33 ms across the user's open-world run, copies at unmap at 3.6 GB/s instead of 0.33, and the user reports the stutter gone. The paragraph below this one, from the review before that session, names the mesh pools as the cause; that was wrong (`passed_over` 0 of 144,103) and is kept only as record. Not done and not required: a foreground-only interval sample of 0.7.4; removing the copies altogether.

Review text, superseded:

Review on 2026-10-12 (CURRENT_STATE, `docs/unmap-snapshots.md`, D-026): the bytes copied at unmap were 16 MiB index and vertex pools on memory type 4, which carry the transfer-source bit; the staging buffers the copy was meant for account for 7 MiB of the session. 0.7.3 speeds up memory type 2 and hardly type 4. 0.7.4 copies transfer-only buffers and drops shadows that may be stale. Open: the user-started comparison on the same route with the settings of the last sessions; what to read is listed in `docs/unmap-snapshots.md`. The task closes when the counters show the pools passed over, the tracker timings no longer accumulate seconds in the unmap path, and the user finds the movement smooth; if spikes remain, the tracker timings of that session say where to look next.

Earlier record:

The user reports stutter during loading and steady display in the third session. Average 120 presents/s does not resolve the report. T-018's timer establishes heavy earlier upload work but only 596 microseconds in a later three-second interval. Cause of steady-scene spikes is unknown.

Done: fully sandbox-verified 0.7.1 diagnostics, actual-config smoke and real-game status-only observation (PID 31124). The user reports severe open-world stutter; the static display cannot judge it. A 39.992-second foreground interval contains 142 intervals over 16 ms, 27 over 33 ms, 12 over 50 ms, maximum 111.204 ms. Final rolling window p50 2.989 ms, p99 24.590 ms. Image-hash time and capture sequence remain zero. A later quiet interval changes focus and cannot serve as a clean comparison.

Resource-off comparison, new user-started PID 33204: settings confirmed off, zero tracked images/descriptors/retained shaders, hashing and captures zero. The selected 30.000261-second interval has 300/300 foreground polls, 74 intervals over 33 ms, 26 over 50 ms and a retained peak of 105.000 ms. Excluded earlier background/focus-transition segments. Workload differs across sessions; do not infer that disabling resources worsens performance. See docs/pacing-resources-off-session.md.

Passthrough comparison: user-started PID 14804 at 13:04:17, log confirms 0.7.1 passthrough/inject and Vulkan device creation; no pipe. The user reports no stutter. This implicates probe-only work as a group, without a timing trace or a proven individual cause.

0.7.2 removed allocation for unreadable buffer-copy fallbacks and the submit-time intermediate copy, retaining data semantics, and added default-off profile_tracker counters (D-024). Real-game PID 41044 still stutters. A 24.030379-second foreground sample has 5,093 intervals, 17 over 50 ms, retained max 129.125 ms; unmap copies 3.46 GiB in 11.625 cumulative wall seconds. Three consecutive 43-44 ms snapshots span nearly all of the largest peak; copy recording concurrently waits with zero copied bytes. See docs/pacing-tracker-session.md for retention limits and evidence.

0.7.3 uses streaming loads only for detected WC mappings on capable CPUs, preserving full snapshots and fallback semantics (D-025). Full build/regression passes, native 118, and actual-config smoke passes before/after deployment. Local Vulkan type-2 8 MiB copy improves 24.023 to 1.270 ms; this is not a game result. Next: user-started same-world comparison after verifying loaded 0.7.3. Settings stay probe, resources/hash off, profile on, default snapshots/shadows. No 30-minute prerequisite; T-020 remains open until a fresh result. Source defaults still keep profiling off.

### T-021: Index identity when the game's mesh pools are not readable (withdrawn on 2026-10-12: the premise was wrong)

The pools are not unmapped: no pool was ever inside an unmapped range in the 0.7.4 session, and all 1,154 to 1,185 direct indexed draws of three open-world frames have their hash from the mapping. The text below rested on the misread generation counters. What remains true and belongs to M2: index bytes of a pool are read from write-combined device-local memory, so the product should hash a mesh once and remember it rather than read it every frame.

Withdrawn text:

The game keeps index and vertex data in 16 MiB pools on device-local host-visible memory and maps and unmaps them while it streams (2,612 generations on 22 pools in 22 minutes). A draw whose pool is unmapped when the layer wants its index bytes cannot be hashed from the mapping, and copying pools at unmap is what stalled the game. Reading megabytes from that memory every frame is not an option for the product either.

Measure in the next open-world session, with 0.7.4: of the indexed draws of a few captures taken while moving, how many have hash source `none`; how long a pool stays unmapped. Then choose: hash mesh data when it arrives through tracked buffer copies from staging (the 0.7.2 session recorded 2 MiB of such copies in 24 s, read from cheap memory) and key a cache on buffer, offset and count; or rely on the mapping where it is always present at draw time. Do not consider keeping the application's mapping alive behind its back without a decision of its own.

### T-010: Gates G2 and G3 in the real game (display scene and a first open-world scene answered on 2026-10-12)

Open world, three frames with 0.7.4 (`captures/open-world-0.7.4/`, fact 24): G2 unchanged (one thread, two command buffers); G3 a holds, with LOD1 sections matching in the shadow pass; the character's draws are direct with one instance. Not looked at in the open world: instance entries and bones (resources were off), texture slots of the LOD1 and shadow programs. Still unobserved: combat, a second instance of the character.

State after the third session and the DX11 baseline: G2 answered; G3 a, b, c, e and f pass for the display scene; G3 d now has all 19 applicable sample slot assignments placed, including the four former ambiguities. None of this is known for the open world, combat, several instances of a character, LOD1 or scenes with indirect draws. Smoothness remains T-020 and rendering remains M2.

Result of the first session (details and evidence paths in CURRENT_STATE): G2 answered for the display scene (one thread, one command buffer with all draws, classic render passes); G3 a passes (12 of 12 LOD0 components); G3 f passes for the scene; G3 b and c strongly indicated, to be made rigorous by T-017; G3 d needs T-008, and name-based mapping is ruled out because the SPIR-V is stripped; G3 e as expected. Still to observe in later sessions: open world and combat, several instances, LOD1, scenes where indirect draws occur.

Preparation that was used: `scripts/collect_probe.py` and `docs/first-probe-session.md` preserve raw frames, API usage, matching evidence and bounded shader/pipeline exports in a labelled, hashed bundle. Sandbox evidence-export regression passes 52 checks; it cannot substitute for a game session. The next required step is actual data, not further duration-only testing.

Start by collecting `efvk_api_usage` to identify the engine's actual paths. G2 is answered by `efvk_status` and `efvk_capture_frame` (threads, command buffers, secondary buffers, submits, render pass kinds). G3 item a is answered by `efvk_match_mod` on `arcane fes 1.2`, with the corresponding character visible. Items b, c and d need T-011. Historical passthrough logs contain none of these captures; D-014 removes the repeated-duration prerequisite but does not supply missing frame data.

### T-011: Track descriptor sets, shader modules and images (standard paths implemented; extended coverage open, M1)

**Done in 0.4.0, sandbox evidence only**: ordinary `vkUpdateDescriptorSets` writes/copies, layouts and pool/set lifetimes; dynamic offset attribution (LIMIT-005 fixed), graphics/compute state separation, immutable samplers, descriptor arrays and variable counts; image/view/sampler and texel-buffer metadata; retained pipeline SPIR-V and `efvk_shader` export with declared names/bindings. The sandbox actually uses two sets, dynamic storage buffers and a sampled texture. Full suite passes; detailed evidence and limits: `docs/probe-resources.md`.

**Added in 0.5.0, sandbox evidence only**: template creation/destruction, copied layout/entry definitions, offset/stride decoding, direct and template pushes with immutable per-recording contents, partial updates and layout disturbance. Push state is separate for each command buffer and bind point; secondary execution clears primary descriptor bindings. Native tracker 62 checks; 17 GPU probe configurations cover ordinary sets, templates, pushes and push templates through both entries with primary/secondary recording. Full suite passes; exact scope is in `docs/probe-resources.md`.

**Remaining**: descriptor buffers, specialized descriptor types, dynamic inline pipeline layouts, swapchain image metadata and full SPIR-V reflection. Newer aliases and compute/texel push wrappers need GPU coverage. Prioritize extended paths using actual engine API usage when available. Buffer visibility is still measured at submit, even when push values are recorded earlier; G3 b/c require separate record-time byte-availability evidence.

### T-012: Measure the overhead of the probe (done in the sandbox)

Implemented `--draws N`, warmup/sample control and host phase timings. `sandbox/benchmark.py` completed 36 runs at 500/2000 draws and 0/2 recording threads through the inject entry, comparing no layer/passthrough/probe. At 2000 draws, probe adds 0.386/0.744 ms to median recording plus submission. FIFO whole-frame time cannot measure CPU overhead by itself. Method and limits: `docs/probe-performance.md`. Timing regression: 84 checks. Game overhead and the M2 5% target remain unverified.

### T-015: Validation layers into `F:\vulkan` (done in the sandbox)

Built pinned Khronos VVL from source under `F:/vulkan`, with child-only activation. Fixed sandbox SPIR-V, shader cleanup and synchronization; 24 positive core/sync configurations plus three negative canaries pass 76 checks. Added to `run_all.py`; no registry registration or SDK installer. Initial default-cache incident and final cache confinement: `docs/validation.md`. Real-game validation is not claimed.

### T-013: Read-back from the GPU when a buffer has no CPU-visible source (open; only if G3 shows the need)

For buffers that are written by copies whose source was never visible to the CPU, or by compute. Appends a copy into a layer-owned buffer at submit time and reads it when the fence signals.

### T-016: Include the sample image panel in full-folder acceptance (M3 follow-up)

The inventory found enabled `k/1.ini` and `Draw_2D.hlsl`, with two images, cursor input and the main toggle variables. Character-only rendering cannot establish full-folder compatibility. Its HLSL pipeline and input/present behavior must be implemented or explicitly reported unsupported. This follows the G2/G3-backed M2 backend; it does not justify editing or disabling the installed mod.

## Done

- T-001 Workspace bootstrap (2026-10-11): English plan, memory system, working agreement, text check, Git.
- T-002 Vulkan headers and build (2026-10-11): `scripts/fetch_vulkan_headers.ps1`, `scripts/build.py`.
- T-003 Region hash rule against the DX11 dump (2026-10-11): 336 of 336 distinct regions match.
- T-004 Layer skeleton and sandbox (2026-10-11).
- T-005 Tracking, frame capture and probe (2026-10-11): `sandbox/probe_test.py`.
- T-006 MCP server (2026-10-11): nine tools, `.mcp.json`, `sandbox/mcp_test.py`.
- T-014 Inject entry (2026-10-11): `layer/src/bootstrap.cpp`, `efmi_vk_inject.dll`, `efmi_vk.cfg`, passive device; probe and modes tests in both entries.

## Waiting for the user

- No user action is needed for offline M2 bone import/remapping. The two 0.8.1 LOD1 snapshot frames close T-023 in the observed scene; the next game checkpoint must exercise replacement rendering, not repeat these captures or a duration-only test.
- Later Vulkan sessions for other scenes (open world, combat, a second instance of the character, LOD1 at a distance): the user starts the game and says where the character is, the agent collects with `--snapshot mod --follow instance`.
- Nothing about installations: what the project needs goes into `F:\vulkan` without asking (user, 2026-10-11). The validation layers are now installed; see T-015.
- The motivation question and the scope question of plan section 10.
