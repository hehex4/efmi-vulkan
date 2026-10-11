# EFMI Vulkan - Tasks

> Last updated: 2026-10-11.

Milestones and gates refer to `docs/plan.md`.

## Current state

T-001 to T-006 and T-014 are done: the workspace, the headers and build, the region hash check, the layer probe, the sandbox, the MCP server and the inject entry, all verified in the sandbox only. Nothing has been loaded into the real game.

## Open

### T-007: M0 inventory (open)

`docs/compatibility-inventory.md` for `arcane.ini` and Core 1.4.8: every INI construct, command and EFMIv1 interface item actually used. Pin versions and fingerprints (game version, Core, `d3d11.dll` 1.2.2.0 and the matching XXMI source commit, the sample mod). Assess how separable the XXMI front end is, three days at most, and settle plan decision D4.

### T-008: DX11 baseline capture with the sample mod (open; needs the game running under DX11 with EFMI)

The existing frame dump has no modded draw (LIMIT-003). A new one with `arcane fes 1.2` on screen, including one key toggle, is needed for the G3 comparison table: per component the IB hash, first index and count, pixel shader hash, the textures at t12 to t18, vertex layout, constant buffer sizes. The EFMI MCP tools can take it.

### T-009: Gate G1 in the real game (waits for the user)

Route: the inject entry through the launcher, no registry (D-011). What the user does in the launcher: add `F:\efmi vulkan\dist\efmi_vk_inject.dll` to the extra libraries of the EFMI profile and remove the `-force-d3d11` argument for the run. What this project prepares: `dist\efmi_vk.cfg` with `mode = passthrough`. After the run, the log `dist\efmi_vk_layer.log` answers whether the DLL arrived before `vulkan-1.dll`, how many exports were redirected and whether device creation was seen (LIMIT-013). Needs the user's decision on the account and a go-ahead on the spot; the launcher settings are the user's to change and to restore.

State on 2026-10-11: the user has added `efmi_vk_inject.dll` to the launcher's injected libraries (next to `efmi_probe.dll`), so every EFMI launch now injects it. `dist\efmi_vk.cfg` with `mode = passthrough` was written the moment this was seen. Still to do by the user: start once without `-force-d3d11`. The argument has no control in the launcher window; it is the value `d3d11_mode_cmd_args` under `Importers.EFMI.Importer` in `XXMI Launcher Config.json` (the launcher source appends it whenever it is not empty; the field is not one of the signed ones). The other way is the launcher's Game Launch setting: Custom with a command that is only the path of `Endfield.exe`, or Manual and starting the game from the official launcher. After the run, read `dist\efmi_vk_layer.log` and the first lines of `Player.log`.

### T-010: Gates G2 and G3 in the real game (blocked on T-009)

G2 is answered by `efvk_status` and `efvk_capture_frame` (threads, command buffers, secondary buffers, submits, render pass kinds). G3 item a is answered by `efvk_match_mod` on `arcane fes 1.2`. Items b, c and d need T-011.

### T-011: Track descriptor sets, shader modules and images (open, M1)

Descriptor set layouts and set contents (`vkUpdateDescriptorSets`, templates, push descriptors), so that a draw shows which buffer range and which image each binding holds and dynamic offsets are attributed to the right set (LIMIT-005). Shader modules with their SPIR-V (hash, size, optional dump), so that names and bindings can be reflected for plan decision D7. Images and views with format and size. Extend the sandbox with a second set, a storage buffer and a sampled texture to test it.

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

- Which account G1 uses and when; for the run the user adds the DLL to the launcher's extra libraries and removes `-force-d3d11`.
- Nothing about installations: what the project needs goes into `F:\vulkan` without asking (user, 2026-10-11). The validation layers are needed from M2 on; see T-015.
- The motivation question and the scope question of plan section 10.
