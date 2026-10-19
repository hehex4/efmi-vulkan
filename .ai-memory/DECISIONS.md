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
- Update, 2026-10-12: the bounded native host feasibility prototype compiles, links and passes 23 original-implementation CPU checks. Keep the hosted approach; no reduced interpreter is needed for this checkpoint. It is standalone, with reference headers as scaffolding and no layer/graphics/mod integration. Exact tested semantics and remaining adapter work: docs/xxmi-host-prototype.md.

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
- D7, texture slots: a mod's `ps-tN` is translated through a table from (Vulkan fragment shader, DX11 slot) to (set, binding), per pass and including bindings of the global set, because the mod assigns its slots in every pass and under DX11 they land on whatever that pass has there (fact 20). Replacing by role ("the diffuse map") is rejected: it would not show what DX11 shows. The table is data produced by `scripts/dx11_baseline.py` from paired frames, not something the layer derives at run time: binding order is not register order and there are no names. Where two textures look alike the entry stays open until content identity exists (T-018). A draw whose fragment shader has no table entry is left unreplaced and reported.
- Instance window: taken from set 2 binding 0 at the draw's dynamic offset, 256 bytes; found by its place in the layout. The DX11 core's per-shader search for the slot has no counterpart.
- Bone buffer: identified as a buffer (the one storage buffer of 8,413,184 bytes the character pipelines bind in set 0), not by a binding number.
- D3, CPU work: the two compute steps of the core are a copy of bone rows per component and frame and a patched copy of 16 constants per matched draw. Both are done on the CPU before the draw is replaced, provided T-017 shows the bytes are there in time.
- D8, vertex input: the variant changes the stride of the blend binding and the format of its unsigned-integer attribute and binds 32-bit indices; attributes are identified by binding and format, not by semantic.
- Not decided: how the fragment stage keeps the game's bone buffer and instance window (T-019).

## D-020: How a capture keeps buffer contents

- Date: 2026-10-12, with layer 0.6.0 (T-017).
- Bytes are kept as they are at submit, not at record: submit is the last moment the layer sees before the GPU may read, and one read per distinct range serves all draws of the command buffer. Record time contributes a checksum per range only; that answers whether the data was complete then at four bytes per range instead of a second copy.
- The request names what to follow (set, binding, byte offset, count, stride, window size) instead of the layer knowing the game's instance entry. The game's rule lives in `scripts/collect_probe.py` as a preset. A later Core-VK will read the same places through its own code; the probe stays generic.
- The first bytes of the followed range are a separate item (`entry`): the game's range is 64 KiB with the entries of many draws, and a comparison of the whole range would call a draw's data incomplete because of its neighbours.
- Selection at record time is by region hash, computed there a second time while a snapshot capture is in its frame. Rejected: reading every draw at record time (cost in every frame of the capture for draws nobody asked about) and selecting at submit only (no record-time evidence).
- Bounded like everything else the probe keeps (D-017): budget in bytes, size limit per uniform range, clamps on every requested size; past the budget a range is still checksummed and compared.

## D-021: What the second probe session settles in the plan

- Date: 2026-10-12, after layer 0.6.0 ran in the game (facts 21 and 22).
- D3, bone import: done by the CPU from the application's own mapped memory. No GPU read-back (T-013 stays closed for bones), no compute pass. The premise of the two-phase execution is measured, not assumed.
- When to read: both moments are valid in the observed scene. The instance entry and the bones of a component may be read when its matched draw is recorded; reading at submit is always at least as good and remains the fallback if another scene shows late data. Which of the two M2 uses is an implementation choice; M2 starts with the read at record time because it needs no deferred work, and keeps the comparison of 0.6.0 as a check.
- How much to read: exactly `VertexGroupCounts[component]` bones of three rows from the entry's offset, as the DX11 core does. Never a fixed window: bytes behind a palette belong to something written later.
- What not to use: the order or distance of palettes in the bone buffer, which changes per frame.
- Scope: one scene with one standing character. The decision is revisited if a later session shows `different` inside a palette or an entry.

## D-022: The pipeline variant gives the vertex stage a set of its own

- Date: 2026-10-12 (T-019, fact 23).
- The vertex module of a replaced draw's pipeline is rewritten so that its storage buffer and its instance window are bound at bindings 0 and 1 of the first set the pipeline does not use (set 3 for the sample character). The layer binds one set of its own there: the merged skeleton as storage buffer and its patched instance entries as dynamic uniform buffer, one 256-byte slot per replaced draw with 64 KiB of room behind the last slot, because the shader's block is 4096 constants long. The fragment module is not touched and reads the game's bone buffer and window, as it does under DX11.
- The variant's pipeline layout keeps the game's set layouts and push constant ranges and adds one set layout at the end, so the sets the game has bound stay valid for it.
- The two variables are found by structure: the only storage buffer of the vertex module, and its uniform buffer at set 2 binding 0. A vertex module with no or several storage buffers, or without that uniform buffer, gets no variant; its draw is left to the game and reported.
- Rejected: replacing the shared bindings. It needs a copy of the global set (about 50 bindings) for every pass that draws the character, and 20 of the 45 draws have a fragment shader that would then read the merged skeleton at a patched offset instead of the component's own first bone.
- Cloning a set and overriding bindings is still needed for textures (set 1, and now and then a binding of the global set, fact 20). That mechanism is separate from this decision.
- Evidence is offline: four words per module, `spirv-val` on 21 real vertex shaders. Creating and drawing with such a pipeline is the first thing M2 checks.

## D-023: Disable upload hashing after the required identity capture; measure spikes separately

- Date: 2026-10-12, third Vulkan probe session, user-reported intermittent stutter during loading and steady display.
- T-018 now has its real content evidence: all four open sample slots resolve. Set image_hash = 0 in the prepared dist/ configuration for the next process; the existing process retains its original setting. Preserve full resource tracking for the first comparison.
- The measured 8.1 seconds is cumulative time inside upload processing, including reads and locks. A steady 3-second interval advances 360 presents with only 596 microseconds added there. Do not claim that average 120/s disproves stutter or that hashing caused every reported spike.
- Add bounded host present-interval diagnostics in 0.7.1 (T-020), including recent percentiles/peaks, lifetime threshold counts and capture/hash tags. This is host timing, not GPU/display timing. No performance fix is considered game-verified until a new user-started comparison session.

## D-024: Preserve buffer evidence while measuring probe-only work

- Date: 2026-10-12, after the user reports smooth movement in confirmed 0.7.1 passthrough and stutter in resource-off probe.
- Remove proven unnecessary copy work without discarding readable fallback data: test source visibility before allocating record-time storage, and copy directly into the target shadow at submit. Keep unmap snapshots and submit-time priority. The game's individual stall source remains unproven.
- Add opt-in profile_tracker measurements in 0.7.2, default off, with cumulative category counters and a fixed 64-event slow history on the present diagnostic's QPC clock. Exclude forwarded driver calls. Account for profiling overhead, nesting and descheduling; never equate the sum to frame time.
- Keep the smooth passthrough run as evidence. Any diagnostic probe candidate requires a user-started game comparison after sandbox verification. Disabling buffer snapshots is a separate capability tradeoff, not a silently applied fix.

## D-025: Accelerate detected WC reads while preserving snapshot evidence

- Date: 2026-10-12, after 0.7.2 foreground world timing correlates the 129 ms peak with three expensive unmaps and a zero-byte copy-record wait. The global resource lock makes these snapshot reads relevant to unrelated recording work.
- Keep the 16 MiB unmap limit, index shadow mask, record fallback and submit priority. Do not lower data coverage to make the performance comparison appear successful.
- Classify the complete mapped range once using VirtualQuery. Use SSE4.1 streaming loads only for committed PAGE_WRITECOMBINE mappings on capable CPUs, with aligned loads, exact prefix/tail handling and memory fences. Recompute on remap, clear on unmap; unknown/mixed/cached ranges use memmove. This changes the copy implementation, not Vulkan synchronization or lock ordering.
- Apply the helper to every mapped-read path, including unmap, record fallback, submit and direct probe reads; CPU shadows still use memmove. Expose the available strategy and current per-buffer WC classification for verification.
- Local headless Vulkan measurements validate the mechanism (type 2: 24.023 to 1.270 ms for 8 MiB), not real-game smoothness. Type-4 device-local reads remain expensive. Keep profile_tracker on for the next comparable game run and ISSUE-005 open; source profiling defaults stay off. Full evidence: docs/pacing-tracker-session.md.

## D-026: At unmap, copy staging buffers only (WITHDRAWN by D-027: the premise was wrong)

- Date: 2026-10-12, after reviewing 0.7.1 to 0.7.3 against the buffer inventory of the 0.7.2 game session.
- The copy at unmap exists for staging buffers. Its rule (transfer-source bit, at most 16 MiB) also matched the game's mesh pools, and they were where the bytes went: 2,612 generations on 22 pools against 87 on 76 staging buffers. The rule is now: transfer source, optionally transfer destination, no other usage, wholly inside the mapped range, within the limit.
- This lowers coverage on purpose, which D-024 and D-025 declined to do. The reason is different from theirs: they would not trade evidence for a better-looking measurement; here the copy of a pool was never the feature's purpose, its one use (a hash while the pool is unmapped) has not occurred in any game bundle, and no layer that has to run during normal play could afford it. What is lost is reported per draw (`hash: null`, source `none`) and counted (`counts.unmap`), not hidden.
- A buffer that is not copied has its shadow knowledge dropped at unmap. Serving bytes that the application may have overwritten through the mapping is worse than serving none.
- 0.7.3's streaming loads stay: they are correct and help the reads that remain. They are not counted as the fix; their own benchmark shows 1.5 times on the memory type the pools use.
- `unmap_snapshot_all` and the `set` command exist so that the old behaviour can be brought back in a running process for a comparison. Switching it on in the game brings the stalls back and is done only with the user's agreement.
- Not decided: how draws get their index identity when a pool is unmapped (T-021).

## D-027: Withdraw D-026; the copy at unmap covers every whole transfer source again

- Date: 2026-10-12, after the first game session with 0.7.4.
- D-026 rested on the claim that the bytes copied at unmap were the game's mesh pools. The counters 0.7.4 added show that no pool is ever unmapped (`passed_over` 0 of 144,103 buffers) and that the volume is staging memory, as BUG-004 and D-025 had said. The claim came from reading `generation` as a count of unmap copies, which it is not, and from an inventory that cannot show short-lived buffers.
- The default goes back to what D-024 and D-025 decided: no lowering of coverage without cause. `unmap_snapshot_all` stays as a setting, default on.
- Kept from 0.7.4 because they are right on their own: dropping the shadow knowledge of a buffer that was mapped and not copied; `counts.unmap`; the `set` command; pinned settings in the test harness.
- The stutter fix is 0.7.3's (D-025) and is now confirmed in the game.
- For the working method: an attribution that changes the layer or the record needs a direct count first. Here the count was one small counter away, and was added in the same change that acted on the unverified reading.

## D-028: Own creation recipes; first M2 variant uses a static vertex declaration

- Date: 2026-10-12, after resuming M2 following the verified stutter correction.
- Retain complete supported graphics/render-pass state, including relevant fixed-function values, and reject unknown extensions explicitly. No asynchronous work may borrow application creation arrays or shader/render-pass handles. Layout metadata and SPIR-V have shared ownership. A shader pair alone is not a pipeline identity.
- The initial variant rebuilds the vertex declaration for the replacement stride/formats. This refines plan D8: dynamic vertex input is not required for the first backend, because the application may not have enabled that feature. The eventual cache key includes the original pipeline identity and replacement vertex declaration. Asynchronous creation and retaining the original draw until ready remain requirements; a worker build is tested, but the live game's request/cache/retirement system is still open.
- Keep D-022's VS-only private set, compatible original sets and push ranges, and untouched fragment module. Unsupported immutable samplers/layout flags never become guessed equivalents. A failed construction or unready transaction leaves the original draw available.
- Sandbox GPU readback is the first check: 16-bit bone index 257, changed stride/indices, two replacement buffers, nonzero dynamic offsets, original fragment data, blending and the next original draw after restoration. This passed under core/sync validation. Historical shaders are validated separately, never described as a new game result.
- The next game observation captures complete pipeline/layout recipes and current bone/instance data before integrating the real sample. Old creation counters do not supply the missing values. 0.8.0 stays in probe mode and never invokes replacement drawing in the game.

## D-029: Select a nonempty present interval without merging frames or losing record-time reads

- Date: 2026-10-12, after the user corrected the LOD labels and actual LOD1 recipe capture succeeded only without snapshots.
- An empty captured present interval is not evidence that the scene or character stopped drawing. User-reported scene state and mesh-section/pass coverage remain separate evidence. Depth-only LOD1 matches cannot substitute for LOD1 color coverage.
- Layer 0.8.1 adds opt-in wait_draws. Keep the same snapshot request armed across intervals without graphics submissions, discard those intervals' captured contents/budgets and return one interval with draw commands. Preserve their ordinary tracked buffer writes. Frame counters, pacing and the default one-interval capture are unchanged.
- Record-time comparisons belong to the actual draw recording, which may precede a skipped present; they are not fabricated from submit-time data. The existing timeout clears pending work. Report skipped_empty_intervals, and advertise a feature so older layers cannot silently ignore the option.
- Selection is bounded and only a diagnostic request. It does not alter gameplay, present synchronization, driver submissions or replacement drawing. No frame-generation producer or game fault is inferred from the observed empty intervals.
- Native and GPU sandbox checks plus the full suite pass. Real LOD1 color-pass snapshots remain open until a user-started run with the deployed candidate; do not hot-replace a loaded DLL or claim the fix verified in the game in advance.

## D-030: Preserve Core bone import semantics with atomic CPU updates

- Date: 2026-10-12, after the actual LOD1 snapshots supplied the remaining timing evidence.
- Copy source bone remap[i] after the leading source matrix; keep 512 implicit identities and separate current/previous slices per instance. Multiply all matrix terms by mesh scale. Patch only nonstatic x/y offsets of constant 5; preserve flags and every other entry byte.
- Import once per logical update/component/instance, allowing a lower LOD number to replace an earlier import. The caller owns update serials; empty present intervals are not a sufficient animation boundary. Preserve the master between updates as Core does.
- Validate both poses and remap ranges before mutating data or stamps. Invalid input leaves the old master and original drawing available. Keep the GPU snapshot separate and immutable for its actual lifetime; this CPU helper does not choose that lifetime.
- The five saved game frames agree byte for byte with an independent Core-derived Python reference over 283 draws. That is offline evidence, not a captured DX11 compute-output comparison or GPU rendering. New sandbox-native cases establish rejected-input and multi-instance behavior separately.

## D-031: First hooked M2 draw is one self-contained component with recording-owned storage

- Date: 2026-10-12, after actual LOD1 capture and verification of the importer/cache/snapshot helpers.
- Use sample component 7 (40 vertices, 108 indices, bones 266-277 of 499) because every weighted vertex depends only on its own palette. Copy original BUF bytes into an explicit, bounded, fingerprinted package; do not call this INI execution or full mod compatibility. The installed sample remains read-only.
- Import both poses independently for each draw. For this first component, neither an empty present interval nor cross-component ordering defines an animation update. Only complete mapped data and a ready compatible variant authorize skipping the original draw.
- Each command recording owns mesh, ready variants and per-draw snapshots. Seal at submit, preserve across resubmission and recycle only after a successful legal begin/reset/pool reset. Keep arenas with completed command buffers to avoid allocation every frame. Vulkan's application completion requirements govern reuse; the layer does not infer completion from frame count or elapsed time.
- Preserve original fragment inputs and application bindings; restore pipeline/VB/IB after emitting. Pending, invalid, unsupported and capacity cases forward the original. The optional runtime switch affects future recordings only. Default package/enable settings keep ordinary probe behavior.
- Sandbox hook rendering, package checks and saved game-data inspection are distinct from the next user-started game replacement checkpoint. Full-character geometry, texture overrides, several instances and the image panel remain open.

## D-032: The patched entry names the matrix before the merged skeleton; test shaders address bones as the game does

- Date: 2026-10-12, review of 0.8.1 to 0.9.0 at the user's request. Corrects one sentence of D-030 ("Patch only nonstatic x/y offsets of constant 5"): which bytes are patched was right, the value was not.
- The offsets written into constant 5 are `1536 + 6 * bones * instance - 3` and that plus `3 * bones`. Evidence from three sides, none derived from the others: the core's INI (`$merged_skeleton_offset - 3`), the DX11 baseline (`(1533, 3030)` in all 23 patched entries of two frames) and the game's Vulkan vertex shader (`offset + 3 + 3 * index`). See BUG-005 and `docs/m2-bone-import.md`.
- A rule for the tests, since three of them agreed with the code and with each other while all were wrong: a sandbox shader that stands in for a game shader reproduces the game's addressing, read from the game's SPIR-V, and a reference for a core rule is checked against a value measured under DX11 where one exists. Agreement between an implementation and a reference written from the same reading is not evidence for the reading.
- The core's INI files are part of its logic: values such as this one are computed there and passed through `IniParams`. Reading the HLSL alone is not enough when a rule of the core is reimplemented.
- 0.9.1 was deployed with the settings 0.9.0 had (component 7 enabled), because the user had that configuration set up before pausing; the first Vulkan start will draw the replacement. That start remains the user's decision.

## D-033: General replacement operations, component differences supplied as data

- User scope amendment, 2026-10-13: only LOD0 and LOD1 are required. Do not develop/test LOD2 or later. The original Core pool indexing stride remains data; later-LOD assets are not resolved or loaded.

- Date: 2026-10-12, after the user resumed and observed the component-7 experiment.
- The user requests whole-character support and explicitly questions the cost of individually adapting every component. Component 7 was a backend bring-up fixture, not the product architecture. Do not expand it into component-number branches or separate handwritten replacement functions.
- Keep the shared Vulkan pipeline/cache, bone import, command lifetime and state-restoration operations. Component hashes, resource declarations, draw ranges, LOD remaps and material bindings are data supplied by the frontend. New supported components must exercise the same operations without backend source edits.
- D-016 still governs INI execution: host the pinned XXMI frontend and replace its backend operations through explicit adapters. A static package/compiler is a backend test input, not proof of INI conditionals, keys, persist, pools, reload or panel compatibility.
- Offline update, 2026-10-12: use command-tree mediation to preserve original CPU evaluation while emitting typed backend requests. Register native Core boundaries before optimisation and retain them as unresolved operations; do not use silent placeholder no-ops. The first adapter passes 28 native checks and executes the sample's 12 draw callbacks without component branches. It does not implement full Core, GPU resources or a production host generation. Details: docs/frontend-draw-plans.md.
- Full-character draws can use bones imported from other components. An independent per-draw master with only the triggering component's palette is insufficient. Establish shared-instance/update identity and completeness before making such draws active; do not render missing bones as zero or mix character instances.
- Preserve real evidence boundaries. This session proves live replacement execution for a small mesh whose original XYZ positions are identical; it does not visually accept M2. The next suitable visual checkpoint should use a substantial character change. Continue offline work using the existing display/LOD0/LOD1 captures.

## D-034: Isolate material images by shader stage and check replacement types

- Date: 2026-10-12, during offline whole-character work following D-033.
- A PS image replacement must not modify the VS's resources just because both stages use the same original Vulkan binding. Relocate only requested FS image declarations into private bindings; retain samplers, original buffers and unrelated shader code. The existing VS skeleton/entry isolation remains independent.
- Image type/dimension, shader usage, actual enabled device features, resource readiness and retained ownership are required in addition to an original-resource slot correspondence. Do not silently overwrite original descriptor sets, guess mappings for unknown shaders, or treat an incompatible operation as a successful no-op.
- The bounded standalone helper and pixel fixture prove the 2D operation, not production pipeline integration. Three 3D original placeholders subsequently receive 2D mod assets under the historical DX11 path; keep the original correspondences but refuse that replacement until its source semantics are established (LIMIT-021).
- Offline 0.9.3 extends this into the shared variant/cache/snapshot backend. Keep material binding order and null masks in the variant key, actual image owners in immutable per-recording snapshots, and source slot resolution in the frontend. Explicit nulls use shader results, not dummy textures. Descriptor budgets include original retained layouts. Headless validation/readback verifies these operations; no whole-character hook or game acceptance is implied.

## D-035: Put the whole character on screen from a static package before linking the frontend

- Date: 2026-10-13, at the user's request after the review of 0.9.2 and 0.9.3 ("replace everything", "do B directly").
- A package built offline from the hosted frontend's plans replaces the live INI engine for now. The plans come from the original XXMI engine, so the draw lists are not a second reading of the INI; what is lost is everything that changes at run time (keys, conditions, reload). This is the "backend test input" D-033 allows, used to reach a visible whole-character result early, as the review recommended, not a replacement for D-016.
- The mod-level logic the package cannot carry is written natively after the core's INI, and only that: identity by position, identification after four components and one frame, import once per frame and component with the lower LOD winning, the mod's LOD variable, hiding. Two deliberate deviations, both towards the game's own picture: the first frame of a new object is drawn by the game instead of skipped, and a draw whose bones are not all imported yet keeps the original instead of using zero matrices.
- Requirements are data, not rules of thumb: which components a draw list needs is computed from the bone indices of its vertices, so a component the game does not draw only blocks the lists that use its bones.
- Textures are step 2. Step 1 is expected to look wrong in color and right in shape and motion; that is its purpose.
- Skeleton copies are shared between draws that read the same bytes, and meshes live in device memory the host can write. Both follow from the review (per-draw copies, system-memory vertex data for 385,000 vertices in five passes).
- One backend per process, chosen by the package's magic; the single-component path of 0.9.1 stays as it is.

## D-036: Borrow slot 3 and give it back; version numbers count in single digits

- Date: 2026-10-13, after the first whole-character session.
- The private set stays at slot 3. When the application has a set bound there, the replacement is recorded and the application's set is bound again with the layout handle and dynamic offsets it was bound with. This rests on two facts: the tracker drops a set as soon as a later bind disturbs it, so a set it still shows is valid and its layout agrees with the current one for the lower sets; and a layout used in a recording outlives the recording. Refusing was safe and wrong for this game. Moving the private set above the application's sets was considered and dropped: the layout at slot 3 would have to match whatever the application bound there.
- A stream of the mod that is shorter than the game's is bound as it is, with the elements where the shader expects them, as the source API does; the mesh buffer is padded so the last vertex reads inside it.
- Version numbers (user, 2026-10-13): three single digits, add one at the right, carry at ten. Recorded in AGENTS.md. The build released as 0.10.0 keeps that name in what was written before; it is 0.9.4 in the sequence.

## D-037: Extend the static character package with exact material contracts

- Date: 2026-10-13, continuing the user's request to finish the next step after the whole-model replacement. Scope stays one character, LOD0/LOD1; the user explicitly authorizes switching back to Vulkan after offline verification.
- Keep the original hosted scenario as data. Store each draw's logical image/null state and exact fragment profiles in a bounded material envelope. Reuse ResourceCache, MaterialProfile, variant compilation and command-owned snapshots. No component-specific branches or new INI interpreter; full hosted keys, conditions, reload and source-identity queries remain T-027.
- Match profiles by exact fragment bytes and retain the entire original component when any material item is unknown, pending or failed. Precompute profile/draw contracts; image bindings keep owners for the command's lifetime, with shared bone slices and sealed resubmission semantics.
- LIMIT-021 is handled by an explicit package option named dx11-invalid-2d-to-3d-zero, based on the existing controlled hardware experiment, for its three annotated slots only. The package records the evidence fingerprint and the measured vendor/device; other devices refuse. Default material selection still refuses dimensional mismatches. This is device-specific compatibility, never a statement of portable D3D11 semantics or proven game pixels.
- Workers prepare uploads; only the application's graphics-queue hook submits them, using exactly one enabled unprotected graphics queue. Device features and loader dispatch initialization are taken from the actual creation chain.
- Full rebuilt regression, historical native contracts/pipeline construction and actual-file/config smoke precede deployment. The user starts the real game. The 0.9.5 flicker fix and all 0.9.6 material appearance stay unverified there until that session.

## D-038: Publish immutable character images to every enabled graphics queue

- Date: 2026-10-13, after 0.9.6 refused graphics-queue setup in the user's real Vulkan session (BUG-008).
- Support all queues of one unprotected graphics family rather than assuming that only one queue is created or that queue index 0 is active. Keep one uploader, selected from the application's actual submission hooks, and retain ResourceCache's single-queue submission contract. Other families/protected arrangements remain explicit refusals.
- After every static image is Ready, enqueue one semaphore signal per graphics queue on the uploader. Only then permit command recording to retain those images. Before forwarding each consumer queue's first application submission, enqueue its one-time wait at ALL_COMMANDS. This supplies cross-queue visibility instead of assuming application synchronization covers private uploads.
- Semaphore/fence allocation belongs to setup, workers never submit to application queues, and no fence or idle wait is added to draw/submission. Shutdown drains outstanding publication objects. Submission errors propagate through legacy, core Submit2 and KHR Submit2 hooks.
- New regression must reproduce the old refusal and prove queue-1-only and separate-upload/render behavior through both entries and all submit APIs. Actual-file/config smoke includes multiple enabled queues. Offline success still does not establish game appearance or the earlier flicker fix.

## D-039: The private set sits right above the pipeline's own sets

- Date: 2026-10-13, after the flicker survived 0.9.5 and 0.9.7.
- The private set's index is the number of sets of the pipeline's layout: 3 for the ordinary character pipelines, 4 for the four-set pipelines of the visibility pass. The variant's layout is the application's sets unchanged plus the private one, so binding the private set disturbs none of the application's and the fragment stage keeps reading its own.
- This replaces the constant 3 of D-022 and D-036. D-036 dropped "above the application's sets" for a three-set pipeline with a stale set 3; that case keeps its answer (use slot 3, bind the application's set again). What is new is that the slot follows the layout.
- The tracker follows four slots. A private set at slot 4 or above cannot be given back, so it is used only while no pipeline layout of the application has ever had that many sets (`Device::max_layout_sets`); otherwise the draw is refused. Widening the tracker was not needed: the game has no layout with more than four sets.
- With a material package a fragment shader without a profile is accepted when its complete layout has no image descriptor for the fragment stage. Refusing there kept the original mesh in a pass that has no texture to get wrong.

## D-040: Unprofiled fragment shaders take a sibling's rules; the mesh is replaced regardless; identities survive gaps

- Date: 2026-10-13, after the 0.9.8 game session.
- D-037 tied material rules to exact fragment bytes and kept the original component for any other shader. In the game that keeps the original mesh in whole states of the character (dithered fade), which is worse than any texture error. Reversed for the mesh: a texture question never keeps the game's mesh.
- Rules for an unknown shader are derived at run time from a profiled shader of the same section and kind of pass, through the images the application has bound: a rule moves to the binding that holds the same image. Chosen over more captured profiles (every state of the game would need its own capture under both APIs) and over replacing by image identity alone (a profiled shader may leave a texture alone that another replaces; the sibling decides). The derived shader then looks like its sibling, whatever the source API would do with the mod's fixed slots in the variant.
- Guard rails: same kind of pass (attachment formats, depth write and compare), every image of the sibling present, at most two images more, one best candidate, rule moves only through certain matches, preparation by the production code, image counts the arenas exist for. Anything else: application's images, retried later.
- An identity is given up after the core's two frames and, in addition, half a second without a draw. A counted frame under Vulkan is a present after which something was drawn, which with frame generation is not a frame of the game.
- The next version number after 0.9.9 is 1.0.0 (AGENTS.md rule); it does not mean a release.

## D-041: The character's frame is a command-buffer recording

- Date: 2026-10-13, after the identity lines of 0.9.9.
- The frame of the mod's logic (same place within a frame, one import per component, "a frame has passed") is the recording of the command buffer that holds the character's draws. Basis: all matched draws of a frame are in one command buffer and one submit in every captured frame, and a recording begins before each frame.
- The present stays as a second clock for periodic work and expiry. It is not a frame boundary at record time under Vulkan: recording runs ahead of presenting.
- Two command buffers with character draws in one frame would count as two frames. The rules tolerate that (place within the radius, a second import), and recordings that interleave are handled by taking the distance between frame numbers without sign.
- Version 1.0.0 follows 0.9.9 by the counting rule of AGENTS.md; it marks no release.

## D-042: The replacement code is a module; the tracker is the host

- Date: 2026-10-13, on the user's request and decision (AGENTS.md: swapping the module into the running game is the agent's choice).
- The line is drawn where state ends: hooks and tracker hold what cannot be rebuilt and stay in the injected DLL; the character backend and everything it owns can start again at any time and is the module. The built-in copy stays the default, so the layer works without any module file and the tests of both paths are the same tests.
- Structures cross as they are, guarded by a stamp over the shared headers and compiler flags. Chosen over a C interface with copied data: the backend reads large parts of the tracker on every draw. The price is that a change to a shared header cannot be swapped; the stamp turns that into a refusal instead of a crash.
- A used module is never unloaded and an old backend is destroyed off the draw path, after an exclusive lock has shown that no call is inside it. Chosen over reference counting on the draw path and over leaking old backends (their textures alone are some 200 MB).
- The new backend is staged and takes over when ready, not at once: otherwise every exchange would show the game's model for as long as textures upload.
- Every exchange in the game is recorded in build/hot-swaps.json; it is a load of new code into the real game.

## D-043: Original main INI in the backend, with native Core contracts and staged F10

- Date: 2026-10-13, user requests live INI conditions, outfit keys, persistence and F10. Same character and LOD0/LOD1 scope; no installed mod, Core or launcher edits.
- Link the pinned original XXMI frontend into both the layer and backend module. Isolate its globals, command/resource maps, pools, original key actions and transitions by generation under a module-local engine lock. Reuse the original expression/parser/matcher/dispatcher; do not make a second INI interpreter.
- A sidecar opts the existing mesh/material profile into live commands and refreshed file assets. Keep the native identity and bone backend established by D-033/D-035/D-041, with explicit Core registration, skeleton initialization/attachment and draw-instance adapters. Verify original evaluated bone ranges and both LOD maps against the profile; there is no per-component code. Accept the profiled mesh override registry only. Source-image hash overrides and arbitrary CustomShaders require their own adapters, rather than invented identities or skipped commands.
- Prepare every reachable DDS branch, including the alternate stocking, but not dormant file declarations. Existing profiled mesh BUF files are revalidated on reload; new stream/semantic structures need profile preparation. Bounded runtime plans preserve order and explicit null material bindings.
- Foreground-process keyboard sampling feeds original listeners. Persist only original persist variables, atomically and off the draw path, in project-owned settings. The original installation and DX11 saved state are read-only and are not used as a save destination.
- F10 parses/prepares a new complete generation while the active one draws, stages queue publication, carries the latest persistent state and suppresses held keys until release. Suspend the old settings writer before adoption. Old sealed commands retain immutable GPU owners and a unique generation token. Failed candidates retain the active generation. Identity/variant warmup may briefly show the game's character; do not promise a visually seamless exchange.
- The user's HLSL question is answered with actual compilation: use DXC to preserve the original panel language and compile it to SPIR-V. Both stages of the unmodified panel validate for Vulkan 1.1. This does not supply its missing Present/backbuffer pipeline, image/IniParams upload or cursor adapters. Keep that work explicitly in T-016.

## D-044: The directory and original language define compatibility

- Date: 2026-10-13/14, explicit user corrections while requesting arbitrary-character support. Yvonne is a second regression fixture; new game collection is deferred. The scope includes every enabled Mod INI and its Core/library/companion dependencies, with shared model, skeleton and texture handling. LOD0/LOD1 remains the requested rendering scope.
- Use the pinned original parser, evaluator, matcher and input system. Existing Mods supply test cases, never a whitelist of accepted syntax. Source parsing and declarations are API-independent; Vulkan adapters must implement each executed graphics operation or refuse it explicitly. A successful parse is not a successful draw.
- Make directory discovery and dependency loading the frontend entry point. Preserve original include ordering, namespaces, disabled names, cross-file references and command priority. Do not choose one character at a time as the production configuration model. Reuse resources and load/upload on demand; a full directory scan does not mean all files become resident.
- Model components, bone ranges, LOD maps, vertex/index buffers and texture choices come from original evaluated declarations and commands. No handwritten per-character/component branches. Keep game shader correspondence in a shared shader catalogue; per-character paired captures are not a normal installation requirement. Unknown shader semantics remain an explicit adapter gap rather than a guessed binding.
- Verify the installed directory offline: original syntax and references, declared file storage/decoding, model draw bounds and bone initialization, then sandbox graphics and generation lifetime. Keep the remaining PNG, CustomShader, ShaderRegex, resource-copy and complete-directory routing work visible. Do not edit original INIs or omit companion files to manufacture compatibility.
- Source audit follow-up, 2026-10-14: inventory the pinned parser dispatch, metadata and operand/operator tables; make missing/new constructs fail coverage checks. Validate parsing and actual execution separately with independent expected values. Retain original key-cycle semantics even when surprising. PNG integration is paused until the requested language/runtime work advances; no all-language claim follows from installed-folder or syntax-only success.

## D-045: Execute original GPU resource commands in an explicit owned domain

- Date: 2026-10-14, user-directed implementation order: general GPU resources,
  then CustomShader, shared shader matching, and complete directory graphics.
  Continue offline until game evidence is actually necessary.
- Keep the pinned original parser/evaluator. The command adapter dispatches
  GPU operations to a generation-owned extension; it does not build another
  INI interpreter. Initial buffers use original description and view-element
  helpers, logical source slots and explicit allocation/view metadata.
- Start with owned resources and private primary command buffers, requiring an
  explicit outside-render-pass scope and host serialization of one queue.
  This is not permission to split game render passes or submit concurrently
  with game calls. Application import and arbitrary inline placement remain
  architecture work; do not sneak waits into the existing character draw hook.
- Original store can affect the same command list's next condition. Read actual
  GPU bytes and wait only in an explicit supported scope; do not silently return
  an older asynchronous value. Regular submissions retain owners through fences.
  Share budget ledgers across reload generations, while isolating mutable data.
- Failed scopes cannot be submitted. Discard makes the candidate unusable;
  original CPU assignments and already-submitted feedback are not transactional.
  A working prior generation retains its own state. A logical source binding is
  not a Vulkan descriptor bind, and buffer execution is not shader execution.
- Image foundation follow-up, 2026-10-14 (1.1.2): require actual physical-device
  image limits and enabled logical-device features in the owned graphics-queue
  domain. Use registry-derived core format footprints, explicit subresources,
  real views and fence-retained allocations. Track pending layouts separately
  from submitted owners; only successful submission publishes GENERAL. This
  does not connect original INI texture semantics, expand the DDS loader or
  authorize application image import. PNG integration stays paused.
- Image-command follow-up (1.1.3): call original texture override/view helpers,
  preserve source metadata and copy ownership, and reuse pinned DDS format and
  footprint decoding. DXGI view compatibility remains stricter than Vulkan's
  compatible-format classes. Clear through the selected view's format, using
  budgeted scratch images when needed; do not convert integer UAV bit patterns
  through floats. Logical RTV/DSV/viewport state does not authorize application
  render-target binding. Integer RTV conversion and other format gaps stay
  explicit. Native D3D11 reference devices exist only in the headless test.
- Conversion follow-up (1.1.4): integer RTV clear conversion follows measured
  native WARP truncation and saturation, with hardware overflow differences
  recorded in LIMIT-029. R16/R32 color/depth bit copies use reusable resident GPU
  scratch without readback. Opaque color-to-D32 transfers require actual enabled
  VK_EXT_depth_range_unrestricted, not support alone. Host float-depth uploads
  validate copied values when that extension is absent (BUG-019). This does not
  authorize changes to an application's enabled extensions or its render passes.
- Parameter follow-up (1.1.5): retain the original float4 Texture1D contract,
  zero-before-Constants initialization, dirty float comparisons, root/draw flush
  boundaries and forced transition publication. iniparams reads the published
  allocation without flushing. Add an explicit initialization hook instead of
  moving existing native Core prepare. Shader consumers use retained views;
  logical stage slots alone do not change application descriptors. A draw host
  callback is optional and absent execution refuses. Keep owned scopes explicit
  through generation preparation and poison them on load/draw failure.

## D-046: Original reflection owns HLSL register identity

- Date: 2026-10-14, compiler foundation 1.1.6.
- Preserve original preprocessing, stage macros, model-5 source validation and
  include rules before DXC compilation. Derive slot numbers from original FXC
  reflection rather than assuming DXC automatic assignment agrees. Reserve
  independent Vulkan binding ranges for stages/classes/counters.
- Keep exact read-only source dependency snapshots and an unchanged old program
  after failed compilation. No caches in installed Mod directories or global
  compiler environment/CWD changes. SPIRV-Reflect is a pinned external source
  dependency under F:/vulkan; retain metadata and strip reflection-only Google
  executable annotations after reading it.
- Independent SPIR-V validation and native DX11/Vulkan output comparisons are
  separate evidence from INI shader execution. Unsupported legacy flags and
  target/device requirements remain explicit. Do not label syntax coverage or
  successful compilation as complete shader/Mod compatibility.

## D-047: CustomShader uses explicit owned domains and retained descriptors

- Date:2026-10-14, owned compute1.1.7. Reuse original keys/state parsers, command
  lists, phases and counters; replace compiler/GPU boundaries. Opt-in preserves
  the character path. All declared sources compile before candidate acceptance;
  failure rejects the candidate instead of original log-and-remove behavior.
- Immutable dispatch descriptors retain owners through ResourceSubmission fences.
  Use the explicit serialized owned resource domain outside render passes and
  shared budgets. This does not authorize application imports or hook waits.
- Validate SPIR-V using actual scalar/uniform layout enablement and a pinned
  validator DLL at an explicit path. Successful DXC compilation alone does not
  prove device compatibility. Unsupported execution cases refuse explicitly.


## D-048: Compute uses executable facts and native source defaults

- Date:2026-10-14, compute follow-up1.1.8. Bound executable storage/provenance
  analysis; refuse unknown layouts/origins. Validate actual sampler/image pairs,
  including array elements; unrelated fetches do not inherit filter restrictions.
- Indirect counts stay GPU-resident and must obey device limits. No hidden CPU
  feedback/wait or semantic-changing count rewrite; fences retain arguments.
- Empty owned source sampler slots use native D3D11 defaults, verified with WARP.
  Explicit host snapshots are separate from INI syntax/application import.
  Sampler quotas are shared within actual host-reserved physical headroom.
- Original owned INI UAV creation does not support append/counter flags. Importing
  application counter views remains separate. Source binding hazards and batch/OM
  effects are required; BUG-022 must be fixed before graphics integration.


## D-049: Source binding state follows native API side effects

- Date: 2026-10-14, binding fix 1.1.9. Logical Vulkan-owned slots represent
  D3D11 state, including read/write hazards and complete OM behavior. Buffer
  ranges are one source subresource; image mip/layer ranges remain distinct.
- Materialize an entire source slot range before publication. Native invalid
  output batches preserve prior bindings; do not publish part of a range.
- Validate semantics using actual system D3D11 WARP calls, with fresh devices for
  invalid calls. Preserve pixel/compute UAV differences and source OM save scope.
  Native queries and owned Vulkan validation are separate from game evidence.
- Correct historical fixtures that assumed invalid simultaneous bindings, while
  keeping their original data assertions. No game or deployment was needed.

## D-050: Validate native signatures before Vulkan IO relocation

- Date:2026-10-14, graphics linker1.2.0. Preserve original FXC register/component
  signatures and SPIR-V IDs. Native D3D11 shares adjacent stage registers; semantic
  identity does not repair an invalid native signature pair. Check that contract
  before relocating Vulkan input locations using case-insensitive source names.
- Bound module/metadata/dependency payload before copying. Keep caller programs
  immutable. Patch/per-vertex dimensions are distinct; wider vectors require the
  logical device's actual maintenance4 enablement, never mere physical support.
- Verify actual pixels against original DXBC on system D3D11 WARP. Compiler,
  independent linker host, original INI draw execution and game rendering are
  separate evidence. An invalid development fixture was corrected before release.

## D-051: Source graphics conventions are measured before draw integration

- Date:2026-10-14, candidate1.2.1. Native SM5 pixel w and vertex/instance IDs differ
  from raw Vulkan built-ins. Use the pinned DXC conversions and actual enabled
  draw-parameter capabilities; do not assume zero offsets or unit clip w.
- Negative Vulkan viewport height preserves source pixel centers, culling and
  front-face input in native pixel references. Original state masks are evaluated
  through the pinned helper. Compare semantic fields rather than struct padding.
- CPU state descriptions, isolated native/Vulkan pixels and production original
  INI drawing remain separate evidence. Continue connecting the owned executor.


## D-052: Graphics shares retained owned resource execution

- Date: 2026-10-14, candidate 1.2.2. Compute and graphics share immutable descriptor
  validation and fence-owned objects. The host explicitly owns the queue domain;
  private passes do not imply permission to interrupt application rendering.
- Validate actual enabled features, formats, state and buffer ranges before
  recording. GPU-produced indices/counts remain an explicit valid-range contract;
  do not introduce hidden waits or CPU readbacks for draw validation.
- Native DX11 pixel references validate graphics API behavior. Original INI stage/
  state conversion and application integration require their own evidence.


## D-053: Original owned draws use effective source state

- Date:2026-10-14, candidate1.2.3. Keep original lists, counters, arguments and
  nested phases; translate effective state at the GPU boundary. Normalize native
  ignored fields before later mask merges, based on actual state creation.
- Original comparison-filter state does not turn ordinary Sample into a depth
  comparison. PS sampler overrides touch slot0 only. Actual Dref instructions
  need their own adapter and refuse until it exists.
- Null PS preserves color while depth/stencil can still execute. Native pixel
  references and failed-root discard establish owned behavior; application import
  and complete-directory execution require separate proof.


## D-054: Validate the complete enabled source corpus with original metadata

- Date:2026-10-14, candidate1.2.4. The pinned parser supplies shader declarations,
  flags, namespace paths and include policy. Do not introduce another INI parser
  or reduce the inventory to selected characters. Preserve built-ins/null/inherit.
- Compile read-only installed sources through the production compiler, retain
  exact include snapshots and independently validate SPIR-V. Record required
  layout options rather than treating relaxed validation as device enablement.
- Full-directory compilation, owned shader execution and application/game
  rendering remain separate milestones. Continue execution work after the audit.


## D-055: Validate owned geometry with unchanged Core HLSL and native output

- Date:2026-10-14, candidate1.2.5. Use actual enabled geometry/layout/raster
  features, primitive modes and bounded interface/output limits. Geometry-stage
  storage belongs to vertexPipelineStoresAndAtomics; stage resource slots remain
  distinct. Last-stage position is required before rasterization.
- Reuse the original installed TextPrinter source and font as read-only inputs.
  Compare actual compute chunk records and rendered glyphs to native WARP; permit
  only bounded floating arithmetic/blend rounding (1e-6, observed5.96e-08), with
  exact integer chunk fields and nonempty foreground/background checks.
- Original HLSL execution in controlled owned fixtures is distinct from complete
  Core INI/window/backbuffer integration and real-game rendering. Continue offline.


## D-056: Preserve source layout identity, edit timing and outer draw lifetime

- Date:2026-10-14, candidate1.2.6. Retain the original input creation signature;
  native registers, not current semantic names alone, determine fetched values.
  Use the pinned element-edit helper and its actual behavior without repairs.
- Native invalid creation preserves the current layout. Unsupported Vulkan fetch
  or device limits refuse explicitly; do not disguise them as source no-ops.
- Apply edits before parameter flush/argument evaluation, including zero draws
  and dispatches. Keep changes through nested CustomShader/Present calls; restore
  at intercepted draw completion, including failure. Shader and GPU resource
  lifetimes remain independent from the immutable CPU layout description.


## D-057: Instance divisors require enabled features and actual base properties

- Date:2026-10-14, candidate1.2.7. Represent instance rate in the graphics
  pipeline and cache identity; query/enable KHR/core features in the owned host.
- Preserve source first-instance addition after division, including divisor0.
  Unknown GPU indirect bases require supportsNonZeroFirstInstance. Do not add
  hidden CPU waits/readbacks. Physical support alone is not logical enablement.
- Retain direct zero-stride fallback, and keep application device feature import
  separate from owned sandbox evidence. Native pixels verify all four draw forms.


## D-058: Preserve fetched vertex bits before shader interpretation

- Date:2026-10-14, candidate1.2.8. The input assembler format determines numeric
  conversion and absent-component defaults. Retype Vulkan inputs to that class,
  then bitcast loads to the original shader type. Do not use numeric casts.
- BGRX supplies float alpha1 before any reinterpretation. Include fixups in the
  cache key even when BGRA/BGRX share the same Vulkan format. Preserve source
  signatures and validate unmodified/lowered modules independently.
- Optional packed16 BGR remains explicit: local DX11 hardware refuses it and
  WARP's successful creation yields only defaults. Creation success alone is
  not evidence for the intended decoded vertex values. Continue other offline
  integration without claiming these formats or application input import.


## D-059: Match phases share one transaction; stage roots retain source boundaries

- Date:2026-10-14, candidate1.2.9. Preserve all-pre then all-post match ordering.
  Abort and transient state are shared within a matched phase, while independent
  root invocations reset transient state. Keep applied GPU state and transaction
  budgets across the complete call. Do not publish a partial failed plan.
- Source ShaderOverride stage scheduling needs independent roots and explicit
  original-call placement. Tracked shader identity differs from temporary custom
  executables. Import proven source facts; do not equate SPIR-V and DXBC hashes.
- Continue the owned implementation offline before application integration.


## D-060: Source shader facts and owned executables have separate lifetimes

- Date:2026-10-15, candidate1.3.0. Preserve the original tracked shader identity
  across CustomShader stage swaps; native query evaluation consumes proven hash
  and DXBC declaration facts. Validate complete snapshots before publication.
- Execute independent stage roots around one explicit original-operation callback
  in the same bounded transaction. No callback may submit or re-enter the runtime.
  A failure prevents submission of earlier recorded work. AfterDispatch does not
  acquire the original AfterDraw input-layout restoration behavior.
- Keep legacy shader substitution, ShaderRegex and application resource/state
  import explicit. Source declaration names alone do not imply an implementation:
  cb_mask/srv_mask return0 in the pinned evaluator, confirmed after correcting
  the initial audit. Owned pre/original/post pixels do not prove game placement.


## D-061: Resource filters use source command matching and explicit identity

- Date:2026-10-15, candidate1.3.1. Reuse original descriptor/draw predicates and
  region helpers. Command matching appends hash then fuzzy results; the source
  creation helper's early return is not equivalent. Keep this query's original
  target type when selecting region versus whole-resource matching.
- Import proven tracking/hash facts separately from GPU objects. Keep weak
  allocation identity through aliases; do not infer original hashes from Vulkan
  handles or newly copied bytes. The source policy owner updates those facts.
- Preserve original dynamic target re-evaluation and pool range reads. Bound
  non-finite casts and total execution, but retain finite invalid-range no-ops
  and unbound slot sentinels. No hidden GPU readback to discover missing hashes.


## D-062: Native regex links retain identity; assembly needs source evidence

- Date:2026-10-15, candidate1.3.2. Compile unchanged original ShaderRegex.cpp;
  retain RunLinkedCommandList type and shared identity inside a bounded wrapper.
  Repeated linking and native unlink/filter restoration must remain valid.
- Keep regex groups/index/hash and source model cache generation-local. Import
  proven native model facts with explicit source hashes. A Vulkan stage/model
  guess cannot justify matching or a silent nonmatch. Native disk caches remain
  disabled so the frontend cannot write installation-relative files.
- Relevant patterned groups currently refuse. Next reuse original disassembly,
  matching/substitution and signature-aware assembly before executable translation.
  Core's patternless GPU result does not establish RabbitFX or game compatibility.


## D-063: Native assembly matching precedes executable publication

- Date:2026-10-15, candidate1.3.3. Use unchanged Flugan disassembly and
  signature-aware assembly around the original regex implementation. Keep
  immutable bounded original DXBC and reject conflicts for a cached identity.
- Prepare against isolated native map nodes so failed translation cannot leave
  linked commands or invalidate live local-variable pointers. Pattern-only
  plans publish; bytecode-changing plans remain analysis-only until a compiler
  and exact executable association are available. Source query facts remain
  original metadata, independently of a patched executable.
- Preserve actual source grammar: only .Pattern is supported by this pinned
  parser. Internal data structures are not evidence for numbered INI syntax.
  Invoke original Rendering getters before regex hashing/disassembly, matching
  the initialization normally performed by the full native loader.


## D-064: Exact bytecode translation keeps device execution requirements

- Date:2026-10-15, candidate1.3.4. Use unchanged vkd3d-shader2.1 for exact DXBC,
  distinct from recompiling original HLSL. Scan instruction/signature facts;
  optional RDEF and application SPIR-V hashes are not authoritative substitutes.
- Only successful native outputs transfer ownership. A failed native compiler
  may free partial output without clearing its pointer. Reject external dump/
  config overrides and retain atomic caller-program publication.
- Preserve source float32 denormal semantics. Strict modules need actual enabled
  float controls and matching properties. The local NVIDIA default is measurably
  different for sign/absolute-value operations, despite matching basic arithmetic.
  Implement and verify software lowering before using this translation path there.
- Keep compiled bytecode, owned GPU execution, runtime regex publication and
  application/game integration distinct. Continue offline without user input.


## D-065: Float32 compatibility separates denormals from other device modes

- Date:2026-10-15, candidate1.3.5. Use bounded scalar/vector integer-bit flushing
  when explicit float32 denormal control is unavailable. Preserve data moves,
  result identity and arithmetic NoContraction decorations. Unsupported shapes
  refuse atomically; never silently drop the source's mode.
- Both paths require enabled SignedZeroInfNanPreserve32 and RoundingModeRTE32.
  An Intel reference test exposed NaN self-comparison optimization when only
  denormal control was declared. Software denormals do not replace these modes.
- Bound GLSL Fract results to the native range below1 without suppressing NaNs.
  Keep exact native WARP comparisons for both local GPUs and retain failing
  evidence separately from the corrected implementation. These tests are owned
  execution, not a real-game validation or whole-language conformance proof.

## D-066: Original buffer views are independent of executable representation

- Date:2026-10-15, candidate1.3.6. Supplement native scanner facts with bounded
  original SM4.x/5.0 buffer declarations. Retain raw/typed/structured class and
  source stride even when translated storage is a scalar word array. Cross-check
  registers/raw flags, and validate the original INI view class/stride at use.
- Typed buffer UAV programs use texel views for native format conversion. Check
  actual logical read/write feature enablement, enabled format queries and
  per-format capabilities using traced binding accesses. Known formats still
  require exact agreement. Capability failures cannot submit partial GPU work.
- Keep owned view adaptation separate from application import and UAV counters.
  A failed resource execution poisons that generation; preserving previous GPU
  output does not imply recovery within the same failed generation.

## D-067: Original INI warnings are recoverable; they never reject a generation

- Date: 2026-10-18. Claude takes over the INI language and runtime work at the user's request ("ini this part is handed to you").
- Basis: the pinned IniHandler, CommandList and ShaderRegex code report problems through LogOverlay and keep what they could parse: duplicate sections keep the first copy, unrecognized lines and broken expressions are skipped, duplicate ShaderOverride hashes are merged with a conflict warning, a numbered Pattern section disables its group. No warning or dire message stops the original loader or an F10 reload. D-044 makes the original language the authority.
- The hosted runtime and the folder inspection rejected any warning. That kept INPUT-001's installed Mod and, in a directory generation, every Mod beside it from loading at all; tests had encoded the deviation (malformed F10 refused, duplicate hash refused, numbered pattern refused, dormant typo refused).
- Now: a generation loads with the original's recovery; warnings are counted, the latest 64 texts kept, logged at load and reported in the status. The inspection completes, lists them and fails only on dire messages. Fixtures still require a reported warning for every invalid construct, so the rule against silent acceptance holds.
- Unchanged: explicit adapter refusals (constructs or operations without a Vulkan implementation, including D-047's compile failures) still refuse the candidate and F10 keeps the active generation for them. Offline package builders stay strict.

## D-068: The execution matrix is the authority for INI execution; overlay and upscaling flip accepted

- Date: 2026-10-18 (candidate 1.3.9, branch claude/ini).
- `docs/ini-execution-matrix.json` replaces the hand-kept execution table of docs/ini-language-coverage.md. Every original command class has an entry; CPU decisions are probed and owned GPU decisions cite sandbox checks, both enforced by a suite step. New classes in a future pinned source fail the suite until decided.
- `special = upscaling_switch_bb` runs as original code: it only clears the generation's bb_is_upscaling_bb flag, whose one original reader is the DX11 `bb` lookup that a hosted generation never runs. If owned backbuffer placement is built, it must honour the flag.
- `special = draw_3dmigoto_overlay` is a no-op: the original draws the 3DMigoto overlay only when the swap chain has one, and a hosted generation has none. This is the original behaviour in that situation, not a rendering feature left out; the rule that missing rendering features refuse stands.

## D-069: Original window and cursor queries read a host sample

- Date: 2026-10-18 (candidate 1.4.0, branch claude/ini), first step of the image panel.
- The original reads window size, cursor position and visibility through GetClientRect, GetCursorInfo and ScreenToClient on G->hWnd and keeps the swap chain size in mResolutionInfo. A hosted generation must not query a real window from inside the game or a test, and its results must be reproducible, so the host supplies one sample per present (RuntimeInput window, cursor, resolution) and the original functions answer from it. G->hWnd is a token that only these functions recognise; nothing passes it to Win32.
- Original fallbacks are kept: no window means no hWnd (window positions are screen positions, window_width falls back to the resolution and then the fallback screen size); a resolution of zero keeps the earlier value, starting from the original's unset -1.
- The window heuristic's render-target step would query the reference D3D context, which a hosted generation does not have. It continues as the original does with no render target bound; RT_WIDTH itself stays refused.
- The fallback screen size is IniHandler's default 1920x1080 because a generation reads no d3dx.ini. The user's d3dx.ini sets 2560x1600; the difference is reached only when neither a window nor a swap chain size is known.

## D-070: Owned CustomShaders use the original shader cache

- Date: 2026-10-18 (candidate 1.4.3, branch claude/ini).
- The pinned CustomShader::compile loads `<file without extension>.<model>.<flags in lowercase hex>.bin` instead of compiling when its last-write time equals the HLSL's exactly, and writes such caches only with cache_shaders. All 19 enabled Mods with an image panel ship `Draw_2D.vs_5_0.8000.bin` and `.ps_` with matching times, so the panel that runs under DX11 is that DXBC, not a recompile of the HLSL beside it.
- GpuShaderRuntime applies the same rule. With enable_cached_bytecode the cache runs through the exact DXBC translation (vkd3d-shader, docs/bytecode-compiler.md); without the opt-in a valid cache refuses, rather than silently running different code from the HLSL. No cache is ever written beside an installed Mod.
- Codex's uncommitted work adds a separate bytecode opt-in for source shaders (ShaderRegex) to the same class; the two can be unified when that work is committed.

## D-071: D3D11 null views become Vulkan null descriptors

- Date: 2026-10-18 (candidate 1.4.3, branch claude/ini).
- D3D11 reads a null shader resource, constant buffer or UAV as zero, reports zero dimensions and drops writes. The image panel relies on it (t101 is null unless minimised). VK_EXT_robustness2 nullDescriptor gives the same behaviour for a descriptor written with a null handle.
- An unbound CustomShader slot is written as a null descriptor when the host reports nullDescriptor enabled on the device (ShaderHost::null_descriptors); otherwise it refuses as before. Only that feature is used; robust buffer and image access stay disabled, so in-bounds behaviour is unchanged.
- In the game this needs the feature on the application's device (step 4): enabling it at device creation or refusing the panel are the options.

## D-072: A generation in the game combines the character extension with owned GPU execution behind a host scope

- Date: 2026-10-19 (candidate 1.4.4, branch claude/ini), phase 1 of completing the INI in the game.
- Replaced character draws must stay inside the application's render pass with the game's pipeline variants (the file plan); original GPU commands (clears, copies, store, CustomShader, non-indexed draws) need the owned outside-render-pass domain. One generation keeps the INI's variables shared, which the image panel needs (it toggles the main INI's outfit variables).
- The owned GPU callbacks therefore apply only while the host has an owned scope open (Present, Constants, key and reload lists). Replaced draws open none and behave exactly as before; a CustomShader inside one refuses until phase 1 step P2 records such work into private command buffers before the application's submission.
- Declaration references executed by the GPU side are mirrored into the plan; references made by replaced draws are not yet visible to the GPU side (recorded limitation). The native extension's preparation runs with the gate closed, and its natively executed CustomShaders are not compiled.

## D-073: In character mode the layer enables what owned GPU work needs on the application's objects

- Date: 2026-10-19 (candidate 1.4.6, branch claude/ini).
- Owned Present work runs on the application's device and swap chain. It needs D3D11 null views (robustness2 nullDescriptor, D-071), vertex IDs with D3D semantics in HLSL-compiled shaders (shaderDrawParameters, core in Vulkan 1.1) and a swap chain image usable as a color attachment.
- Only in character mode, only when supported and not already requested: the layer adds the robustness2 extension with nullDescriptor alone; sets shaderDrawParameters in the application's own Vulkan 1.1 or draw-parameter feature structure for the duration of vkCreateDevice and restores it afterwards, or adds a structure of its own; adds color attachment usage to a swap chain lacking it (every swap chain supports it). Application requests are never changed otherwise.
- The status reports each addition, so a session shows exactly what was enabled. None of this affects the application's own rendering; it is not an anti-cheat measure in any direction.

## D-074: Operands the original answers with Win32 or the device state

- Date: 2026-10-19 (candidate 1.4.7, branch claude/ini), phase 3.
- fps reads the original FPSCounter in the generation's globals; the host supplies the steady clock at each presentation (ticks_us, the original's GetSystemTicks) and the counter advances before the Present list, as in RunFrameActions. Zero leaves it.
- effective_dpi and cursor_hotspot_x/_y are answered by a host-value adapter from the host's sample; the original's Win32 calls (GetDpiForMonitor with a temporary per-monitor awareness, GetIconInfo) run in the layer at the presentation, not inside a generation. Without a sample the original's floor 96 and a zero hotspot remain. This supersedes the "stay refused" part of D-069 for these operands.
- rt_width/rt_height are answered only in the owned domain, from o0 with the original's view rule and state caching; the window heuristic uses the same answer. In replaced draws the game's render target is not known, so they still refuse there (D-069's note stays true outside the owned domain).
- ->hashregion/->spatialhash on owned resources return what the original returns for resources without its CPU data cache: -2 for null, -3 for images, UNKNOWN (-1) for buffers. Hashes of game data stay refused until game bindings are known; Core's spatial identification keeps its bounded adapter.
