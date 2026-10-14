# EFMI Vulkan - Known issues

> Last updated: 2026-10-14.

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

Observed on 2026-10-12 with 0.8.0: the user's LOD1 world has many present intervals without scene draws although surrounding intervals and counters contain graphics. All 36 full-snapshot attempts were empty; no-snapshot collection obtains all nine LOD1 sections. An empty interval is not evidence of an absent character or broken scene. T-023's 0.8.1 opt-in server wait now passes native/GPU/full-suite checks and two real LOD1 observations: first attempts succeed after 8/7 empty intervals, with all 58 relevant entries and both bone windows equal at record/submit. Default capture still returns a single possibly empty interval; the exact present producer remains unproven. Do not label LOD0-world depth-only LOD1 matches as distant-LOD1 color coverage. Evidence: docs/m2-game-recipes.md.

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

## BUG-005: The patched bone offsets were one matrix too high (0.8.2 to 0.9.0; fixed in 0.9.1 before any game run)

`MergedSkeleton::patch_entry` wrote the row of the instance's first merged bone into constant 5. The game's vertex shaders read bone `i` at `offset + 3 + 3 * i`, so the entry has to name the matrix before it: the core passes `$merged_skeleton_offset - 3` (in `MergedSkeleton.ini`, not in the HLSL), the DX11 baseline holds `(1533, 3030)` in every patched entry of the 499-bone sample, and the disassembled Vulkan vertex shader of the component 7 draw adds the 3. With 0.9.0 every vertex of a replaced draw would have used the next bone's matrix and the last bone a zero matrix. Found in review on 2026-10-12; 0.9.0 was deployed with the replacement enabled but no game process had loaded it.

Why the suite passed: the native test, the Python reference and the sandbox vertex shader were all written from the same reading (the fixture read `offset + 3 * index` and put its original mesh on bone 1). Now the fixture's shader addresses bones as the game's does, the native test checks the pair measured under DX11, and the reference follows the INI. With the corrected fixture 0.9.0 fails all four GPU configurations. Account: `docs/m2-bone-import.md`. What remains unverified is unchanged: no DX11 compute output was compared and no real character has been drawn.

## LIMIT-020: A capture lists a replaced draw as the application recorded it

With the M2 backend active, `vkCmdDrawIndexed` is recorded for captures before the replacement decision, and the record is not marked. A captured frame therefore shows the original draw (index range, pipeline, bindings) whether or not it reached the driver. Which draws were replaced is known only in total, from `status.replacement` (`matched`, `replaced` and the refusal counters). A per-draw mark would need the layer's own draw to be recorded as well; not built.

## LIMIT-021: Three original texture correspondences do not admit the sample's 2D replacement

The display slot table has 19 original-resource correspondences. Shader typing reveals three 3D targets: 427e2365 ps-t15 to (0,35), 4e9748d0 ps-t15 to (0,36), 50c1e4bb ps-t14 to (0,36). The saved pairing confirms 1x1x1 3D sRGB RGBA8 originals on both APIs; the mapping itself is not disproven. Both saved DX11 mod-on captures bind 2D custom assets in those slots (draws 162, 145, 126). Binding metadata does not prove the DX11 shader's actual interpretation/pixel output. The Vulkan image adapter refuses these assignments and the slot table marks their incompatible dimensions. Do not silently treat the entire material callback as ready after dropping these operations. Sixteen other assignments pass the 2D rewriter offline. Details/evidence: docs/fragment-materials.md and build/fragment-images/report.json. Source/API semantics can be investigated offline before requesting more game input.

Follow-up: matching saved DX11 bytecode was located and FNV-1 checked; each affected declaration and SampleLevel instruction is 3D. A 2D resource therefore violates the D3D11 declaration contract. Headless DX11 controls pass on NVIDIA and WARP, but mismatched views yield zero on NVIDIA and sampled color on WARP, while both retain the bound view. Debug runtime is absent, so no DX11 validation pass is claimed. Evidence: build/dx11-dimensions/report.json. Default refusal remains; an explicit device/profile compatibility policy must not be confused with valid explicit-null semantics or actual game pixel proof.

0.9.6 static-package policy: dx11-invalid-2d-to-3d-zero explicitly marks these three exact profiles and binds the package to the measured vendor/device (4318/11545); other hardware refuses. Default material selection still refuses mismatches. All eleven affected historical contracts and their pipelines pass offline with this named policy, but game pixels remain unverified. See D-037 and docs/character-materials.md.

## LIMIT-022: Full frontend source profiles remain incomplete

The three formerly missing actual LOD1 fragments (0aeef59b, 310c25c3, a6d7088a)
have verified original DX11 correspondence from the user-positioned session of
2026-10-13. Both historical Vulkan LOD1 frames reproduce the same eight rules;
see docs/lod1-material-capture.md. Material replay now accepts 359 of 370 draws,
with eleven LIMIT-021 dimension refusals and no unknown sample profile.
Original Core texture queries need proven source-API identities, including the
effects of prior bindings. The historical runner intentionally supplies no such
provider and retains 283 unresolved calls. Native query mechanics are tested with
synthetic identities only. Full runtime/generation and DLL integration are still
implementation work and must not be marked done by a new capture. Do not suppress unknown runtime queries or broaden scope to LOD2+.

0.9.6 links static materials and shared resource helpers into the DLL under T-028;
it deliberately does not execute original Core source-identity queries. The full
hosted runtime remains open. Its packaged policy accepts all 370 historical draw
contracts; this does not resolve the historical runner's 283 source-query calls.

## LIMIT-023: The variant design assumes three application sets; a matched draw uses four (lifted for the character backend in 0.9.8)

Status 2026-10-13: 0.9.8 takes the private set's index from the layout (three to eight sets) and the character backend replaces draws on four-set pipelines; see BUG-009. The single-component backend of 0.9.1 and `MaterialProfile` still name set 3; they are not in use. The text below is the review finding as written.

Found in review on 2026-10-13 from saved game frames, no new session. `prepare_vertex_variant`
requires exactly three source sets, moves the vertex buffers to a private set 3 and expects the
instance window at set 2 binding 0; the fragment patch and `MaterialProfile` also name set 3. In
the 0.9.1 display frame (`captures/m2-component-0.9.1-20261012/.../display-a/attempt-01`) matched
draw 38, component 10, is recorded with pipeline 437: four sets, depth only (no color attachment),
bone buffer at set 3 binding 2. Its preflight refuses. Four-set pipelines are ordinary in this
game: 73 to 120 draws per saved frame have a set 3 bound. The other four saved frames have no
matched draw on such a pipeline, so the case depends on the scene or the pass.

Consequences. The sample deletes the mesh of component 10, so the right result for that draw is a
skip, and a skip needs the frontend to run for the draw, which needs its entry and bone windows
located without the variant preflight. If the draw falls back, the original mesh stays in that
depth pass while the color passes omit it. A mod that draws geometry for such a component needs a
variant whose private set index follows the layout (set count, below `maxBoundDescriptorSets`)
instead of the constant 3. Related and unexplained: the live 0.9.1 session counted 227 refusals of
57,751 matches under one combined reason string; whether a bound set 3 was the condition is not
known, and no saved frame shows a three-set matched draw with a set 3 still bound. A counter per
refusal condition would answer it in the next session. A whole-character acceptance has to
account for every refused draw, because one fallback is one frame of the original mesh.

## LIMIT-024: Scope of the static whole-character backend

1.0.2 follow-up: an opt-in runtime sidecar supplies original main-INI conditions,
keys, persistence and F10; see docs/live-ini.md. Without a sidecar the package
remains static. General image-hash override/CustomShader/Include support and the
HLSL panel drawing adapter remain open; do not claim full-folder compatibility.
The native rendering limits below still apply. Actual-character acceptance of
the live main INI is pending.

One scenario is fixed when the package is built; keys, conditions, reload and runtime Core texture-identity queries remain absent. A draw that is instanced, in a secondary command buffer, in dynamic rendering, on or with pushed descriptors at the private set's slot retains the original. Pipelines with three to eight sets are replaced since 0.9.8 (private set at slot 3 or above; slots past the tracked four only while no application layout is that large). Ordinary bound application set 3 is restored since 0.9.5. Stream elements may extend up to 64 bytes past the file stride with padded backing storage. Identity follows position across frames; within a frame it requires the same exact position. LOD0/LOD1 only. The earlier 0.10.0 mesh build ran in the game, 0.9.6 adds static materials with sandbox/offline evidence only. See docs/character-backend.md and docs/character-materials.md.

## BUG-006: Character draws refused while the application had a set 3 bound (build 0.10.0; fixed in 0.9.5; NOT the cause of the flicker)

Correction of 2026-10-13: the refused draws were the character's draws on four-set pipelines in the visibility pass (BUG-009). A set 3 was bound there because the pipeline itself uses it. 0.9.5 let those draws pass this check and they failed the next one, so the flicker stayed. The restoration of an application set after a three-set replacement is still right and still tested. The sentence below that explains the flicker and the 227 refusals of 0.9.1 by this condition names the wrong mechanism.

The replacement binds its private set at slot 3. Both backends refused a draw when the tracker showed an application set there, to avoid overwriting it. In this game most draws bind four sets and a set 3 stays valid across the three-set character draws, so the refusal hit about one character draw in 150 (82 in 378 frames, live counters of 2026-10-13) and each one showed the game's own mesh for a frame: the flicker the user saw at LOD 0. The same condition explains the 227 refusals of the 0.9.1 session. 0.9.5 keeps the handle of the layout a set was bound with and binds the application's set 3 again after the replacement. Verified in the sandbox with a scene that fails against the old build; the game confirmation is pending. The single-component backend of 0.9.1 still refuses; it is not in use.

## LIMIT-025: Shadow passes of a LOD 0 character read partly LOD 1 poses

Found offline on 2026-10-13 from two saved LOD 0 world frames while looking for the flicker; not its cause. The first shadow pass draws LOD 1 meshes and imports their remapped poses; the second shadow pass draws LOD 0 meshes, and a component drawn early there reads the LOD 1 poses of components that come later (component 2 for components 1 and 9, matrix terms differing by up to 0.4). Both passes are depth only on a D16 target. The passes with color attachments read the same bones as the frame's final state. The core imports by the same rule under DX11. A master per LOD would separate them.

## BUG-007: Internal upload command buffers lacked loader initialization (fixed before 0.9.6 deployment)

The first 0.9.6 full sandbox regression exposed access violations in all three material cases through the layer entry; the inject cases passed. The new integrated uploader allocated internal command buffers through downstream Vulkan functions without initializing their loader dispatch data. Device setup now retains the loader callback, and internal queue/command-buffer setup calls it where available. Both material entries, all ten character hook cases and the final full suite pass. This was a pre-release sandbox failure, not a game crash. Evidence: build/material-0.9.6-regression.txt, build/material-loader-fix-tests.txt and build/material-0.9.6-regression-final.txt.

## LIMIT-026: Static material queue scope (multiple queues supported in 0.9.7)

0.9.6 required exactly one enabled unprotected graphics queue and refused the actual game at initialization (BUG-008). 0.9.7 supports up to 32 enabled queues in one unprotected graphics family, a single uploader and explicit one-time publication to all consumers. Multiple graphics families and protected queues still refuse. The chosen uploader must continue submitting during file preparation. BC compression uses enabled features, not merely hardware support. Both entries, cross-queue drawing and actual DDS uploads pass offline; the new version's game result remains pending.

## BUG-008: The static material backend refuses the game's graphics queue setup (fixed offline in 0.9.7)

User-started PID 18868 on 2026-10-13 loads 0.9.6 but shows the original model. Log and status prove initialization refused at "materials require one unprotected graphics queue", before any mesh or image upload, with zero replacements. No shader/asset fault or game crash is established. The exact requested queue count was not retained by that build. The two-queue sandbox reproduces the same refusal through both old DLL entries. 0.9.7 removes the single-queue restriction within one graphics family and adds semaphore publication before consumer work. Eighteen character-hook cases pass 261 checks, including queue 1 only and upload/render on separate queues through all submit APIs; full suite and real-file/config smoke pass. Deployed at 16:23 after the user's normal exit, with dist smoke passing; no 0.9.7 game acceptance yet. Evidence: captures/materials-0.9.6-20261013/, build/material-queues-0.9.6-control.json and build/material-0.9.7-regression.txt.

## BUG-009: Character draws in the visibility pass keep the original mesh; flicker at LOD 0 (0.10.0 to 0.9.7; replaced since 0.9.8; effect on the flicker not established)

Game, 2026-10-13 with 0.9.8: twelve seconds standing show no refusal, so these draws are replaced. The user first found the normal state free of flicker, later not. Whether this pass ever caused the visible flicker is therefore open; the text below gives the inference as it was made.

User report with 0.10.0 and again with 0.9.7. Measured on the running 0.9.7 process: `refused_material` 85 in 393 frames, nothing else refused; in forty captured frames every refused character draw is on four-set pipelines 399/402/404/406 in render pass 12 (no color, depth EQUAL without write on the G-buffer's depth, a storage buffer written by the fragment shader, no images). The backend required three sets (0.10.0: refused under `refused_set3`; 0.9.5 to 0.9.7: `refused_variant` or `refused_material`), so the game's own mesh was tested there against the mod's depth. Inferred, not yet observed: the game reads the result as "part hidden" and omits the part for some frames. 0.9.8 puts the private set above the pipeline's own sets and accepts a fragment shader whose layout has no fragment-stage image without a material profile. Sandbox scene red against 0.9.7 and green with 0.9.8; the four real vertex shaders rewrite to set 4 and validate. Evidence: captures/flicker-0.9.7-20261013/, docs/character-backend.md (last section).

## ISSUE-008: The cause of the flicker was named before the refused draws were looked at (2026-10-13)

The first diagnosis took the counter name `refused_set3` for the mechanism and "fixed" it in 0.9.5 without capturing a frame that contained a refused draw; the user reported the flicker unchanged two builds later. Same kind of error as ISSUE-007. Rule: a refusal counter names the check that fired, not why the draw is what it is; before acting on one, capture a frame with such a draw and read its pipeline and pass. Also: `scripts/smoke_m2_config.py` shows `images_failed` for a material package because its sandbox device has no BC compression enabled; `scripts/check_character_assets.py` is the check for materials.

## BUG-010: A fragment shader without a profile kept the game's mesh (0.9.6 to 0.9.8; fixed in 0.9.9, confirmed in the game for the view from below)

Game, 2026-10-13 with 0.9.9: the user finds the view from below right; no draw is refused; 26 fragment shaders took a sibling's rules.

Game, 2026-10-13 with 0.9.8. The dithered fade (camera close to the character; dash) switches every pass of every component to another fragment shader. The material package knows shaders by their exact bytes, and the backend refused a draw whose shader it did not know, keeping the game's mesh; components the mod hides stayed hidden. On screen: the original model mixed with the mod's, as a flicker in the dash and steadily when looking up from below. 0.9.9 takes the rules from the profiled sibling (docs/character-materials.md) and otherwise draws the mod's mesh with the application's images. Verified on the captured game data and in the sandbox; game result pending.

## LIMIT-027: After-images of the dash keep the game's mesh

Seen in captures/flicker-0.9.8-20261013/ (frame 150562 and others): character draws on four-set pipelines whose vertex shaders (8d5133a1, 2cf51393) have no storage buffer; the variant preflight says `spirv_required_buffer_missing` and the draw is refused under `refused_variant` (954 in that session). Where these shaders take the pose from is not known. The after-image therefore has the shape of the original model.

1.0.1 follow-up, PID 19452: the user reports that dashing also seems fine. In a requested 24.156-second dash interval, 58 captured frames contain 3,766 character matches on 98 supported pipelines; runtime refusal, pending and identification deltas are all zero while sibling/inherited material rules execute. Neither historical after-image vertex shader appears in these draws. Other parts of the session have variant refusals with the same retained missing-buffer reason, but were not captured at draw level. Keep the historical path as unsupported/uncovered; do not describe the sampled dash as visually broken or this path as newly fixed. Evidence: captures/stability-1.0.1-20261013-204259-19452/dash/.

## ISSUE-009: The normal-state flicker has no measured cause (2026-10-13; answered the same day, see BUG-011)

With 0.9.8 the user reports an occasional flicker while standing or walking. Twelve seconds of counters in that state: nothing refused, nothing identifying, nothing pending, one position for all character draws. Candidates not yet tested: lost identities (2,653 `identifying` draws in the session; 0.9.9 makes identities survive gaps and logs each new one), poses of other components being one import behind within a frame, the picture of generated frames. Do not act on any of these before the 0.9.9 counters and log say which.

## BUG-011: Frames counted at the present; a moving character became a new object for a frame (fixed in 1.0.0; sampled 1.0.1 game checks pass)

The flicker the user reported from the first whole-character session on, in the normal state. Measured with the identity lines of 0.9.9 on 2026-10-13: 16 of 19 new identities next to a replaced object were made within the same counted frame as that object's last draw, 4 to 8 ms after it and 1 to 3 cells away. The backend read a present-based frame count when a draw was recorded; the game records its next frame before the previous one is presented, at irregular moments (frame generation). Within a frame an identity matches only the same place, so the character that had moved was a new object, and the game drew it for a frame. Earlier explanations (BUG-006, BUG-009) removed real faults that were not this one. 1.0.0 takes the recording of the command buffer as the frame. Sandbox phase red against 0.9.9 and green with 1.0.0; game result pending.

Follow-up on 2026-10-13: the latest log proves 1.0.1 loaded in PID 27816; the user now says models and textures seem fine. The session still records 50 nearby identity creations, 38 within zero counted frames. No game process or probe remains at review time, and the events have not been correlated with actions or pixels. This neither establishes a new flicker diagnosis nor closes full movement acceptance. Evidence: captures/user-review-1.0.1-20261013/.

New session PID 19452: the user reports no apparent flicker. Before exchange, 1,534 LOD0 and 101 LOD1 drawing frames have no refusal/identification/pending; after the unchanged-code exchange, 710 LOD0 frames and a requested 964-frame dash interval also stay clean. The user confirms normal appearance after exchange. This is positive real-game evidence for the sampled paths. Clustered identity lines elsewhere, with user-reported dashing, remain uncorrelated with individual effect draws and are not proof that ordinary flicker persists. See captures/stability-1.0.1-20261013-204259-19452/; LIMIT-027 remains a separate uncovered path.

## BUG-012: Expanded INI dependency graphs exposed freed optimizer branches (fixed in candidate 1.0.4)

The original optimizer owns conditional branch lists temporarily and releases
them when finished. The adapter retained every raw registered pointer, including
removed branches, and the expanded Core dependency graph produced an RTTI access
violation during adaptation. Filter temporary branch roots before optimization,
then recurse through surviving owning commands. Nested eliminated-branch and
original Core fixtures cover the repair. This was reproduced offline; no game
crash is attributed to it.

## BUG-013: Key/preset-only callees were left uncompiled (fixed in candidate 1.0.4)

Dependency discovery previously started from scene/Present roots. A command
called only by a key or preset could stay empty; cycle parsing also copies its
temporary activation lists, so retaining those list addresses is invalid.
Record stable named callees at the original Override parser boundary and compile
their dependency graphs once. Enumerate presets before Constants. Fixtures
exercise hold activation/release, callbacks from Constants presets and both
cycle entries. Preserve original accumulated cycle callbacks and smart selection.

## LIMIT-028: Original INI syntax coverage is not complete Vulkan execution

Candidate 1.0.4 adds source-derived parsing/evaluation checks and CPU directory
generations. The full-folder inspector parses shader/resource metadata that the
production character runtime cannot yet execute. Non-native CustomShader,
ShaderOverride and ShaderRegex sections refuse; general copy/clear/readback,
non-indexed/indirect draws, dispatch and GPU parameters remain open. Global
system/driver settings and every argument combination are not exhaustively
validated. docs/ini-language-coverage.md is the current operation-family table.

Through 1.1.1, original owned-buffer commands, whole-pool operations and keyed/
expiring pool lifetime now have GPU execution tests. The 1.1.2 image foundation
adds directly tested owned allocation, views, pitched transfers, clears and
MSAA resolve. The 1.1.3 adapter connects original INI declarations, DDS, image
copy pools, view-specific clears and properties. Remaining formats/conversions,
general shader execution, application import and inline placement stay open.
Do not treat these owned-domain tests, 184 registry format records or 726 syntax
tests as complete Mod support. Logical image slots do not emit shader descriptors.

## ISSUE-010: A mode fixture exited before its first probe request

During the first 1.0.4 full regression, inject-late sandbox PID 3612 ended
normally after two frames (build/tests/sandbox-vv5mn581/stdout.txt). Its output
was already closed within the startup second, before the scheduled status
request; the client reported a closed-pipe error. The mode code's normal timeout
is four seconds, so timeout expiration is not established. No failure cause or
game implication is inferred. The unmodified standalone modes test and the next
complete suite both passed all 40 checks. Keep the original failure record in
build/next/run_all-1.0.4.out; no blind retry or production IPC change was added.

## BUG-014: A scalar pool write discarded unrelated resource aliases (fixed in candidate 1.0.5)

The adapter conservatively erased every cached reference in a pool when any
variable in that pool was assigned. This lost live bindings in other slots and
the resource half of mixed elements. Respect the original per-resource is_null
state instead: reference assignment sets it, the original pool engine changes it
only for affected elements, and subsequent binding resolution observes it.
Source-driven cases fail before the fix and pass afterward, including variable
and resource reset, mixed values, independent slots and proxy behavior. The
separate production-runtime fixtures check owned plans and generation isolation.
No historical game symptom has been attributed to this offline-found defect.
The full rebuilt regression passes in build/next/run_all-1.0.5.out; not deployed.

## BUG-015: Referenced buffers were reused as writable copy-cache allocations (fixed in candidate 1.0.8)

The owned GPU adapter reused a destination's current binding when its buffer
description matched. If that binding came from ref, a subsequent copy overwrote
the aliased source. It also retained only the current description, unlike the
original ResourcePool, so copy_desc could lose previous cached data after a size
change. The GPU byte-result test reproduces the first defect against 1.0.7 in
build/resource-commands/copy-alias-before-fix.{json,txt}. Separate owned pools now
retain allocations per description and instruction/custom resource. Reference
assignments change the binding without entering borrowed allocations in a copy
pool. The fix and region-copy fixtures pass 243 GPU checks with clean validation;
the full rebuilt suite passes in build/next/run_all-1.0.8.out. This backend was
never deployed into the game.

## BUG-016: Custom pool view metadata and null caches differed from the source (fixed in candidate 1.0.9)

The adapter inferred format/offset from every source view. Original GetViewInfo
only interprets stage target views, so a typed UAV fetched into a pool by a plain
range still has unknown custom-resource format. Rebinding it as an SRV can select
a raw view. The GPU fixture fails against 1.0.8; preserved in
build/resource-commands/metadata-before-fix.{json,txt}. Target-specific metadata
now matches the original helper. Null keeps its cached resource/view, and
source_stride is set by copies rather than overwritten by generic SetResource.
Format/stride-only unassigned aliases do not create zero-sized buffers. The
303-check GPU suite passes with clean validation; the complete rebuilt regression
passes in build/next/run_all-1.0.9.out.
No deployed/game result is implied.

## BUG-017: A CPU command failure left recorded GPU work submittable (fixed in candidate 1.1.1)

The GPU extension marked its scope failed only when its own command/query
callback failed. An original CPU pool expression could fail after earlier GPU
commands had been recorded, return failure from IniRuntime, and still allow
GpuResourceRuntime::finish to submit that partial work. The new fixture fails
before the fix in build/resource-commands/cpu-scope-before-fix.{json,txt}.

IniRuntime now notifies its extension on any failed Present/draw execution,
including failures outside GPU callbacks. An active GPU scope is poisoned and
cannot submit; discard releases its pending owners. The targeted suite passes
485 checks with clean core/sync validation, including CPU failures in both
Present and draw, retained bindings and rejected frame duration. Full rebuilt
regression passes in build/next/run_all-1.1.1.out. No deployment or game result
is implied.

## BUG-018: Stencil-only clears forwarded an invalid unused depth value (fixed in candidate 1.1.3)

The owned image foundation validated the depth field only when the clear range
included depth. A legal original `clear = od stencil 11` therefore forwarded
depth 11 into VkClearDepthStencilValue, whose range requirement applies even
to stencil-only commands. Validation reported VUID-depth-00022; the preserved
failure is build/resource-commands/stencil-before-fix.{json,txt}. This was an
offline test failure, not a game observation.

The low-level executor supplies depth zero when that aspect is unused. The INI
adapter separately applies D3D11's depth clamping when it is used. Actual depth
and stencil readbacks, ignored nonfinite depth and the original explicit-flag
priority pass in the 1682-check targeted suite with zero validation messages.
The final full-suite checkpoint is recorded in CURRENT_STATE.md.

## BUG-019: Float depth uploads lacked the required range/extension check (fixed in candidate 1.1.4)

The owned host-image uploader accepted arbitrary D32 and D32/S8 depth-plane
bytes on devices without VK_EXT_depth_range_unrestricted. Vulkan requires
copied depth in [0,1] in that case, so accepting the upload did not establish
defined results. An older packed D32/S8 DDS fixture also generated random depth
bits; zero core/sync validation messages did not verify that payload constraint.

Host uploads now inspect only copied texels, ignoring row/image padding, and
reject out-of-range or NaN values before recording. The ordinary DDS fixture
uses valid depths; a separately enabled test device covers arbitrary float
bits. The new color-to-D32 bridge requires actual logical-device extension
enablement because it cannot inspect device-only bytes without a readback.
Depth-to-color and the R16 family do not require that extension. The explicit
low-level buffer-to-depth primitive still relies on the caller's range proof.
This is offline resource work, not a defect observed in the deployed game.

## LIMIT-029: Native integer RTV overflow differs between reference and hardware

Candidate 1.1.4 follows native WARP truncation, NaN-to-zero and channel saturation.
The observed hardware agrees for fractions/ordinary values but can differ for
finite inputs outside the 32-bit range. The adapter chooses WARP's deterministic
saturation rather than an undefined C++ float-to-integer cast or a vendor rule.
Tests compare 19 integer formats and two typeless-view cases against WARP. The
archived DDI specification's rounding wording also differs from both measured
native devices; no cross-vendor bit-identical overflow behavior is claimed.
See docs/gpu-resource-commands.md and build/resource-commands/dx11-clear-*.txt.

## BUG-020: Constants post was omitted and root abort leaked into post (fixed in candidate 1.1.5)

RuntimeCompileConstants previously supplied no post list. execute_pair reused
the pre phase's aborted flag for post, although original RunCommandListComplete
creates a fresh state for each root invocation. The adapter now compiles/runs
both Constants phases, clears default persist dirty state between them, and
isolates root abort while preserving nested abort behavior. A plain run can
execute the nested abort in both phases; the new root-isolation fixture uses
explicit pre run to distinguish that legitimate case from leaked state.

The GPU fixture verifies separate zero/pre/post publication, post-persist dirty
state, aborted-pre values and post execution. The CPU fixture also exercises
pre-only nested abort with post execution. Full regression passes in
build/next/run_all-1.1.5.out. This was an offline source-audit finding, not a newly
observed game defect.

## LIMIT-030: HLSL compilation is not complete CustomShader execution

Candidate 1.1.6 compiles all six original stage kinds and retains native source
registers, but the general INI shader executor is not connected yet. DXC refuses
the original enable_backwards_compatibility flag in SPIR-V mode. Native model-5
compilation itself rejects the old software/preshader flags. ieee_strictness
passes for the tested Vulkan 1.2 target, not the 1.1 target; the eventual executor
must check actual logical-device support and float-control properties. Shader
storage formats, device limits, null resources, graphics conventions/state and
application placement require execution adapters. See docs/hlsl-compiler.md.
This is an offline compatibility boundary, not a new game regression.

## BUG-021: Draw argument guards omitted direct compute queries (fixed in candidate1.1.7)

Only indexed arguments were traversed. A valid dispatch using iniparams->width
reached an unadapted original query and failed the signed argument check. The
common walker now covers all parsed draw/dispatch forms and indirect targets.
Before-fix evidence: build/resource-commands/dispatch-query-before-fix/. The
original INI GPU fixture and final full regression pass after correction. This
does not enable unimplemented indirect/graphics execution. No game defect claim.

## LIMIT-031: Owned compute is a partial CustomShader execution adapter

Candidate1.1.7 executes direct compute with original lists and owned resources.
Graphics/state/draws, indirect dispatch, workgroup storage, added capabilities,
null source resources and alignment adaptation remain. Low-level sampler/counter
descriptors work; original source-state mapping remains. Storage views require
declared format agreement. Sampler validation conservatively checks every image
against every sampler; exact usage analysis is next. Actual enabled features and
serialized queue scope are explicit host inputs. General directory rendering,
application import/render-pass integration are not enabled. See custom-shaders.md.


## BUG-022: Source binding hazards and batch publication (fixed in 1.1.9)

Native WARP proves UAV binding clears conflicting SRV/VB/IB and output slots;
input binding against an output yields null. Buffer view ranges still conflict.
Texture mips/layers can be independent. Original RTV/DSV assignment calls
OMSetRenderTargets with eight slots, clearing pixel UAVs even for null RTVs.
Current logical slots only overwrite entries; ranges publish individually despite
source SetSlotRange's batch behavior. OM restore must clear conflicting inputs
and compute UAVs. The old scope fixture's UAV+SRV coexistence assertion is wrong.
Probe source/results:build/source-state-probe/hazards.cpp and hazards.txt.
Invalid duplicate-UAV batches preserve prior bindings, but can contaminate later
native hazard observations even after ClearState. Isolate invalid cases or run
last. New1.1.8 distinct-resource compute/native output comparisons remain valid.
This was an offline semantic gap, not a game regression. Reproduced by the new
native reference before the fix at 3728 checks; evidence is retained under
build/resource-commands/binding-hazards-before-fix/. Candidate 1.1.9 fixes source
hazards, atomic publication and OM restoration. Additional native probes show
pixel UAV assignments replace their whole set (unlike compute), DSV dimensions
may exceed color dimensions, and complete OM binding discards high pixel UAVs.
The pinned save helper only preserves slots 0-7. Old fixtures now unbind writers
before reading, without removing byte assertions. Full rebuilt suite passes:
resources 4006, clean core/sync through shutdown and all remaining steps in
build/next/run_all-1.1.9.out. This does not implement application binding import,
read-only application DSV flags or graphics drawing.

LIMIT-031 follow-up1.1.8: ordinary fixed shared storage, constructor/array sample
pairs, indirect dispatch and owned sampler defaults/host snapshots now work.
Special layouts/provenance, graphics/null/alignment cases and application import
remain. Original owned INI UAV creation lacks append/counter flags; do not invent
those semantics from low-level counter support.

## BUG-023: Raw Vulkan graphics built-ins differ from SM5 (fixed1.2.1)

Native DX11 WARP pixel references expose missing compiler conversions. For clip
w=2, source SV_Position.w is2 while raw Vulkan returned0.5. Nonzero start vertex/
instance offsets similarly leaked into SV_VertexID/SV_InstanceID. Evidence for
all three independent failures is under build/graphics-coordinate-before-fix/.
DXC options -fvk-use-dx-position-w, -fvk-support-nonzero-base-vertex and
-fvk-support-nonzero-base-instance correct these. Native/Vulkan direct and indexed
pixel comparisons, signed base vertices, multiple instances and winding pass.
The generated DrawParameters capability needs actual shaderDrawParameters enabled;
production graphics must gate it or implement an explicit lowering fallback.
Full rebuilt1.2.1 suite passes, resource4559/core+sync clean. This is an offline
compiler defect discovered before original graphics integration, not a game claim.
