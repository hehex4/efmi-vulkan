# EFMI Vulkan - Tasks

> Last updated: 2026-10-11.

Milestones and gates refer to `docs/plan.md`.

## Current state

T-001 to T-006 and T-014 are done: workspace, build, hashes, probe, sandbox, MCP server and inject entry, all verified in the sandbox. T-011 has its ordinary descriptor, image and shader path in layer 0.4.0 (`build/next/`), with full regression passing. Three historical game launches used 0.2.0 passthrough; G1 remains incomplete (T-009).

## Open

### T-007: M0 inventory (open)

`docs/compatibility-inventory.md` for `arcane.ini` and Core 1.4.8: every INI construct, command and EFMIv1 interface item actually used. Pin versions and fingerprints (game version, Core, `d3d11.dll` 1.2.2.0 and the matching XXMI source commit, the sample mod). Assess how separable the XXMI front end is, three days at most, and settle plan decision D4.

### T-008: DX11 baseline capture with the sample mod (open; needs the game running under DX11 with EFMI)

The existing frame dump has no modded draw (LIMIT-003). A new one with `arcane fes 1.2` on screen, including one key toggle, is needed for the G3 comparison table: per component the IB hash, first index and count, pixel shader hash, the textures at t12 to t18, vertex layout, constant buffer sizes. The EFMI MCP tools can take it.

### T-009: Gate G1 in the real game (in progress; next step is the user's)

Route: the inject entry through the launcher, no registry (D-011). `dist\efmi_vk.cfg` has `mode = passthrough`.

Done on 2026-10-11 (details in CURRENT_STATE): one DX11 launch with the DLL present, 78 s, clean exit; one Vulkan launch that crashed in the NVIDIA driver about 5 s after device creation, with XXMI's `d3d11.dll` and the DX11 probe also injected (BUG-001). Timing of the injection is confirmed to be early enough.

Run 3 (20:38, only this DLL injected) passed the previous crash point. The user now reports exiting before 30 minutes, with scene changes. No game process remains; the layer log contains no device-destruction/frame-count record. This is partial evidence only. A full 30-minute G1 session remains necessary. `dist/` stays at 0.2.0 passthrough; 0.4.0 is built/tested in `build/next/`. Do not switch to probe based on the early exit.

What the user set in the launcher for run 3: the injection mode of the XXMI DLL to Skip (the Chinese label of that option is the one translated from "Skip"), leave only `F:\efmi vulkan\dist\efmi_vk_inject.dll` in the injected libraries, keep `d3d11_mode_cmd_args` empty, start. If the game still crashes: start it from the official launcher with nothing injected, to see whether Vulkan works by itself today. After each run read `dist\efmi_vk_layer.log`, `Player.log` and the launcher log.

To restore normal EFMI afterwards: `d3d11_mode_cmd_args` back to `-force-d3d11`, injection mode back to the first option (Direct), `efmi_probe.dll` back in the list if wanted.

Where the argument lives: `d3d11_mode_cmd_args` under `Importers.EFMI.Importer` in `XXMI Launcher Config.json`; no control in the settings window, not a signed field (launcher source `core/game_launcher.py`, `build_cmd_args`).

### T-010: Gates G2 and G3 in the real game (blocked on T-009)

G2 is answered by `efvk_status` and `efvk_capture_frame` (threads, command buffers, secondary buffers, submits, render pass kinds). G3 item a is answered by `efvk_match_mod` on `arcane fes 1.2`. Items b, c and d need T-011.

### T-011: Track descriptor sets, shader modules and images (in progress, M1)

**Done in 0.4.0, sandbox evidence only**: ordinary `vkUpdateDescriptorSets` writes/copies, layouts and pool/set lifetimes; dynamic offset attribution (LIMIT-005 fixed), graphics/compute state separation, immutable samplers, descriptor arrays and variable counts; image/view/sampler and texel-buffer metadata; retained pipeline SPIR-V and `efvk_shader` export with declared names/bindings. The sandbox actually uses two sets, dynamic storage buffers and a sampled texture. Full suite passes; detailed evidence and limits: `docs/probe-resources.md`.

**Next**: decode descriptor update templates and push descriptors, with deterministic sandbox tests. Their intercepted paths currently invalidate stale bindings and report the unsupported path explicitly. Descriptor buffers and specialized descriptor types remain unimplemented. Shader metadata extraction is not full SPIR-V reflection. Submit-time snapshots do not answer record-time readability for G3 b/c; assess that separately once a game capture exists.

### T-012: Measure the overhead of the probe (open)

Frame time of the sandbox with many draws (it needs a `--draws N` mode) without the layer, in passthrough mode and in probe mode. Needed before a long run in the game.

### T-015: Validation layers into `F:\vulkan` (open; before M2)

The sandbox and the layer have never run under the Khronos validation layers (LIMIT-002). Put the SDK, or at least the validation layer binaries, into `F:\vulkan` and run the sandbox under them in a test's own child process. Nothing outside `F:\vulkan` may be written by the installation; if the installer cannot do that, report it instead of running it.

### T-013: Read-back from the GPU when a buffer has no CPU-visible source (open; only if G3 shows the need)

For buffers that are written by copies whose source was never visible to the CPU, or by compute. Appends a copy into a layer-owned buffer at submit time and reads it when the fence signals.

## Done

- T-001 Workspace bootstrap (2026-10-11): English plan, memory system, working agreement, text check, Git.
- T-002 Vulkan headers and build (2026-10-11): `scripts/fetch_vulkan_headers.ps1`, `scripts/build.py`.
- T-003 Region hash rule against the DX11 dump (2026-10-11): 336 of 336 distinct regions match.
- T-004 Layer skeleton and sandbox (2026-10-11).
- T-005 Tracking, frame capture and probe (2026-10-11): `sandbox/probe_test.py`.
- T-006 MCP server (2026-10-11): nine tools, `.mcp.json`, `sandbox/mcp_test.py`.
- T-014 Inject entry (2026-10-11): `layer/src/bootstrap.cpp`, `efmi_vk_inject.dll`, `efmi_vk.cfg`, passive device; probe and modes tests in both entries.

## Waiting for the user

- A new on-the-spot instruction for the account/timing of the complete G1 run; run 3 ended early. The launcher's run-3 configuration is already recorded above. No game run was started by this development round.
- Nothing about installations: what the project needs goes into `F:\vulkan` without asking (user, 2026-10-11). The validation layers are needed from M2 on; see T-015.
- The motivation question and the scope question of plan section 10.
