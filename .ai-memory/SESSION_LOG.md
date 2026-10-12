# EFMI Vulkan - Development log

> A short summary is appended at the end of each round: what was actually done, the scope of verification, open problems and where to pick up next time. No chat and no complete logs.

## 2026-10-11: Review of the v1 plan, v2 plan, workspace bootstrap

The user asked for a feasibility review of the v1 plan (a Chinese document in the EFMI output folder). The review read the local Core INIs, a sample mod, the game directory, the Unity player log and the two reference projects. Main findings: the game runs Vulkan by default; native graphics jobs are enabled; the ACE anti-cheat is present; Core issues a dispatch and a buffer copy for every replaced draw, which is illegal inside a Vulkan render pass; ReShade does not expose `vkMapMemory`; the existing frame dump has no modded draw.

A v2 plan was then written. Further reading of the XXMI source and of the mods showed that the IB hash is a plain CRC32C over the index bytes of the draw, that the two merged-skeleton compute shaders only copy and patch, and that RabbitFX mods cannot be stubbed. The plan chooses an own Vulkan layer, the mod INI plus the EFMIv1 interface as compatibility surface, and two-phase execution with CPU-side data preparation.

The user then asked for: a memory system modelled on `F:\EFMI MCP\.ai-memory`, a Git repository, no Chinese characters in the workspace, a live capture tool like the DX11 one to help development, and to start working. Answers to three questions: Vulkan headers go to `F:\vulkan`; commit locally after every verified change; the plan document is tracked.

Nothing was run in the game in this round.

## 2026-10-11: Headers, region hash check, layer probe, sandbox, MCP server

The Vulkan headers were cloned to `F:\vulkan\Vulkan-Headers` (tag `vulkan-sdk-1.4.321.0`). The LunarG installer was not run; it needs elevation and changes system settings.

`scripts/verify_region_hash.py` confirmed the region hash rule on the existing DX11 dump: 336 of 336 distinct index regions match, none mismatches. The same dump shows index buffers bound at large byte offsets.

The layer was written as a read-only probe: interception of memory, buffer, pipeline, command buffer, draw, copy, render pass, submit and present calls; index buffer shadows fed from staging copies; frame capture with region hashes; a named pipe with the framing of the EFMI MCP probe. The sandbox program draws five indexed meshes from a device-local and a mapped index buffer, optionally recording on several threads into secondary command buffers; its shaders are assembled in source because there is no shader compiler. The Python side has the pipe client, a command-line tool and a stdio MCP server with nine tools, among them `efvk_match_mod`, which answers gate item G3 a.

Verification: `sandbox/run_all.py` passes (unit 36, probe 151 / 153 / 153 with 0 / 2 / 5 threads, modes 11, MCP 35). All of it is sandbox evidence on one NVIDIA GPU without validation layers; the game was not involved and nothing was registered system-wide.

Open and next: descriptor set, shader and image tracking (T-011) for G3 b to d; the M0 inventory (T-007); a DX11 baseline with the sample mod (T-008); G1 waits for the user's decision on account and registration (T-009).

## 2026-10-11: Inject entry

The user asked whether the DLL can be loaded into the game by injection, as with d3d11, instead of registering a layer. The launcher log confirmed that XXMI injects `d3d11.dll, efmi_probe.dll` into `Endfield.exe` at start, and the EFMI MCP probe's `bootstrap.cpp` showed the export redirection it uses on the system `d3d11.dll`.

Added `layer/src/bootstrap.cpp` (loader notification, redirection of the exports of `vulkan-1.dll` through jump stubs), an inject path in the create and proc-addr hooks, the passive device for devices created before attaching, the settings file `efmi_vk.cfg`, the second output `efmi_vk_inject.dll`, attach information in the status, and three sandbox options that load a DLL early, late, or late with the functions resolved again through the exports. Layer version 0.2.0.

Verification: `sandbox/run_all.py` passes (unit 36; probe 156 / 158 / 158 with the layer entry and 158 / 160 with the inject entry; modes 40; MCP 35). The injection in these tests is the sandbox calling `LoadLibrary` itself; no cross-process injection and nothing in the game.

The user also said that anything the project needs installed goes into `F:\vulkan` without asking (D-012).

Next: T-011 (descriptor, shader and image tracking), T-007 (inventory), T-015 (validation layers into `F:\vulkan`); G1 through the launcher waits for the user (T-009).

## 2026-10-11: Preparing G1 with the user

The user added `F:\efmi vulkan\dist\efmi_vk_inject.dll` to the launcher's injected libraries and asked how to remove `-force-d3d11`. `dist\efmi_vk.cfg` with `mode = passthrough` was written at once, because from now on every launch through the EFMI profile injects the DLL. Reading the launcher source (`core/game_launcher.py`, `build_cmd_args`) showed that the argument comes from the configuration value `d3d11_mode_cmd_args`, which has no control in the settings window and no signature; the user was told how to clear it and how to restore it. Nothing was run in the game by this project, and the launcher configuration was not edited by this project.

## 2026-10-11: First launches with the DLL in the game; a crash under Vulkan

The user cleared `d3d11_mode_cmd_args` and reported that the launch failed. Read-only diagnosis from four logs: the launch before the edit (DX11) ran 78 s with the DLL present and exited cleanly; the launch after it (Vulkan) reached device creation through the hooks and crashed 5 s later inside `nvoglv64`, called from `vulkan-1` from `unityplayer`, during swapchain set-up. This project's DLL is not on the stack. XXMI's `d3d11.dll` and the DX11 probe were injected into the same process, and D3D modules were loaded right before the crash.

Recorded as BUG-001 with the cause open. Gains: the launcher's injection is early enough (4.6 to 5.5 s before `vulkan-1.dll`), the engine's creation calls pass through the hooks, and `Player.log` reports `jobified=0` under Vulkan.

The user was asked to repeat the Vulkan launch with only this DLL injected (XXMI DLL injection mode Skip), and, if that crashes as well, with nothing injected. Nothing in the launcher configuration or the game was changed by this project.

## 2026-10-11: Third launch, Vulkan with only this DLL

The user set the launcher's XXMI DLL injection mode to Skip, removed the DX11 probe from the injected libraries and launched. The game passed the swapchain set-up where the previous run crashed and kept running; the user was asked to use the session as the 30-minute G1 run and to report when it ends. BUG-001 is thereby avoided and its cause strongly indicated (the DX11 hooks).

## 2026-10-11: Call counters while the G1 run was going

While the user played the G1 session, layer 0.3.0 was written: a call counter on every intercepted function, about forty counted-only functions generated by one template, the names the application resolves, the enabled extensions, the probe command `api_usage` and the tool `efvk_api_usage`. It was built and tested in `build\next\` (`EFMI_VK_DIST`), since the game held the DLL in `dist\`; `run_all.py` learned to build into that directory. All steps pass. `dist\` is to be rebuilt after the game exits.

## 2026-10-11: Ordinary descriptors, images and retained shaders; G1 early exit clarified

Resumed from CURRENT_STATE/TASKS and a screenshot of the earlier round. The user confirmed that run 3 ended before 30 minutes, after scene changes. No Endfield process was present; the layer log has no device-destruction/frame total. G1 remains incomplete. No game was started or injected during this round. The existing `dist/` DLL/configuration remain 0.2.0 passthrough; the earlier planned promotion to probe is deferred.

Implemented the ordinary-descriptor part of T-011 as layer 0.4.0: layouts and lifetimes, writes/copies, immutable samplers, variable counts, per-set/per-array dynamic offsets, compatibility disturbance, separate graphics/compute binding state, and submit-time snapshots with buffer ranges/visibility and image metadata. Added shader lifetime retention, SPIR-V hashes/export and the `efvk_shader` tool with limited declared-name/binding extraction. Templates, push descriptors and descriptor buffers explicitly invalidate stale knowledge and still need decoding. D-013 records the timing/lifetime decisions.

The sandbox now actually uses two descriptor sets, a dynamic storage array and a sampled texture, copies from a descriptor set it frees, destroys shader modules before drawing, and updates a storage descriptor on the rewrite trigger. New native tests cover lifetime and uncommon state semantics without a Vulkan driver. Python test artifacts now stay in `build/tests/`; compiler scratch stays in component `build/tmp/`. On this machine the `python` app alias does not launch the interpreter, so commands use `D:/Python312/python.exe` explicitly.

Verification this round: `EFMI_VK_DIST=F:/efmi vulkan/build/next` and `D:/Python312/python.exe -B sandbox/run_all.py`, all steps pass. Build/text clean; Python 44/44; native tracker 37/37; layer probe 385/385, 387/387, 387/387; inject probe 387/387, 389/389; modes 40/40; MCP 46/46. Evidence is sandbox/CPU-only; no visual review, validation layers, overhead measurements or real-character verification. `docs/probe-resources.md` records schema, limitations and the exact tested paths.

Next: T-011 template/push decoding and sandbox cases; T-012 overhead measurement; T-007 offline compatibility inventory. G1's complete 30-minute session and all further game loading still need the user's on-the-spot instruction.

## 2026-10-11: Reuse the prior G1 evidence and prepare the probe session

The user questioned the 30-minute requirement and then explicitly instructed the project to use the previous run's data. D-014 and the plan now accept historical run 3 as initial loading evidence, with long-duration stability deferred. A repeated 30-minute passthrough session is no longer a prerequisite. The run remains 0.2.0 passthrough evidence; no duration/frame total or missing probe data was inferred.

After confirming no game or sandbox process was running, preserved the old DLLs, manifest, configuration and historical log in `build/backups/before-probe-20261011-212452-288/`. Rebuilt 0.4.0 into `dist/` and set `dist/efmi_vk.cfg` to probe mode. The layer source did not change. The historical game log remains byte-for-byte unchanged; test logs were confined to `build/`.

Verification this round: full `sandbox/run_all.py` with `D:/Python312/python.exe`, process-local `EFMI_VK_DIST=F:/efmi vulkan/dist` and a native-test log path under the backup directory. Build/text passed; Python 44/44; native tracker 37/37; layer probe 385/385, 387/387, 387/387; inject probe 387/387, 389/389; modes 40/40; MCP 46/46. A separate sandbox used the actual `dist/` settings with no mode environment override, reported 0.4.0 probe, captured five draws, and exited with code 0 (`build/tests/sandbox-xm23wqvx/`). The layer and inject copies have identical bytes (SHA-256 `ab54243ae1db4072169e4587686a7df2c71115c7f4303fe2f116363305e07cf0`).

No game was started or injected, and no launcher or system configuration changed. Loading 0.4.0 in the real game and all G2/G3 capture evidence remain open. The next user-chosen short probe session can collect API usage and a frame with the sample character visible. Long-duration observation is a separate follow-up. Offline development can continue meanwhile.

## 2026-10-11: Descriptor templates and push descriptors, without the game

The user requested development of parts that need no game testing. Implemented the next T-011 increment as layer 0.5.0: retained template definitions and offset/stride decoding; direct and template push descriptors; immutable recording snapshots, per-command-buffer/per-bind-point separation, partial updates and layout disturbance. Added core/KHR and maintenance6 wrappers. Executing secondary command buffers now invalidates primary descriptor bindings while retaining copied draw snapshots. JSON exposes binding kind, descriptor timing and resource-metadata timing separately (D-015). Standard writes, templates and pushes share one decoder; unsupported types/layouts remain explicit unknowns.

The sandbox now has ordinary/template/push/push-template material modes. New modes render using the descriptors, including padded template data, an initial template destroyed before drawing, partial direct pushes and different storage ranges for each draw. Template rewrites use a second template. Added native cases for template lifetime/reuse, arbitrary/zero stride, overlapping entries, ignored immutable-sampler input, partial pushes, independent command buffers/bind points, secondary execution and reset.

Verification: built under `build/next/` and ran all of `sandbox/run_all.py`; all steps passed. Final targeted probe run after test cleanup also passed all 17 configurations. Python 44; native tracker 62; modes 40; MCP 46. Ordinary layer 416 / 418 / 418, inject 418 / 420; templates layer 420 / 422, inject 422 / 424; direct pushes and push templates each layer 427 / 429, inject 429 / 431. Core 1.1 template and KHR push commands have GPU coverage; newer aliases and compute/texel wrappers do not. No validation-layer, visual or overhead result was established.

With no game/sandbox process running, backed up 0.4.0 to `build/backups/before-0.5-20261011-214412-992/` and copied the exact tested package to `dist/`, verifying SHA-256 equality. DLL hash: `c9a8b41db3234ecebce64300d0c1dd34de259b6ebd23827516001df4c89a8005`. A sandbox using the actual `dist/` cfg reported 0.5.0 probe, captured five push-template draws with two recording threads and exited normally (`build/tests/sandbox-f6c4cx5r/`). The historical game log is unchanged. No game, launcher configuration, external installation or system state was changed.

Next offline work: T-012 overhead measurement, T-015 validation layers, T-007 compatibility inventory. T-011 standard paths are implemented; specialized paths and G3 byte-availability evidence remain open. No new game session is needed for the next offline tasks.

## 2026-10-11: Many-draw timing and probe overhead

Added repeated meshes with `--draws`, raw host timing phases, warmup/sample control, rotated three-mode benchmark trials and a many-draw capture/timing regression. Child baselines clear inherited explicit-layer activation; failed sandbox startup cleans up its process. Layer code/binary remain 0.5.0. The sandbox build and full regression against `build/next/` pass: unit 44, native 62, all 17 probe configurations, modes 40, timing 84, MCP 46. Copied the tested sandbox to `dist/`; no DLL/config/log change.

Completed 36 timing runs (500/2000 draws, 0/2 recording threads, no layer/passthrough/probe, three rotated trials). At 2000 draws, the median recording-plus-submit increment is 0.424 ms primary / 0.553 ms secondary. FIFO keeps frame medians around 5.55 ms. This is neither game performance nor evidence of the M2 5% target. `docs/probe-performance.md` preserves method, limits and fingerprints; `build/tests/benchmark-owy1jn92/` holds raw samples. T-012 is complete; continuing T-015 and T-007 without the game.

## 2026-10-11: Validation dependency and sandbox correctness

Built pinned Khronos VVL under F:/vulkan, added core/synchronization validation and negative canaries to the suite, fixed sandbox read-only SPIR-V and failure-path shader cleanup, added upload barriers and per-image presentation semaphores. Full regression passes; final validation passes 76 checks over 24 positive configurations and three canaries. Layer DLL is unchanged. Copied the corrected sandbox to dist/. Repeated 36 performance trials after fixes: 2000-draw probe increment 0.386 ms primary / 0.744 ms secondary. No game was launched.

An initial VVL trial wrote a 36-byte default temp cache outside the project. The harness now confines all its child temp/cache variables to build/tests/; subsequent tests confirm that the old cache stays unchanged. The external file was left untouched. Exact coverage, evidence, hashes and limitations are in docs/validation.md and docs/probe-performance.md. T-015 is complete; T-007 inventory continues.

## 2026-10-11: Reproducible compatibility inventory and frontend assessment

Completed T-007 without the game: a lexical inventory tool, metadata-only snapshot and compatibility contract covering main mod/Core syntax, every direct EFMIv1 reference, resource/pool behavior and the image panel missed by the original plan. Fingerprinted 46 Core and 86 mod files and selected game binaries/content indices. Verified local d3d11.dll equals the official v1.2.2 release bytes; pinned source a7847eae82aaac32175c9a7d199673d5c9323bcd. Engine version is known; the marketing/content release label remains unknown and is not guessed.

D-016 retains hosted XXMI reuse after inspecting parser/state dependencies and the pinned WWMI-DX12 host, with a compiled prototype deferred to M3. Corrected the sample description: configured 13 slots, actual ids 0-11, plus enabled k/1.ini panel. Snapshot check matches every fingerprint/lexical item and the English-only check passes. No source from the reference engines was compiled or copied into the layer, no game was started, and no installed mod/launcher file changed. Remaining M2 backend decisions need actual G2/G3 data.

## 2026-10-11: Preserve first-probe evidence and stop at the game-data boundary

Added scripts/collect_probe.py and server/efmi_vk/evidence.py: raw frame, before/after status/API usage, INI fingerprints/matches, G2 summary and bounded retained pipeline/SPIR-V export. Bundles require explicit PID/target, reject wrong executable labels and existing output directories, retain partial files on interruption, and write a hashed manifest last. Live-query gaps and truncation are explicit. No launcher/inject/process-start behavior exists.

The evidence regression passes 52 checks through loader/inject entries with push templates and secondary recording: raw draws and snapshot timing, region matching, shader retention, all file fingerprints, scope/overwrite refusal and a simulated pipeline lifetime gap. Added it to run_all.py. The first-session guide states which G2/G3 questions a bundle can answer and which still require record-time measurements or a modded DX11 baseline.

Final full regression against `dist/` passed: Python 44, native tracker 62, all 17 probe configurations, modes 40, timing 84, core/synchronization validation 76, MCP 46 and evidence export 52. Evidence: `build/final-offline-regression.txt`. DLL and sandbox hashes match `build/next/`.

The native test's default logger appended one 121-byte startup line to `dist/efmi_vk_layer.log`. Verified that the preceding 2246 bytes exactly matched the saved historical log, preserved the mixed file under `build/backups/native-log-repair-20261011-223459/mixed.log`, and restored the exact original bytes. Fixed native_test.py to use an explicit test log and working directory under build/tests/. The corrected native test passes 62 checks and leaves the restored log's SHA-256 unchanged (BUG-002).

The user's requested stopping point is now reached: current offline prerequisites are implemented and verified; actual game descriptors, thread/submission structure, sample IB regions and shader identities are needed before choosing the next backend work. No game was launched or injected. The historical G1 result remains accepted with the long-duration observation deferred. All changes are committed locally. The closing check found an existing origin at https://github.com/loyal37/efmi-vulkan.git; this round did not configure it or push anything.

## 2026-10-11: Review of the 0.4.0 / 0.5.0 commits before they go into the game

The user asked for a review of the seven commits made by another agent (descriptor, image and shader tracking, templates and pushes, performance measurement, validation layers, compatibility inventory, evidence collector), none of which had been in the game, and for problems to be fixed directly.

Method: read the layer changes in full (`resources.cpp`, `resources.h`, `resource_hooks.h`, the diffs of `state.*`, `layer.cpp`, `ipc.cpp`), the tests, scripts and server changes; rebuilt into `build/next/` and ran the whole suite, which passed with the numbers the memory claimed. The validation layer under `F:/vulkan` exists and the suite uses it.

Findings: no error in what the tracker reports for the sandbox. Four parts scaled with the observed process and were fixed in 0.5.1 (D-017): unbounded SPIR-V retention; full scans of all retained shaders on every module or pipeline destruction and of all descriptor sets on every pool reset; no bound on the descriptor contents of a capture; no tracking level between full and passthrough. Also fixed: vertex input pointers were read for mesh pipelines; the evidence collector failed on a stage without a known module; an inline module was kept even with resource tracking off.

Verification: `sandbox/limits_test.py` (new, 100 checks), native tracker 78 checks (16 new), the rest unchanged; the whole suite passes. One transient linker failure was seen and recorded (ISSUE-002). `dist/` was updated to 0.5.1 from the tested files with the previous package saved under `build/backups/`; the settings file keeps `mode = probe` and now lists the two fallback settings as comments. Nothing was launched or injected.

## 2026-10-12: First probe session in the game

The user started the game in the Vulkan configuration with layer 0.5.1 in probe mode and the sample character on screen. Read through the pipe only: status and call counters twice, one collector bundle with the sample mod's INI, and two more captures with live reads of the instance config window and the bone buffer.

Results: one recording thread, one command buffer with every draw, classic render passes (G2); all 12 LOD0 components of `arcane.ini` match by region hash, read from mapped memory (G3 a); the 65536-byte dynamic uniform window and the bone matrices at the offsets of its entry 5 are where EFMI expects them (G3 b, c, from live reads that were partly stale); material textures in set 1, SPIR-V without names (G3 d); descriptor sets are allocated and written through templates every frame. About 120 frames per second and flat memory with full tracking in this scene.

Recorded: CURRENT_STATE (full numbers and evidence paths), D-018, T-017 (snapshot at submit), LIMIT-008 and LIMIT-014. The evidence is under `captures/`, which Git ignores. Nothing in the launcher, the game or the mods was changed by this project.

Next: T-017, then the same scene again; T-008 (DX11 baseline with the mod) when the user has a DX11 session.

## 2026-10-12: Launcher switched back to DX11 at the user's request

The user asked this project to change the two launcher values itself so that the DX11 baseline can be taken. With neither the launcher nor the game running, `XXMI Launcher Config.json` was copied to `build/backups/` and two lines of the EFMI profile were replaced by text (`d3d11_mode_cmd_args` to `-force-d3d11`, `xxmi_dll_inject_mode` to `DIRECT`); the result parses to exactly the intended configuration. The signed list of injected libraries was left alone, and `dist/efmi_vk.cfg` was set to passthrough for the DX11 session. The working agreement now states under which conditions the agent may change a launcher setting.

Reading the launcher log for this also showed that the DX11 probe was injected in this morning's successful Vulkan session, while the crashed one before it had XXMI's `d3d11.dll` as well: BUG-001 is caused by XXMI's `d3d11.dll` under Vulkan.

## 2026-10-12: DX11 baseline captured and paired with the Vulkan frame

The user started the game under DX11 with the sample mod. One full frame was taken through the DX11 probe of the sibling project (its Python client used as a library with byte code writing off; nothing was written there) and saved under `captures/dx11-baseline`. New: `server/efmi_vk/dx11_pair.py` (pairing on plain dictionaries, 25 unit checks) and `scripts/dx11_baseline.py` (`capture`, `summarize`). `docs/dx11-baseline.md` holds the result; the plan got facts 15 to 19, the decisions D-019, the tasks T-018 and T-019.

Main results: the mod is drawn pass for pass like the game's own components, and the same objects are drawn equally often under Vulkan; the instance entry and the bone offsets have the structure read under Vulkan in the morning; the instance window has a fixed place under Vulkan where DX11 needs a per-shader search; texture slots need a table per fragment shader; the vertex input differs in the blend indices only; under DX11 the pixel shader keeps reading the game's bone buffer and instance window, which one shared Vulkan binding does not give for free.

Not done: the frame with the mods off. The probe refused to press F11 because the game window was minimized and sent nothing; this project did not touch the window. Also found: `python` on the PATH is the Store stub (ISSUE-003). The layer was not changed; unit tests and the text check were run, the full suite was not.

## 2026-10-12: Mods-off frame; the slot table is per Vulkan fragment shader and per pass

After the user restored the game window, the DX11 probe pressed EFMI's F11 through the sibling project's own toggle routine (it reads EFMI's notification from the frame to confirm the state), one frame was saved with the mods off and one after the second press with the mods on again. Pairing that frame showed three things the first frame could not: all 12 original components are drawn equally often under both APIs (45 draws); one DX11 pixel shader corresponds to two Vulkan fragment shaders with different binding numbers, so the table is keyed by the Vulkan fragment shader; and the mod's slot assignments apply in every pass, landing on other roles in some passes, which a faithful port has to reproduce.

`scripts/dx11_baseline.py` now saves the slots each shader declares and pairs only those (DX11 keeps stale bindings), pairs the game's own draws one by one, and has a `shaders` command for frames saved earlier. `docs/dx11-baseline.md` was rewritten around the four frames; the plan has fact 20 and a corrected fact 18. T-008 is done. A heredoc turned a regular expression's word boundary into a backspace character while patching; found by the control character count and repaired from a script file.

## 2026-10-12: DX11 texture content hashes

While the DX11 session was still open, one more pair of F11 presses gave a frame with the mods off in which mip 0 of every file-backed texture was read back and hashed (`capture --textures`, 65 textures, about 83 MB). The mods were confirmed on again afterwards. The frame itself no longer pairs with the Vulkan frame: every component is drawn once more than ten minutes earlier.

## 2026-10-12: Launcher back to its Vulkan values

The user closed the game and the launcher after the DX11 baseline. On that go-ahead the two values of the EFMI profile were set back by text replacement (`d3d11_mode_cmd_args` empty, `xxmi_dll_inject_mode` `SKIP`), after a check that neither process ran and with a copy of the file in `build/backups/`; the result parses to exactly the intended configuration and two lines changed. `dist/efmi_vk.cfg` is `mode = probe` again. Nothing was launched.

## 2026-10-12: Layer 0.6.0, contents at submit with a record-time comparison (sandbox only)

Built T-017. The layer reads what selected draws take from buffers twice: a checksum inside the draw call, the bytes at submit; it keeps the bytes within a budget and reports per range whether the two reads agree. A follow rule turns values found in a uniform range into windows of the draw's storage ranges, which is how the bone rows are reached from the instance entry. The sandbox got late-written uniform data so that the comparison is exercised in both directions, also with secondary command buffers and push descriptors. Client, collector (`--snapshot`, `--follow`), tests and `docs/probe-snapshots.md` follow. The full suite passed against `build/next` and against `dist/`, where 0.6.0 now is.

Two patch scripts sent through a shell here-document lost their backslashes again (a regular expression before, C string escapes this time); the second failed its own match check and changed nothing. Patches with backslashes go through a script file.

## 2026-10-12: Second probe session, layer 0.6.0 in the game; G3 b and c pass for the display scene

The user started the game under Vulkan. The collector took one frame with the contents of the mod's 45 draws and one with all indexed draws and every pipeline. The first frame showed everything equal between record and submit; the second showed two bone windows that differed, both in the shadow pass. Series followed to find out where: with windows of exact sizes the bytes that arrive late turned out to lie behind the palettes, never inside one (component 4: 55 bones always equal, 56 not), and the preset's large window was the reason for the first sighting. Reading the meshes from the game gave the same bone counts the mod's INI states. The order of the palettes in the buffer changed between frames, which first looked like a contradiction between two measurements.

The DX11 pairing was repeated against the new frame with all pipelines; the slot table is now keyed by SPIR-V hash throughout. Plan facts 21 and 22, D-021, T-017 closed. The layer was not changed in this round; no test run was needed.
