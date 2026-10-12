# EFMI Vulkan - Known issues

> Last updated: 2026-10-12.

## LIMIT-001: Probe data and long-duration stability are not verified in the real game

Historical passthrough launches established injection timing and device interception with 0.2.0. Run 3 passed the earlier crash point and included scene changes, but the user exited before 30 minutes. The user accepts that run as initial G1 loading evidence (D-014); long-duration stability is deferred, not established. Layer 0.5.0 is prepared in `dist/`, but all its capture/resource-tracking results are sandbox-only; no probe-mode game capture or real-character verification exists. G2/G3 remain open.

## LIMIT-002: Validation dependency installed and sandbox validated

Khronos VVL 1.4.321 and SPIRV-Tools are built under `F:/vulkan`. The sandbox passes core/synchronization validation through both EFMI entries and all four descriptor modes. The full suite now requires it. No full SDK or glslc is installed; GPU-assisted and real-game validation remain unverified. See `docs/validation.md`, including the initial temporary-cache incident.

## LIMIT-003: The existing DX11 frame dump is not a merged-skeleton baseline

`FrameAnalysis-2026-10-07-123937` has zero CustomShader executions and no mod entry point hit; only the ShaderRegex checks of EFMI and RabbitFX ran. It was good for checking the region hash rule on unmodified draws, not for the merged-skeleton comparison. Since 2026-10-12 the comparison has its own frame: `captures/dx11-baseline/frame-a` (LIMIT-015).

## LIMIT-004: RabbitFX mods are out of reach for the first version

6 of the 9 local character mods pass their textures only through RabbitFX, which patches DX11 shader assembly. They cannot be degraded gracefully by stubbing RabbitFX.

## LIMIT-005: Dynamic offsets attributed to the first set (fixed in 0.4.0)

Offsets now split using the set layouts in set/binding/array order. The sandbox checks two sets with a dynamic uniform buffer and a dynamic storage-buffer array, both multi-set and nonzero-firstSet binds, through both entries and with secondary command buffers. Missing layouts produce an explicit unknown reason instead of guessed offsets.

## LIMIT-006: Extended descriptor paths and full reflection remain incomplete

Layer 0.5.0 adds templates and direct/template push descriptors to ordinary writes/copies and resource metadata. Descriptor buffers, specialized descriptor types and dynamic inline pipeline layouts remain unknown; unsupported paths discard stale knowledge. Only graphics and compute bind points are tracked. At most 256 stored elements per set per draw are emitted, with explicit counts/truncation. Swapchain images are not tracked as images yet. Unknown layout extensions have conservative compatibility across different handles. Core 1.4 and maintenance6 push wrappers and KHR template lifecycle/update aliases lack GPU coverage; see LIMIT-011.

`efvk_shader` exports retained SPIR-V and extracts direct declared names/bindings; it is not full reflection (no decoration groups, type/member layout, specializations, pipeline-library linkage or shader objects). See `docs/probe-resources.md`. G3 remains unverified in the game; b/c also need record-time availability evidence (LIMIT-008).

## LIMIT-007: A shadow knows only what passed the CPU

Sources are staging copies (source mapped when the copy is recorded or submitted, or snapshotted at unmap when at most 16 MiB), `vkCmdUpdateBuffer` and `vkCmdFillBuffer`. A buffer filled by compute, or by a copy whose source was never CPU-visible, is reported with `src: none` and counted under `unknown_writes`. There is no read-back from the GPU (T-013). `unknown_writes` in the frame statistics is the number to watch in the game.

## LIMIT-008: Hash and read-back are "as of now", not "as of the draw"

Seen in the game on 2026-10-12: the uniform scratch buffer alternates between two buffers and is rewritten every frame, so a read a few milliseconds after a capture returned the right bytes for 8 of 12 components once and for 1 of 12 the next time. T-017 adds a snapshot at submit.

The hashes of a captured frame are computed when the command buffer is submitted, after all of its recorded writes were applied; a copy recorded after a draw in the same command buffer already affects that draw's hash. `read_buffer` returns what the mapping or the shadow holds at the time of the request. For a host-visible buffer the application rewrites every frame, that is not what an earlier draw used.

Ordinary descriptor contents and all `src` visibility remain submit-time snapshots. Push descriptor values in 0.5.0 are retained when recorded, but retaining a handle/range does not prove record-time CPU byte availability. Neither path establishes G3's premise or tracks update-after-bind changes during GPU execution.

## LIMIT-009: What a draw record leaves out

Vertex buffer slots 0 to 3 and descriptor sets 0 to 3 only. Graphics and compute descriptor state are separate in 0.4.0, but compute dispatch has no sandbox GPU coverage yet. No push constants or dynamic state; indirect arguments are not read and indirect draws get no region hash.

## LIMIT-010: The frame boundary is `vkQueuePresentKHR`

An application that does not present makes a capture time out. With several devices, the probe answers for the one that presented last.

## LIMIT-011: Scope of the sandbox evidence

One GPU and driver (NVIDIA RTX 5060 Laptop). The sandbox uses classic render passes, one queue/swapchain, `vkQueueSubmit`, `vkCmdCopyBuffer`, `vkMapMemory`, two descriptor slots, storage arrays, an immutable sampler, a sampled texture and shader modules destroyed before capture. In 0.5.0 it exercises ordinary writes/copies, core 1.1 templates and KHR direct/template pushes through both entries with primary and secondary command buffers. Dynamic rendering, `2`/maintenance6 variants, core 1.4 push wrappers, KHR template lifecycle/update aliases, indirect draws, dispatch, compute/texel pushes, `vkCmdUpdateBuffer`, `vkCmdFillBuffer` and inline pipeline modules have no GPU coverage. Native C++ tests are separate CPU evidence. The rendered picture was not inspected. Core/synchronization validation and synthetic overhead measurements are in `docs/validation.md` and `docs/probe-performance.md`; neither is game evidence.

## LIMIT-012: The layer entry needs a registry entry for an elevated process

The Vulkan loader ignores path variables such as `VK_LAYER_PATH` in an elevated process, so the manifest in `dist/` would not be found there. The game probably runs elevated (EFMI's `d3dx.ini` has `require_admin = true`, and the EFMI MCP notes say the game runs as administrator). The layer entry would therefore need an implicit layer entry under HKLM, a system-wide change. The inject entry avoids this and is the entry planned for the game (D-011); the layer entry stays the fallback.

## LIMIT-013: What is and is not known about the inject entry in the game

Known from three historical launches on 2026-10-11 (CURRENT_STATE): injection precedes `vulkan-1.dll` by about 4 to 5.5 seconds, and the engine's creation calls reach the hooks. Only 0.2.0 passthrough has been in the game; probe mode and later builds have not.

Still open:

- Long-duration stability. Run 3 survived the previous crash point and included scene changes but ended early. The user accepts it as initial loading evidence, so repeating 30 minutes is not a prerequisite (D-014). BUG-001 remains strongly associated with the removed DX11 hooks.
- A module that statically imports `vulkan-1.dll` and was loaded earlier would keep its old addresses. Not observed so far: the engine's calls arrived.
- Other Vulkan users in the process (Streamline created instances of its own in run 2) go through the same redirected exports. Devices they create are tracked like the game's; the probe answers for the device that presented last.
- The sandbox tests still simulate injection with `LoadLibrary` inside the sandbox.

## BUG-001: The game crashed in the NVIDIA driver in the first Vulkan run with three libraries injected (avoided; strongly indicated cause: the DX11 hooks)

- **Seen**: 2026-10-11 20:29, PID 24040, about 5 s after device creation, while the engine set up its swapchain. Stack: `unityplayer` -> `vulkan-1` -> `nvoglv64`. Crash report: `%LOCALAPPDATA%\Temp\Hypergryph\Endfield\Crashes\Crash_2026-10-11_122943556`.
- **In the process**: XXMI `d3d11.dll` (3Dmigoto), `efmi_probe.dll` (DX11 probe; had redirected `D3D11CreateDevice` of the system `d3d11.dll`), `efmi_vk_inject.dll` in passthrough mode.
- **Not on the stack**: `efmi_vk_inject.dll`.
- **Suspect**: D3D11 / DXGI hooks active while the NVIDIA Vulkan driver creates D3D objects for presentation (D3D modules were loaded right before the crash). Unproven.
- **Not excluded**: this DLL; the game's Vulkan path by itself.
- **How to tell**: run Vulkan with only `efmi_vk_inject.dll` injected (launcher: XXMI DLL injection mode Skip); if that crashes, run Vulkan with nothing injected.
- A Vulkan session never needs XXMI's `d3d11.dll` or the DX11 probe, so the configuration for G1 is the one of the test above regardless of the outcome.
- **Attribution narrowed on 2026-10-12**: a Vulkan launch at 09:44 with XXMI `d3d11.dll`, the DX11 probe and this DLL crashed again (crash report `Crash_2026-10-12_014543454`, about 50 s after start); the launch at 09:47 with the DX11 probe and this DLL, but without XXMI's `d3d11.dll` (injection mode Skip), ran for more than nine minutes in probe mode. The DX11 probe is therefore not the cause; XXMI's `d3d11.dll` in a Vulkan process is. Two crashes with it, two clean runs without it.
- **Result of the test (run 3, 20:38)**: with only `efmi_vk_inject.dll` injected the game passed the point of the crash and kept running. Rule from now on: for a Vulkan session the launcher's XXMI DLL injection mode is Skip and the DX11 probe is not in the injected libraries.

## ISSUE-001: Other implicit Vulkan layers are registered on this machine

Steam overlay, Steam Fossilize and the Epic Online Services overlay are registered under HKLM, the OBS hook under HKCU. They may be active in the game's Vulkan path as well. When reading real-game results, remember that this layer is not the only one in the chain. The sandbox tests switch them off for their child processes.

## LIMIT-014: The cost of full tracking in the game is known for one scene only

First figures, 2026-10-12, character display scene: about 120 frames per second with full tracking (no figure without the probe; it may be the game's cap), working set flat at 11.5 to 11.6 GB over 7 minutes, 22.9 MB of SPIR-V kept for 1106 shaders, about 190 descriptor set allocations and template updates per frame. The paragraph below describes what remains unmeasured.

Sandbox overhead figures (`docs/probe-performance.md`) come from a program that does not update descriptor sets per frame and has two shader modules. In the game, every descriptor update takes the exclusive resource lock and updates ordered maps per written element, and every bind takes the lock shared. Memory is bounded for SPIR-V (512 MiB default) and for one capture's descriptor contents (64 MiB default), not for the number of live descriptor sets, images or views. The fallbacks are `resources = 0` and `mode = passthrough` (D-017, `docs/first-probe-session.md`).

## ISSUE-002: The tracker test once failed to link with LNK1163 and linked on the next attempt

2026-10-11, building into `build/next/`: `state.obj : fatal error LNK1163` (invalid selection for a COMDAT section) while linking `efmi_vk_tracker_test.exe`; `state.cpp` had not changed since a build that linked. The same command succeeded immediately afterwards and in every later run. Cause unknown. If it comes back, look at whether two builds can write `sandbox/build/tracker/` at the same time; do not add a silent retry.

## BUG-002: Native regression appended to the historical game log (fixed)

The tracker test links the layer logger, whose default path is beside its executable. Running the final suite against dist/ appended one test startup line to the existing game log. The original 2246-byte prefix exactly matched the saved backup; no historical bytes were lost. Preserved the mixed log in build/backups/native-log-repair-20261011-223459/mixed.log and restored the exact original bytes from the before-0.5 backup.

sandbox/native_test.py now always sets EFMI_VK_LOG and its working directory to a new directory under build/tests/. The corrected native test passes 62 checks, with the historical log unchanged before/after (SHA-256 2e8a46cf4985363ce1b5daea4942b5195e4dee1ffaefbe67e1543946e8f961b4).

## LIMIT-015: Scope of the DX11 baseline

One frame with the mods on, one scene, one character, key states as found; the Vulkan frame it is paired with comes from another session of the same morning with another window size. No frame with the mods off exists (the F11 press was refused for a minimized window), so the slots the mod replaces pair with nothing: for component 0 only the diffuse map is placed, light map and normal map are two bindings in unknown order. Pairing is by look (shape, size, mip count, format); equal looks are reported as ambiguous groups and never resolved by guessing (T-018). Three of 21 common objects were not paired because they are drawn twice with the same shaders into the same targets. A slot table from this frame covers the 24 pixel shaders of this scene only.

## ISSUE-003: `python` on the PATH of this machine is the Windows Store stub

`python` and `python3` resolve to `%LOCALAPPDATA%\Microsoft\WindowsApps` and exit with code 49 without output. Use `D:\Python312\python.exe` (as `.mcp.json` does) or `py`. Seen on 2026-10-12 when a capture script appeared to do nothing.
