# EFMI Vulkan - Tasks

> Last updated: 2026-10-12.

Milestones and gates refer to `docs/plan.md`.

## Current state

T-001 to T-006 and T-014 are done: workspace, build, hashes, probe, sandbox, MCP server and inject entry. T-011 covers ordinary descriptors, templates, direct/template pushes, images and shader modules; after the review of 2026-10-11 the layer is 0.5.1 (bounded retention, reduced tracking level, D-017), verified with the full sandbox regression suite and copied to `dist/` with probe mode configured. The current round stayed offline/sandbox-only at the user's request. T-012 performance, T-015 core/synchronization validation and T-007 inventory are complete. The first probe session in the game took place on 2026-10-12 (T-010); T-008 is done since 2026-10-12 (four DX11 frames, mods on and off, paired with the Vulkan frame); next are T-017, T-018 and T-019. Historical game launches used 0.2.0 passthrough; run 3 is accepted as initial G1 loading evidence with long-duration observation deferred (D-014 / T-009). Probe-mode game data exists since 2026-10-12 for one scene.

## Tasks and follow-ups

### T-007: M0 inventory (done; static assessment)

`docs/compatibility-inventory.md` and `docs/compatibility-inputs.json` cover every lexical construct/command and direct EFMIv1 item used by the main mod and Core, plus the previously missed image panel. Pinned the exact release DLL and source commit; saved Core/mod tree and game binary/content-index fingerprints. Engine version is known; the game marketing label is not inferred. D-016 retains the hosted XXMI engine approach, with explicit adapter boundaries and a later compiled prototype. Reproducible snapshot check passes. No runtime compatibility is claimed.

### T-008: DX11 baseline capture with the sample mod (done on 2026-10-12)

State of the machine for this session: at the user's request the launcher's EFMI profile was set back to DX11 by this project (`d3d11_mode_cmd_args` = `-force-d3d11`, `xxmi_dll_inject_mode` = `DIRECT`; copy of the file before the change in `build/backups/launcher-config-before-dx11-20261012-100111.json`). The injected libraries are still `efmi_probe.dll` and `efmi_vk_inject.dll`; that value is signed and was not touched, so `dist/efmi_vk.cfg` was set to `mode = passthrough` for the DX11 session. **Before the next Vulkan session**: the two launcher values back to empty and `SKIP` (by the user, or by this project when asked), and `dist/efmi_vk.cfg` back to `mode = probe`.

Done on 2026-10-12 in the user's DX11 session (PID 9264): one full frame with the mod on screen and constant buffer contents, `captures/dx11-baseline/frame-a.*`, taken with `scripts/dx11_baseline.py` through the DX11 probe of the sibling project, and paired with `captures/first-probe`. Results and tables are in `docs/dx11-baseline.md`; facts 15 to 19 of the plan and D-019 follow from it. G3 d is answered in kind (a table per fragment shader), G3 b and c are confirmed in structure from the DX11 side (instance entry, constant 5, bone buffer), G3 e in detail (only the blend index attribute and its stride differ, plus 32-bit indices).

The frame with the mods off followed the same morning, once the user had restored the game window: the DX11 probe pressed EFMI's F11, the frame after it (`frame-off`, EFMI printed `Mods Display: Disabled`) was saved, and a second press switched the mods on again (`frame-on`, `Mods Display: Enabled`). With it all 12 original components pair draw for draw with the Vulkan frame (45 draws each side), and each slot the mod's draw sections set was looked up per pass: 15 placed, 4 one of two (T-018), 37 not declared by the shader of that pass. The table is keyed by the Vulkan fragment shader (111 entries for 36 fragment shaders over the four frames, no contradiction). The mod's own toggle keys only select index ranges and were not exercised.

### T-009: Gate G1 in the real game (initial loading accepted; long-duration observation deferred)

Route: the inject entry through the launcher, no registry (D-011). Historical G1 runs used passthrough. `dist/efmi_vk.cfg` now selects probe mode for the next short capture session (D-014).

Done on 2026-10-11 (details in CURRENT_STATE): one DX11 launch with the DLL present, 78 s, clean exit; one Vulkan launch that crashed in the NVIDIA driver about 5 s after device creation, with XXMI's `d3d11.dll` and the DX11 probe also injected (BUG-001). Timing of the injection is confirmed to be early enough.

Run 3 (20:38, only this DLL injected) passed the previous crash point. The user reports exiting before 30 minutes, with scene changes, and explicitly instructs the project to use that run's data. No game process remains; the layer log contains no device-destruction/frame-count record. This is accepted initial loading evidence, with long-duration stability still unverified. Do not require a repeat 30-minute run as a prerequisite. `dist/` now holds sandbox-verified 0.5.1 probe, ready for a user-initiated session; this version has not loaded in the real game.

What the user set in the launcher for run 3: the injection mode of the XXMI DLL to Skip (the Chinese label of that option is the one translated from "Skip"), leave only `F:\efmi vulkan\dist\efmi_vk_inject.dll` in the injected libraries, keep `d3d11_mode_cmd_args` empty, start. If the game still crashes: start it from the official launcher with nothing injected, to see whether Vulkan works by itself today. After each run read `dist\efmi_vk_layer.log`, `Player.log` and the launcher log.

To restore normal EFMI afterwards: `d3d11_mode_cmd_args` back to `-force-d3d11`, injection mode back to the first option (Direct), `efmi_probe.dll` back in the list if wanted.

The two launcher settings are independent (the user asked on 2026-10-11 whether the JSON value can go back once the injection mode is Skip; it cannot for a Vulkan session). `d3d11_mode_cmd_args` selects the game's renderer: with `-force-d3d11` the game runs Direct3D 11 and this DLL sees only the engine's probing instance. The injection mode decides whether XXMI's `d3d11.dll` is put into the process. A Vulkan session needs the value empty and the mode Skip; a normal EFMI session needs `-force-d3d11` and the mode Direct, preferably without this DLL in the injected libraries.

Where the argument lives: `d3d11_mode_cmd_args` under `Importers.EFMI.Importer` in `XXMI Launcher Config.json`; no control in the settings window, not a signed field (launcher source `core/game_launcher.py`, `build_cmd_args`).

### T-017: Snapshot at submit of the buffer ranges a matched draw uses (next; M1 to M2 bridge)

The first probe session read the instance config and the bones live, after the frame, and most reads were already overwritten (CURRENT_STATE). The layer should keep, for the draws of a captured frame whose region hash is in a list given with the request, the bytes of each uniform buffer range of at most 64 KiB as they are at submit, and for the instance config layout the bone rows at the two offsets of entry 5. With that, G3 b and c can be checked for all 12 components in one frame and across passes, and the same code path is what M2's CPU bone import reads. Also measure whether the bytes are already in place when the draw is recorded (compare a record-time and a submit-time read). Sandbox test with a mapped buffer that is rewritten every frame. The DX11 baseline fixes what to keep: the 256 bytes of the instance entry at the dynamic offset of set 2 binding 0 (16 constants; constant 5 holds the two bone offsets in 16-byte elements) and the bone rows from there.

### T-018: Tell textures apart by content (open; M1, needed by D7 and T-016)

Two textures of the same size, mip count and format cannot be paired between a DX11 and a Vulkan frame, and a slot table built from looks alone stays ambiguous there. Four such entries exist for the sample mod: component 0 main pass `ps-t16` and `ps-t17` (set 1 bindings 1 and 3), component 0 render pass 24 `ps-t16`, component 4 main pass `ps-t17`. The layer sees every upload from process start: hash the bytes of mip 0 when they are copied from a buffer into an image (`vkCmdCopyBufferToImage`, source mapped or shadowed), keep the hash with the image and report it with the descriptor contents. On the DX11 side the sibling probe can read mip 0 of a bound texture; `dx11_baseline.py` then keys on the content hash. Check whether XXMI's own texture hash can be reproduced from the same bytes: the image panel mod (T-016) matches by texture hash.

### T-019: Keep the fragment stage on the game's data in a replaced draw (open design question; before M2)

Under DX11 the core replaces the bone buffer and the instance window for the vertex shader only; the pixel shader keeps the game's and reads them (fact 19). Under Vulkan one binding serves both stages. Options: (a) in the pipeline variant, re-point the vertex module's two variables to bindings of an extra set the layer owns (two decorations each; the set layouts the game binds stay compatible because sets 0 to 2 are unchanged); (b) replace the shared bindings and show from the listings and on screen that what the fragment shader reads there (two translation terms of the component's first bone when a flag is set) stays right or does not matter. Decide with the SPIR-V of the paired fragment shaders in `captures/first-probe`.

### T-010: Gates G2 and G3 in the real game (first session done on 2026-10-12; b, c, d need more)

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

- Before the next Vulkan session: the launcher's EFMI profile back to its Vulkan values (the user, or this project when asked) and `dist/efmi_vk.cfg` back to `mode = probe` (T-008).
- Further probe sessions when T-017 is built: the same scene again, then open world and combat.
- Nothing about installations: what the project needs goes into `F:\vulkan` without asking (user, 2026-10-11). The validation layers are now installed; see T-015.
- The motivation question and the scope question of plan section 10.
