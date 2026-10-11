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

## LIMIT-012: An elevated process ignores the environment variables the tests use

The Vulkan loader ignores path variables such as `VK_LAYER_PATH` in an elevated process, so the manifest in `dist/` would not be found there. The game probably runs elevated (EFMI's `d3dx.ini` has `require_admin = true`, and the EFMI MCP notes say the game runs as administrator). Loading the layer into the game therefore needs a registry entry for an implicit layer, which is a system-wide change that waits for the user (T-009).

## ISSUE-001: Other implicit Vulkan layers are registered on this machine

Steam overlay, Steam Fossilize and the Epic Online Services overlay are registered under HKLM, the OBS hook under HKCU. They may be active in the game's Vulkan path as well. When reading real-game results, remember that this layer is not the only one in the chain. The sandbox tests switch them off for their child processes.
