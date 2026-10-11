# EFMI Vulkan - Development log

> A short summary is appended at the end of each round: what was actually done, the scope of verification, open problems and where to pick up next time. No chat and no complete logs.

## 2026-10-11: Review of the v1 plan, v2 plan, workspace bootstrap

The user asked for a feasibility review of the v1 plan (a Chinese document in the EFMI output folder). The review read the local Core INIs, a sample mod, the game directory, the Unity player log and the two reference projects. Main findings: the game runs Vulkan by default; native graphics jobs are enabled; the ACE anti-cheat is present; Core issues a dispatch and a buffer copy for every replaced draw, which is illegal inside a Vulkan render pass; ReShade does not expose `vkMapMemory`; the existing frame dump has no modded draw.

A v2 plan was then written. Further reading of the XXMI source and of the mods showed that the IB hash is a plain CRC32C over the index bytes of the draw, that the two merged-skeleton compute shaders only copy and patch, and that RabbitFX mods cannot be stubbed. The plan chooses an own Vulkan layer, the mod INI plus the EFMIv1 interface as compatibility surface, and two-phase execution with CPU-side data preparation.

The user then asked for: a memory system modelled on `F:\EFMI MCP\.ai-memory`, a Git repository, no Chinese characters in the workspace, a live capture tool like the DX11 one to help development, and to start working. Answers to three questions: Vulkan headers go to `F:\vulkan`; commit locally after every verified change; the plan document is tracked.

Nothing was run in the game in this round.
