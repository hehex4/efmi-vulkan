# EFMI Vulkan - Known issues

> Last updated: 2026-10-11.

## LIMIT-001: Nothing is verified in the real game

Every result so far comes from static inspection, from an offline DX11 dump or from the sandbox. Whether the layer loads next to the anti-cheat (G1), how the game records and submits (G2) and whether the data is reachable (G3) are open.

## LIMIT-002: No Vulkan SDK on this machine

Only the headers are present (`F:\vulkan\Vulkan-Headers`). There is no `glslc`, no `spirv-val` and no validation layer, so the sandbox and the layer are checked only by running on the installed NVIDIA driver. A driver accepting a call sequence is weaker evidence than a clean validation run. The plan requires validation from M2 on.

## LIMIT-003: The existing DX11 frame dump is not a merged-skeleton baseline

`FrameAnalysis-2026-10-07-123937` has zero CustomShader executions and no mod entry point hit; only the ShaderRegex checks of EFMI and RabbitFX ran. It was good for checking the region hash rule on unmodified draws, not for the merged-skeleton comparison.

## LIMIT-004: RabbitFX mods are out of reach for the first version

6 of the 9 local character mods pass their textures only through RabbitFX, which patches DX11 shader assembly. They cannot be degraded gracefully by stubbing RabbitFX.

## LIMIT-005: Dynamic offsets are attributed to the first set of a bind call

`vkCmdBindDescriptorSets` passes one array of dynamic offsets for all sets of the call; splitting it needs the set layouts, which are not tracked yet. The probe records the whole array with the first set of the call and none with the others. Correct when a call binds one set, as the sandbox does. Fixed by T-011.

## LIMIT-006: Descriptor contents, shaders and images are not tracked

A draw shows descriptor set handles, not what the sets hold. Gate items G3 b (instance config buffer), c (bone buffer) and d (texture bindings) cannot be answered with the probe as it is. T-011.

## LIMIT-007: A shadow knows only what passed the CPU

Sources are staging copies (source mapped when the copy is recorded or submitted, or snapshotted at unmap when at most 16 MiB), `vkCmdUpdateBuffer` and `vkCmdFillBuffer`. A buffer filled by compute, or by a copy whose source was never CPU-visible, is reported with `src: none` and counted under `unknown_writes`. There is no read-back from the GPU (T-013). `unknown_writes` in the frame statistics is the number to watch in the game.

## LIMIT-008: Hash and read-back are "as of now", not "as of the draw"

The hashes of a captured frame are computed when the command buffer is submitted, after all of its recorded writes were applied; a copy recorded after a draw in the same command buffer already affects that draw's hash. `read_buffer` returns what the mapping or the shadow holds at the time of the request. For a host-visible buffer the application rewrites every frame, that is not what an earlier draw used.

## LIMIT-009: What a draw record leaves out

Vertex buffer slots 0 to 3 and descriptor sets 0 to 3 of the graphics bind point only; no compute bindings; no push constants; no dynamic state; the arguments of indirect draws are not read. Indirect draws get no region hash.

## LIMIT-010: The frame boundary is `vkQueuePresentKHR`

An application that does not present makes a capture time out. With several devices, the probe answers for the one that presented last.

## LIMIT-011: Scope of the sandbox evidence

One GPU and driver (NVIDIA RTX 5060 Laptop). The sandbox uses classic render passes, one queue, one swapchain, `vkQueueSubmit`, `vkCmdCopyBuffer` and `vkMapMemory`; the hooks for dynamic rendering, the `2` variants of submit, copy, bind and map, indirect draws, dispatch, `vkCmdUpdateBuffer` and `vkCmdFillBuffer` compile but are not exercised by a test. The rendered picture was not inspected.

## LIMIT-012: The layer entry needs a registry entry for an elevated process

The Vulkan loader ignores path variables such as `VK_LAYER_PATH` in an elevated process, so the manifest in `dist/` would not be found there. The game probably runs elevated (EFMI's `d3dx.ini` has `require_admin = true`, and the EFMI MCP notes say the game runs as administrator). The layer entry would therefore need an implicit layer entry under HKLM, a system-wide change. The inject entry avoids this and is the entry planned for the game (D-011); the layer entry stays the fallback.

## LIMIT-013: What is and is not known about the inject entry in the game

Known from two launches on 2026-10-11 (CURRENT_STATE): the launcher injects the DLL 4.6 to 5.5 s before `vulkan-1.dll` is mapped, the redirection is in place in time, and the engine's instance and device creation pass through the hooks. Only passthrough mode has been in the game; probe mode has not.

Still open:

- Whether a Vulkan session survives with the DLL present (BUG-001).
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
- **Result of the test (run 3, 20:38)**: with only `efmi_vk_inject.dll` injected the game passed the point of the crash and kept running. Rule from now on: for a Vulkan session the launcher's XXMI DLL injection mode is Skip and the DX11 probe is not in the injected libraries.

## ISSUE-001: Other implicit Vulkan layers are registered on this machine

Steam overlay, Steam Fossilize and the Epic Online Services overlay are registered under HKLM, the OBS hook under HKCU. They may be active in the game's Vulkan path as well. When reading real-game results, remember that this layer is not the only one in the chain. The sandbox tests switch them off for their child processes.
