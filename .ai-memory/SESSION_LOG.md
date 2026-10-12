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
