# EFMI Vulkan - Known issues

> Last updated: 2026-10-11.

## LIMIT-001: Nothing is verified in the real game

Every result so far comes from static inspection or from the sandbox. Whether the layer loads next to the anti-cheat (G1), how the game records and submits (G2) and whether the data is reachable (G3) are open.

## LIMIT-002: No Vulkan SDK on this machine

Only the headers are present (`F:\vulkan\Vulkan-Headers`). There is no `glslc`, no `spirv-val` and no validation layer, so the sandbox and the layer are checked only by running on the installed NVIDIA driver. A driver accepting a call sequence is weaker evidence than a clean validation run. The plan requires validation from M2 on.

## LIMIT-003: The existing DX11 frame dump is not a merged-skeleton baseline

`FrameAnalysis-2026-10-07-123937` has zero CustomShader executions and no mod entry point hit; only the ShaderRegex checks of EFMI and RabbitFX ran. It can be used for hash checks on unmodified draws, not for the merged-skeleton comparison.

## LIMIT-004: RabbitFX mods are out of reach for the first version

6 of the 9 local character mods pass their textures only through RabbitFX, which patches DX11 shader assembly. They cannot be degraded gracefully by stubbing RabbitFX.

## ISSUE-001: Other implicit Vulkan layers are registered on this machine

Steam overlay, Steam Fossilize and the Epic Online Services overlay are registered under HKLM, the OBS hook under HKCU. They may be active in the game's Vulkan path as well. When reading real-game results, remember that this layer is not the only one in the chain, and that layer order matters for what each one sees.
