# EFMI Vulkan - Technical decisions

Design decisions D1 to D11 of the product itself are in `docs/plan.md`, section 3. This file records the decisions about how the project is run, and later changes to the design with their reasons.

## D-001: Changes stay inside the project; EFMI, the game and EFMI MCP are read-only

- Date: 2026-10-11.
- Decision: writes are limited to `F:\efmi vulkan` and to `F:\vulkan` (Vulkan headers / SDK). The EFMI/XXMI installation, the launcher configuration, existing mods, the game installation and `F:\EFMI MCP` are only read.
- Reason: same boundary the user set for the sibling project; this project needs real data from those places but no change to them.

## D-002: Memory split by seven responsibilities

- Date: 2026-10-11.
- Decision: overview, architecture, current state, decisions, issues, tasks and session log are seven Markdown files in `.ai-memory/`; the root `AGENTS.md` is the entry point for resuming and maintaining them, and `CLAUDE.md` imports it.
- Reason: the user asked for a memory system modelled on `F:\EFMI MCP\.ai-memory`. Only the structure was taken from there; no content of that project was copied.

## D-003: English only, no Chinese characters in the workspace

- Date: 2026-10-11 (user).
- Decision: file names and contents are English; `scripts/check_text.py` fails the regression suite when it finds CJK characters. The plan written in Chinese earlier the same day was translated to `docs/plan.md` and the Chinese file removed.
- Note: the conversation with the user stays in Chinese.

## D-004: Commit locally after every verified change; no remote

- Date: 2026-10-11 (user, stated twice).
- Decision: each change that passed its verification is committed unprompted. No remote is configured and nothing is pushed until the user names a repository. `docs/plan.md` is tracked (the user chose this explicitly; it differs from the EFMI MCP project, where the plan stays local).

## D-005: Vulkan headers live in `F:\vulkan`, outside the repository

- Date: 2026-10-11 (user: install to `F:\vulkan`).
- Decision: `scripts/fetch_vulkan_headers.ps1` makes a shallow clone of `KhronosGroup/Vulkan-Headers` at tag `vulkan-sdk-1.4.321.0` into `F:\vulkan\Vulkan-Headers`. Build scripts look for headers in this order: `%VULKAN_SDK%\Include`, `F:\vulkan\Include`, `F:\vulkan\Vulkan-Headers\include`.
- Not done: the LunarG SDK installer was not run. It needs elevation, accepts licence terms and changes system environment variables and the registry, which is the user's to do. If the user installs the SDK into `F:\vulkan`, the build picks it up without a change.
- Consequence: there is no `glslc` and no validation layer on this machine. The sandbox builds its two shaders with a small SPIR-V builder in its own source.

## D-006: A live probe for development, modelled on EFMI MCP

- Date: 2026-10-11 (user asked for a tool like the DX11 one to help development).
- Decision: the read-only diagnostics of the layer are served over a named pipe `\\.\pipe\efmi-vk-<PID>` with the same framing as the EFMI MCP probe (`[u32 header_len][u32 payload_len][header JSON][payload]`). `server/efmi_vk` holds the client, a CLI and a stdio MCP server; `.mcp.json` registers the server for this workspace.
- Reason: gates G2 and G3 are questions about live data (threads, submits, index bytes, bindings). Answering them through tools an agent can call is faster and more repeatable than reading log files, and the sandbox gives the same tools a target that needs no game.
- Boundary: the probe is read-only. It does not change what the application draws.

## D-007: The sandbox is the regression target; the real game is a separate layer of evidence

- Date: 2026-10-11.
- Decision: every probe feature gets a sandbox test with known expected values (the sandbox writes `expected.json`). Results from the sandbox are never reported as results from the game.
- Reason: the game cannot be used for routine testing (anti-cheat, account risk, user's decision per run).

## D-008: One Python build script instead of batch files

- Date: 2026-10-11.
- Decision: `scripts/build.py` finds `vcvars64.bat` (through `VCVARS` or `vswhere`) and the Vulkan headers, runs `cl` for the layer and the sandbox and writes the layer manifest. The regression suite calls it.
- Reason: one place for the header search order of D-005; the tests can build into another directory with `--out` while a process still holds the DLL in `dist/`.

## D-009: Two modes of the layer; draw records are always kept in probe mode

- Date: 2026-10-11.
- Decision: `EFMI_VK_MODE=passthrough` hooks only device destruction and present and starts no pipe; it is the mode for gate G1. In probe mode every command buffer keeps the records of its draws all the time, not only while a capture is armed.
- Reason: a capture then never depends on when a command buffer was begun, which matters for engines that record ahead of the frame boundary. The original record cost was about 200 bytes and one vector push per draw; 0.4.0 adds descriptor allocation ids and shared layout references, so that old estimate no longer describes the current record. Heavy-scene overhead remains unmeasured (T-012).

## D-010: Shadows are fed at submit time and only for index buffers by default

- Date: 2026-10-11.
- Decision: buffer writes recorded into a command buffer are applied to the shadow when that command buffer is submitted, reading the staging source at that moment. Only buffers with index usage get a shadow unless `EFMI_VK_SHADOW_USAGE` widens the mask.
- Reason: what the staging buffer holds at submit time is what the GPU copies; a shadow for every buffer of a game would cost too much memory. The DX11 dump shows large shared index buffers bound at byte offsets (plan fact 14), so the shadow is per buffer with an interval set of known bytes, and hashes are taken over sub-ranges.
- Open: whether the game's uploads are visible to the CPU at all is gate G3; LIMIT-007 lists what is not covered.

## D-011: The game gets the DLL by injection with export redirection; the layer entry is the fallback

- Date: 2026-10-11 (the user asked whether loading can work by injection, as with d3d11).
- Decision: a second entry was added to the same binary. Loaded with `LoadLibrary`, the DLL redirects the export table entries of `vulkan-1.dll` in memory and calls the original addresses from its hooks. `efmi_vk_inject.dll` selects this entry by its file name, `efmi_vk_layer.dll` the layer entry. For the game the plan is the launcher's extra-library injection, which already brings the user's DX11 probe into `Endfield.exe`.
- Reason: no registry entry and no system-wide change; independent of elevation (LIMIT-012); the same launch flow the user already runs; the redirection technique is the one the EFMI MCP probe uses on `d3d11.dll` in the real game.
- Cost: the DLL has to be in the process before the engine resolves its Vulkan functions, which is not known for the game (LIMIT-013). A device created earlier is served by the passive device and not tracked.
- Settings: `efmi_vk.cfg` next to the DLL, because the launcher cannot hand environment variables to the game; an environment variable still wins, which the tests rely on.

## D-012: What the project needs is installed into `F:\vulkan` without asking

- Date: 2026-10-11 (user).
- Decision: dependencies such as headers, SDKs and tools go into `F:\vulkan` directly. This replaces asking before each download.
- Boundary kept: nothing an installation would write outside that folder (registry, system environment variables, system directories). If an installer cannot be confined to the folder, that is reported instead of run.

## D-013: Descriptor snapshots are explicitly at submit; bytecode follows pipeline lifetime

- Date: 2026-10-11.
- Decision: the first T-011 increment tracks ordinary descriptor writes/copies and snapshots contents when a captured command buffer is submitted. Dynamic offsets are taken at bind time and split by layout. Shader bytes have shared ownership between their live module and pipelines; ids do not rely on handles remaining unique.
- Reason: descriptor sets are references, not inline copies of their contents. Querying them later from the pipe could silently return another frame's data; retaining immutable capture JSON avoids that. Module destruction after pipeline creation is normal and must not remove shader diagnostics prematurely.
- Limits: submit-time visibility does not establish G3's record-time premise. Templates, push descriptors and descriptor buffers discard stale tracked knowledge until decoding is implemented. A small declared-binding SPIR-V reader is not full reflection. Details: `docs/probe-resources.md`.
- Deployment consequence: run 3 ended before 30 minutes according to the user, so the G1 gate remains incomplete. This round builds/tests 0.4.0 under `build/next/` and keeps `dist/` at 0.2.0 passthrough. The earlier planned switch to probe waits for gate completion and a newly authorized game session.
