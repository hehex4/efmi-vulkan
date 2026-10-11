# EFMI Vulkan - Development log

> A short summary is appended at the end of each round: what was actually done, the scope of verification, open problems and where to pick up next time. No chat and no complete logs.

## 2026-10-11: Review of the v1 plan, v2 plan, workspace bootstrap

The user asked for a feasibility review of the v1 plan (a Chinese document in the EFMI output folder). The review read the local Core INIs, a sample mod, the game directory, the Unity player log and the two reference projects. Main findings: the game runs Vulkan by default; native graphics jobs are enabled; the ACE anti-cheat is present; Core issues a dispatch and a buffer copy for every replaced draw, which is illegal inside a Vulkan render pass; ReShade does not expose `vkMapMemory`; the existing frame dump has no modded draw.

A v2 plan was then written. Further reading of the XXMI source and of the mods showed that the IB hash is a plain CRC32C over the index bytes of the draw, that the two merged-skeleton compute shaders only copy and patch, and that RabbitFX mods cannot be stubbed. The plan chooses an own Vulkan layer, the mod INI plus the EFMIv1 interface as compatibility surface, and two-phase execution with CPU-side data preparation.

The user then asked for: a memory system modelled on `F:\EFMI MCP\.ai-memory`, a Git repository, no Chinese characters in the workspace, a live capture tool like the DX11 one to help development, and to start working. Answers to three questions: Vulkan headers go to `F:\vulkan`; commit locally after every verified change; the plan document is tracked.

Nothing was run in the game in this round.

## 2026-10-11: Headers, region hash check, layer probe, sandbox, MCP server

The Vulkan headers were cloned to `F:\vulkan\Vulkan-Headers` (tag `vulkan-sdk-1.4.321.0`). The LunarG installer was not run; it needs elevation and changes system settings.

`scripts/verify_region_hash.py` confirmed the region hash rule on the existing DX11 dump: 336 of 336 distinct index regions match, none mismatches. The same dump shows index buffers bound at large byte offsets.

The layer was written as a read-only probe: interception of memory, buffer, pipeline, command buffer, draw, copy, render pass, submit and present calls; index buffer shadows fed from staging copies; frame capture with region hashes; a named pipe with the framing of the EFMI MCP probe. The sandbox program draws five indexed meshes from a device-local and a mapped index buffer, optionally recording on several threads into secondary command buffers; its shaders are assembled in source because there is no shader compiler. The Python side has the pipe client, a command-line tool and a stdio MCP server with nine tools, among them `efvk_match_mod`, which answers gate item G3 a.

Verification: `sandbox/run_all.py` passes (unit 36, probe 151 / 153 / 153 with 0 / 2 / 5 threads, modes 11, MCP 35). All of it is sandbox evidence on one NVIDIA GPU without validation layers; the game was not involved and nothing was registered system-wide.

Open and next: descriptor set, shader and image tracking (T-011) for G3 b to d; the M0 inventory (T-007); a DX11 baseline with the sample mod (T-008); G1 waits for the user's decision on account and registration (T-009).

## 2026-10-11: Inject entry

The user asked whether the DLL can be loaded into the game by injection, as with d3d11, instead of registering a layer. The launcher log confirmed that XXMI injects `d3d11.dll, efmi_probe.dll` into `Endfield.exe` at start, and the EFMI MCP probe's `bootstrap.cpp` showed the export redirection it uses on the system `d3d11.dll`.

Added `layer/src/bootstrap.cpp` (loader notification, redirection of the exports of `vulkan-1.dll` through jump stubs), an inject path in the create and proc-addr hooks, the passive device for devices created before attaching, the settings file `efmi_vk.cfg`, the second output `efmi_vk_inject.dll`, attach information in the status, and three sandbox options that load a DLL early, late, or late with the functions resolved again through the exports. Layer version 0.2.0.

Verification: `sandbox/run_all.py` passes (unit 36; probe 156 / 158 / 158 with the layer entry and 158 / 160 with the inject entry; modes 40; MCP 35). The injection in these tests is the sandbox calling `LoadLibrary` itself; no cross-process injection and nothing in the game.

The user also said that anything the project needs installed goes into `F:\vulkan` without asking (D-012).

Next: T-011 (descriptor, shader and image tracking), T-007 (inventory), T-015 (validation layers into `F:\vulkan`); G1 through the launcher waits for the user (T-009).

## 2026-10-11: Preparing G1 with the user

The user added `F:\efmi vulkan\dist\efmi_vk_inject.dll` to the launcher's injected libraries and asked how to remove `-force-d3d11`. `dist\efmi_vk.cfg` with `mode = passthrough` was written at once, because from now on every launch through the EFMI profile injects the DLL. Reading the launcher source (`core/game_launcher.py`, `build_cmd_args`) showed that the argument comes from the configuration value `d3d11_mode_cmd_args`, which has no control in the settings window and no signature; the user was told how to clear it and how to restore it. Nothing was run in the game by this project, and the launcher configuration was not edited by this project.

## 2026-10-11: First launches with the DLL in the game; a crash under Vulkan

The user cleared `d3d11_mode_cmd_args` and reported that the launch failed. Read-only diagnosis from four logs: the launch before the edit (DX11) ran 78 s with the DLL present and exited cleanly; the launch after it (Vulkan) reached device creation through the hooks and crashed 5 s later inside `nvoglv64`, called from `vulkan-1` from `unityplayer`, during swapchain set-up. This project's DLL is not on the stack. XXMI's `d3d11.dll` and the DX11 probe were injected into the same process, and D3D modules were loaded right before the crash.

Recorded as BUG-001 with the cause open. Gains: the launcher's injection is early enough (4.6 to 5.5 s before `vulkan-1.dll`), the engine's creation calls pass through the hooks, and `Player.log` reports `jobified=0` under Vulkan.

The user was asked to repeat the Vulkan launch with only this DLL injected (XXMI DLL injection mode Skip), and, if that crashes as well, with nothing injected. Nothing in the launcher configuration or the game was changed by this project.
