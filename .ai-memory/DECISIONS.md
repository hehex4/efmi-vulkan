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
