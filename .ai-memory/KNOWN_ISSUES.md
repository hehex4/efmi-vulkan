# EFMI Vulkan - Known issues

> Last updated: 2026-10-12.

## LIMIT-001: Probe data and long-duration stability are not verified in the real game

Historical passthrough launches established injection timing and device interception with 0.2.0. Run 3 passed the earlier crash point and included scene changes, but the user exited before 30 minutes. The user accepts that run as initial G1 loading evidence (D-014); long-duration stability is deferred, not established. Layer 0.5.0 is prepared in `dist/`, but all its capture/resource-tracking results are sandbox-only; no probe-mode game capture or real-character verification exists. G2/G3 remain open.

## LIMIT-002: Validation dependency installed and sandbox validated

Khronos VVL 1.4.321 and SPIRV-Tools are built under `F:/vulkan`. The sandbox passes core/synchronization validation through both EFMI entries and all four descriptor modes. The full suite now requires it. No full SDK or glslc is installed; GPU-assisted and real-game validation remain unverified. See `docs/validation.md`, including the initial temporary-cache incident.

## LIMIT-003: The existing DX11 frame dump is not a merged-skeleton baseline

`FrameAnalysis-2026-10-07-123937` has zero CustomShader executions and no mod entry point hit; only the ShaderRegex checks of EFMI and RabbitFX ran. It was good for checking the region hash rule on unmodified draws, not for the merged-skeleton comparison. Since 2026-10-12 the comparison has its own frame: `captures/dx11-baseline/frame-a` (LIMIT-015).

## LIMIT-004: RabbitFX mods are out of reach for the first version

6 of the 9 local character mods pass their textures only through RabbitFX, which patches DX11 shader assembly. They cannot be degraded gracefully by stubbing RabbitFX.

## LIMIT-005: Dynamic offsets attributed to the first set (fixed in 0.4.0)

Offsets now split using the set layouts in set/binding/array order. The sandbox checks two sets with a dynamic uniform buffer and a dynamic storage-buffer array, both multi-set and nonzero-firstSet binds, through both entries and with secondary command buffers. Missing layouts produce an explicit unknown reason instead of guessed offsets.

## LIMIT-006: Extended descriptor paths and full reflection remain incomplete

Layer 0.5.0 adds templates and direct/template push descriptors to ordinary writes/copies and resource metadata. Descriptor buffers, specialized descriptor types and dynamic inline pipeline layouts remain unknown; unsupported paths discard stale knowledge. Only graphics and compute bind points are tracked. At most 256 stored elements per set per draw are emitted, with explicit counts/truncation. Swapchain images are not tracked as images yet. Unknown layout extensions have conservative compatibility across different handles. Core 1.4 and maintenance6 push wrappers and KHR template lifecycle/update aliases lack GPU coverage; see LIMIT-011.

`efvk_shader` exports retained SPIR-V and extracts direct declared names/bindings; it is not full reflection (no decoration groups, type/member layout, specializations, pipeline-library linkage or shader objects). See `docs/probe-resources.md`. G3 remains unverified in the game; b/c also need record-time availability evidence (LIMIT-008).

## LIMIT-007: A shadow knows only what passed the CPU

Sources are staging copies (source mapped when the copy is recorded or submitted, or snapshotted at unmap when at most 16 MiB), `vkCmdUpdateBuffer` and `vkCmdFillBuffer`. A buffer filled by compute, or by a copy whose source was never CPU-visible, is reported with `src: none` and counted under `unknown_writes`. There is no read-back from the GPU (T-013). `unknown_writes` in the frame statistics is the number to watch in the game.

## LIMIT-008: Hash and read-back are "as of now", not "as of the draw"

Seen in the game on 2026-10-12: the uniform scratch buffer alternates between two buffers and is rewritten every frame, so a read a few milliseconds after a capture returned the right bytes for 8 of 12 components once and for 1 of 12 the next time. Since layer 0.6.0 a capture can keep the bytes at submit and compare them with a read at record time (`docs/probe-snapshots.md`); that is verified in the sandbox and ran in the game on 2026-10-12 (fact 21): entries and own palettes were complete at record time in the display scene.

The hashes of a captured frame are computed when the command buffer is submitted, after all of its recorded writes were applied; a copy recorded after a draw in the same command buffer already affects that draw's hash. `read_buffer` returns what the mapping or the shadow holds at the time of the request. For a host-visible buffer the application rewrites every frame, that is not what an earlier draw used.

Ordinary descriptor contents and all `src` visibility remain submit-time snapshots. Push descriptor values in 0.5.0 are retained when recorded, but retaining a handle/range does not prove record-time CPU byte availability. Neither path establishes G3's premise or tracks update-after-bind changes during GPU execution.

## LIMIT-009: What a draw record leaves out

Vertex buffer slots 0 to 3 and descriptor sets 0 to 3 only. Graphics and compute descriptor state are separate in 0.4.0, but compute dispatch has no sandbox GPU coverage yet. No push constants or dynamic state; indirect arguments are not read and indirect draws get no region hash.

## LIMIT-010: The frame boundary is `vkQueuePresentKHR`

An application that does not present makes a capture time out. With several devices, the probe answers for the one that presented last.

Observed on 2026-10-12 with 0.8.0: the user's LOD1 world has many present intervals without scene draws although surrounding intervals and counters contain graphics. All 36 full-snapshot attempts were empty; no-snapshot collection obtains all nine LOD1 sections. An empty interval is not evidence of an absent character or broken scene. Bounded collector selection keeps attempts but is insufficient for these snapshots. T-023's 0.8.1 opt-in server wait passes native/GPU/full-suite checks in build/next; real LOD1 snapshot recovery is still unverified and the game retains 0.8.0. Do not label LOD0-world depth-only LOD1 matches as distant-LOD1 color coverage. Evidence: docs/m2-game-recipes.md.

## LIMIT-011: Scope of the sandbox evidence

One GPU and driver (NVIDIA RTX 5060 Laptop). The sandbox uses classic render passes, one queue/swapchain, `vkQueueSubmit`, `vkCmdCopyBuffer`, `vkMapMemory`, two descriptor slots, storage arrays, an immutable sampler, a sampled texture and shader modules destroyed before capture. In 0.5.0 it exercises ordinary writes/copies, core 1.1 templates and KHR direct/template pushes through both entries with primary and secondary command buffers. Dynamic rendering, `2`/maintenance6 variants, core 1.4 push wrappers, KHR template lifecycle/update aliases, indirect draws, dispatch, compute/texel pushes, `vkCmdUpdateBuffer`, `vkCmdFillBuffer` and inline pipeline modules have no GPU coverage. Native C++ tests are separate CPU evidence. The rendered picture was not inspected. Core/synchronization validation and synthetic overhead measurements are in `docs/validation.md` and `docs/probe-performance.md`; neither is game evidence.

## LIMIT-012: The layer entry needs a registry entry for an elevated process

The Vulkan loader ignores path variables such as `VK_LAYER_PATH` in an elevated process, so the manifest in `dist/` would not be found there. The game probably runs elevated (EFMI's `d3dx.ini` has `require_admin = true`, and the EFMI MCP notes say the game runs as administrator). The layer entry would therefore need an implicit layer entry under HKLM, a system-wide change. The inject entry avoids this and is the entry planned for the game (D-011); the layer entry stays the fallback.

## LIMIT-013: What is and is not known about the inject entry in the game

Known from three historical launches on 2026-10-11 (CURRENT_STATE): injection precedes `vulkan-1.dll` by about 4 to 5.5 seconds, and the engine's creation calls reach the hooks. Only 0.2.0 passthrough has been in the game; probe mode and later builds have not.

Still open:

- Long-duration stability. Run 3 survived the previous crash point and included scene changes but ended early. The user accepts it as initial loading evidence, so repeating 30 minutes is not a prerequisite (D-014). BUG-001 remains strongly associated with the removed DX11 hooks.
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
- **Attribution narrowed on 2026-10-12**: a Vulkan launch at 09:44 with XXMI `d3d11.dll`, the DX11 probe and this DLL crashed again (crash report `Crash_2026-10-12_014543454`, about 50 s after start); the launch at 09:47 with the DX11 probe and this DLL, but without XXMI's `d3d11.dll` (injection mode Skip), ran for more than nine minutes in probe mode. The DX11 probe is therefore not the cause; XXMI's `d3d11.dll` in a Vulkan process is. Two crashes with it, two clean runs without it.
- **Result of the test (run 3, 20:38)**: with only `efmi_vk_inject.dll` injected the game passed the point of the crash and kept running. Rule from now on: for a Vulkan session the launcher's XXMI DLL injection mode is Skip and the DX11 probe is not in the injected libraries.

## ISSUE-001: Other implicit Vulkan layers are registered on this machine

Steam overlay, Steam Fossilize and the Epic Online Services overlay are registered under HKLM, the OBS hook under HKCU. They may be active in the game's Vulkan path as well. When reading real-game results, remember that this layer is not the only one in the chain. The sandbox tests switch them off for their child processes.

## LIMIT-014: The cost of full tracking in the game is known for one scene only

Third session, 0.7.0: the user reports intermittent stutter despite about 120 average presents/s. Upload processing accumulated 8.1 seconds; a later steady three-second interval adds only 596 microseconds. Later 0.7.1 game observations confirm spikes with hashing off and with both hashing/resources off (ISSUE-005 / T-020). Attribution remains open; next-launch passthrough is prepared and sandbox-verified only.

Second figures, 2026-10-12 with 0.6.0, same scene: 120 frames per second while idle and during about 1,100 snapshot captures, working set 11.8 GB and flat over 25 minutes, 18.9 MB of SPIR-V for 1015 shaders, about 2,200 live descriptor sets, 3.4 MB kept by a capture of the mod's 45 draws and 14.1 MB by one of all 210 indexed draws.

First figures, 2026-10-12, character display scene: about 120 frames per second with full tracking (no figure without the probe; it may be the game's cap), working set flat at 11.5 to 11.6 GB over 7 minutes, 22.9 MB of SPIR-V kept for 1106 shaders, about 190 descriptor set allocations and template updates per frame. The paragraph below describes what remains unmeasured.

Sandbox overhead figures (`docs/probe-performance.md`) come from a program that does not update descriptor sets per frame and has two shader modules. In the game, every descriptor update takes the exclusive resource lock and updates ordered maps per written element, and every bind takes the lock shared. Memory is bounded for SPIR-V (512 MiB default) and for one capture's descriptor contents (64 MiB default), not for the number of live descriptor sets, images or views. The fallbacks are `resources = 0` and `mode = passthrough` (D-017, `docs/first-probe-session.md`).

## ISSUE-002: The tracker test once failed to link with LNK1163 and linked on the next attempt

2026-10-11, building into `build/next/`: `state.obj : fatal error LNK1163` (invalid selection for a COMDAT section) while linking `efmi_vk_tracker_test.exe`; `state.cpp` had not changed since a build that linked. The same command succeeded immediately afterwards and in every later run. Cause unknown. If it comes back, look at whether two builds can write `sandbox/build/tracker/` at the same time; do not add a silent retry.

## BUG-002: Native regression appended to the historical game log (fixed)

The tracker test links the layer logger, whose default path is beside its executable. Running the final suite against dist/ appended one test startup line to the existing game log. The original 2246-byte prefix exactly matched the saved backup; no historical bytes were lost. Preserved the mixed log in build/backups/native-log-repair-20261011-223459/mixed.log and restored the exact original bytes from the before-0.5 backup.

sandbox/native_test.py now always sets EFMI_VK_LOG and its working directory to a new directory under build/tests/. The corrected native test passes 62 checks, with the historical log unchanged before/after (SHA-256 2e8a46cf4985363ce1b5daea4942b5195e4dee1ffaefbe67e1543946e8f961b4).

## LIMIT-015: Scope of the DX11 baseline

Four frames of one DX11 session (mods on twice, off, on again), one scene, one character, the mod's key states as found; the Vulkan frames come from separate sessions with another window size. The third Vulkan session adds content hashes and resolves all four sample ambiguities (T-018); unique shape/size/format matches still have weaker evidence than content matches. Earlier frames with differing per-component draw counts cannot be paired, so both frames need the same scene state. The slot table covers this scene's observed fragment shaders only, and a fragment shader whose pipeline was not exported is listed under its pipeline number. M2 must not guess mappings for new shaders.

## ISSUE-003: `python` on the PATH of this machine is the Windows Store stub

`python` and `python3` resolve to `%LOCALAPPDATA%\Microsoft\WindowsApps` and exit with code 49 without output. Use `D:\Python312\python.exe` (as `.mcp.json` does) or `py`. Seen on 2026-10-12 when a capture script appeared to do nothing.

## LIMIT-016: What the snapshot of 0.6.0 does not see

Indexed draws only, sets 0 to 3, uniform and storage buffers. Record time is represented by a checksum, so `different` does not say which bytes changed. `not_recorded` covers three cases that the item does not tell apart: the draw was recorded before the captured frame began, its index bytes were unreadable when it was recorded, or they had another hash then. The submit-time bytes are the CPU side at `vkQueueSubmit`; a write to mapped memory after that and data produced by compute are not seen. The follow window has a fixed size chosen by the caller (12,288 bytes in the game preset), not the size of a component's bone palette, which the instance entry does not give; in the game the preset reaches behind the last palettes and reports `different` for them in most frames although the palettes themselves are complete (measure with a window of the mod's bone count instead). With `resources = 0` a selected draw has no ranges. The MCP tools do not offer the snapshot.

## LIMIT-017: What the content hash of 0.7.0 covers

Only images filled by `vkCmdCopyBufferToImage` (and its `2` forms) over the whole of mip 0, layer 0; a blit, an image copy, a clear or rendering leaves the state as it was. Formats outside the list in `format_block` report `unsupported_format`. The value is of the upload as submitted, not of what the image holds after later partial writes (those set `partial`). The third session confirms cross-API content matches for this scene and resolves T-018; it does not prove identity for every texture, streaming state or scene. Reading/copying/hashing upload memory accumulated about 8.1 seconds for 2.55 GiB. The prepared dist/ cfg therefore disables hashing after preserving the required evidence (D-023). Counters and CRC equality alone cannot identify intermittent frame-time spikes or guarantee collision-free identity.

## ISSUE-004: The sandbox's expected file could not be opened right after it appeared (handled)

2026-10-12, once in a full suite run: `PermissionError` in `harness.py` when opening `expected.json` immediately after its existence was seen. The sandbox writes a temporary file, closes it and renames it, so its own handle is gone by then; something else held the fresh file for a moment. The harness now waits out a sharing violation inside the 30 s readiness deadline it already has. This is a wait for a known condition with a deadline, not a retry of a failed build step (compare ISSUE-002).

## ISSUE-005: User-visible intermittent stutter with probe tracking (resolved on 2026-10-12 with the streaming loads of 0.7.3, confirmed in the game with 0.7.4)

Resolution: PID 27188, 0.7.4, open world: 24 / 3 / 2 of 87,005 present intervals over 16 / 33 / 50 ms; copies at unmap at 3.6 GB/s; the user reports the stutter gone. Evidence and conditions in `docs/unmap-snapshots.md`. Earlier record follows.

Latest game comparison: 0.7.2 probe/inject PID 41044 still stutters with resources/hash off and profiling on. A clean 24-second world sample has 17 host intervals over 50 ms, retained max 129.125 ms. Expensive unmap snapshots span 128.807 ms of that peak, while a zero-byte copy-record operation waits about 44 ms; the paths contend on res_mx. This strongly identifies a contributor. The bounded history rolls over, so uncorrelated peaks do not exclude lost work. Evidence: captures/pacing-tracker-work-20261012/, docs/pacing-tracker-session.md. 0.7.3 is fully sandbox-verified and deployed after game exit, accelerating detected WC reads without removing snapshots. No post-fix game result yet. Earlier user-confirmed smooth 0.7.1 passthrough remains the control.

The user reports spikes during loading and steady display in PID 19264 on 2026-10-12. Upload processing accumulates about 8.1 seconds for 2.55 GiB; in a later three-second interval it adds only 596 microseconds while 360 presents advance. The average does not measure tail latency, and 0.7.0 has no interval history. Cause of the steady-scene spikes is not established. The texture identity data is now complete, so dist/ disables image hashing for the next launch; it does not hot-reload. 0.7.1 adds bounded host interval diagnostics for a controlled comparison. No game-tested stutter fix is claimed. See docs/third-probe-session.md and docs/frame-pacing.md.

Follow-up with 0.7.1, PID 31124, hashing off/resources on: the user says open-world movement stutters severely and the static display cannot reveal it. Foreground observation over 39.992 seconds: 27 intervals over 33 ms, 12 over 50 ms, maximum 111.204 ms, no captures and zero hash time. This confirms persistent host interval spikes without hashing; it does not identify the cause or establish GPU/display frame times. At that point resource tracking had no off-baseline. A supplementary quiet interval changed focus and is not a clean comparison. See docs/pacing-hash-off-session.md.

Resource-off follow-up, PID 33204: user reports continuing stutter after restart; live settings confirm resources and hashing off. A 30.000261-second selected foreground interval (300/300 polls) has 74 intervals over 33 ms, 26 over 50 ms and a retained peak of 105.000 ms. Hash work, tracked images/descriptors and captures remain zero. Earlier focus-confounded segments are excluded. Different world workloads prevent causal comparison of the counts. Remaining draw/buffer tracking, injection, the game and the rest of the graphics stack are unisolated. Prepared passthrough for the next process after a successful actual-config sandbox smoke; current game remains probe until exit. Passthrough has no pipe and still includes injection/setup/present accounting. See docs/pacing-resources-off-session.md.

## BUG-003: Unreadable buffer copies allocated fallback storage (fixed in 0.7.2; game impact unknown)

The record-time path allocated and zero-filled the requested range before discovering that its source was not CPU-readable; clearing the vector left its capacity until command reset. Native coverage now proves a 64 MiB unreadable source allocates no fallback or destination byte storage. The submit path also no longer copies through a temporary vector. Submit priority, unmap snapshots, record-time fallback and self-copy semantics pass native/GPU regression. These were real unnecessary costs, but their role in ISSUE-005 is not established. Optional profiling adds clock/counter overhead and omits some hook paths; nested/concurrent wall times must not be summed. See docs/tracker-work.md.

## BUG-004: Slow mapped reads hold the resource lock during full staging snapshots (fixed in 0.7.3; confirmed in the game on 2026-10-12)

Confirmed: with the streaming loads the same volume of staging copies takes 7.7 s per 27.9 GB instead of 213.8 s per 69.7 GB, and the long present intervals are gone. The next paragraph was written in between and is wrong: the copied buffers were staging buffers, not mesh pools (`passed_over` 0 of 144,103; D-027).

Wrong attribution of 2026-10-12, kept as record: the title this entry had spoke of staging snapshots. The buffer inventory of the same session shows the copied buffers were the game's 16 MiB index and vertex pools on memory type 4 (2,612 generations on 22 pools; 87 on the 76 staging buffers). They matched the rule because they carry the transfer-source bit and are exactly the limit in size. 0.7.3 accelerates memory type 2 by a factor of 19 and memory type 4 by 1.5 in its own benchmark, so it does not reach them. 0.7.4 copies transfer-only buffers; see `docs/unmap-snapshots.md` and D-026. Not game-tested.

Earlier text:

0.7.2's unmap path copies every eligible mapped transfer-source buffer under exclusive res_mx, using ordinary memcpy. A foreground sample copies 3.46 GiB in 11.625 cumulative wall seconds; large snapshots and shared-lock waits overlap the largest present peaks. Local Vulkan type-2 WC memory reproduces slow reads: 8 MiB takes 24.023 ms normally and 1.270 ms with the production 0.7.3 helper. Snapshot capability is retained; the helper selects streaming loads only after OS WC classification and CPU support checks. Cached/unknown mappings keep memmove; exact bounds, remap and fallback paths pass native/regression coverage. GPU-local mapped type 4 remains expensive even with this helper (150.952 ms for 8 MiB), so it is not a universal mapped-memory performance fix. Game improvement and removal of all stutter remain unverified; keep ISSUE-005 open. D-025 and docs/pacing-tracker-session.md.

## ISSUE-006: The suite in dist/ read the diagnostic settings file and replaced the tested DLL (handled)

2026-10-12: running `sandbox/run_all.py` without `EFMI_VK_DIST` builds into `dist/` and tests there. With the diagnostic settings of a game session in `dist/efmi_vk.cfg` (reduced tracking, profiling) most steps failed, and the build step had replaced the DLL with a new, untested build of the same sources (builds are not byte-identical). The tested files from `build/next` were copied back. The harness now pins every setting to the defaults of the source for a test that names its mode (`PINNED_SETTINGS`), and the native test does the same; `mode=None` still means "use the settings file". Deployment is: full suite against `build/next`, copy those files to `dist/`, then `run_all.py --no-build` there and a smoke run with the settings file as the game reads it.

## LIMIT-018: A draw whose pool is unmapped has no hash (0.7.4 only; not observed, and the default is back in 0.7.5)

Status: in the game no pool is ever unmapped and every open-world hash comes from the mapping, so this did not occur. With 0.7.5 the copy covers every transfer source again; the text below applies only with `unmap_snapshot_all = 0`.

Index bytes are read from the application's mapping or from a shadow fed by tracked buffer copies. Since 0.7.4 a pool is not copied at unmap and its shadow is dropped then, so a draw that is hashed while its pool is unmapped reports `hash: null` with source `none`. Before 0.7.4 it would have reported a hash from the copy, at the price of the stalls of BUG-004. How often this happens in the open world is not measured (T-021). `unmap_snapshot_all = 1` brings the old behaviour back for a capture that needs it.

## LIMIT-019: The 0.7.4 game figures are not a foreground-only sample

The 87,005 present intervals with 3 over 33 ms span everything between two status reads about twelve minutes apart, with the game partly in the background; the 0.7.2 figures it is compared with are a 24-second foreground window. Two attempts to wait for a foreground window found none, then the user ran the route. The user's own report and the copy speed measured inside the process do not depend on this, but a like-for-like interval comparison does not exist.

## ISSUE-007: A wrong attribution was acted on before it was checked (2026-10-12)

The review of the stutter work concluded from per-buffer generation counters that the game's mesh pools were being copied at unmap, changed the copy rule (0.7.4), recorded D-026 and rewrote BUG-004 accordingly. `generation` also counts tracked buffer copies, and the inventory used could not show short-lived staging buffers. The counters added in the same change showed the error in the next game session. Corrected in D-027 and `docs/unmap-snapshots.md`; the code default is restored in 0.7.5. No harm in the game: the wrong rule skipped nothing there.
