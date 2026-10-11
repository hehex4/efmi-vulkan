# EFMI Vulkan - Tasks

> Last updated: 2026-10-11.

Milestones and gates refer to `docs/plan.md`.

## Current state

Bootstrap of the workspace is in progress. Nothing has been loaded into the real game.

### T-001: Workspace bootstrap (in progress, 2026-10-11)

English plan, memory system, working agreement, text check, Git repository.

### T-002: Vulkan headers and build scripts (open)

Fetch the pinned headers into `F:\vulkan\Vulkan-Headers`; build scripts for layer and sandbox that find MSVC and the headers.

### T-003: Verify the region hash against existing DX11 dumps (open)

Use the IB files of `FrameAnalysis-2026-10-07-123937` to confirm that the hash in the file name equals CRC32C over the index bytes of the draw. Turns fact 5 of the plan from "read in the source" into "checked against data".

### T-004: Layer skeleton and sandbox (open, M1)

Pass-through layer, sandbox program, end-to-end test that the layer loads in the sandbox and counts frames.

### T-005: Tracking, frame capture and probe (open, M1)

Memory and buffer tracking, index buffer shadows, draw records, region hashes, named pipe, Python client and CLI, sandbox test with expected values.

### T-006: MCP server (open, M1)

Stdio MCP server over the client; `.mcp.json`; a tool that matches the IB hashes of a mod INI against a captured frame (serves G3 item a).

### T-007: M0 inventory (open)

`docs/compatibility-inventory.md` for `arcane.ini` and Core 1.4.8; pin versions and fingerprints; XXMI front-end separability assessment (three days at most).

### T-008: DX11 baseline capture with the sample mod (open, needs the game running under DX11 with EFMI)

The existing frame dump has no modded draw. A new one with `arcane fes 1.2` on screen is needed for the G3 comparison table. The EFMI MCP tools can produce it.

### T-009: Gate G1 in the real game (blocked on the user)

Needs the user's decision on the account and an explicit go-ahead on the spot. Also needs a decision on how the layer is registered, which depends on whether the game process is elevated.

### T-010: Gates G2 and G3 in the real game (blocked on T-009)

## Waiting for the user

- Which account G1 uses, and when.
- Whether to install the LunarG Vulkan SDK into `F:\vulkan` (needed from M2 on for the validation layers).
- The motivation question and the scope question of plan section 10.
