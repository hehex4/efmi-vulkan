# EFMI Vulkan - Tasks

> Last updated: 2026-10-11.

Milestones and gates refer to `docs/plan.md`.

## Current state

T-001 to T-006 and T-014 are done: workspace, build, hashes, probe, sandbox, MCP server and inject entry. T-011 covers ordinary descriptors, templates, direct/template pushes, images and shader modules; after the review of 2026-10-11 the layer is 0.5.1 (bounded retention, reduced tracking level, D-017), verified with the full sandbox regression suite and copied to `dist/` with probe mode configured. The current round stayed offline/sandbox-only at the user's request. T-012 performance, T-015 core/synchronization validation and T-007 inventory are complete. The next backend milestone needs actual G2/G3 data. Historical game launches used 0.2.0 passthrough; run 3 is accepted as initial G1 loading evidence with long-duration observation deferred (D-014 / T-009). No probe-mode game data exists.

## Tasks and follow-ups

### T-007: M0 inventory (done; static assessment)

`docs/compatibility-inventory.md` and `docs/compatibility-inputs.json` cover every lexical construct/command and direct EFMIv1 item used by the main mod and Core, plus the previously missed image panel. Pinned the exact release DLL and source commit; saved Core/mod tree and game binary/content-index fingerprints. Engine version is known; the game marketing label is not inferred. D-016 retains the hosted XXMI engine approach, with explicit adapter boundaries and a later compiled prototype. Reproducible snapshot check passes. No runtime compatibility is claimed.

### T-008: DX11 baseline capture with the sample mod (open; needs the game running under DX11 with EFMI)

The existing frame dump has no modded draw (LIMIT-003). A new one with `arcane fes 1.2` on screen, including one key toggle, is needed for the G3 comparison table: per component the IB hash, first index and count, pixel shader hash, the textures at t12 to t18, vertex layout, constant buffer sizes. The EFMI MCP tools can take it.

### T-009: Gate G1 in the real game (initial loading accepted; long-duration observation deferred)

Route: the inject entry through the launcher, no registry (D-011). Historical G1 runs used passthrough. `dist/efmi_vk.cfg` now selects probe mode for the next short capture session (D-014).

Done on 2026-10-11 (details in CURRENT_STATE): one DX11 launch with the DLL present, 78 s, clean exit; one Vulkan launch that crashed in the NVIDIA driver about 5 s after device creation, with XXMI's `d3d11.dll` and the DX11 probe also injected (BUG-001). Timing of the injection is confirmed to be early enough.

Run 3 (20:38, only this DLL injected) passed the previous crash point. The user reports exiting before 30 minutes, with scene changes, and explicitly instructs the project to use that run's data. No game process remains; the layer log contains no device-destruction/frame-count record. This is accepted initial loading evidence, with long-duration stability still unverified. Do not require a repeat 30-minute run as a prerequisite. `dist/` now holds sandbox-verified 0.5.1 probe, ready for a user-initiated session; this version has not loaded in the real game.

What the user set in the launcher for run 3: the injection mode of the XXMI DLL to Skip (the Chinese label of that option is the one translated from "Skip"), leave only `F:\efmi vulkan\dist\efmi_vk_inject.dll` in the injected libraries, keep `d3d11_mode_cmd_args` empty, start. If the game still crashes: start it from the official launcher with nothing injected, to see whether Vulkan works by itself today. After each run read `dist\efmi_vk_layer.log`, `Player.log` and the launcher log.

To restore normal EFMI afterwards: `d3d11_mode_cmd_args` back to `-force-d3d11`, injection mode back to the first option (Direct), `efmi_probe.dll` back in the list if wanted.

The two launcher settings are independent (the user asked on 2026-10-11 whether the JSON value can go back once the injection mode is Skip; it cannot for a Vulkan session). `d3d11_mode_cmd_args` selects the game's renderer: with `-force-d3d11` the game runs Direct3D 11 and this DLL sees only the engine's probing instance. The injection mode decides whether XXMI's `d3d11.dll` is put into the process. A Vulkan session needs the value empty and the mode Skip; a normal EFMI session needs `-force-d3d11` and the mode Direct, preferably without this DLL in the injected libraries.

Where the argument lives: `d3d11_mode_cmd_args` under `Importers.EFMI.Importer` in `XXMI Launcher Config.json`; no control in the settings window, not a signed field (launcher source `core/game_launcher.py`, `build_cmd_args`).

### T-010: Gates G2 and G3 in the real game (prepared; needs the first user-initiated probe session)

Preparation complete: `scripts/collect_probe.py` and `docs/first-probe-session.md` preserve raw frames, API usage, matching evidence and bounded shader/pipeline exports in a labelled, hashed bundle. Sandbox evidence-export regression passes 52 checks; it cannot substitute for a game session. The next required step is actual data, not further duration-only testing.

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

- The next short probe session when the user chooses its account/timing. Use the launcher's recorded run-3 configuration and show the sample character for G3 a. A repeated 30-minute passthrough run is not required. No game run was started by this preparation round.
- Nothing about installations: what the project needs goes into `F:\vulkan` without asking (user, 2026-10-11). The validation layers are now installed; see T-015.
- The motivation question and the scope question of plan section 10.
