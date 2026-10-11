# EFMI Vulkan - Project overview

> Created 2026-10-11. The structure follows `F:\EFMI MCP\.ai-memory`; the content covers this project only.

## Goal

Run existing EFMI merged-skeleton mods of Arknights: Endfield on the game's native Vulkan renderer, with the mod directories (INI, BUF, DDS) unchanged. Today EFMI works only because the launcher forces the game onto Direct3D 11.

The first acceptance sample is `Mods\arcane fes 1.2`. RabbitFX, shape keys, on-screen text and hunting mode are outside the first version. The full plan, with facts, decisions, gates and milestones, is `docs/plan.md`.

## Components and technology

- `layer/`: a Vulkan layer in C++17, MSVC x64. At present it is a read-only probe: memory, buffers, ordinary descriptor sets, shader/image metadata, command buffers, submits and captured draws over a named pipe. The current 0.4.0 build is staged in `build/next/`; `dist/` remains at historical 0.2.0 passthrough while G1 is incomplete.
- `sandbox/`: a deterministic Vulkan test program (`dist/efmi_vk_sandbox.exe`) and the regression scripts. It is the target for everything that can be tested without the game.
- `server/`: a Python package `efmi_vk` (standard library only) with the pipe client, a command-line tool and a stdio MCP server, so that an agent can query a running Vulkan process the way the EFMI MCP project does for DX11.
- `scripts/`: dependency fetch, the repository text check, offline analysis of DX11 frame dumps.

## Entry points on this machine

- Project: `F:\efmi vulkan`. Vulkan headers: `F:\vulkan\Vulkan-Headers` (tag `vulkan-sdk-1.4.321.0`, matching the installed runtime `vulkan-1.dll` 1.4.321). No Vulkan SDK is installed; there is no `glslc` and no validation layer.
- MSVC: `D:\Microsoft` (found by `vswhere`). Python: `D:\Python312\python.exe`. Git: `D:\git`.
- EFMI workspace (read-only): `D:\XXMI-Launcher-Portable-v2.1.6\EFMI`. Game (read-only): `D:\Hypergryph Launcher\games\EndField Game`. Unity player log and PSO caches: `%USERPROFILE%\AppData\LocalLow\Hypergryph\Endfield\`.
- Sibling project for DX11 capture (read-only reference): `F:\EFMI MCP`.
- Reference sources: `SpectrumQT/XXMI-Libs-Package` (branch `master`, engine in `DirectX11/`), `lekarsenten/wwmi-dx12` (branch `main`, GPL-3.0).

## Long-term constraints

- Change only this project. The EFMI/XXMI installation, the launcher configuration, existing mods and the game files are read-only.
- The layer goes into the real game only when the user says so on the spot; gate G1 carries an account risk. Nothing is ever done to evade or hide from the anti-cheat.
- No system-wide change without being asked: the tests enable the layer through environment variables of their own child processes only.
- Everything in the workspace is English without Chinese characters; commit after every verified change; no push until a remote is named. See `AGENTS.md`.
