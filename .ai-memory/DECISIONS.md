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
- Limits: submit-time visibility does not establish G3's record-time premise. In 0.4.0, templates, pushes and descriptor buffers discarded stale tracked knowledge; D-015 subsequently adds template/push decoding. Descriptor buffers remain unsupported. A small declared-binding SPIR-V reader is not full reflection. Details: `docs/probe-resources.md`.
- Historical deployment choice, superseded by D-014: because run 3 ended before 30 minutes, this implementation round built/tested 0.4.0 under `build/next/` and kept `dist/` at 0.2.0 passthrough.

## D-014: Reuse run 3 as initial loading evidence; defer the long-duration observation

- Date: 2026-10-11 (user).
- Decision: the user confirmed exiting run 3 before 30 minutes after scene changes, then instructed the project to use the previous run's data. Accept it as initial G1 loading evidence and continue development and short probe preparation without requiring another 30-minute passthrough session. The original duration was a project observation target, not a technical requirement.
- Evidence boundary: the historical run used 0.2.0 passthrough. Loading and device creation were logged; scene changes and the early exit were reported by the user. Exact duration, total frames, long-duration stability, probe-mode game behavior and real-character capture data remain unverified. Do not relabel historical results as a new test.
- Deployment: after confirming no game process remained, preserved the old DLLs/settings/log under `build/backups/`, rebuilt and regression-tested 0.4.0 in `dist/`, and set `dist/efmi_vk.cfg` to probe. A sandbox using that actual configuration captured a frame successfully.
- Boundary kept: this decision prepares the next session; it does not authorize the agent to start or inject the game. Each real-game launch still follows the user's on-the-spot instruction. Launcher settings remain read-only, and blocked loading still stops the project.

## D-015: Push contents are retained at recording time; resource visibility stays at submit

- Date: 2026-10-11 (implementation of the next T-011 increment).
- Decision: template definitions own their copied entries and retained layout metadata. Ordinary template updates use the same decoder as writes. Each push update creates immutable contents retained by subsequent draw records, separate for each command buffer and bind point. Partial writes preserve compatible elements; layout disturbance and reset invalidate them. Immutable sampler-only push bindings start undefined.
- Reason: looking up a push bank's final state at submit would give earlier draws the last update's descriptors. Recording immutable values prevents this and survives secondary-buffer flattening. A push template's bind point comes from its creation metadata, not from the currently bound graphics pipeline.
- Diagnostic boundary: `snapshot` is `record` for pushes and `submit` for ordinary sets; `resource_snapshot` is always `submit`. Retaining a buffer handle/range is not evidence that its bytes were readable at record time. This does not satisfy G3 b/c by itself.
- Scope: standard buffer/image/texel/sampler descriptors; unknown templates and unsupported layouts discard stale state. Descriptor buffers and specialized types remain unimplemented. GPU and CPU coverage are recorded separately in `docs/probe-resources.md`.

## D-016: Retain XXMI hosting with explicit adapter boundaries; correct sample scope

- Date: 2026-10-11, M0 static dependency assessment.
- Decision: keep plan D4's hosted XXMI frontend for M3. The pinned v1.2.2 parser/expression sources share D3D-shaped state, globals and resource code; they are not an isolated portable library. WWMI-DX12 shows a concrete host arrangement but uses an older baseline and a substantial adapter. Define state/query/resource/input boundaries and test new Pool/Region/ElementFormat semantics in a later compiled prototype. A reduced interpreter stays a fallback if that prototype fails.
- Evidence: local d3d11.dll exactly matches the official v1.2.2 archive and source tag a7847eae82aaac32175c9a7d199673d5c9323bcd. Reference host commit 113f42cc2dc5702dbba624ee16abac96d68ed3b5. No extraction, compilation or runtime equivalence of a host is claimed. Details and all input hashes: docs/compatibility-inventory.md and its JSON companion.
- Scope correction: the enabled mod folder contains arcane.ini and k/1.ini, an HLSL image panel. The main file configures 13 slots but defines component ids 0-11. Full-folder acceptance includes the panel; do not silently disable it or count character-only rendering as complete compatibility. Existing mod files remain read-only.

## D-017: Whatever the probe keeps is bounded, and there is a tracking level below full

- Date: 2026-10-11, review of 0.4.0 / 0.5.0 before the first probe session in the game.
- Decision: SPIR-V is kept only up to `shader_budget` (default 512 MiB); a capture keeps descriptor contents only up to the request's `descriptor_budget` (default 64 MiB); destruction and pool reset do work proportional to what is destroyed, not to what exists; `resources = 0` is a tracking level between full probe and passthrough.
- Reason: the sandbox has two shader modules, a handful of descriptor sets and five draws. The game has a 206 MB pipeline cache, and its counts are unknown until the first session. Unbounded retention and full-table scans under the exclusive resource lock were the parts most likely to hurt the game, and a session that goes badly should be able to fall back without losing the draw data.
- Not decided: the defaults are guesses to be revised with the first real numbers. A budget reached in the game is reported (`shaders_not_retained`, `descriptors_omitted`), not hidden.
- Kept as it was: the per-update cost of descriptor tracking (exclusive lock and map updates per written element). Its size in the game is unknown; `efvk_api_usage` shows the update rate.

## D-018: What the first game data settles in the plan

- Date: 2026-10-12, after the first probe session (CURRENT_STATE).
- D5, threading: the engine records on one thread in the observed scene and `Player.log` says `jobified=0`. The mod lock stays in the design, but bring-up does not need a single-threaded mode forced from outside. To be re-checked in the open world.
- D6, hashes: index data is read from the application's own mapping; the shadow path was not needed for any draw. Shadows stay for the case of device-local index buffers, which this scene does not have.
- D7, texture slots: mapping by resource name is ruled out, the SPIR-V carries no names. The mapping has to come from binding order and a per-shader profile, checked against the DX11 baseline.
- D3, two-phase execution: its premise looks right. Instance config and bones are in mapped memory and all per-frame data arrives without buffer copies. It is not proven until T-017 shows the bytes at submit and at record time.
- D8, vertex input: confirmed as needed; the game's blend stream has stride 12 with 8-bit indices, the mod's has stride 16 with 16-bit indices.
- Compute: 47 dispatches per frame, all outside render passes and none needed by the character's vertex shader for bones. The merged skeleton does not depend on them.

## D-019: What the DX11 baseline settles in the plan

- Date: 2026-10-12, after pairing a DX11 frame of the sample mod with the Vulkan frame (`docs/dx11-baseline.md`, facts 15 to 19).
- D7, texture slots: a mod's `ps-tN` is translated through a table from (fragment shader, DX11 slot) to (set, binding). The table is data produced by `scripts/dx11_baseline.py` from paired frames, not something the layer derives at run time: binding order is not register order and there are no names. Where two textures look alike the entry stays open until content identity exists (T-018). A draw whose fragment shader has no table entry is left unreplaced and reported.
- Instance window: taken from set 2 binding 0 at the draw's dynamic offset, 256 bytes; found by its place in the layout. The DX11 core's per-shader search for the slot has no counterpart.
- Bone buffer: identified as a buffer (the one storage buffer of 8,413,184 bytes the character pipelines bind in set 0), not by a binding number.
- D3, CPU work: the two compute steps of the core are a copy of bone rows per component and frame and a patched copy of 16 constants per matched draw. Both are done on the CPU before the draw is replaced, provided T-017 shows the bytes are there in time.
- D8, vertex input: the variant changes the stride of the blend binding and the format of its unsigned-integer attribute and binds 32-bit indices; attributes are identified by binding and format, not by semantic.
- Not decided: how the fragment stage keeps the game's bone buffer and instance window (T-019).
