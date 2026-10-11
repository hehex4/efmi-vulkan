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

## 2026-10-11: Third launch, Vulkan with only this DLL

The user set the launcher's XXMI DLL injection mode to Skip, removed the DX11 probe from the injected libraries and launched. The game passed the swapchain set-up where the previous run crashed and kept running; the user was asked to use the session as the 30-minute G1 run and to report when it ends. BUG-001 is thereby avoided and its cause strongly indicated (the DX11 hooks).

## 2026-10-11: Call counters while the G1 run was going

While the user played the G1 session, layer 0.3.0 was written: a call counter on every intercepted function, about forty counted-only functions generated by one template, the names the application resolves, the enabled extensions, the probe command `api_usage` and the tool `efvk_api_usage`. It was built and tested in `build\next\` (`EFMI_VK_DIST`), since the game held the DLL in `dist\`; `run_all.py` learned to build into that directory. All steps pass. `dist\` is to be rebuilt after the game exits.

## 2026-10-11: Ordinary descriptors, images and retained shaders; G1 early exit clarified

Resumed from CURRENT_STATE/TASKS and a screenshot of the earlier round. The user confirmed that run 3 ended before 30 minutes, after scene changes. No Endfield process was present; the layer log has no device-destruction/frame total. G1 remains incomplete. No game was started or injected during this round. The existing `dist/` DLL/configuration remain 0.2.0 passthrough; the earlier planned promotion to probe is deferred.

Implemented the ordinary-descriptor part of T-011 as layer 0.4.0: layouts and lifetimes, writes/copies, immutable samplers, variable counts, per-set/per-array dynamic offsets, compatibility disturbance, separate graphics/compute binding state, and submit-time snapshots with buffer ranges/visibility and image metadata. Added shader lifetime retention, SPIR-V hashes/export and the `efvk_shader` tool with limited declared-name/binding extraction. Templates, push descriptors and descriptor buffers explicitly invalidate stale knowledge and still need decoding. D-013 records the timing/lifetime decisions.

The sandbox now actually uses two descriptor sets, a dynamic storage array and a sampled texture, copies from a descriptor set it frees, destroys shader modules before drawing, and updates a storage descriptor on the rewrite trigger. New native tests cover lifetime and uncommon state semantics without a Vulkan driver. Python test artifacts now stay in `build/tests/`; compiler scratch stays in component `build/tmp/`. On this machine the `python` app alias does not launch the interpreter, so commands use `D:/Python312/python.exe` explicitly.

Verification this round: `EFMI_VK_DIST=F:/efmi vulkan/build/next` and `D:/Python312/python.exe -B sandbox/run_all.py`, all steps pass. Build/text clean; Python 44/44; native tracker 37/37; layer probe 385/385, 387/387, 387/387; inject probe 387/387, 389/389; modes 40/40; MCP 46/46. Evidence is sandbox/CPU-only; no visual review, validation layers, overhead measurements or real-character verification. `docs/probe-resources.md` records schema, limitations and the exact tested paths.

Next: T-011 template/push decoding and sandbox cases; T-012 overhead measurement; T-007 offline compatibility inventory. G1's complete 30-minute session and all further game loading still need the user's on-the-spot instruction.

## 2026-10-11: Reuse the prior G1 evidence and prepare the probe session

The user questioned the 30-minute requirement and then explicitly instructed the project to use the previous run's data. D-014 and the plan now accept historical run 3 as initial loading evidence, with long-duration stability deferred. A repeated 30-minute passthrough session is no longer a prerequisite. The run remains 0.2.0 passthrough evidence; no duration/frame total or missing probe data was inferred.

After confirming no game or sandbox process was running, preserved the old DLLs, manifest, configuration and historical log in `build/backups/before-probe-20261011-212452-288/`. Rebuilt 0.4.0 into `dist/` and set `dist/efmi_vk.cfg` to probe mode. The layer source did not change. The historical game log remains byte-for-byte unchanged; test logs were confined to `build/`.

Verification this round: full `sandbox/run_all.py` with `D:/Python312/python.exe`, process-local `EFMI_VK_DIST=F:/efmi vulkan/dist` and a native-test log path under the backup directory. Build/text passed; Python 44/44; native tracker 37/37; layer probe 385/385, 387/387, 387/387; inject probe 387/387, 389/389; modes 40/40; MCP 46/46. A separate sandbox used the actual `dist/` settings with no mode environment override, reported 0.4.0 probe, captured five draws, and exited with code 0 (`build/tests/sandbox-xm23wqvx/`). The layer and inject copies have identical bytes (SHA-256 `ab54243ae1db4072169e4587686a7df2c71115c7f4303fe2f116363305e07cf0`).

No game was started or injected, and no launcher or system configuration changed. Loading 0.4.0 in the real game and all G2/G3 capture evidence remain open. The next user-chosen short probe session can collect API usage and a frame with the sample character visible. Long-duration observation is a separate follow-up. Offline development can continue meanwhile.
