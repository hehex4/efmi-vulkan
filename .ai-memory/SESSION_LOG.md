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

## 2026-10-12: T-019 decided from the shaders of the two Vulkan sessions

The retained SPIR-V of the character's 26 programs was read for which stage refers to the bone buffer and the instance window. The vertex shaders are uniform in the two things that matter (one storage buffer, window at set 2 binding 0), the fragment shaders are not (20 of 45 draws read the bones). That makes the vertex-only re-pointing both exact and simple, and the shared replacement neither. `spirv.py` got `buffer_variables` and `repoint` with unit tests, `scripts/stage_usage.py` reproduces the table and checks the rewritten modules with `spirv-val` from the validation layer build in `F:\vulkan`.

## 2026-10-12: Layer 0.7.0, content hash of texture uploads and pipeline creation inventory (sandbox only)

Built T-018 after the game was closed. Image uploads to mip 0, layer 0 are hashed at submit and the value travels with the descriptor contents; the pairing tool matches textures by content before it falls back to their look and now lists the fate of every slot the mod sets. The same build counts what graphics pipeline creation uses, as preparation for re-creating pipelines in M2. The sandbox's texture became 4x4 with three uploads, one of them with padded rows, which the first test run hashed correctly.

The first full suite run had two failures, neither in the layer: an MCP test still expected the 1x1 texture, and the harness hit a sharing violation on the sandbox's freshly renamed expected file (ISSUE-004). After both corrections the two tests passed against `build/next` and the full suite passed against `dist/`.

## 2026-10-12: Third probe session, resolved texture slots and reported frame-time spikes

Restored memory and connected to the user's already running 0.7.0 Vulkan process, PID 19264. The user reported intermittent stutter during both loading and steady display. Saved status/API evidence and frame 22563 (captures/third-probe, complete, 172 manifest files verified), all 12 character components and all pipelines. No new game process was launched, no input was sent and no rendering was changed.

Compared the existing mods-off DX11 baseline using its 65 saved texture hashes. All four open entries resolve by content; 19 sample slots are placed, 37 undeclared, none open. Saved a metadata-only profile and the third-session report; preserved earlier comparison files. Corrected the comparison's console report to distinguish a binding conflict from multiple DX11 shader identities sharing one Vulkan shader (zero binding conflicts, two unrelated UI identities). T-018 is complete for this scene. Pipeline creation requirements for M2 were recorded as well.

The cumulative upload-processing timer is about 8.1 seconds over 2.55 GiB; a steady three-second interval adds only 596 microseconds while 360 presents advance. This does not identify the cause of all reported stutter. Backed up the cfg and set image_hash = 0 for the next process, keeping resources on. Added D-023 and T-020. Layer 0.7.1 adds bounded host present intervals, window quantiles and peaks with hash/capture tags; no per-frame allocation or resource lock. This is host timing and not GPU/display timing or a verified performance fix.

Full build/regression against build/next passed: unit 88, native 85, 17 probe configurations (503-518), modes 40, limits 148, timing 94, validation 76, MCP 47, evidence 78 (build/pacing-regression-20261012.txt). Normal sandbox tests explicitly enable upload hashes even if the game's cfg disables them; disabled-path tests still pass. Native tests retain an 80 ms hitch among 8 ms intervals across ring rollover.

After the user confirmed normal exit, verified no target process remained, backed up dist/ to build/backups/before-0.7.1-20261012-122110/ and copied the exact tested package. DLL SHA-256 729d914e1f4a1ac66ae26ee2d421707d898b7426e52c756a5997cec50dfe0b7e. Actual-config inject smoke (build/tests/sandbox-un722puo/) confirms 0.7.1 probe, resources on, upload hashing off, five draws and host timing. The historical game log is unchanged; no launcher setting changed. Next: a user-started hash-off comparison; 0.7.1 has not loaded in the game, and the stutter issue remains open.

## 2026-10-12: Hash-off timing in the display and open world, layer 0.7.1

The user started PID 31124 and requested a spike check after 20 seconds in the display. Status confirms 0.7.1, resources on, hashing off, zero captures. Initial reads have recent maxima 13.076/21.729 ms but the game is background at both endpoints. The user clarified that the static character cannot reveal stutter and the open world stutters severely, then confirmed readiness for foreground world sampling.

After five seconds to return to the game, collected nine status reads over 39.992 seconds, with 400/400 foreground polls. Between reads, 11,681 host present intervals contain 142 over 16 ms, 27 over 33 ms and 12 over 50 ms. Maximum 111.204 ms at frame 69089; final rolling window p50 2.989 ms, p99 24.590 ms. All hashing counters and capture sequence remain zero. A supplementary 20.080-second start/end-only interval has 46 over 33 ms and recent maximum 141.033 ms, but only 78/200 foreground polls; recorded as focus-confounded, not a clean comparison. Manifests and analysis are under captures/pacing-hash-off-20261012/.

Recorded docs/pacing-hash-off-session.md and updated T-020/ISSUE-005. Hash-off does not remove world host spikes; resource tracking, game work, driver and presentation remain unisolated. Settings are unchanged; next useful comparison is resources = 0 in the same world after a user-chosen restart. No capture, input, focus change, launch, stop, build or layer change was performed. Verification is evidence fingerprints and English/diff checks; the unchanged layer did not need a regression rerun.

## 2026-10-12: Prepare the resource-off comparison after measurement

With the preceding evidence saved, backed up dist/efmi_vk.cfg to build/backups/before-resources-off-20261012-124818/ and enabled resources = 0, leaving image_hash = 0. The current game process retains its startup settings; nothing was hot-switched or restarted. Actual-config inject smoke (build/tests/sandbox-a_60l8x8/) confirms 0.7.1 probe, resources and image hashing off, zero tracked images/descriptor sets, five captured draws and host pacing available. No layer change or rebuild. This configuration is ready for a user-started same-world comparison, not yet game-verified.

## 2026-10-12: Resource-off stutter persists; passthrough prepared

The user restarted and reports continuing stutter. PID 33204 started at 12:50:29, layer 0.7.1 probe/inject, resources and image hashing confirmed off. Status-only observations captured no frames. The first 40 seconds were mostly background; the second observation had an early focus transition. Selected its final 30.000261 seconds (12:57:21.745-12:57:51.745), all 300 foreground polls and seven endpoints active: 6,766 intervals, 276/74/26 over 16/33/50 ms, largest retained 105.000 ms at frame 53703. Final window p50 2.887 ms, p99 36.888 ms. Hash counters, tracked images/descriptors/retained shaders and capture sequence remain zero. A first analysis assertion correctly rejected a proposed 35-second range containing one background poll; the recorded analysis uses the clean 30-second subset.

Evidence: captures/pacing-resources-off-20261012/, root manifest covering 26 files; report: docs/pacing-resources-off-session.md. Updated current state, tasks, pacing guide and ISSUE-005. Neither disabled resource tracking nor disabled hashing is the sole explanation; remaining buffer/draw tracking and the broader rendering stack are unisolated. Different workloads across runs prevent treating the count difference as a regression caused by the setting.

After measurement, backed up cfg to build/backups/before-passthrough-20261012-125909/ and changed only mode to passthrough. PID 33204 retains its probe setting until exit; no game restart or launcher edit occurred. Actual-config inject smoke (build/tests/sandbox-m_8zuprp/) passes with 546 frames, normal exit, matching destruction count and no probe pipe. DLL SHA-256 remains 729d914e1f4a1ac66ae26ee2d421707d898b7426e52c756a5997cec50dfe0b7e. No layer source/build change or full regression rerun. Next: a user-started same-world passthrough comparison using the user's smoothness report; passthrough still includes injection/setup/present accounting and is not a no-DLL baseline.

## 2026-10-12: User reports no stutter in passthrough

Verified the new game PID 14804 (13:04:17 start), 0.7.1 passthrough/inject log, Vulkan device creation and absence of a probe pipe. The user reports that stutter is gone. Saved captures/pacing-passthrough-20261012/ with log, configuration and a fingerprinted observation. This is a subjective gameplay comparison, without host/GPU/display timing. It narrows T-020 to probe-only work as a group; keep the game in passthrough while investigating buffer snapshots and repeated copy work offline. No game/launcher/config change in this round of observation.

## 2026-10-12: Layer 0.7.2, buffer-copy savings and opt-in work timings

Removed allocation/zero-fill for unreadable record-time buffer sources and the temporary submit copy, preserving submit priority, record-time fallback, unmap snapshots and disjoint self-copy. Added profile_tracker (default off), six timing categories, byte counts, a fixed 64-event history for work of at least 2 ms, and shared QPC timestamps on present peaks. The scopes exclude forwarded driver calls but include tracker waiting/descheduling; nesting and measurement overhead are explicit. BUG-003 describes the proven waste; ISSUE-005 remains open because the game hot path is not identified.

Built in build/next. First full suite failed only the new category-coverage assertion: ordinary staging remains mapped. Added the existing rewrite trigger to exercise unmap-before-copy; the timing test then passed 126 checks. Final full regression passed (build/tracker-work-regression-final-20261012.txt): unit 88, native 101, 17 probe configurations 503-518, modes 40, limits 148, timing 126, validation 76, MCP 47, evidence 78. Native tests also verify bounded history, concurrent accounting, profiling disabled and a 64 MiB unreadable source with no byte-storage allocation.

Observed that the game had exited, checked twice before deployment, and backed up dist/ to build/backups/before-0.7.2-20261012-132253/. Deployed six exact candidate files; both DLL hashes are f8b30ce440856995f0c7776b1adf4196d541e651b4cd4705f3f9fde6e394fde6. The cfg now prepares probe with resources/hash off and profile_tracker on; default buffer shadows and 16 MiB unmap snapshots remain. Candidate and deployed actual-config smoke pass (build/tests/sandbox-bbni7c7u/, sandbox-_5s9b_x_/), five correct draws, timings available, no environment overrides, game log unchanged. No launcher edit, automatic launch or game-tested performance claim. Next requires a user-started same-world comparison with the tested candidate.

## 2026-10-12: 0.7.2 foreground spikes correlate with unmap copies; deploy 0.7.3

The user reports stuttering again. Confirmed user-started PID 41044, 0.7.2 probe/inject, resources/hash off and profiling on. Initial status was background with stale present history; a foreground wait timed out. After the user's explicit readiness, 13 status reads over 24.030379 seconds have 240/240 foreground polls and all endpoints active. The 5,093 host intervals include 203/51/17 over 16/33/50 ms, largest retained 129.125 ms. No captures or hashing. Unmap work copies 3.46 GiB in 11.625 cumulative wall seconds; three sequential unmaps cover 128.807 ms of the largest peak while a zero-byte copy-record operation waits about 44 ms. Preserve wall-time/nesting/retention limits: 11 of 12 query gaps overflow the 64 slow-event capacity. Evidence has 21 verified fingerprints under captures/pacing-tracker-work-20261012/; docs/pacing-tracker-session.md records scope and exclusions.

Implemented 0.7.3 to accelerate detected PAGE_WRITECOMBINE mappings with CPU-gated SSE4.1 streaming loads, exact ranges and fences. Cached/unknown mappings keep memmove. Mapped policy is classified once, recomputed on remap and cleared on unmap. Snapshots and fallbacks are retained, including unmap-before-copy and submit-time priority; lock ordering is unchanged. Added strategy/mapping metadata. Native tests use actual WC pages with inaccessible neighbours and cover offsets, tails, remap and all copy paths. D-025 and BUG-004 record the decision and remaining limits.

An initial build caught the JSON writer method typo; corrected before the successful full build/regression (build/mapped-read-regression-final-20261012.txt): unit 88, native 118, 17 probe configurations 503-518, modes 40, limits 148, timing 126, Vulkan validation 76, MCP 47, evidence 78. Separate headless Vulkan measurements of the production helper on this GPU give type-2 8 MiB medians 24.023 versus 1.270 ms, cached type 3 unchanged, device-local type 4 still expensive at 150.952 ms. Every byte matches. These are local copy timings, not a game-tested speedup.

Observed that the game had exited and checked again before backup/replacement. Backed up dist/ to build/backups/before-0.7.3-20261012-150100/ and deployed the exact candidate. Both DLLs: 4cb8fdc1c111530cdbc8d76c9c2924c1cf61ec6fa701ba64a18edc5b36548c85. Candidate and deployed actual-config inject smoke pass (build/tests/sandbox-wcxt2ysn/, sandbox-zg_0n_36/): five correct hashes, WC mappings/strategy reported, unchanged game log. Settings stay probe, resources/hash off, profile on, default snapshot/shadow limits. No launcher change or automatic game launch/stop. Next requires a short user-started 0.7.3 world comparison; T-020 / ISSUE-005 remains open and no 30-minute prerequisite applies.

## 2026-10-12: Review of the stutter work; the copied buffers were mesh pools; layer 0.7.4

The user asked for a review of the commits made for the stutter and whether it is solved. The three layer versions were read in full: correct, well tested, and honest about being unverified in the game. The open point was the attribution. The session's own buffer inventory, read by generation counter, shows that the copies at unmap were not staging buffers but the game's 16 MiB index and vertex pools on device-local memory, which share the transfer-source bit and the size limit of the rule written for staging buffers. The benchmark that justified 0.7.3 measured the staging memory type; for the pools' type it shows a factor of 1.5.

0.7.4 narrows the rule to transfer-only buffers, drops shadows that could be stale, counts what it passes over and adds a `set` command for the two unmap settings. Native tests 126, limits 172; the full suite passed against `build/next`. A first run in `dist/` rebuilt the DLL there and failed on the diagnostic settings file (ISSUE-006); the tested files were copied back, the harness now pins its settings, and the suite passed in `dist/` without rebuilding. A here-document with an apostrophe in a comment failed in the shell before it ran; the patch went through a script file.

This is a correction of a rule from the first days of the probe, not of the later commits. Whether the game is smooth with it has to be measured in a session the user starts.

## 2026-10-12: The game is smooth with 0.7.4, and the review before it was wrong about why

The user started the game with 0.7.4 and reported that the stutter seemed gone. The first status read settled the question the review had opened: `passed_over` 0. Nothing with further usage is ever unmapped; the copies are staging buffers, tens of thousands of them, and they now run at 3.6 GB/s because of the streaming loads that 0.7.3 added. Across the user's open-world run 3 of 87,005 present intervals exceeded 33 ms. The mesh-pool story came from a counter that also counts tracked writes and from looking at 251 live buffers of a session that had created 280,000.

Three captures in the open world gave the first data from there: one recording thread as before, about 1,190 draws, instancing up to 675, every hash from the mapping, and the mod's LOD1 sections matching in the shadow pass.

The record was corrected rather than extended: `docs/unmap-snapshots.md` rewritten around the measurement with the wrong reasoning kept as its last part, D-026 withdrawn by D-027, BUG-004 and ISSUE-005 closed with the right cause, T-020 closed, T-021 withdrawn, plan facts 24 and 25. Source 0.7.5 restores the default of the copy at unmap and keeps what was right in 0.7.4; it passed the full suite in `build/next` and waits for the game to exit before it goes to `dist/`.

## 2026-10-12: Resume and deploy 0.7.5 after game exit

Restored current state/tasks and checked the screenshot against the repository. The game was no longer running. Backed up dist/ to build/backups/before-0.7.5-20261012-155227/, copied the exact tested binaries from build/next and corrected the cfg comment to describe the restored default. Settings stay probe, resources/hash off and tracker profiling on. Both DLL SHA-256 values are f331ff7c3a390affbc7adfe617a1e1364f9d5441821b86f2ecdabf4ff39c7475.

Full --no-build suite on the deployed files passes (build/deployment-0.7.5-regression.txt): unit 88, native 126, 17 probe configurations 504-519, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 78. Actual-config inject smoke confirms 0.7.5, the full unmap default and correct draw hashes (build/tests/sandbox-2c8cp1sr/); the historical game log is unchanged. No game or launcher start/stop and no launcher edit. Continue M2 offline; no repeated stutter-duration test is required for this deployment.

## 2026-10-12: M2 pipeline recipes, GPU variant/restoration proof and 0.8.0 deployment

Continued until the next necessary game observation. Existing captures lacked complete depth/blend/render-pass state, so added independent core creation recipes and explicit unsupported reasons. The private variant builder reuses shared SPIR-V/layout metadata, redirects only VS bones/instance to set 3, reproduces compatible original layouts and changes the static vertex declaration. The draw helper restores pipeline/VB/IB without touching the original sets. None of it is invoked by probe draw hooks. D-028 refines D8 without requiring a new enabled game feature; T-022 tracks the next gate.

Added CPU ownership/ignored-pointer/failure tests and a headless GPU fixture. The latter builds on a worker after original creation handles die, uses bone index 257 in an RGBA16_UINT replacement, switches uint16 to uint32 indices, applies both own buffer offsets and a nonzero dynamic uniform offset, and confirms original fragment color/blending by pixel readback. The immediately following app draw has no rebinding and still renders correctly. Both RenderPass 1/2 and none/layer/inject pass with zero validation errors/warnings; three negative canaries prove validation is active. No window is opened. All 21 historical character VS modules also match the Python rewrite exactly and pass spirv-val.

Full build/regression: unit 88, native tracker 126, native recipes 120, variant runner 37 over 9 GPU processes, 17 probe cases 514-529, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 78. Log: build/m2-recipes-regression-20261012.txt. Historical shader report: build/variant-shaders-l0syviji/report.json. No layer build/test failure; a temporary actual-config smoke script initially selected draw.hash instead of draw.ib.hash and was corrected, then passed on candidate and dist/.

Game absence checked twice; backup build/backups/before-0.8.0-20261012-163359/; copied exact tested package to dist/. Both DLL SHA-256 values: 6e95b328e5ef019ad3e6d025cad966f2e3b326b97cd34d2b0270155610d73f3d. Settings now probe, resources on, image hashing and optional tracker profiling off; WC fix and unmap defaults retained. Actual-config smokes verify recipes, five hashes before/after rewrite and unchanged historical game log. No launcher edit, game launch or game injection this round.

Stop for the user-started 0.8.0 observation with the sample character visible, display then world if available: collect full matched pipeline/layout recipes plus --snapshot mod --follow instance, prioritize all matched pipelines with --pipeline-limit 128, and inspect preflight and LOD1 bones. No 30-minute prerequisite. M2 remains partial: real animation, bone remapping, textures, asynchronous cache and in-flight retirement still need implementation/evidence. Details: docs/m2-pipeline-variants.md.

## 2026-10-12: Review of 0.8.0 (recipes, variant builder, deployment): no defect found

Requested by the user ("review Codex's commits"); covers `d79cce9` and `09e3e89`. No source change. The game was not running.

Checked directly, not taken from the commit's own account:

- Rebuilt HEAD into `build/next` and ran the whole suite: all steps pass with the documented counts (unit 88, tracker 126, recipes 120, variants 37, 17 probe configurations 514 to 529, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 78). Output: `build/next/run_all.out`.
- `scripts/verify_variant_shaders.py captures/third-probe --dist build/next`: 21 of 21 (`build/variant-shaders-v8lds7nt/`).
- `dist/`: both DLLs hash to `6e95b328...`, the settings file to the value in `build/deployment-0.8.0.json`. The launcher configuration was last written at 15:28:27, before both deployments, and holds the Vulkan values (`SKIP`, empty arguments).
- Read `pipeline_recipe`, `pipeline_variant`, `spirv_repoint`, the hooks and the changes in `state.cpp`. What runs in a game process is the capture at render pass and pipeline creation and the CPU-only preflight inside the `pipeline` command; `PipelineVariant::build` and `emit_variant_draw` have no caller in the layer. The capture refuses `pNext`, flags and extended dynamic state before it follows a pointer, reads depth and blend state only when the subpass uses such attachments, and static viewport arrays only when they are not dynamic. Nothing was added to the per-draw path.

What the earlier game data says about the next session (a prediction, not a result): the 936 pipeline creations of the third session had flags 0, no `pNext`, two stages and only core dynamic states; the 39 pipelines of the 45 matched draws have three set layouts, no push constants, no immutable samplers and no binding flags. Whether the game's render pass creations carry `pNext` or flags was never recorded, so `render_pass_extension` cannot be excluded beforehand.

One note for the integration, added to T-022: 33 of the 45 matched draws use pipelines that declare five vertex bindings (binding 4 with stride 0, the default tangent buffer), while the tracker keeps four slots (`kMaxVb`).

## 2026-10-12: Current 0.8.0 recipes, corrected LOD labels and bounded selection

User-started PID 17484 loads 0.8.0. Display and later world bundles contain complete supported recipes for 39 and 45 matched pipelines. The user corrected the interpretation: they had switched to LOD1 initially and returned to LOD0 after the agent called captures empty. The later two world bundles are LOD0 state with depth-only LOD1 sections, not the requested distant-LOD1 result. Scene labels and missing evidence are preserved in docs/m2-game-recipes.md.

Added a bounded selection collector and CLI section/color requirements, retaining every attempt, manifest fingerprints, explicit exhaustion/interruption and unknown pipeline state. Sandbox evidence tests pass 109 checks. Source layer and deployed DLL stay 0.8.0. Three 12-attempt full-snapshot LOD1 series were still empty, including varied delays; a no-snapshot raw series contains five graphics intervals out of sixteen. An ordinary full recipe collection without snapshots then succeeds on its first attempt: frame 369782, 872 scene draws, 58 matches, 36 supported recipes, all nine LOD1 sections including color passes. Current-session VS checks pass 21/21/19 modules. All saved bundle fingerprints verified.

Do not treat this as complete LOD1 bone evidence. T-023 adds a capture-window fix before another user test: keep recording armed through empty intervals, return one nonempty interval, preserve timeout semantics. No new game launch, replacement rendering, deployment or launcher edit. Also corrected the temporary 0.8.0 smoke's wrong log filename and withdrew its historical-log preservation claim; other smoke checks remain valid.

## 2026-10-12: 0.8.1 keeps snapshot recording armed through empty intervals; candidate verified

Implemented T-023/D-029 in the server capture state machine, Python client and CLI. The default capture is unchanged. The optional wait discards empty intervals' capture data while preserving normal tracking and record-time reads; the response describes one interval with draws and reports the skipped count. Timeout cancels snapshot work. Old layers are rejected explicitly for the new option.

Native tests exercise a draw recorded across two intervening presents, upload/dispatch in a skipped interval, a later buffer write, correct record/submit comparisons, nonmerged statistics, default empty capture and timeout cleanup. All GPU snapshot configurations use the new request too. Full build/regression passes (build/capture-wait-draws-regression-20261012.txt): unit 88, native tracker 144, native recipes 120, variant runner 37, 17 probe cases 517-532, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 113.

Candidate actual-config inject smoke passes (build/capture-wait-actual-config-next.json): 0.8.1 probe/resources on, image hashing/profiling off, five snapshot draws with wait enabled, full recipe and correct hashes before/after rewrite. The actual game-log filename is now required and its hashes are saved; equal in this run only. Both candidate DLLs SHA-256 7e311a9f9f358aa50d90c579abfc1384877a8b391aea8ef886d1e87a90ba50d7. Source/candidate only: PID 17484 and dist/ retain 0.8.0. No automatic game stop, launch, deployment or launcher edit. Next requires normal exit, deployment and a user-started LOD1 snapshot observation; no LOD0 recapture or duration test.

## 2026-10-12, 17:36: Deploy the verified 0.8.1 after user-confirmed normal exit

Checked game absence before backup and again before replacing files. Backup: build/backups/before-0.8.1-20261012-173645/. All eight package files copied exactly from the tested build/next and verified by SHA-256; both DLLs retain 7e311a9f9f358aa50d90c579abfc1384877a8b391aea8ef886d1e87a90ba50d7. Deployment manifest: build/deployment-0.8.1.json. Actual-config inject smoke on dist/ passes, including the new wait request with five snapshotted draws, complete recipe, correct hashes before/after rewrite and unchanged actual game log (build/capture-wait-actual-config-dist.json). No rebuild, launcher edit or automatic game launch. Settings remain probe/resources on, image hashing/profiling off. No 0.8.1 game result yet; ready for the user's actual LOD1 scene to collect the missing bone/instance timing.

## 2026-10-12: Actual LOD1 snapshots with deployed 0.8.1

The user entered LOD1 and authorized collection. Confirmed user-started PID 34644 loaded 0.8.1 with probe/resources on, hashing/profiling off. Both bounded collections select their first attempt after 8/7 empty present intervals: frames 19506/20414 have 818/819 graphics draws and 60 dispatches each. All nine LOD1 sections occur, including 25 color-subpass draws per frame; 58 total matches and 36 complete/preflight-supported recipes each. All 58 instance entries and both actual VS bone windows agree at record/submit in both frames. No snapshot omissions; both sets of 147 file fingerprints and parent manifest references verify (captures/m2-0.8.1-20261012/fingerprint-report.json).

T-023 is verified for this scene and the observed LOD1 color-pass timing gap is closed. Data are sufficient to continue offline CPU bone import/remapping; no further scene change or duration test. No replacement rendering, launcher edit, game restart or deployment occurred. Dist/ remains the loaded 0.8.1. Report and evidence limits: docs/m2-game-recipes.md.

## 2026-10-12: Continue offline with CPU skeleton import, candidate 0.8.2

Read the installed Core rolling-buffer importer, initializer, patcher and update rules, and the sample's eight R16_UINT LOD1 remaps without editing them. Implemented bounded atomic CPU import, per-instance/current/previous layout, finer-LOD priority and byte-preserving entry patches. Native tests pass 42 checks; production C++ matches an independent Core-derived Python reference after all 283 matched draws from five saved frames (74 imports, 209 repeats, 407,808-byte master). No direct DX11 output buffer or live replacement is claimed.

Full build/regression in build/next passes: unit 88, tracker 144, bones 42, recipes 120, variants 37, 17 probe cases 517-532, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 113 (build/bone-import-regression-20261012.txt). Source/candidate 0.8.2; dist/ and the game retain 0.8.1. Probe hooks do not call the new helper. Continue offline pipeline cache and GPU resource lifetime work. Details: docs/m2-bone-import.md, T-024, D-030.

## 2026-10-12: Cache and draw-snapshot building blocks, candidate 0.8.3

Implemented a bounded asynchronous variant cache and private per-draw VS snapshots. Requests never wait for driver compilation; retirement cancels queued builds and command owners retain ready pipelines. Snapshot storage checks alignments/ranges, pads the last dynamic uniform window and becomes immutable when sealed. Native cache 21 and allocation/range tests 25 pass. The existing GPU test now uses these production helpers, poisons input arrays, retires/stops the cache before submit, retains the pipeline, resubmits the sealed commands, and still passes pixel/state-restoration checks in both render-pass versions through no layer/layer/inject with clean validation.

Full build/regression passes in build/next (build/variant-cache-regression-20261012.txt): unit 88, tracker 144, bones 42, recipes 120, cache 21, snapshots 25, variant runner 39, 17 probe cases 517-532, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 113. Dist/ and the running game remain 0.8.1. Helpers are not called by the draw hooks yet; continue minimal sample/command-recording integration. T-025 and docs/m2-cache-and-snapshots.md record the scope.

## 2026-10-12: First complete hooked component, candidate 0.9.0

Connected explicit sample component-7 assets, the CPU importer, asynchronous variants and command-owned snapshots to the actual indexed draw hook. Every draw imports its own palette; mesh and arenas prepare on a worker. Complete transactions replace and restore bindings; unready/unsupported/unreadable cases keep the original. Submission seals snapshots and successful legal reset/begin/pool reset recycles completed slots without per-frame allocation. Runtime enable/disable affects future recordings only. See D-031 and docs/m2-component-hook.md.

Native mesh rejection/declaration tests pass 30. Four actual-DLL GPU configurations (layer/inject, RenderPass 1/2) pass 45 runner checks with 30 pose updates, original next-draw recovery, all reset forms, unchanged resubmission after disable, deliberately unmapped-bone fallback and remap recovery. Core/sync validation is clean. The first full suite found a missing CRC initialization in the new standalone native test, not the DLL; fixed the test initialization and rebuilt that executable. The full final suite passes (build/m2-integration-regression-final-20261012.txt): unit 88, tracker 144, bones 42, recipes 120, cache 21, snapshots 25, variants 39, M2 45, probe 517-532 across 17 cases, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 113.

The original component-7 package is accepted by the production decoder (40 vertices, 108 indices, 12 local bones at merged 266). All ten matching draws from five saved display/LOD0/LOD1 frames satisfy its explicit profile. The candidate's actual configuration enables that package; scripts/smoke_m2_config.py verifies readiness, ordinary hashes/snapshots/rewrite, clean validation and normal sandbox exit, with the actual game log unchanged (build/m2-component-actual-config-next.json). No installation edits, launcher changes or game restart. Dist/ and PID 34644 remain 0.8.1. Next requires normal exit for exact-file deployment, then a user-started first component replacement observation. Full-character rendering and M2/M3 acceptance remain open.

## 2026-10-12, 18:58: Deploy verified 0.9.0, then pause as requested

The user confirmed normal game exit and requested a pause after configuration. Verified game absence before backup and before each copy. Backed up all old dist/ files with fingerprints to build/backups/before-0.9.0-20261012-185849/. Copied twelve exact tested files, verifying each against build/candidate-0.9.0.json; both DLLs SHA-256 791e7d163c82562acc8732947d615b5caa38530d42ef67b18e618a02d7259d00. The explicit project-owned component package retains SHA-256 318e8354297fcdfd4b352d0ec031a6b2ba6472335fefe0be2c1b16f3eed52167. Manifest: build/deployment-0.9.0.json; implementation commit 592247d.

The deployed actual-config inject smoke passes with M2 ready/enabled, correct five-draw snapshots/hashes before and after rewrite, clean core/sync validation and normal sandbox exit. The actual game log remains unchanged (build/m2-component-actual-config-dist.json). No rebuild or repeated full suite is needed for identical files. Config enables only the explicit component-7 experiment; image hashing/profiling remain off and WC/full-unmap defaults are retained. No launcher change or game launch. Updated memory and paused; resume development or user-started replacement observation only when the user asks. No real-character result for 0.9.0 is claimed.

## 2026-10-12, 19:25: Review of 0.8.1 to 0.9.0; the patched bone offsets were one matrix too high; 0.9.1 deployed

The user asked for the review to continue; it covers `c1bb6db` to `2d64775`. The game was not running. An independent rebuild of 0.9.0 into `build/review-0.9.0` passed the whole suite with the documented counts before anything was changed.

Reading `merged_skeleton.cpp` against the DX11 baseline showed the difference: the layer patched `(1536, 3033)` where the baseline has `(1533, 3030)`. Confirmed three ways before touching code: the core's `MergedSkeleton.ini` passes `$merged_skeleton_offset - 3` to the overrider; all 23 patched vertex entries of two DX11 frames hold `(1533, 3030, 258)`; the disassembled Vulkan vertex shader of the component 7 draw computes `constant5.x + 3 + 3 * index` and picks the entry by `gl_InstanceIndex`. The tests had shared the reading (native expectation, Python reference, sandbox shader). Made the tests right first: with a game-shaped vertex shader the 0.9.0 layer fails all four GPU configurations and four native checks. Then the one-line fix, version 0.9.1 (BUG-005, D-032, `docs/m2-bone-import.md`).

Full build and suite in `build/next` pass (bones 43, M2 45, the rest as before; `build/review-0.9.0/run_all-0.9.1.out`). Replay of the five saved frames: 283 records, same master hash, every patched entry `(1533, 3030)`. Deployed at 19:15 after checking twice that no game or sandbox process ran: backup `build/backups/before-0.9.1-20261012-191540/`, eleven binaries compared by SHA-256, both DLLs `d2850254ded6477c777fc1f06d5f93c8ce921f74897e2881ca996fb8b7cae417`, settings values untouched. Suite on `dist/` without building and the actual-config smoke pass; the game log is unchanged. No launcher edit, no game start.

Nothing else needed changing. Checked with game data rather than assumed: single-instance requirement against `gl_InstanceIndex`; no `Bind*2`, reset or free of command buffers in the 0.8.1 session and one command buffer per frame for the graphics draws, so the arena limit of eight is not near; the package against `CommandList_Draw_Component7`; the mod's index list for component 7 hashes to the game's `a3fe5c47` when written as 16-bit. Recorded LIMIT-020 (a capture does not mark replaced draws). The pause stands; next is the user-started session of T-026.

## 2026-10-12: Resume, first live replacement execution and general-backend direction

User-started PID 3080 loaded 0.9.1 with component 7 enabled in the display/LOD0 scene. A
4.008-second window adds 806 matches and replacements with stable refusal counters. Later
unsupported grows from 96 to 227; the grouped last reason does not identify the individual
condition. Unreadable/exhausted remain zero. Preserved the live status and a full draw/snapshot
bundle, verified all 176 fingerprints, and confirmed both component-7 draws have supported
recipes and record-equal entry/current/previous snapshots. Full-frame offline replay refuses
the additional component-10 pipeline 437's layout before execution. No passed replay claimed.

Located the tiny eye-region mesh and compared its live original XYZ positions to the mod:
identical, although packed normal/UV bytes differ. Saved an uncommitted mesh-space plot.
The user cannot reasonably accept appearance from this part and requests whole-mod support
using general logic, not separate per-component code. Recorded D-033/T-027. The user confirmed
voluntary exit after the pipe disappeared. No launch/stop, runtime toggle, deployment or
external write occurred. Details: docs/m2-first-replacement.md. Continue offline development;
the earlier pause is superseded, and visual/animation acceptance remains open.

## 2026-10-12: General stream contracts and ordered draw transactions, 0.9.2

Added shared backend inputs for vertex stream stride/element mappings and an ordered list of
resolved replacement draws. The old component-7 declaration/emitter now use the shared paths;
no component-number or sample-hash dispatch was added. Every changed stream element is checked
against an explicit source contract, untouched default streams are retained, and failures leave
the output unchanged. The emitter validates the entire bounded list before issuing anything,
supports an intentional empty skip and restores the application once after all selected ranges.

Full build and suite pass in build/next (build/general-draw-0.9.2-regression.txt): recipes 134,
mesh/declarations 49, other suite counts unchanged. Native fixtures cover six UV/blend layout
combinations on arbitrary bindings; command tracing checks ranges, signed base vertices,
instance arguments and restoration. GPU readback proves two distinct replacement entries,
no partial draw on a later invalid item, unchanged FS data, original next draw and sealed
resubmission under clean core/sync validation in both render-pass versions and layer entries.
Source/build/next are 0.9.2; dist is unchanged at 0.9.1. No game action or full-mod compatibility
claim. Details: docs/general-replacement-operations.md. Continue T-027's frontend, resource,
shared-skeleton and material integration using these data inputs.

## 2026-10-12: Pinned XXMI standalone CPU host feasibility

Compiled/linked unmodified XXMI 1.2.2 parser, expression, command-list and pool sources with
the pinned reference's adapter headers and local CPU shims. A headless executable passes 23
checks covering the D-016 semantic questions; keys use synthetic button state, no hooks.
Initial test expectations incorrectly merged root duplicate sections and read ElementFormat's
semantic index before execution. Corrected those against upstream; engine sources unchanged.
The reproducible full build also corrected a self-including wrapper filename and preserved
the original units' Win32 min/max macro requirements separately from CRC compilation.

scripts/probe_xxmi_host.py verifies dependency commits/clean tracked trees, stages original
files under F:/vulkan, builds all units, records fingerprints and returns failure without
running a stale binary. Final report: build/xxmi-host/report.json, 23 passed, zero failed.
Method/limits/attribution: docs/xxmi-host-prototype.md. The GPU seam explicitly refuses when
called, but reference headers still contain unused no-ops and are not a production adapter.
No real mod/resource/Core runtime, layer change, deployment or game action. D-016's bounded
CPU feasibility checkpoint is complete; continue T-027 resource/shared-character integration.

## 2026-10-12: General command-tree adapter and real sample callback plans

Added frontend/xxmi/draw_plan: original expressions, branches and named calls execute while
GPU operations become ordered bindings/draws/layout requests. The adapter has no component
numbers, sample hashes or filenames. Explicit native boundaries survive optimisation and
retain their phase bits. Invalid/unknown operations, unadapted expression queries, aliases
and budget overflows reject the complete output; original skip/layout state stays unchanged.
Resource declaration ownership and frontend variable rollback remain caller responsibilities.

The full standalone build passes the original 23 CPU checks and 28 planner checks. Added a
read-only actual-INI fixture with the original Windows text-mode CRLF handling and parser
diagnostic rejection. scripts/check_mod_draw_plans.py verifies the tested binary/sources,
executes 12 draw callbacks with 16 selected persistent-scalar scenarios at LOD 0 and 1, and
checks selected ranges against the real vertex/index files. All 384 plans pass: 404 draws,
192 mesh-empty callbacks, 18 unique indexed ranges, 38 bound resource declarations. The INI
retains its known hash. Reports: build/xxmi-host/report.json and mod-plan-report.json.

Only callback selection and file ranges are established. OverrideTextures remains an explicit
unresolved native operation; the full mod's initialization, shared bones, GPU assets/materials,
input/persist/reload and panel are not implemented compatibility. No layer change/deployment,
launcher write or game action. Updated architecture/decision scope and the new method document
docs/frontend-draw-plans.md. Continue T-027 offline with those remaining integrations.

## 2026-10-12: Compressed DDS metadata and actual resource sizes

Added a general bounded 2D DDS decoder returning Vulkan formats and exact mip ranges into
owned file data, without decompression, color conversion or payload copies. Rejects unsupported
containers/formats, unsafe sizes, padding, inconsistent headers and incomplete/extra bytes.
The headless host rebuild passes 23 frontend, 28 planner and 15 asset checks. The sample
verifier additionally runs the native decoder on all ten bound DDS files and independently
compares format/dimensions/ranges with the headers; all pass. No GPU upload is claimed.

Measured selected assets: 38 files, 315,328,648 bytes; four DDS images are 8K with 64 MiB
compressed payloads each. Nine selected textures are DX10 BC7, one legacy BC5U, all with
one mip. An initial inspection of every declaration encountered two absent DDS files, but
the verified callback plans never use them; demand loading must preserve this distinction.
INI/assets unchanged; reports in build/xxmi-host/. No DLL/config/deployment/game action.
Updated docs/resource-assets.md and memory. Continue asynchronous GPU resource preparation,
material descriptors and shared character/Core integration without per-component branches.

## 2026-10-12: Standalone GPU texture upload and exact sample readback

Added owning image upload tickets, separating preparation from serialized caller-queue
submission. Images/views stay private until a completion fence is polled; staging is then
released. Decoded formats/mip blocks remain unchanged. Enabled BC/capability/device-limit
checks and partial creation failures publish no partial image. Outstanding uploads remain
owned even after a fence-status error; cancellation destruction waits, normal polling does not.

scripts/check_image_upload.py --sample builds a headless Vulkan test with child-only core/sync
validation. Synthetic 27 checks pass, including changed CPU source after prepare, all compressed
mips, duplicate-submit refusal, disabled BC/extent rejection and an injected staging allocation
failure. GPU blit distinguishes UNORM 128 from sRGB about 55. All ten actual selected DDS files,
including four 8K textures, upload and read back their entire compressed payload exactly;
all runs have zero validation errors/warnings. Evidence: build/assets-gpu/report.json.
One initial test build hit Win32's small macro; renamed the local test variable before execution.

No layer/DLL/config change, injection, game action or original asset write. This is a tested
standalone uploader, not game scheduling/material rendering. Actual enabled-device/queue
state, resource-manager budgets/ownership, material bindings, shared skeletons and full
Core/frontend integration remain T-027 work. Updated docs/resource-assets.md and memory.

## 2026-10-12: Bounded worker texture cache and retained-generation ownership

Added TextureCache and shared allocation budgets. Declarations deduplicate lexically without
touching files; concurrent demands queue one read, and unused missing declarations stay dormant.
The worker reads/decode/prepares/records then polls/releases staging; only the caller submits
already recorded work on its fixed graphics queue. At most one pending upload per cache.
Actual allocation sizes are charged before GPU allocation, including retained old-generation
images. Source, staging and residency have separate aggregate limits. Failed requests are
sticky until a new generation, and stop drains pending work off the rendering path.

Headless GPU fixture passes 57 checks, including 256 concurrent demands, reload with changed
source bytes, retired-generation lifetime, capacity/path/foreign-handle refusal, all three
budget failures, cancellation and injected submission failure. Instrumented driver seams verify
worker/caller separation. Ten actual textures pass both full compressed readback and the cache;
five sample-cache checks pass. Peak bytes: resident 283,377,664, staging 67,108,864, source
67,109,012. All core/sync validation is clean. Report: build/assets-gpu/report.json; log:
build/texture-cache-check.txt. Builder now fingerprints inputs and refuses malformed reports.

Layer source/build/next remain 0.9.2 and dist/0.9.1 remains untouched. This is standalone texture
preparation/ownership, not whole-mod rendering. Buffer resources, descriptor/material support,
shared character/Core and real frontend generation integration remain. No game action or asset
write. Updated docs/resource-assets.md, architecture, current state and tasks.

## 2026-10-12: Fragment image isolation, pixel proof and dimension audit

Added standalone FS image-binding relocation, retaining all nonbinding words and original
VS/sampler/buffer inputs. Initial actual-shader check refused Depth=Unknown declarations;
the specification and actual modules confirm this is not a depth-image assertion. Added
opaque-value tracing and explicit depth/escape refusals, retaining unrelated shadow sampling.

Headless asset suite now passes 79 checks with clean core/sync validation. GPU material test
uses the same original image in VS/FS: private FS turns red without changing VS displacement,
the next original draw stays blue, a zero 2D texture returns zero, and untouched pixels remain
green. Images survive their cache being retired before drawing. Production layer unchanged.

Historical validation found three mappings whose target image type is 3D. The first chat
description called this a mapping issue; checking both APIs refined that conclusion: original
pairings are matching 1x1x1 3D placeholders. Frame-a/frame-on custom DX11 draws 126/145/162 later
bind mod 2D views in those slots. Bindings alone do not establish sampled behavior. Kept the
slot correspondence, added refusal annotations and LIMIT-021 instead of silently converting
dimensions. The other 16 moves across seven shaders match Python and pass spirv-val; all nine
source modules validate. The verifier fingerprints evidence and audits both raw DX11 captures
through the read-only sibling decoder. Report: build/fragment-images/report.json.

Updated docs/fragment-materials.md, D-034, architecture, tasks/current state and known issues.
Still standalone: descriptors, pipeline/cache integration, full Core/shared character and
dimension semantics remain open. No game, DLL/config deployment or original asset write.

## 2026-10-12: DX11 dimension mismatch is not portable

Located all three historical PS binaries in the read-only EFMI output archives. Their zero-seed
FNV-1 hashes equal capture shader ids. Local disassembly confirms Texture3D declarations and
SampleLevel instructions in the affected slots. verify_fragment_images.py now records and
reproduces this evidence. Microsoft D3D11 section 22.3.12 requires a matching Texture3D resource.

Built standalone headless DX11 observation fixture and runner. Four controls (matching 2D/red,
3D/blue, null 2D and null 3D) pass on both NVIDIA hardware and WARP. Invalid 3D-shader/2D-view
samples at three Z coordinates and the reverse dimension mismatch yield RGBA zero on NVIDIA
and texture color on WARP. PSGetShaderResources retains the requested view on both. Green
clear distinguishes these results from dropped draws. Debug runtime unavailable on both;
recorded as unavailable, not passed. No feature installation, game or injected DLL.

Evidence: build/dx11-dimensions/report.json, with source/exe fingerprints and per-run logs.
Default Vulkan refusal remains; no universal dimension-coercion policy is inferred. Valid
explicit-null INI semantics need their own shader operation, not just a zero 1x1 image that
can behave differently under dimension queries or border sampling. Updated current state,
tasks, LIMIT-021 and docs/fragment-materials.md. No layer/config/deployment or asset write.

## 2026-10-12: General explicit-null image semantics

Added shared bounded fragment analysis and explicit-null requests for float32 sampled 2D/3D
images. Selected sample/fetch/size/mip results become typed zero, opaque accesses disappear,
and other resources remain intact. Unsupported opaque merges/escapes, QueryLod, arrays/MS and
sparse operations refuse atomically. No component identifiers or dimension-mismatch policy.

Headless asset suite passes 92 with zero core/sync diagnostics. Pixel controls use white
sampler borders and size/mip queries: a real or zero 1x1 image returns nonzero, while the null
rewrite returns zero and another blue image remains readable. Existing FS/VS isolation and
original-draw restoration controls still pass. Both synthetic source/output validate.
Historical adapter coverage: nine shaders, 19 mapped bindings pass spirv-val after nulling;
this is not a claim that the actual INI nulls all of them. Existing 16 relocation checks and
three expected 3D/2D dimension refusals pass. Reports: build/assets-gpu/report.json and
build/fragment-images/report.json; log build/fragment-null-check.txt.

Updated docs/fragment-materials.md, architecture, tasks and current state. No layer source,
DLL/deployment/config or asset changes. Continue production material descriptor/pipeline
ownership and shared character/Core offline. The user already confirmed voluntary game exit.

## 2026-10-12: Material variants and retained image snapshots in 0.9.3

Integrated ordered fragment image/null contracts into the shared worker-built pipeline
variant and cache. Keys include material shape, not image handles. New private layouts
retain VS bindings 0/1 and add only required FS images at 2+i. Descriptor budget checks
include original retained layouts. Invalid/missing/overlapping sources refuse explicitly.
DrawSnapshots preallocate image owner slots, validate every image before mutation and
retain owners through sealed recording/resubmission until legal reset or destruction.

Full build/regression passed in build/next: unit 88, tracker 144, bones 43, recipes 134,
cache 28, snapshots 34, variants 63, mesh 49, M2 hook 45, probes 517-532, modes 40,
limits 174, timing 126, validation 76, MCP 47, evidence 113. Log:
build/material-0.9.3-regression.txt. New GPU cases in no-layer/layer/inject and RenderPass
1/2 prove red/green per-draw images, null output, original blue restoration, original FS
buffers, rejection atomicity and retained image/pipeline owners over repeated submission.

This is shared backend integration and sandbox evidence. The existing component hook
requests no materials; the hosted XXMI frontend and TextureCache remain standalone.
Source/build/next are 0.9.3; dist stays 0.9.1. No game/deployment/launcher/config or installed
asset changes. Continue mesh-buffer assets, frontend resolution and shared character/Core
with existing captures; no new user game action is needed yet. Updated material docs,
architecture, D-034, current state and tasks.

## 2026-10-12: Bounded vertex/index asset uploads

Added BufferUpload, an immutable device-local byte ticket with explicit prepare, record,
caller submit and poll phases, actual allocation budgets and retained Ready ownership.
Supports vertex/index usages, exact source lengths, transfer visibility and atomic failure
publication. It does not infer stride/index format, parse INI, create texel/storage views
or perform queue handoffs independently of the application.

Headless asset suite passes 138 with clean core/sync validation. Exact 1/3/6/4101-byte
readbacks, source destruction, immutable/pre-fence state, cancellation, submit failure and
both allocation limits are covered. All 28 actual callback-selected BUF files match their
source fingerprints and full GPU readbacks. Material pixel tests now use uploaded VB/IB.
Historical fragment rewrite/null validation passes with the rebuilt fixture. Evidence:
build/assets-gpu/report.json, build/buffer-upload-check.txt, build/fragment-images/report.json.

Updated resource docs, architecture, current state and tasks. This is standalone asset
work; layer/build/next stay 0.9.3 and dist stays 0.9.1. No installed asset, game, deployment
or configuration change. Continue common buffer/image resource cache scheduling and
frontend/shared character integration offline.

## 2026-10-12: One asynchronous cache for all selected mesh and image assets

Generalized the existing texture scheduler into ResourceCache with typed image/buffer
owners and path-plus-usage keys. Kept TextureCache names as compatibility aliases. One
worker/outstanding upload and caller queue handoff share source/staging/resident budgets
across resource kinds and retained generations. No per-component file loading path.

Headless suite passes 149 with clean core/sync validation. Mixed-kind/usage identity,
128 concurrent mesh demands, deliberate wrong decoder, exact outcomes, changed reload
bytes, foreign handles, owner lifetime and intended worker/caller API execution pass.
The material pixel fixture now loads both mesh buffers and images through one cache,
retires it, then renders with retained resources. All 38 actual bound sample assets
(28 BUF and 10 DDS) also pass five cache/validation checks. Peak bytes: resident
315,327,232, staging 67,108,864, source 67,109,012. Final owner release zeros the ledgers.
Evidence: build/assets-gpu/report.json, sample-resources.txt and
build/resource-cache-check.txt. Historical fragment validation also passes.

Updated resource docs, architecture, tasks/current state. Source/build/next remain
0.9.3, dist remains 0.9.1; this common scheduler is standalone, with no deployment or
game/config/installed asset action. Continue hosted frontend resource/command resolution
and shared character/Core identity/completeness using saved data.

## 2026-10-12: Keep the single-character replacement ahead of memory optimization

The user questioned the measured 301 MiB resource footprint, then explicitly directed
work back to the plan before that discussion: get this character's complete replacement
working, and address performance later. An unverified residency/eviction draft was
saved only in build/drafts/residency-unverified.patch and removed from source. No build,
deployment, installed asset change or game action occurred for that draft. The tested
cache at 6dfce2f remains intact. Continue hosted command/resource resolution, shared
character/Core identity and bone completeness, using component-independent operations.

## 2026-10-12: Connect owned frontend file plans to resource preparation

Added a generation-owned snapshot of original XXMI file declarations and an owned
command/resource representation. It preserves strides, index formats, binding order,
explicit nulls, layouts, draw arguments and unresolved native phases. Unsupported
transformations, dynamic/Core resources and foreign declarations refuse atomically.
The cache handoff freezes metadata, deduplicates uploads and collects all Ready owners
without file/driver work on the request path. Pending/failure keeps prior output intact.

Verified: host 23, command planner 28, resource planner 19 and DDS 15; all 384 actual
callback plans match original declarations, with 404 draws/18 index ranges/38 files.
GPU resource suite 161 passes under clean core/sync validation. New checks cover real
VB/IB/image uploads from file plans, repeated references, pending/null/native states,
caller metadata changes, foreign generations, partial vertex elements and owner
retention. Historical fragment checks also pass. Initial host fixture namespace typo
was corrected before the passing build. Logs build/frontend-resource-{host,mod,gpu}.txt.

No layer change, deployment, game action or installed asset modification. Source and
build/next remain 0.9.3; dist remains 0.9.1. Continue resolving per-draw binding state and
native shared character/Core operations. GPU asset readiness is not Core execution,
runtime draw-range validation or real-character replacement acceptance.


## 2026-10-13: Resolve file bindings separately for every planned draw

Continued the user's original single-character plan after interruption. Added
per-draw vertex/index/material snapshots with Ready owners, copied layouts and
arguments, and explicit file/null/application states. Any unresolved native call
or late invalid file range rejects the complete transaction before command output.
Buffer uploads retain only typed whole-file index extrema for conservative bounds.
This can refuse valid slices; all 404 current sample draws pass.

Verification: headless asset suite 200, all 28 actual BUF full readbacks/extrema,
clean core/sync validation; material pixels consume resolved draw metadata after
cache retirement. Host 23/28/19/15, 384 actual plans and historical fragment checks
pass. Logs build/file-draw-bindings-{gpu,host,mod,fragments}.txt. Memory/docs synced.
This is standalone frontend/assets work, with no layer/deployment/game/config or
installed-asset change. Source/build/next remain 0.9.3 and dist 0.9.1. Continue
shared character/Core identity and bone completeness before the next game test.

## 2026-10-13: Execute original skeleton initialization and read its evaluated pools

Added bounded original CPU pool writes/reads, pool references and dynamic callback
aliases to command mediation. Declaration references snapshot metadata instead of
creating D3D resources. Guard direct indices against nonfinite/out-of-range casts;
reject cyclic/unsupported operations and expiring resource aliases. Variable writes
conservatively invalidate declaration aliases in a mixed pool. CPU effects remain
nontransactional while refused draw output stays unchanged.

The skeleton adapter copies evaluated original offsets/counts/remap references into
owned metadata and validates a complete partition, preserving sparse/empty component
ids. Original sample initialization gives 499 bones, 8 instances, 13 declared slots,
12 active components and 8 exact R16 remap files. LOD2/3 remain unassigned. Native GPU
initialization stays an explicit unresolved event, not a claimed successful no-op.

Host tests pass 23/28/19/15 plus Core pools 11 and skeleton metadata 10. The actual
initialization matches the independent earlier literal reference; all 384 callback
plans/404 draw ranges still pass. Reports build/xxmi-host/{report,core-skeleton-report,
mod-plan-report}.json; logs build/core-skeleton-{host,metadata,mod-plans}.txt.
No layer/deployment/game/config/installed-asset change. Continue original spatial
identification and shared skeleton completeness before whole-character emission.

## 2026-10-13: Run original spatial identification on CPU instance windows

Added explicit resource-expression mediation and a bounded SpatialHash reader
using pinned XXMI quantization. A transitive original command-graph compiler
includes runtime callbacks; the original Core spatial state machine and pools
execute unchanged. Native fixture tests two objects, first-frame delay, custom
classification, movement, insufficient evidence, duplicate callers and expiration.
Missing/truncated/nonfinite/reserved-sentinel windows refuse. Twelve cases pass.
A pointer/member typo caused an initial host build failure and was fixed; the
verified runner now checks executable/source hashes before this fixture.

User narrowed scope to LOD0/LOD1. Recorded that decision and restricted remap
resolution to those levels while retaining the original Core pool stride. No
later-LOD assets or rendering tests are required. Source/build/next stay 0.9.3,
dist stays 0.9.1. No layer/deployment/game/config or installed-asset action.
Next: retained palette identities/completeness, native component/material effects
and whole-character command emission through the common backend.

## 2026-10-13 - Shared palette masters and owned remaps

- Continued LOD0/LOD1 single-character priority. Added a bounded CPU palette bank and generation-prepared R16 remap loading from executed original initialization; no component-specific importer.
- Verified sparse ids, cross-component completeness, separate current/previous poses, LOD priority, update retention, invalid input atomicity, identity capacity/retirement and immutable snapshots (24 native checks). Host metadata/file checks now 15; host 23/planner 28/resources 19/DDS 15/pools 11 unchanged. Original spatial 12 and 384 sample plans pass.
- Historical replay: 283 records, 74 imports, 262 incomplete snapshot refusals, 21 complete byte-identical snapshots. Five isolated frames use separate identities, not assumed animation continuity. Eight native-loaded remaps match the independent source bytes. Evidence build/shared-palettes/report.json and build/xxmi-host/core-skeleton-report.json.
- Reviewed the new sources and changed host/tests. Runtime epoch adapter, native Core/material resolution and layer integration remain next. No layer source change or rebuild required; source/build/next 0.9.3 and dist 0.9.1 unchanged. No deployment, installed-asset write, launcher edit or game action.

## 2026-10-13 - Spatial identity epochs for shared palettes

- Added SpatialPalettes without changing pinned original pool code. It consumes original slot/hash outputs and uses original distance math to validate continuity; repeated slot numbers alone never grant access to old masters. Owner reset/failure obligations are explicit.
- Rebuilt the CPU host. Original Core/palette integration passes 21 checks, including real original FIFO slot replacement; native palette checks 24, original metadata 15, historical records 283 and callback plans 384 still pass. Reports build/xxmi-host/core-spatial-report.json, build/shared-palettes/report.json and build/xxmi-host/mod-plan-report.json.
- Reviewed new sources and fixture integration. No layer source or version change; source/build/next 0.9.3, dist 0.9.1 unchanged. Next is native Core component/material execution and retained draw data integration, then DLL linkage. No installed source changes, deployment or game action.

## 2026-10-13 - Native callback execution and palette ownership through plans

- Added explicit native handlers, bounded original callback invocation and per-draw immutable palette owners through file preparation/resource resolution. Unhandled boundaries still block execution; nested failure cannot be ignored into a successful partial plan.
- Review found that pre execution may retarget the post callback. Fixed late alias resolution and added a case that distinguishes it from a stale post target. Native bridge 12, host 23/planner 28/resources 19/DDS 15/pools 11/metadata 15, original spatial 21, palette 24 and all 283 historical records/384 callback plans pass. GPU assets pass 201 with clean validation; reports build/xxmi-host/report.json, build/assets-gpu/report.json and build/shared-palettes/report.json.
- This is bridge plumbing, not completed native EFMI component/material execution. Continue those operations and layer integration offline. No layer source or installed assets changed; source/build/next 0.9.3 and dist 0.9.1 unchanged. No deployment or game action.


## 2026-10-13 - Original component entry and shared skeleton bridge

- Connected actual original TextureOverride matching, pre/post roots and original Core component callbacks to the shared CPU palette bridge. Only LOD0/LOD1, one resolved rolling-buffer instance and a prepared generation are accepted. Original instance iteration remains intact.
- Initial fixture lookup missed preserved entry-section capitalization; corrected its case-insensitive comparison. A hand-written iteration boundary later exposed an incorrect post-phase iterator state and caused a host stack overflow. Removed that boundary, retained original iteration and added a separate native command-stack budget. Neither failed result was treated as verification.
- Final host 23/planner 28/resources 19/DDS 15/pools 11/metadata 15/native bridge 14 passes. Original spatial 21, shared palettes 24 and 283 historical records pass. Full entry fixture: 566 plans, 370 draws, 283 first-frame skips, 137 mesh-empty callbacks, five isolated identity epochs and four refusal/control cases. Independent draw/entry/palette byte comparison passes; 384 isolated callback plans remain passing.
- OverrideTextures stays unresolved (283 events), registration is an explicit fixture input and synthetic repeat updates are not animation evidence. Report build/core-components/report.json. Next: material/profile resolution and shared backend integration. No layer/DLL/deployment/game or installed-asset change; source/build/next 0.9.3, dist 0.9.1.


## 2026-10-13 - Shader-specific material plans and historical coverage

- Added generic exact-fragment profiles and ordered per-draw material contracts/Ready owners. Unknown slots, aliases, incompatible image types and late readiness errors refuse atomically; source-undeclared slots and explicit nulls stay distinct. No component ids in the adapter.
- Asset GPU suite passes 221 checks with zero validation diagnostics. The rendered private-image fixture uses the new material plan. Historical audit: 21 fragment modules, 370 draws, 353 accepted contracts, six unknown LOD1 mapping refusals and eleven known 2D-for-3D refusals. Combined accepted shader patches validate. Existing fragment checks remain 16 moves and 19 null targets passing.
- Existing captures cannot identify source DX11 slots for LOD1 fragments 0aeef59b, 310c25c3 and a6d7088a. Did not propagate source slots solely from shared Vulkan image identity: slot numbers differ by source shader. Reports build/material-plans/report.json and build/assets-gpu/report.json.
- Continue vertex declarations and remaining frontend/Core/backend integration offline. No layer source/version change, deployment, game action or installed-asset write. Source/build/next 0.9.3 and dist 0.9.1 unchanged.


## 2026-10-13 - Original vertex layout rules through general stream contracts

- Wrapped the pinned original command translation unit to expose its CPU layout evaluator. Added exact-VS semantic profiles and per-draw declarations using original ordered layout rules plus shared stream patches. File strides and element formats are data, including index-only four-byte source streams.
- Native vertex tests pass 18; full original entry replay now checks all 370 vertex declarations alongside arguments, entries and bones. All 17 drawn VS modules have prior exact paired semantics. Existing host, spatial 21, palette 24/283 and callback 384 regressions pass. Reports build/xxmi-host/report.json and build/core-components/report.json.
- An added build fingerprint initially named a nonexistent util.h; the builder stopped before compilation and was corrected to actual headers. Final build/replays pass. Source declarations and installed assets remain unchanged.
- Continue native texture queries and unified resource/variant/snapshot handoff. This remains CPU frontend evidence; no actual game pipeline creation, layer source/version change, deployment or game action. Source/build/next 0.9.3, dist 0.9.1.

## 2026-10-13 - Combined retained frontend GPU transactions

- Joined Ready file draws, resolved vertex/material contracts and compact palette owners to existing asynchronous variants, snapshots and ordered emission. Late preparation/refusal is atomic; inherited advancing application streams refuse without new-index bounds proof. Snapshot append failure may consume unused arena slots but emits nothing.
- Standalone GPU asset suite passes 253 checks with clean core/sync validation. Pixel controls prove separate materials, first/base offsets, 16-bit bone indices, original FS isolation and full original-state restoration. The recording alone retains all replacement owners after producer retirement, survives sealed resubmission, and releases them after completion.
- Historical material audit remains 353 accepted, six unknown LOD1 correspondences and eleven dimension refusals over 370 draws. Text checks pass; no layer source/version change or full-suite rebuild is required for this standalone addition. Source/build/next 0.9.3 and dist 0.9.1 unchanged.
- Next: original texture-query/override mediation and runtime/profile integration. No DLL deployment, game action, launcher/configuration change or installed-asset write.

## 2026-10-13 - Original texture matching and callback mediation

- Added an explicit source-identity provider, per-plan pixel binding state, CheckTextureOverride and filter mediation. Reused original hash/draw-context matching, inclusive range resolution and callback execution. Unknown identities, fuzzy rules, missing this, unadapted callbacks and recursive command graphs refuse atomically; nested this/phase and original filter semantics are preserved.
- The initial fixture wrongly expected plain queries to run both phases at once and assumed a half-open range. Original source shows explicit pre/post selects combined execution and ranges include the endpoint; corrected fixture expectations and used those original paths. Range expression guards were also extended so resource queries in bounds cannot reach D3D scaffolding.
- Final native texture checks 21, original installed Core API synthetic-identity checks 22, spatial 21, shared palette 24/283 and full entry replay 566 plans/370 draws pass. No real identity provider is substituted into historical data; 283 unresolved material calls remain explicit. Host executable cf744cdd2673c2fd54ab809aef135ad9b9455f0590b8e0ff9523d87d7a0cf521.
- Continue source profile/runtime integration offline where evidence permits. No layer/DLL version change, game/deployment/configuration action or installed-asset modification. Source/build/next 0.9.3, dist 0.9.1; only LOD0/LOD1 are in scope.

## 2026-10-13 - Real saved pipeline recipes on the standalone GPU driver

- Added a reproducible generated-case runner joining native full-Core vertex output, accepted material contracts and saved pipeline/layout/shader recipes. Exact metadata roundtrips precede driver calls; reports retain source fingerprints, case/draw mappings and omissions. Generated/game data is not committed.
- First build missed an asset header include path. First GPU pass exposed a test-only missing derived blend attachment count (62 validation errors while driver calls returned success); production capture retained it. Fixed the reconstruction and added a pre-driver count check.
- Final result: 88 created variants covering 353 historical draws, 177 checks, zero validation errors/warnings on RTX 5060 Laptop. Six unknown LOD1 mappings and eleven dimension refusals remain. Asset 253 and full original CPU/material replay pass. The child enables supported independent blending; no game features/settings changed.
- This closes historical driver construction, not character rendering or DLL integration. Source/build/next 0.9.3 and dist 0.9.1 unchanged. Remaining source identity/correspondence and runtime integration are recorded; no deployment or game action.

## 2026-10-13 - Prepare the next source capture without entering the game

- Extended the DX11 capture helper with optional original shader archiving (bytes, SHA-256 index and explicit partial errors) and file-backed texture hashing for inactive bound slots. Existing default capture behavior remains. Archives stay in this project; differing bytes under an existing shader hash name refuse overwrite.
- Eleven offline fake-probe checks pass for disassembly failure, existing declarations, collision/path refusal, default/expanded texture selection and unreadable content. CLI help and English-text checks pass. No live probe, installed resource, launcher/configuration or game process was used.
- Documented the exact next source-data target: actual LOD1 DX11 for three unmapped fragment modules, using the two saved Vulkan LOD1 bundles first. This requires a user-initiated test; no 30-minute run is needed. LIMIT-021 and runtime/source identity/DLL implementation remain distinct unfinished work (LIMIT-022). Existing dist 0.9.1 is unchanged, and whole-character visual acceptance is not claimed.

## 2026-10-13, 12:00: Review of 4b398f8 to a3a7905 (0.9.2, 0.9.3, standalone frontend), implementation and design

Requested by the user, who also asked for a judgement of the design. 28 commits, about 11,000 lines. The game was not running; dist/ still holds the 0.9.1 deployed on 2026-10-12 (hashes equal); the launcher file holds the Vulkan values, its other differences from the last backup are the launcher's own counters. No source change in this review.

Rebuilt and rerun independently, all with the documented counts: full layer suite of HEAD in `build/review-0.9.3` (0.9.3); `scripts/probe_xxmi_host.py` (23, 28, 15, 19, 11, 15, 14, 18, 21); `check_core_components.py` (566 plans, 370 draws, entries, palettes and vertex declarations equal); `check_shared_palettes.py` (74 imports, 21 snapshots); `check_mod_draw_plans.py` (384 plans, 404 draws); `check_material_plans.py` (353 accepted, 6 unknown, 11 dimension); `check_historical_variants.py` (88 pipelines created on the driver, 177 checks, validation clean); `check_image_upload.py` (253, validation clean). The figures of the live 0.9.1 session in `docs/m2-first-replacement.md` equal the saved status files.

Read in full: the layer changes (variant builder with fragment patch, cache key, snapshot images, stream patcher), `fragment_images`, `palette_bank`, `spatial_palettes`, `component_bridge`, `draw_transaction`, `material_plan`, `vertex_plan`, `dds`; the interfaces and structure of `draw_plan`, `resource_cache`, `image_upload`. The corrected bone offset of 0.9.1 carries through: the native apply step patches through `MergedSkeleton::patch_entry`. No defect found.

Design: the build-out is the plan's architecture (hosted original engine, native built-ins only where the core touches the GPU, data-only plans, shared backend without component branches, refusal keeps the original draw) and is judged sound; using the original layout evaluator and spatial identification instead of rewriting them is the right call after BUG-005. Seven points are recorded under T-027, the new LIMIT-023 among them; the first is that 27 offline commits now rest on a rendering path nobody has seen draw a visibly changed mesh in the game.

## 2026-10-13, 12:04: Prepare the visible replacement check

The user asked for the first point of the review. The game was already running (PID 31340, started by the user at 11:57 under Vulkan, 0.9.1 with the unaltered package), so the check needs one restart: the package is read at device creation.

Looked at how component 7 is drawn before choosing the change (saved 0.9.1 frame, pipeline 368): a multiplicative, stencil-masked decal without depth write. A patch that leaves the face would be cut by the stencil, so the change keeps it on the face and makes it large: one strip scaled 3.5 times about its centroid and moved 1 cm forward, the other left alone for comparison in the same frame. Added `scripts/package_visible_check.py`; the result `build/m2-component7-visible.bin` passes the decoder of the deployed 0.9.1. Switched `dist/efmi_vk.cfg` to it after a backup. Layer binaries untouched; no sandbox run while the game is up; no launcher edit; the agent neither stopped nor started the game.

12:08: the user had closed the game; the smoke with the deployed settings and the new package passes (the script gained `--version` for binaries older than the source). The user asked whether everything could be replaced instead. Measured the sample's structure from its files to answer: six components are deleted by the mod, four are self-contained copies of the original meshes, and the costume is on components 1 and 2, which need bones of other components (figures under T-027). So the whole character needs the shared skeleton and the frontend in the DLL; that is development, not a setting.

## 2026-10-13, 12:55: Layer 0.10.0, the whole character from a static package; deployed

The user answered the two-way choice with "do B directly": whole character, built by this agent, meshes and bones first. Codex processes were running on the machine; no commit of theirs arrived during the work.

Measured before designing: the mod deletes six components, four are self-contained copies, the costume is on components 1 and 2 with bones of other components. Read the core's INI for the parts a static package cannot carry (spatial identification, the per-instance draw loop, the first-frame skip, the import bookkeeping) and checked the GPU's memory types: type 4 is device memory the host can map, the kind the game's pools use, so the 32 MB of meshes need no upload queue.

Built: `scripts/package_character.py` (draw lists from the hosted frontend's plans, skeleton from the evaluated core initialization, entry sections and the one layout edit from the INI); `layer/src/character_package` and `character_backend`; shared skeleton copies in `DrawSnapshots`; `needs_import` in `MergedSkeleton`; a pipe option to end a frame for tests that never present. Tests: native decoder (37), GPU hook scene with three components and a neighbour (49 over four runs), shared slices (13). First full run of the new GPU test passed; hardened afterwards for the game (staggered re-hash, an object already met in a frame matches only at the same position, eviction of the weakest unidentified identity, 16 identities, 64 skeleton copies per arena) and reran everything.

Offline against six saved frames: sections, vertex declarations (equal to the semantic reference for all 426 items), a model of the decision rules, vertex slot and layout facts. The texcoord strides of the mod's files equal the game's per component and LOD, so the only declaration change is the widened blend index; no per-shader profile is needed for this mod.

Deployed 12:50 after the full suite in `build/next`; suite on `dist/` and the smoke with the real package pass. No launcher edit, no game start. Not seen in the game yet.

## 2026-10-13, 13:35: The whole character is on screen; flicker at LOD 0 traced to set 3; 0.9.5

The user started the game with build 0.10.0, reported "the model is roughly fine, but LOD 0 flickers now and then" and sent a screenshot showing the mod's outfit on the character in the open world. First real-game result for the whole-character backend.

The agent first believed the game had exited (a process check cut after four lines, see CURRENT_STATE) and analysed saved frames instead: the suspicion that LOD 1 imports of the shadow pass disturb the color passes did not hold, the mismatch exists only in the two shadow passes (LIMIT-025). The user pointed out that the game was still running. The live counters then answered at once: `refused_set3` 82 in 378 frames and nothing else. The per-reason counters of 0.10.0 are what made this a one-read diagnosis.

Fix as D-036 and BUG-006. Test first: the scene with a four-set pipeline before and after the character fails against the deployed build with the same counter, passes with the fix. Renumbered to 0.9.5 when the user set the version rule. Full suite and smoke pass in `build/next`. The user then closed the game; deployed at 13:28 with a backup, hashes compared, smoke on dist/ passes. No launcher edit, no game start or stop by the agent.

## 2026-10-13, 14:28: Switch to DX11 for the user's LOD1 source capture

- Restored current memory: the static whole-character backend and 0.9.5 are now deployed, superseding this chat's earlier component-7-only state. The user explicitly requested the DX11 switch and will start the game, disable the mod and report LOD1 readiness.
- Complete process checks found no game or launcher, including an immediate pre-write check. Backed up the active launcher JSON and dist/efmi_vk.cfg to build/backups/launcher-dx11-20261013-142837/, with hashes and change.json evidence.
- Changed only the launcher's EFMI d3d11_mode_cmd_args from empty to -force-d3d11 and xxmi_dll_inject_mode from SKIP to DIRECT. The existing signed extra libraries already include the DX11 probe; both DLL paths exist. Signed values and all other launcher content are unchanged, verified by whole-JSON comparison and exact-byte readback.
- Changed only the project-owned layer mode from probe to passthrough for this DX11 session. No binary, version, mod/Core, game asset or system setting changed. No game launch, probe connection or capture occurred; no new layer regression is claimed for this configuration-only change.
- Next: capture actual LOD1 after the user's ready message with shader archiving and all-bound static texture hashes, targeting the three missing material correspondences. Keep the later 0.9.5 Vulkan flicker acceptance separate. No LOD2+ or long-duration test.


## 2026-10-13: Original LOD1 source capture and verified material profiles

- User entered DX11 and positioned actual LOD1. Initial capture still had mod meshes; retained as initial-mod-on. At the user's explicit request, probe F11 disabled the mod and restored focus. Frame 35553 has 954 game calls and two overlay calls, no XXMI indexed mesh draws. Archived/verified 212 DXBC shaders and hashed 247 static textures, no failures. Capture released; no installed file write.
- Both saved Vulkan LOD1 bundles reproduce four original-mesh pass sequences, exact target vertex declarations and eight rules for the three missing fragments. Two placed images use unique shape/format, not content. Captured source DXBC proves six undeclared slots. Evidence manifest fingerprints 452 files.
- Added the offline raw-evidence audit and per-fragment manifest resolver; updated material checks/table. Audit and SPIR-V checks pass; native replay 359 accepted/11 dimension refusals/zero unknown out of 370. Asset suite 253 and rebuilt hosted native suites pass. No layer change/deployment or new Vulkan rendering claim.
- Latest user asks to finish the next work after the whole-model replacement. Next: integrate static character textures, explicitly handle the device-specific dimension case, full sandbox regression, then user-started Vulkan acceptance. The general hosted runtime remains separate. See docs/lod1-material-capture.md.

## 2026-10-13, 15:47: Static whole-character materials, 0.9.6 deployment and Vulkan preparation

- Continued the requested whole-character work on Claude's existing mesh/bone backend. Added a bounded static material envelope from hosted per-draw pixel bindings, exact fragment profiles and original DDS paths. Shared ResourceCache, native material contracts, variants and command-owned image snapshots now run inside the layer. Whole-component refusal preserves originals; shared bones and application state restoration remain intact. No per-component material branches, later LODs, keys or full hosted runtime were added.
- Made LIMIT-021's three dimensional mismatches an explicit, device-bound package policy from the existing controlled DX11 hardware evidence. Default material selection remains strict. Historical native production contracts cover all 370 draws, eleven using the named policy; 105 saved pipeline combinations create, 211 checks, zero validation diagnostics. Actual game appearance remains unknown.
- The initial layer-entry material cases crashed in the sandbox because upload command buffers lacked loader dispatch initialization. Added the loader callback to internal allocation/setup; inject and layer cases both pass. Final full rebuilt suite passes, including native character 67, snapshots 52, character-hook 127 and all existing steps. Rebuilt assets 253 and pinned host suites also pass. No game crash or load occurred in this round.
- New package build/character/arcane-materials.bin has 20 profiles and nine DDS files alongside the unchanged mesh/bone payload. Candidate actual-config smoke through both entries uploads each file once, nine Ready, zero failures. Resident image allocation is 216,268,800 bytes in the sandbox; no game VRAM/performance claim.
- User answered the pending Vulkan switch question with an explicit request to switch back. After three stopped-process checks, backed up existing files and launcher JSON to build/backups/before-0.9.6-20261013-154745/. Deployed thirteen verified files, exact candidate-byte comparison passes. Both DLLs SHA-256 21b9be301fb8c6b80a268e4434c352a3a5d9e40e78b21a07f3609052e37dba83.
- Changed only the launcher's EFMI d3d11_mode_cmd_args to empty and xxmi_dll_inject_mode to SKIP. Whole-JSON comparison and byte readback preserve every other value and byte, including signed settings. Project config now selects probe/resources, hashing/profiling off, enabled replacement and arcane-materials.bin. Dist actual-config smoke passes through both entries; historical game log unchanged. Manifest build/deployment-0.9.6.json. Full regression ran on identical candidate bytes, not again on dist.
- Next requires the user-started game: verify loaded version, image readiness, material/refusal/set-restoration counters and whole-outfit appearance/motion at LOD0/LOD1. Include the earlier flicker fix; neither sandbox results nor old game evidence closes this acceptance. No game start/stop, installed mod write or system setting change by the agent. Reviewed source diff and synchronized memory for a local commit.

## 2026-10-13: Original model in 0.9.6 traced to queue setup; verified 0.9.7 fix

- User enters Vulkan and reports the original model. Read current PID 18868 status and log: correct 0.9.6/inject/probe load, then package initialization refuses the one-graphics-queue condition. Backend disabled/unready, zero replacements, zero image requests. Saved read-only evidence in captures/materials-0.9.6-20261013/. No game frame capture or installed file change was needed.
- Added support for multiple enabled queues in one unprotected graphics family. First active queue uploads; once all images are Ready, per-queue semaphore signals/waits publish them before any recorded replacement can execute. No draw/submit fence wait or background submission to an application queue. Queue inventory, selected uploader and publication are observable. Version 0.9.7; same mesh/material package.
- Full rebuilt regression passes (build/material-0.9.7-regression.txt): character-hook 261 checks across eighteen cases, all existing steps unchanged/passing. Added queue 1 only with unused queue 0 and separate upload/render queues, both entries and all three submit APIs. Material pixels, state restoration and resubmission pass under clean core/sync validation.
- New two-queue fixture against both deployed 0.9.6 entries reproduces exactly the game's refusal with zero textures/replacements and clean validation (build/material-queues-0.9.6-control.json). The first control runner expected the final snapshot although the fixture intentionally exits early on refusal; corrected the runner to read its saved active snapshot. This was a control-script path error, not an additional layer defect.
- Actual candidate configuration with nine real DDS files passes through both entries using two enabled queues with only queue 1 active. Nine reads/submissions/Ready, publication true, one queue wait, zero failures, 216,268,800 resident image bytes. Saved build/character-assets-config-next-0.9.7.json. Candidate DLLs SHA-256 3ff4517d29ac23cdd877b45f34f777f2146391488955d91f6eb181b40e02995d.
- User has been asked to exit normally so the DLL can be replaced. At this checkpoint dist and PID 18868 still use 0.9.6; no launcher/system/game/mod changes. Review and memory recorded for a local commit. Next: backup/deploy after exit, actual-config smoke on dist, then a user-started game check. Visual materials and the included flicker fix are still pending.

Deployment follow-up, 16:23: the user confirms normal exit, and repeated complete process checks find neither game nor launcher. Backed up to build/backups/before-0.9.7-20261013-162318/, deployed thirteen files and compared their hashes against the fully tested candidate. Actual-config smoke on dist passes through both entries with two enabled graphics queues and only queue 1 active, nine Ready images and completed publication. Report build/character-assets-config-dist-0.9.7.json; manifest build/deployment-0.9.7.json. The same DLL hash above is deployed; game log and launcher bytes are unchanged. No agent game start or launcher edit. The next step requires the user's new game session, not further offline test repetition.

## 2026-10-13, 17:20: Flicker persists with 0.9.7; traced to the visibility pass; 0.9.8 built, not deployed

- User: Codex added the textures and they seem right, but it still flickers. The game runs (PID 7784, 0.9.7, user-started); everything on it was read-only (status, forty frame captures, pipeline and shader requests).
- Counters: `refused_material` 85 in 393 frames, no other refusal. Captures: all refused character draws on four-set pipelines 399/402/404/406 in render pass 12, depth EQUAL without write, no color, fragment-written storage buffer, no images. These are the draws counted as `refused_set3` under 0.10.0; the 0.9.5 diagnosis was wrong (BUG-006 corrected, BUG-009 and ISSUE-008 added).
- 0.9.8: private set index from the layout (D-039), `Device::max_layout_sets`, restoration at the private slot, no material profile needed for a layout without fragment-stage images. Files: layer/src/pipeline_variant.cpp, character_backend.cpp, resources.cpp, state.h, common.h; sandbox/character_gpu_case.h (character draw on the four-set pipeline).
- Verification, sandbox and offline: new phase green with 0.9.8 and red against the deployed 0.9.7 (`refused_variant`); the four real vertex shaders rewrite to set 4 and pass spirv-val; full suite passes in build/next (run_all-0.9.8.out); actual settings and nine real DDS files through both entries, nine ready (build/character-assets-config-next-0.9.8.json). The first smoke used `scripts/smoke_m2_config.py` and showed five failed images: its sandbox device has no BC compression, the layer is not at fault. The running game has 1,157 three-set, 190 four-set and no larger pipelines, so slot 4 is free there.
- Not done: deployment (the game is running) and any game check of 0.9.8. Remaining: deploy after the user closes the game, asset smoke on dist, user-started session reading the refusal counters and the flicker.

Deployment follow-up, 17:59: the user reports the exit; process checks by full image list find neither game nor launcher. Backup build/backups/before-0.9.8-20261013-175909/, thirteen files copied from build/next with equal hashes, manifest build/deployment-0.9.8.json. Real-texture smoke on dist (actual settings, two graphics queues, queue 1 only) passes through both entries, nine images ready. No launcher edit, no system setting, no agent-started game. Remaining: the user's game session with 0.9.8.

## 2026-10-13: Explained the deployed workflow and XXMI participation

- User asked for the complete current replacement flow and how much XXMI participates. Checked current source/config (0.9.8), scripts/build.py, scripts/probe_xxmi_host.py, scripts/package_character.py, character_materials.py, the native character backend and the relevant backend/host documentation.
- The original pinned XXMI C++ frontend actually executes selected callback logic and Core initialization offline. Static packaging consumes one scenario's draw/resource plans and bone metadata; entry matching and the supported layout edit are also read by the packager. Mesh/remap bytes are embedded, DDS files remain referenced. Verified shader-slot correspondences supplement the plans.
- At game runtime the launcher injects the Vulkan DLL; the project's native backend matches draws, tracks identity/LOD, merges bones, prepares resources/variants, emits replacement or hide operations and restores game bindings. The DLL build includes shared asset/GPU helpers but no XXMI interpreter. Runtime Core identification follows native rules derived from Core; it does not execute the original Core INI on each draw. Dynamic source-texture queries, keys/conditions/persist/reload and full hosted integration remain open.
- Clarified current versus planned support rather than assigning an unsupported percentage. Updated the stale TASKS current-state paragraph to the already documented 0.9.8 deployment. Memory-only change; text/diff checks apply, no build, deployment, launcher edit or new game observation.

## 2026-10-13, 19:00: 0.9.8 in the game; shader variants and identity; 0.9.9 built, not deployed

- Deployed 0.9.8 at 17:59 (previous entry). The user started the game and reported: normal state apparently fine, dash flickers, looking up from below badly wrong (screenshot), later the normal state flickers too. Everything on PID 36720 was read-only: status, 30 frame captures, pipeline and shader requests, 30 captures with instance entries.
- Found: the dithered fade uses fragment shaders the package has no profile for, in every pass; 0.9.8 kept the game's mesh there. Each is a profiled shader plus one noise image with shifted bindings. Standing still nothing is refused.
- 0.9.9: layer/src/material_sibling.h/.cpp (image variables of a fragment module, rule transfer), character_materials (one profile at a time), character_backend (signatures of profiled shaders, derivation, fallback to the application's images, variant cache 2,048, identity expiry by frames and time, radius by gap, stale eviction, counters, log), common.h. Tests: sandbox/character_test.cpp (sibling checks with the game's bound images; --derived mode), character_gpu_case.h (identity gap, four-set draw in material runs, shifted-binding shader), variant_fixture.h, character_test.py. Scripts: flicker_capture.py, flicker_shaders.py, sibling_replay.py, sibling_check.py.
- Verification, sandbox and offline: 25 of 25 dither variants derive one stable rule set from the captured frames and pass the production preparation with plans equal to the sibling's; native 77; scene 277 over eighteen runs with clean validation; red against the deployed 0.9.8 for all three new phases; full suite in build/next (run_all-0.9.9.out).
- Not done: deployment (the game is running), any game check of 0.9.9, the after-images (LIMIT-027), the cause of the normal-state flicker (ISSUE-009).

Deployment follow-up, 19:00: the user reports the exit; process checks by full image list find neither game nor launcher. The 0.9.8 session log is saved with the captures. Backup build/backups/before-0.9.9-20261013-190005/, thirteen files copied from build/next with equal hashes, manifest build/deployment-0.9.9.json. Real-texture smoke on dist (actual settings, two graphics queues, queue 1 only) passes through both entries, nine images ready. No launcher edit, no system setting, no agent-started game. Remaining: the user's game session with 0.9.9.

## 2026-10-13, 19:20: 0.9.9 in the game; the frame boundary; 1.0.0 built, not deployed

- Deployed 0.9.9 at 19:00 (previous entry). The user: looking up is right, the normal state still flickers; and asked for deployment without leaving the game with the agent refreshing it (T-029, not started, needs a decision on the loading rule).
- Read-only on PID 32004: no refusals; derived rules in use; the identity log shows 16 of 19 new identities within the same counted frame as the replaced object's last draw, a few cells away. Cause: frames counted at the present, read at record time (BUG-011, D-041).
- 1.0.0: `CmdBuf::character_frame` (state.h, state.cpp), character_backend.cpp (two clocks; identify takes both), common.h; sandbox/character_gpu_case.h (two recordings without a present, the character a cell further). Red against the deployed 0.9.9, green with 1.0.0; character tests 77 and 277; full suite in build/next (run_all-1.0.0.out).
- Not done: deployment (the game is running), any game check of 1.0.0, T-029, LIMIT-027.

Deployment follow-up, 19:26: the game was closed when checked (full image list: neither game nor launcher). 0.9.9 session log saved with the captures. Backup build/backups/before-1.0.0-20261013-192546/, thirteen files copied from build/next with equal hashes, manifest build/deployment-1.0.0.json, real-texture smoke on dist passes through both entries. No launcher edit, no system setting, no agent-started game. The user decided T-029's rule: the replacement logic becomes a module, and swapping it into the running game is the agent's choice (AGENTS.md updated). Remaining: the user's game session with 1.0.0; T-029.

## 2026-10-13, 20:25: Host and module; exchange of the replacement code (1.0.1 built, not deployed)

- User: write the replacement logic as its own module; whether to hot-swap is the agent's choice. Rule added to AGENTS.md (commit b97368c). 1.0.0 was deployed at 19:25 with the game closed (previous entry).
- New: layer/src/character_module.h, command_owner.h, layer/module/module_main.cpp, scripts/hot_swap.py, docs/hot-swap.md. Changed: state.h (CommandOwner in CmdBuf; backend slot, lock, frame counter and properties in Device), m2_backend.h/.cpp (dispatch through the function table under the shared lock, staging, exchange, probe command), character_backend.cpp/.h (own command type, usable/ready, function table), draw_snapshots.h, layer.cpp (submit hooks), ipc.cpp (`module`), build.py (stamp, module target), probe_client.py (swap_module), sandbox scene and tests.
- Found on the way: the stamp must not hash its own compiler flag (layer and module got different stamps and the good module was refused); a command buffer keeps its owner across recordings, so an owner of the old backend has to give way once reset (otherwise the new backend never replaces in that command buffer again); totals of the scene are counted before the exchange because every backend starts its counters at zero; module copies need a serial in the name within one second.
- Verification, sandbox only: character tests 77 and 357; scripts/hot_swap.py twice against the windowed sandbox with the real mesh package, clean exit; full suite in build/next (run_all-1.0.1.out).
- Not done: deployment of 1.0.1, any exchange in the game, the user's check of the 1.0.0 flicker fix.

Deployment follow-up, 20:25: the game was closed when checked. Backup build/backups/before-1.0.1-20261013-202328/, fifteen files copied from build/next with equal hashes, manifest build/deployment-1.0.1.json, real-texture smoke on dist passes through both entries. The layer log had not changed since the 1.0.0 deployment, so 1.0.0 never ran in the game. No launcher edit, no system setting, no agent-started game, no exchange in the game yet. Remaining: the user's session with 1.0.1 (flicker in the normal state; first exchange).

## 2026-10-13: Review of intervening work and the user's correct-looking model/texture report

- User asks to restore memory, understand the intervening work and recommend what comes next; reports that models and textures now seem fine. Reviewed memory, the four implementation commits from 0.9.8 through 1.0.1, current module documentation and the original milestone plan. No implementation or deployment was requested or performed in this review.
- The latest game log now proves 1.0.1/inject/probe loaded in PID 27816 at 20:24:31, with the package configured and mesh uploaded. Process enumeration and probe discovery are empty at review time, so no live status, capture or exchange was attempted. No exit cause is established. Archived the 93-line latest session and its fingerprint in captures/user-review-1.0.1-20261013/.
- Retained the tentative user visual report separately from coverage not specified (movement, combat, LOD and effects). The log still has 50 nearby identity creations, 38 within zero counted frames; these are not correlated with actions or pixels and do not establish visible flicker. No module events or hot-swap ledger exist. Updated stale task summaries and preserved the open real-game acceptance items.
- Recommended next: short scoped LOD0/LOD1 movement/effect verification plus the first unchanged-code exchange, then any observed identity/after-image fault, then live original INI/Core integration with keys, persist and reload for this character. LOD2+, a duration-only test and multi-mod performance work stay outside the current scope. Documentation-only update; text and diff checks apply, no new layer regression claimed.

## 2026-10-13: Live 1.0.1 stability, first game module exchange, and requested dash check

- User starts the game and reports no apparent flicker; PID 19452 loads 1.0.1/inject/probe. Read-only LOD0 baseline: 21.031 seconds, 1,534 drawing frames, all eight sample points foreground. Matched 94,087 = replaced 49,375 + hidden 44,712, every refusal/identification/pending delta zero, nine Ready images. Six LOD1 frames then capture 58-60 character matches each and 39 supported pipelines; across 101 drawing frames all 5,896 matches are replaced or intentionally hidden with no error delta.
- Outside these clean intervals, clustered identity events and variant refusals still occur. The user confirms dashing. The retained reason is the historical missing vertex bone-buffer condition, but no draw-level correlation exists for those events; no new cause or visible flicker was claimed.
- Exercised the prior standing authorization for the first real module exchange. Verified both existing module files against the 1.0.1 deployment hash and clean implementation sources, then scripts/hot_swap.py --pid 19452 --out dist --no-build --timeout 35. Same code, version and package; no source or deployed-file change. At 20:49:41 the module takes over in 0.8 seconds, generation 1, nine Ready images, no module error. The user confirms normal appearance without a persistent anomaly. Post-exchange counters over 10.016 seconds/710 LOD0 drawing frames remain clean. The ledger records module hash/path, source commit and outcome; counters were compared only within each generation.
- User then reports that dashing also seems fine and asks for detection. Requested a short foreground dash interval and captured 58 frames in 24.156 seconds: 3,766 character matches, 98 supported pipelines, no historical after-image vertex shader. Over 964 drawing frames, refusal, pending and identification deltas stay zero, with 5,040 sibling-derived and 1,235 inherited material replacements. This supports the sampled dash; it does not fix or exercise LIMIT-027's historical no-bone path. Corrected the earlier wording that implied residual after-images were necessarily a current visible fault.
- Evidence: captures/stability-1.0.1-20261013-204259-19452/ with raw statuses, captured original draws, pipelines, module ledger copy and fingerprints. Capture records do not annotate replacement pixels. No source change, rebuild, new regression run, launcher edit or process start/stop; only the authorized unchanged-code module exchange affects the running game. T-029's first functional game exchange is verified. Next is T-027 live INI/Core functionality for this character, with the historical after-image path retained as an uncovered follow-up. Memory/docs updated and checked for a local commit.

## 2026-10-13, 22:33: 1.0.2 original main INI, keys, persistence and F10; HLSL panel compilation

- User requests the next runtime stage: original INI conditions, outfit keys, saved settings and F10. During development asks whether Vulkan can support the panel's HLSL. Continued the runtime task and verified that actual HLSL independently, without editing the installed mod/Core or launcher.
- Linked the pinned original XXMI parser/evaluator/matcher/key dispatcher into the layer and backend module. Added isolated frontend generations, native character Core boundaries, reachable resource catalogue, dynamic draw/material plans, foreground input sampling, atomic project-owned persistence, and asynchronous F10 parsing/upload/adoption. Original evaluated bone partitions and LOD maps must match the native profile. Old sealed commands retain immutable owners and a unique generation token. Latest persistent values survive adoption; old settings writers are suspended and held keys do not retrigger.
- Scope: this character's main arcane.ini and LOD0/LOD1. Thirteen listeners on ten physical key combinations, alternate stocking DDS, original branches and existing profiled BUF refresh. Full source-image override identity, arbitrary CustomShaders/includes/controllers and the auxiliary image panel remain unsupported. Native Core texture boundary accepts only the profiled mesh registry; it does not simulate unknown image hashes. See D-043 and docs/live-ini.md.
- Verification: original runtime 45 checks, including all 21 actual sections and all key listeners; real Vulkan runtime runner 28 checks across both entries and one/two graphics queues (pixels, malformed INI, bad DDS, valid reload, persistence, held F10, retained old commands and restoration). Full sandbox/run_all.py passes in build/next/run_all-1.0.2.out; core/sync validation clean. Corrected two test harness errors during development: the layer helper overwrote the validation search path, and the green pixel expectation needed the first component's original fragment-buffer multiplier. Neither was a runtime defect.
- HLSL answer with evidence: official checksum-verified DXC 1.9.2609 installed only in F:/vulkan/dxc-1.9.2609. Unchanged k/Draw_2D.hlsl compiles in both stages and validates for Vulkan 1.1, with slots 100/101/120 retained; build/panel-hlsl/report.json. No panel rendering claimed. It still needs image/IniParams upload, cursor/window queries, pipeline state and safe backbuffer drawing.
- Deployment: game and sandbox absent at two checks, no exit cause inferred. Prepared build/character/arcane-live.bin plus sidecar; settings destination build/character/settings/arcane_fes.ini. Backup build/backups/before-1.0.2-20261013-223323/. Fifteen deployed files hash-match the verified candidate, manifest build/deployment-1.0.2.json. Candidate and dist actual-config smokes each pass through layer/inject with ten Ready images on two enabled queues, queue 1 submitting; clean validation. ABI 16b90a85431f86b4 stays compatible. No game load, module swap in the game or game pixels tested in this round.
- Next: user-started game, real-character outfit/stocking keys and F10, persistent choice after F10 and later normal restart. No duration-only test or new LOD capture required. T-016 panel drawing remains separate from the successful HLSL compilation.

## 2026-10-13, 22:58: First live INI check and request for generic characters

- User reports working outfit keys and no apparent F10 change. User-started PID 31108 loads 1.0.2/inject/probe. Read-only log/status checks prove two completed F10 exchanges at 22:53:43.879 and 22:53:49.113, generation 2, no reload rejection/error/pending work, ten Ready/published images and none failed. All thirteen saved variables match the latest live state; stocking is 1, outfit switches 0 and dirty false. No pre-F10 snapshot or game restart was collected.
- Evidence: captures/live-ini-1.0.2-20261013-225835-31108/, including raw statuses, this PID's log, project settings, summary and fingerprints. In 182 drawing frames, replacements increase by 6,155 and intentional hides by 6,107. Also 504 variant refusals, 72 identifying and two incomplete draws occur, with the historical missing-buffer reason; no draw-level shader/input correlation or newly reported visual failure. Do not label the interval clean or claim all effects work.
- No input injection, source/DLL/config change, reload request, module swap or process start/stop by the agent. Explained why unchanged inputs and retained settings can make a successful F10 visually inconspicuous. Restart persistence and all-key visual coverage remain open. Documentation-only verification uses text/diff checks; no new build/regression is claimed.
- User then requests extending the present capability to arbitrary characters. Continue with a generic profile/import/runtime audit and implementation; ask for a second existing character Mod while independent work proceeds. Do not infer all-Mod compatibility or reintroduce LOD2+ and performance scope.

## 2026-10-14: Directory language and asset baseline; generic Core inputs (1.0.3)

- User broadens arbitrary-character support to the full enabled Mods directory and all original language/model/texture handling; Yvonne remains a regression input and game collection is deferred. D-044 records the shared architecture. Original installation stays read-only.
- Original Include/namespace/command/resource/key/regex inspection parses 36 installed INIs, 1,330 sections and 8,937 commands without diagnostics. Eight positive/negative directory fixtures pass. Parsing does not execute custom graphics. All 108 DDS files decode and 310 available buffer declarations align; 16 PNGs and 11 absent declared files remain explicit audit findings. Source fingerprints unchanged.
- All nine exported skeleton initializers pass; six isolated callback fixtures pass. Ardelia/Liino/Typhoeus need companion/custom operations, despite full-directory syntax passing. No missing source dependency was edited away. Reports: build/ini-folder/, build/folder-assets/, build/mod-corpus/; scope detailed in docs/mod-folder-runtime.md.
- Shared runtime maps original component ids by evaluated bone ranges and handles exact IB-only hides without fabricated identity/bones. Empty aliases preserve original null semantics. Optional source-API bone/config facts feed a native Core boundary; unknown facts stay unknown, invalid slots refuse and valid requests recover. No source slot is guessed from Vulkan descriptor coordinates.
- Yvonne's generic 462-bone/nine-component mesh package passes the production decoder. Its 14 original entry sections execute against synthetic native inputs, emitting 36 draws and six empty skips, with 23 listeners including F10. Texture-role inspection uses an explicitly limited RabbitFX fixture; production rejects it pending original shader gating and fallback semantics. No material/GPU/new-character acceptance is claimed.
- Build and entire sandbox/run_all.py pass in build/next/run_all-1.0.3-verified.out: runtime 48 plus second main INI, character package 79, character GPU 357, live INI GPU 28, validation 76 and all other steps. Earlier harness corrections: constants before key parsing, engine built-ins, actual file fixtures versus null aliases, separate settings namespace. ABI remains 16b90a85431f86b4; dist stays 1.0.2. No deployment, module exchange or game action.
- Latest user steering explicitly requires coverage from original parser source rather than the 36 installed examples. Continue grammar/command inventory and complete runtime parsing/dependency/execution adapters; pause PNG integration. Generic directory rendering, CustomShader, resource copies, ShaderRegex and source image/shader contracts remain open.

### Standalone PNG checkpoint, paused before integration

- Implemented frontend/assets/png.h/.cpp using Windows WIC, with typed grayscale/RGBA precision, default sRGB metadata, immutable decoded output, per-image bounds and shared source reservations. It is not linked into the layer or resource cache.
- scripts/check_png.py passes 28 CPU cases: seven independent valid pixel fixtures, five malformed/dimension/budget refusals preserving output, and all sixteen installed PNGs compared to Pillow. Report build/png/report.json. Original files unchanged; no GPU upload or panel result.
- User redirects priority to all source-defined INI syntax before integration. Preserve this verified standalone component and continue language/runtime work. No version/deployment change; source/candidate remains 1.0.3.

## 2026-10-14: Source-defined INI corpus and CPU directory generations (1.0.4)

- User explicitly requires reading upstream code and adapting all original syntax,
  beyond the installed examples. Added a source-derived inventory, original-parser
  fixtures and independent production-runtime expression results. Source/executable
  fingerprints prevent stale parser evidence; negative tests require structured
  diagnostics. Recognized deprecated notices are not syntax failures.
- 726 syntax cases cover all ordinary dispatch keywords, 48 operators, named
  operands, target members, resource/pool declarations, stages/ranges, CustomShader
  state, key/preset options, override metadata and regex structure. 65 expression
  results validate every operator plus edge semantics. No exhaustive system/driver
  option or graphics-execution claim is made.
- Production RuntimeConfig.mods_directory imports original Includes and compiles
  dormant explicit lists. Namespace routing, companion updates, settings confinement
  and failed reload isolation pass. Legacy explicit inputs keep reachable Core scope.
  Callback-only key/preset dependencies, preset forward references, CPU IniParams,
  hold restoration and abort are now executed through the original engine.
- Expanded Core graphs reproduced a dangling optimizer branch pointer (BUG-012);
  stable-root adaptation fixes it. Key/preset-only targets could remain empty
  (BUG-013); stable named callee discovery fixes copied cycles too. Native callback
  abort failed a new fixture, then passed after its pre/post loops were corrected.
- Initial cycle test expectations were wrong for original smart selection and
  accumulated callback lists; corrected the fixture, preserving original behavior.
  One late-injection sandbox exited normally after two frames before a probe request;
  no cause established (ISSUE-010). Unmodified modes subsequently passed; no blind
  retry or IPC change. The full original failure log remains available.
- Full rebuilt suite passes in build/next/run_all-1.0.4-final.out: parser 726,
  evaluation 65, runtime 65 plus Yvonne inspection, native callbacks 15, character
  package/GPU 79/357, live INI GPU 28, modes 40, validation 76 and all other steps.
  ABI remains 16b90a85431f86b4. Docs distinguish parser acceptance, CPU execution
  and actual Vulkan graphics support. Diff/text checks completed for local commit.
- Dist remains 1.0.2. No deployment, module swap, capture, game input, process
  start/stop or original-installation writes. Continue offline source-defined pool
  reset/description adapters and general graphics families; PNG integration paused.

## 2026-10-14: Original pool reset, metadata and mixed references (1.0.5)

- Continued the requested source-defined runtime work after committing 1.0.4 as
  073a2a5. PoolCopyOperation already implements metadata copy, proxy/index reset
  and independent element resets without a graphics API; the adapter now executes
  those original methods instead of accepting only whole-pool reference.
- Found BUG-014: any scalar write discarded all cached resource aliases in that
  pool. Reference assignment now mirrors the original is_null field; resolution
  observes element-specific reset state. Unrelated slots and both independent
  halves of mixed elements retain the original semantics. Added original
  reset_per_frame_limits execution for the selected counters.
- Eleven new pool cases first fail against 1.0.4 (preserved in
  build/ini-language/pool-before-1.0.5.json). They then pass with the fix; an
  additional counter test brings the pool suite to 23. Three production-runtime
  cases prove file-plan binding, immutable previous plans after reset and separate
  generations, bringing runtime checks to 68 plus the Yvonne main-INI inspection.
- Full rebuild and sandbox/run_all.py pass in build/next/run_all-1.0.5.out:
  source syntax 726, original expression results 65, character package/GPU 79/357,
  live INI GPU 28, modes 40, Vulkan validation 76 and every remaining step. Shared
  ABI remains 16b90a85431f86b4. Reviewed diff and text checks for local commit.
- Scope remains CPU pool semantics and owned file plans. General resource byte
  copies/clears/readback, expiring/keyed resource ownership, CustomShader, dispatch,
  ShaderRegex and full directory rendering remain open. PNG integration stays
  paused. Dist remains 1.0.2; no game actions, original-installation writes,
  deployment or module exchange occurred.

## 2026-10-14: Original INI GPU buffer command foundation (1.0.6)

- The user approves the resource -> CustomShader -> shader matching -> directory
  graphics order and asks to continue offline until a game check is needed. The
  goal remains active; this checkpoint is one implementation stage, not complete
  original-language Vulkan execution.
- Added ResourceCommands allocation, transfer, clear, readback and fence owners,
  plus GpuResourceRuntime as an opt-in extension of the production original
  parser/evaluator. Original buffer descriptions, store range calculation,
  variable output and buffer-view element calculation are reused directly.
  References, compatible copies/copy_desc, typed/structured/raw clear, immediate
  store feedback, dynamic slots, optimizer batches, pool reset and copy limits
  now have actual GPU byte-result checks. Stage slots remain logical bindings.
- New tests exposed an SRV clear mismatch in the first full run. Preserved
  build/resource-commands/view-before-fix.{json,txt} and
  build/next/run_all-1.0.6.out. Corrected view kind, format/range retention, cached
  views, and the distinction between original SRV/UAV end boundaries and CB
  region lengths. Raw-capable buffers can have typed float views too.
- The targeted fixture has 120 passing checks with zero core/sync validation
  errors or warnings through shutdown. File padding/size metadata, same-list
  branches, two independent generations, rejected candidates, retained owners,
  allocation accounting and failed-scope cleanup are covered. The second full
  suite passes in build/next/run_all-1.0.6-final.out. The frozen-source rebuild and
  full suite also pass in build/next/run_all-1.0.6-verified.out: syntax 726,
  expression results 65, CPU runtime 68, character package/GPU 79/357, live INI
  GPU 28, validation 76 and every remaining step. Diff/text reviewed for commit.
- D-045 and docs/gpu-resource-commands.md describe the explicit private outside-
  render-pass execution domain. It performs no application imports, descriptor
  emission, draw-hook waits, deployment or game operations. Dist stays 1.0.2.
  Next offline work: slot ranges and general buffer regions/properties/options,
  then images and the remaining common shader/directory adapters. PNG stays paused.

## 2026-10-14: Original buffer slot-range GPU execution (1.0.7)

- Continued the active offline goal after be9617f. Original slot-range commands
  now bind/fetch pools, broadcast resources and run per-slot compatible copies.
  Original range expressions resolve once with guarded numeric conversions.
  Plain ref fetch bypasses scalar copy counters/overrides; existing source views,
  fresh structured one-element views and constant-buffer region rules remain
  distinct. Invalid ranges poison the scope and cannot submit.
- Targeted GPU tests pass 155 checks with no core/sync validation messages.
  The initial negative-index expectation followed a misleading source comment:
  unsigned size_t modulo maps -1 to element zero for a three-element pool on
  this build. The test now checks the original result. The added CB fixture
  obtains its source through slots to satisfy original parse-time flag inference.
- Frozen-source full rebuild/regression passes in build/next/run_all-1.0.7.out:
  GPU resources 155, source syntax 726, evaluation 65, CPU runtime 68, character
  package/GPU 79/357, live INI GPU 28, validation 76 and every remaining step.
  ABI stays 16b90a85431f86b4. Reviewed diff and text before local commit.
- Dist remains 1.0.2; no game actions, original source/installation writes,
  deployment or module exchange. Next: buffer regions/options/properties, then
  images and the shared shader/directory stages. PNG integration stays paused;
  the active goal is not complete and further offline work needs no user input.

## 2026-10-14: Owned GPU copy pools and original buffer regions (1.0.8)

- After 5087006, the next GPU fixture reproduced BUG-015 against 1.0.7: ref then
  copy reused a borrowed allocation and overwrote the source. Preserved
  build/resource-commands/copy-alias-before-fix.{json,txt}. Separate owned pools
  now retain each original buffer description; copy_desc restores its previous
  allocation and bytes across size changes without mutating a source alias.
- Adapted source CB windows, CB rounding, view-less structure conversion, region
  bounds/stride truncation, raw views and no_view_cache. Slot reads use only the
  metadata returned by the source API. Unwritten cached bytes remain unchanged;
  tests make no promise about new uninitialized tails. Invalid ranges/overrides
  poison the scope. Indexed copies use the bound format width instead of a
  conflicting declaration stride. New source_stride starts as the original zero.
- Targeted GPU suite: 243 passing checks, zero core/sync validation messages.
  Full frozen-source rebuild/regression passes in build/next/run_all-1.0.8.out:
  parser 726, evaluation 65, CPU runtime 68, character package/GPU 79/357,
  live INI GPU 28, validation 76 and all remaining steps. Reviewed diff, report
  source hashes and English text before local commit. ABI remains unchanged.
- Continue offline with resource properties, including target-specific view
  metadata, null/cache and source_stride behavior, before images and shaders.
  Dist remains 1.0.2. No game action, deployment, module swap or writes to the
  original installation. PNG integration stays paused; the goal remains active.

## 2026-10-14: Buffer properties and source view/null metadata (1.0.9)

- Continued after 73efd18. A new GPU fixture reproduces BUG-016 against 1.0.8:
  plain pool fetch preserves unknown custom format, but the adapter inferred the
  stored view's typed format. Preserved metadata-before-fix.{json,txt} under
  build/resource-commands. Use target-specific view facts as the source does.
- Added owned resource identity, stride/source_stride, size, format, flags,
  offset and extent metadata queries. Original declaration overrides/sentinels
  remain distinct; pure Constants queries do not allocate. Draw offsets reuse
  original VB/IB helpers with RuntimeDraw context. this preserves its property
  and region arguments. Source hash/spatial properties remain unadapted.
- Null now retains cached resource/view owners, source_stride survives null and
  direct range fetch, and format/stride-only aliases remain unassigned. The GPU
  fixtures check these independently plus actual this-region bytes, declaration
  queries, context refusals, poisoned submissions and complete owner cleanup.
- Full frozen-source rebuild/regression passes in build/next/run_all-1.0.9.out:
  GPU resources 303 with zero core/sync validation messages, source parser 726,
  evaluation 65, CPU runtime 68, character package/GPU 79/357, live INI GPU 28,
  validation 76 and all remaining steps. Reviewed diff, exact report source
  hashes and English text before local commit; ABI stays 16b90a85431f86b4.
- Next offline increment is whole-pool assignment/remaining buffer copy options,
  then images and the shader/directory stages. Source clear does not implement a
  range loop, so do not invent one from keyword similarity. Next version 1.1.0.
  Dist stays 1.0.2, PNG integration paused, no game action or deployment. The
  requested complete directory/GPU-language goal remains active and incomplete.

## 2026-10-14: Whole-pool resource assignments and buffer modifiers (1.1.0)

- Restored memory after fc230c3, answered the user's next-step question and
  continued the active offline resource -> shader -> directory goal. Implemented
  original bare/wildcard whole-pool assignments using a retained source and a
  local destination target. Repeated commands, proxy roots, per-element counters,
  source-stride metadata reads, mixed resets and resolved null remain distinct.
- mono and set_viewport do no additional buffer work upstream. resolve_msaa
  still creates/reuses a destination but does not copy buffer bytes, even with
  copy also specified. Tests read actual scalar/range/whole-pool GPU results and
  initialized cache contents; no assertion depends on uninitialized allocations.
- The first cache expectation incorrectly treated equal byte widths as equal
  descriptions; the different source stride correctly selects another owned
  cache. Corrected the fixture to verify both retained allocations and their
  independent byte contents. No upstream or installed mod source was changed.
- Targeted suite passes 402 checks, zero core/sync validation messages through
  shutdown. Full frozen-source rebuild/regression passes in
  build/next/run_all-1.1.0.out, including original parser 726, evaluation 65,
  CPU runtime 68, character package/GPU 79/357, live INI GPU 28, validation 76,
  modes 40, MCP 47 and every remaining step. ABI is unchanged.
- Next offline audit: keyed/expiring pool ownership, retained resources after
  reassignment/expiry and original spatial frame duration. IniRuntime currently
  does not supply gFrameTime. Images, GPU parameters and shader/directory work
  remain open. Dist stays 1.0.2; PNG stays paused. No game input, deployment,
  module exchange or original-installation write. The active goal continues.

## 2026-10-14: Keyed/expiring GPU pools and CPU failure propagation (1.1.1)

- Continued offline after 8109fe5. Original FIFO/spatial pools now address owned
  buffers, including missing-key templates, optional read allocation, mixed
  elements, physical ranges, whole-pool writes, expiry and read refresh. Tests
  distinguish null pool entries from earlier retained bindings and mutable copy
  allocation reuse from retained historical contents.
- Present and draw accept a host frame duration and expose original frame_time.
  The existing character backend forwards its measured duration bounded to
  1-250 ms. Spatial radius uses original ceil scaling; invalid clocks and
  overflowing conversion refuse. The source engine still owns lookup/mapping.
- The added CPU-failure test fails before the fix: IniRuntime returns failure
  after prior GPU recording, but finish still submits that work. Saved
  build/resource-commands/cpu-scope-before-fix.{json,txt}. BUG-017 is fixed by
  notifying runtime extensions on failed Present/draw calls; active GPU scopes
  become failed even when a CPU expression caused the error. Both entry paths
  and allocation cleanup are covered. Already-submitted store work is not undone.
- Targeted and full-suite GPU checks: 485 passing with zero core/sync validation
  messages through shutdown. Frozen-source full rebuild/regression passes in
  build/next/run_all-1.1.1.out: parser 726, evaluation 65, CPU runtime 68,
  character package/GPU 79/357, live INI GPU 28, validation 76, modes 40,
  MCP 47, evidence 113 and all other steps. ABI remains 16b90a85431f86b4.
- Next is general mutable images/resource commands, GPU IniParams and the shared
  shader/directory stages. Remaining buffer properties/conversions are still
  explicit gaps. Source hash helpers have target-specific offsets and original
  cached-data behavior; audit them before adding GPU queries. Existing immutable
  2D DDS upload is not array/volume/mutable-image execution. PNG remains paused.
  No deployment, game action, module swap or original-installation write; dist
  remains 1.0.2. The active goal continues and next work needs no user input.

## 2026-10-14: Owned mutable image execution foundation (1.1.2)

- The user asked what comes next. Restored the current memory/clean a09b073
  checkpoint and continued the existing active offline-development goal. Added
  general owned 1D/2D/3D images, arrays, cubes, mip chains and actual image views.
  Descriptions use real device limits and enabled features; allocation honors
  dedicated requirements and shared resident/staging budgets. Core format facts
  are generated from the pinned Vulkan registry and freshness-checked.
- Added pitched buffer-image transfer, compatible image region copy, typed
  color/depth/stencil clear and color MSAA resolve. Per-batch layout state is
  separate from committed owners; only successful submit publishes GENERAL.
  Discard/rejected submission and fence/view lifetime have pixel/byte tests.
  Extension/multiplanar formats, sparse/imported images, cross-dimensional/block
  copies, volume slice views and depth/stencil resolve remain explicit gaps.
- Targeted tests and the full rebuilt suite pass: 563 GPU resource checks with
  zero core/sync validation messages through shutdown. The full frozen-source
  report is build/next/run_all-1.1.2.out: parser 726, evaluation 65, runtime 68,
  character package/GPU 79/357, live INI GPU 28, validation 76, modes 40,
  MCP 47, evidence 113 and all remaining steps. Final source hashes match the
  resource report. ABI remains 16b90a85431f86b4.
- These are direct owned-executor image results. Original INI image declarations,
  views/copy pools and clear/store/property behavior still need adaptation. The
  source audit in docs/gpu-resource-commands.md records relevant original helper
  order and view semantics, including the 3D depth override assigning Height.
  Typeless aliases require clear conversion by the DXGI view format, not simply
  by the Vulkan allocation format. Reuse the original CPU helper rules.
- Next is that INI adapter, then GPU IniParams, CustomShader, shared shader
  matching and complete directory graphics. No game input needed; the active
  goal remains incomplete. PNG paused, LOD0/LOD1 only. Candidate build/next is
  1.1.2; dist stays 1.0.2. No deployment, module exchange, game/launcher action,
  system registration or original installation/Mod writes.

## 2026-10-14: Restored commit context and explained current texture matching

- Read CURRENT_STATE/TASKS first, then recent commits, dist configuration, live INI documentation and production material code. Source/candidate is 1.1.2, deployed host is 1.0.2. Relevant material policy originates in b90f9e9 (guarded sibling rules); abebbaf adds live INI image choices. Later owned image primitives do not imply a deployed material-policy change.
- Verified exact-byte fragment profiles, per-section/pass sibling lookup, shared image-id precedence, unique shape fallback, complete sibling coverage, two-extra limit, fewest-extra selection and ambiguity rejection. Pass keys include attachment formats/samples, subpass, depth write and compare. Shape records view type/format and allocation extent; it is not a content hash or proof that pixels match.
- Without a verified/derived correspondence, retain application images on the mod geometry and retry later. Explicit absent/null and device-bound ZeroFile rules remain distinct. Runtime upload hashing is off and original image-hash override sections are not accepted by the deployed character registry. Prepared-contract/resource/pipeline failures can still refuse a draw; the unknown-profile fallback alone does not keep the original mesh.
- Explanation and memory updates only. No layer/source implementation edit, new build, game access, module exchange, deployment or launcher change. Existing unrelated GPU-image adapter edits remain untouched. Text/diff checks cover this documentation change.

## 2026-10-14: Original INI image execution (1.1.3)

- Restored CURRENT_STATE/TASKS, active offline goal and in-progress image adapter.
  Preserved the intervening ba636c5 material-matching documentation. The user asked
  what comes next; explained the remaining image/parameter/shader/directory order
  and continued the already authorized work without requesting game input.
- Connected original texture declarations and source CPU view/description helpers
  to owned Vulkan images. Added bounded legacy/DX10 DDS metadata and subresource
  loading using pinned DirectXTK format/footprint helpers, including arrays, cubes,
  volumes, BC formats and packed depth/stencil plane extraction. Kept original
  file/declaration metadata precedence and the source's empty Texture3D bug.
- Extended logical slots, references, copies, per-description pools, null/ranges,
  counters, properties and color resolve to images. Clear operations use selected
  DXGI view formats; reusable budgeted scratch images preserve integer channel
  bits and handle sRGB/float aliases. Native headless system D3D11 WARP checks
  establish UNKNOWN-format and typed/typeless view-family behavior. Production
  creates no D3D11 device and still requires an explicit owned queue scope.
- Tests caught BUG-018: stencil-only clear passed an invalid unused depth value
  to Vulkan. Saved the failing validation output, fixed low-level sanitization,
  and verified separate source depth clamping and original flag priority. A final
  source review caught validation of ignored 1D fields; added a failing original
  INI fixture, saved its report and restricted guards to the consumed overload.
  The earlier full run was deliberately interrupted for that fix and is recorded
  as incomplete in build/next/run_all-1.1.3-interrupted-review.out.
- Final targeted and frozen-source full rebuilt regressions pass in
  build/next/run_all-1.1.3.out: resource 1682, original parser 726, evaluation 65,
  CPU runtime 68, character package/GPU 79/357, live INI GPU 28, validation 76,
  modes 40, MCP 47, evidence 113 and all remaining steps. Resource core/sync
  validation has zero messages through shutdown; source hashes match the report.
  ABI remains 16b90a85431f86b4. Text check covers 283 project files.
- Source/candidate is 1.1.3; dist remains 1.0.2. Remaining image formats/conversions,
  buffer queries, GPU IniParams, CustomShader, shader matching and full directory
  graphics remain open. Recorded the original parameter texture/flush/dirty rules
  as the next adapter audit. No deployment, module swap, game/launcher action,
  original-installation write or system registration. PNG paused, LOD0/LOD1 only.
  The active goal remains incomplete and the next work needs no user input.

## 2026-10-14 - Integer RTV and color/depth image conversions (1.1.4)

- Resumed from the project memory and answered the user's next-step question;
  continued the active offline resource/runtime goal. No game input was needed.
- Added original integer RTV float conversion. Headless WARP comparisons cover
  19 integer formats and two typeless view cases over nine input sets. Both
  WARP and hardware truncate tested fractions; finite overflow can differ.
  The implementation deliberately follows WARP's saturation and records that
  boundary as LIMIT-029, without undefined float-to-integer casts. Initial native
  observations are saved in build/resource-commands/dx11-clear-*.txt.
- Connected single-sample R16/R32 color/depth family copies through a reusable
  resident buffer. Tests check native copy semantics, every mip/layer, partial
  rectangles and offsets for all eight compatible color formats, untouched
  texels, repeated copies, allocation budgets and fence-retained retirement.
  Instrumented execution records zero CPU maps and implicit submissions for
  repeated transfers; only the explicit finish submits their batch.
- Enforced actual enabled unrestricted-depth extension for opaque color-to-D32
  transfers. copy_desc, reverse D32 copies and both R16 directions work without
  it. A separate headless device enables the extension for arbitrary float-bit
  round trips. Source review found BUG-019: host float-depth data lacked a range
  check. Added copied-texel validation with pitch padding ignored and corrected
  the ordinary packed DDS fixture to contain valid depth. This was offline
  validation work, not a deployed-game observation.
- Targeted and final frozen-source full rebuild/regression pass in
  build/next/run_all-1.1.4.out: resource 3285, parser 726, evaluation 65, CPU runtime
  68, character package/GPU 79/357, live INI GPU 28, validation 76, modes 40,
  MCP 47, evidence 113 and all remaining steps. Resource core/sync validation
  has zero messages through shutdown; all 52 recorded source hashes match.
  ABI stays 16b90a85431f86b4. Text check covers 284 project files.
- Candidate/source is 1.1.4, dist stays 1.0.2. No deployment, module exchange,
  game/launcher action, original-installation write or system registration.
  Next is GPU IniParams publication; the audit records that the existing prepare
  callback runs after Constants, so zero initialization needs a separate earlier
  boundary. Further resource formats/options, CustomShader, shared shader
  matching and complete directory graphics stay open. PNG paused, LOD0/LOD1 only.
  The active goal is incomplete; next version 1.1.5 needs no game input.

## 2026-10-14: Original GPU parameter publication, 1.1.5

- Finished the previously unverified parameter boundary interfaces and added
  GpuResourceRuntime's persistent float4 Texture1D/SRV. Zero publication precedes
  Constants; original dirty comparisons, root ends, injected draws/dispatch and
  forced key transitions determine later uploads. Special target reads do not
  flush, and explicit logical slot unbinding survives updates.
- Corrected Constants post execution and root abort leakage (BUG-020), keeping
  nested abort and source pre/post-run semantics. Initial load failure now
  poisons an active GPU scope just like Present/draw failure. Source dimensions,
  budgets, discarded commands and retained submission/view owners are tested.
- Headless GPU tests read actual boundary values, signed-zero/NaN behavior,
  hold restoration and equal-value transitions. A native DXC compute consumer
  reads parameter views and resized arrays. This does not implement general
  CustomShader or change the application's shader bindings.
- Frozen-source full build/regression: build/next/run_all-1.1.5.out, all steps
  pass. Resources 3509, parser 726, evaluator 65, CPU runtime 68, character
  package/GPU 79/357, live INI GPU 28, validation 76, modes 40, MCP 47, evidence
  113. Resource core/sync validation is clean through shutdown; all 57 source
  hashes match. Text check: 288 files clean. ABI remains 16b90a85431f86b4.
- Source/candidate 1.1.5; dist 1.0.2 unchanged. No game/deployment/system action.
  User reiterated continuing each offline part without stopping until a game
  test is necessary. Continue with CustomShader; source include, nested phase,
  state restoration and counter rules are audited in docs/gpu-resource-commands.md.
  Installed SPIRV-Reflect SDK 1.4.321.0 under F:/vulkan, commit
  e55086b044225f9b511ae44dbf9b079d3625943f, for upcoming reflection work; not yet
  linked. Remaining resource formats, shader matching and directory graphics
  stay open. PNG paused, LOD0/LOD1 only. Next version: 1.1.6.

## 2026-10-14: Original HLSL compiler foundation, 1.1.6

- Added a read-only bounded source/include compiler preserving original macros,
  main/model-5 validation and include policies. Native DXBC reflection selects
  source slots; DXC and pinned SPIRV-Reflect supply remapped SPIR-V and metadata.
  Counter descriptors and stage/register-class ranges remain independent.
- Fixed standalone D3DPreprocess's empty-include rejection with a whitespace-only
  preprocessing representation. Native D3DCompile comparison proves the source
  accepts the empty file; recorded dependency bytes are unchanged.
- Targeted and full-suite compiler checks: 116, 33 independently valid modules.
  Actual Vulkan compute output matches native DX11 WARP and 65 float4 inputs.
  All rebuilt regression steps pass: build/next/run_all-1.1.6.out; resource checks
  3544, zero core/sync messages through shutdown, character package/GPU 79/357,
  live INI GPU 28, parser726/evaluator65/runtime68, validation76/modes40/MCP47/
  evidence113. ABI unchanged. Compiler evidence includes dependency hashes.
- Source/candidate1.1.6, dist1.0.2. No deployment, game, launcher or system action.
  General INI CustomShader execution is next, not claimed by these tests.
  Remaining flag/device limitations are LIMIT-030; architecture choice D-046.
  Continue offline immediately per user, next1.1.7; PNG paused, LOD0/LOD1 only.

## 2026-10-14: Original CustomShader owned compute,1.1.7

- Added opt-in original CustomShader parsing/execution, direct compute, nested/
  null stages, original counters and state restoration across abort/failure.
  Dormant source failure rejects a candidate; retained old generations survive.
  Native diagnostic NUL normalization preserves later DXC errors.
- Owned Vulkan pipelines/descriptors/views/samplers cover structured/raw/typed
  buffers, image arrays/storage and low-level UAV counters. Recorded dispatches
  retain distinct bindings; fences retain retired owners. Production layout
  validation gates packed float3 storage. Native WARP/Vulkan bytes agree.
- Reproduced/fixed dispatch resource-query guards (BUG-021); interrupted an
  earlier full run for the correction. Final frozen-source rebuilt regression:
  build/next/run_all-1.1.7.out, all steps pass. Resources3633, compiler117/33 valid
  modules, parser726/evaluator65/runtime68, character79/357, liveINI28,
  validation76/modes40/MCP47/evidence113. Core/sync clean through shutdown;
  66 resource and6 compiler source hashes match. ABI unchanged, text301 clean.
  D-047/LIMIT-031 record owned execution boundaries.
- Source/candidate1.1.7; dist1.0.2. No deployment/game/launcher/system action.
  Continue immediately with compute gaps, then graphics/matching/directory
  integration. No game input needed; next1.1.8, PNG paused, LOD0/LOD1 only.


## 2026-10-14: Shared memory, sampling and indirect compute,1.1.8

- Added bounded workgroup size/provenance analysis, precise constant-array sample
  pairs, indirect GPU execution/original INI adaptation, native null samplers,
  explicit host slots and shared sampler quotas. Native DX11 comparisons cover
  shared aggregates/barriers/atomics and interpolation/clamp/negative LOD.
- Corrected test-only Windows small macro collision and region spelling. Original
  indirect first-comma parsing requires a resource alias for multi-argument
  regions. Original owned UAV flags explicitly lack append/counter support.
- Full frozen-source rebuilt regression:build/next/run_all-1.1.8.out, all pass.
  Resources3720, compiler117/33 modules, parser726/evaluator65/runtime68,
  character79/357/liveINI28, validation76/modes40/MCP47/evidence113. Core/sync
  clean;69 resource and6 compiler source hashes match; ABI16b90a85431f86b4.
- Independent native audit during the suite found BUG-022 binding hazards,
  batch publication and OM restore gaps. Source/evidence:build/source-state-probe/.
  Invalid duplicate-UAV calls affect later native observations; isolate them or
  run last. An older coexistence assertion is wrong; passing the suite is not
  full source equivalence. Fix immediately in1.1.9, then graphics/integration.
- Source1.1.8; dist1.0.2 unchanged. No game/deployment/launcher/system action.
  Continue offline without stopping; no game input needed, PNG paused, LOD0/LOD1.


## 2026-10-14: Native source binding semantics, candidate 1.1.9

Continued offline without user input, as requested. Reproduced BUG-022 before
changing production code: new native buffer reference failed at 3728 checks.
Added source subresource conflicts, atomic range publication, complete attachment
validation and source OM save/restore effects. Pixel UAV assignment replaces its
whole set while compute assignment preserves other nonconflicting registers.
Native WARP also verifies larger depth targets, disjoint image mip/layer reads,
null UAV register effects, and high pixel UAV loss during complete OM binding.
Invalid native calls use isolated devices. Empty CustomShader calls are optimized
away upstream; the restore test now contains an actual command.

Old fixtures explicitly unbind writers before subsequent SRV use; all prior byte
checks remain. Fixed the incorrect compute UAV/SRV coexistence assertion. Full
frozen-source build and regression passes in build/next/run_all-1.1.9.out:
resources 4006 with zero core/sync messages through shutdown, compiler 117/33 valid
modules, parser 726/evaluator 65/runtime 68, character 79/357, live INI 28,
validation 76, modes 40, MCP 47, evidence 113, and every other step. All 71 resource
and 6 compiler source hashes match. Dist stays 1.0.2; no game/deployment action.

Continue immediately with graphics semantic linking, source state and draws,
then matching and complete enabled-directory integration. Next 1.2.0. Alphabetic
IO sorting alone is not stage linking; DX11 position-w and vertex/instance offsets
need native output comparisons. No game input is needed. PNG remains paused;
LOD0/LOD1 only.
