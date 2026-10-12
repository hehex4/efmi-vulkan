# EFMI Vulkan - Current development state

> Last updated: 2026-10-12, general frontend callback plans verified offline. Source/build/next are 0.9.2; dist remains 0.9.1 with component 7 enabled. Shared declaration editing and ordered draw transactions pass the full sandbox suite. The standalone hosted frontend passes 23 CPU plus 28 planner checks and 384 sample callback plans; it is not linked into the DLL. The user confirmed voluntary game exit. The live 0.9.1 backend replaced component-7 draws; appearance and animation are not accepted. Continue offline T-027/D-033 toward the whole character; no separate handwritten component implementations. The stutter fix remains 0.7.3's WC reads (D-027). Protocol 1, server 0.1.0.

## General frontend draw plans (offline only)

Texture-cache checkpoint: bounded asynchronous demand loading now deduplicates declarations, leaves unused missing files dormant, prepares/records on a worker, submits only on the caller's fixed queue and publishes after worker fence polling. Shared allocation ledgers retain charges through old-generation GPU owners. Headless checks pass 57 plus five actual-cache checks and ten full DDS payload readbacks, with clean core/sync validation. The sample's ten cached images peak at 283,377,664 resident bytes, 67,108,864 staging bytes and 67,109,012 source bytes. See docs/resource-assets.md and build/assets-gpu/report.json. This remains standalone; buffer resources, material descriptors, shared character/Core and layer integration are open.

GPU asset checkpoint: a separate ImageUpload ticket now prepares/upload-transitions DDS images, withholds image/view handles until a completion fence is polled, and releases staging after completion. Native headless GPU checks pass 27; all ten actual selected textures pass complete compressed-byte upload/readback with zero core/sync validation diagnostics (build/assets-gpu/report.json). GPU blit tests distinguish linear/sRGB interpretation. It is not linked into the layer and has no upload scheduler, descriptor binding or game overhead result. Caller must serialize submission on its graphics queue and retain Ready images through all uses; no cross-queue ownership or game feature enablement is implemented. See docs/resource-assets.md.

Asset decoding checkpoint: frontend/assets/dds.cpp preserves compressed bytes, color-space formats and mip ranges without copying payloads. All 15 native decoder checks pass alongside host 23/planner 28; all ten selected actual DDS files pass independent header/range comparison. Bound asset total is 315,328,648 bytes (four 8K/64 MiB textures). Two missing DDS declarations are unreferenced in the tested callbacks, so demand loading must distinguish declaration from use. See docs/resource-assets.md. The later GPU ticket above extends this; material binding and layer integration are still open. Layer source/build/next and dist versions are unchanged.

The original XXMI command trees now produce ordered resource-binding/draw/layout requests through frontend/xxmi/draw_plan.cpp. No component id or sample asset name occurs in this adapter. Native Core boundaries stay explicit unresolved operations; unsupported commands/queries, invalid arguments and budgets reject the whole plan. Native checks pass 28 in addition to the 23 host checks. The original caller skip flag/layout are not changed. Resource declarations are borrowed from a retained frontend generation; frontend variable effects are not rolled back.

The actual sample INI (unchanged SHA-256 697d0c98378b83d3aef592a54536470bff4410993f475c1514ed701c4b6f71bd) executes all 12 draw callbacks in 16 selected scalar scenarios at both LOD values: 384 plans, 404 draw items, 192 mesh-empty runs, 18 unique index ranges and 38 bound resource declarations. Every selected range fits its actual index/vertex files. Reports: build/xxmi-host/report.json and mod-plan-report.json. Details: docs/frontend-draw-plans.md. Core initialization/shared bones, resource GPU ownership, material descriptors, real keys/persist/reload and the panel remain open. No layer rebuild was required for this standalone frontend-only change; no deployment or game action.

## Hosted XXMI frontend feasibility (standalone CPU only)

The pinned 1.2.2 sources now compile/link in a headless host and pass 23 native CPU checks: expressions/control flow/post phase, pools/expiration/aliases, callback references, duplicate-section distinctions, Region arguments, ElementFormat execution and duplicate synthetic key listeners. Reproducible builder rejects changed dependency revisions/trees, fingerprints inputs, and never executes a stale binary after failure. Report: build/xxmi-host/report.json; method and limits: docs/xxmi-host-prototype.md.

This closes D-016's bounded host feasibility checkpoint, not full INI/Core compatibility. The reference D3D-shaped headers are compile scaffolding, with unused graphics no-ops; missing local GPU adapters explicitly refuse. No graphics device, real key input, installed-mod execution or layer integration occurred. No DLL/deployment/config change; full 0.9.2 regression above remains applicable. Continue general resource/shared-skeleton/material integration offline.

## 0.9.2 shared declaration editing and draw transactions (sandbox only)

Added data-driven stream contracts (original/replacement stride and element offset/format) and ordered multi-draw transactions. The legacy M2 package delegates to these operations; its restrictive eligibility and configuration are unchanged. Contracts preserve shader locations and untouched streams, and reject incomplete/ambiguous mappings atomically. Transactions preflight all resolved items before emitting anything, allow an intentional empty skip, and restore the application's bindings once after all draws. No component identifiers are used by either operation.

Full build/suite passes in build/next: unit 88, tracker 144, bones 43, recipes 134, cache 21, snapshots 25, mesh/declarations 49, variants 39, M2 hook 45, probes 517-532, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 113. Log: build/general-draw-0.9.2-regression.txt. The GPU fixture renders two private ranges and the original next draw, rejects an entire partly ready transaction with pixel proof, and resubmits sealed storage under clean core/sync validation. Details: docs/general-replacement-operations.md. No deployment or new game request; frontend/shared character skeleton/material integration remains open.

## 0.9.1 first game replacement and direction for general mod support

User-started Endfield PID 3080 loaded 0.9.1 at 19:22:00 with the component-7 package enabled. In a 4.008-second status window, matched and replaced both increase by 806, with pending 22 and unsupported 96 unchanged, and unreadable/exhausted zero. Later counters are matched 57,751, replaced 57,502, pending 22 and unsupported 227. The grouped last refusal names primary/classic/single-instance/free-set-3/extended-binding constraints but does not identify which one failed; do not claim the whole session had no new refusals. The user confirmed voluntary exit. No runtime switch, deployment, launcher edit or automatic game start occurred.

Saved display frame 51,570 contains 46 matched draws; component 7 is draw 86/87 on supported pipeline 368. Both instance entries and both bone windows compare equal at record/submit. The 176 bundle file fingerprints verify. Pipeline 437 for component 10 draw 38 lacks the current three-set layout precondition; full-frame bone replay refused before execution, not passed. Evidence and limits: docs/m2-first-replacement.md. Captures retain the application's original draw state (LIMIT-020), so counters are execution evidence, not pixel proof.

Mesh-space inspection locates component 7 in two small strips near the eyes. Its XYZ positions equal the live original exactly; packed vertex/UV bytes do not all match. The user cannot reasonably judge this tiny part. Stop requesting microscopic visual acceptance; use a substantial character replacement at the next appropriate game checkpoint. D-033 keeps component differences in data and preserves the hosted XXMI frontend direction of D-016. Shared skeleton assembly, resource/declaration handling, textures, INI execution, toggles and panel remain work, not implemented compatibility.

The sections below are historical checkpoints; the earlier pause and no-game statements no longer describe the current state.

## Review of 0.8.1 to 0.9.0: one defect, corrected in 0.9.1 and deployed (2026-10-12, 19:25)

Requested by the user ("continue the review"); covers `c1bb6db` to `2d64775`. The game was not running.

The defect: `MergedSkeleton::patch_entry` wrote `(1536, 3033)` into constant 5 of the patched entry for the sample; the right pair is `(1533, 3030)`. The game's vertex shaders read bone `i` at `offset + 3 + 3 * i`. Evidence, each independent of the others: `MergedSkeleton.ini` passes `$merged_skeleton_offset - 3`; the DX11 baseline has `(1533, 3030, 258)` in all 23 patched vertex entries of `frame-a` and of `frame-on`; the disassembled Vulkan vertex shader of the component 7 draw (shader 111231) adds 3 before `3 * index`. With 0.9.0 the replaced component would have been skinned with each next bone's matrix. The suite did not show it because the native test, the Python reference and the sandbox vertex shader shared the reading.

Changed: `patch_entry` (one subtraction), the native expectations plus a check of the DX11 pair, the reference in `scripts/verify_bone_import.py`, the M2 sandbox vertex shader (now `offset + 3 + 3 * index`, original mesh on bone 0), `scripts/smoke_m2_config.py` (takes the expected version from the source), version 0.9.1. With the corrected fixture the 0.9.0 layer fails all four GPU configurations (`build/review-0.9.0/`, 5 of 13 checks); 0.9.1 passes.

Verified: full build and suite in `build/next` (unit 88, tracker 144, bones 43, recipes 120, cache 21, snapshots 25, variants 39, mesh 30, M2 45, 17 probe cases 517 to 532, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 113; `build/review-0.9.0/run_all-0.9.1.out`). Replay of the five saved game frames: 283 records, master hash unchanged, every patched entry `(1533, 3030)` (`build/bone-replay-yuopgr1q/report.json`). These are sandbox and offline results.

Deployed at 19:15: backup `build/backups/before-0.9.1-20261012-191540/`, eleven tested binaries copied from `build/next` and compared by SHA-256, both DLLs `d2850254ded6477c777fc1f06d5f93c8ce921f74897e2881ca996fb8b7cae417`; the settings file keeps its values, only its first comment line names 0.9.1. Manifest `build/deployment-0.9.1.json`. Suite on `dist/` without building passes (`build/deployment-0.9.1-regression.txt`); smoke with the real settings: 0.9.1, backend configured, enabled and ready, validation clean, game log unchanged (`build/m2-component-actual-config-dist-0.9.1.json`). No launcher edit, no game start.

The rest of the review found no further defect. Read in full: `m2_backend`, `variant_cache`, `draw_snapshots`, `merged_skeleton`, `m2_mesh`, the hook and tracker changes. Checked against game data: the entry is chosen by `gl_InstanceIndex`, so requiring one instance with first instance 0 and reading the entry at the start of the window is right; the 0.8.1 session used no `Bind*2` call, no command buffer reset or free, 23 command buffer allocations, and its graphics draws sit in one command buffer per frame (ids 1 to 3 seen), well inside the limit of eight arenas; the package matches `CommandList_Draw_Component7` of the mod (four buffers, `drawindexed = 108,0,0`); binding 4 is carried over as T-022 asked. The independent rebuild of 0.9.0 passed with Codex's counts before any change. One limit recorded: a capture does not mark replaced draws (LIMIT-020).

What the next session has to watch, besides appearance: `status.replacement` should show `replaced` growing with `unsupported`, `unreadable` and `exhausted` at zero or explained by `last_reason`. Only component 7 is replaced, with the game's own textures; the mod's index list for it equals the game's (written as 16-bit it hashes to `a3fe5c47`) and the INI calls the mesh a copy, so a correct replacement should look like the original and a wrong one shows as a broken part; the vertex positions were not compared. `{"cmd":"m2","enabled":0}` on the pipe returns to the original draws for later recordings.

## 0.9.0 deployed and verified; paused at the user's request

The user reported normal game exit and then requested a pause after configuration. Verified no Endfield or sandbox process before backup and again before copying. Backed up all old dist/ files with verified fingerprints to build/backups/before-0.9.0-20261012-185849/. Copied the twelve exact candidate files, including cfg, and checked each against build/candidate-0.9.0.json. Both DLLs SHA-256: 791e7d163c82562acc8732947d615b5caa38530d42ef67b18e618a02d7259d00. The separately referenced project-owned component package is unchanged, SHA-256 318e8354297fcdfd4b352d0ec031a6b2ba6472335fefe0be2c1b16f3eed52167. Manifest: build/deployment-0.9.0.json; source commit: 592247d.

Deployed actual-config inject smoke passes with backend configured/enabled/ready, correct ordinary capture/hash/rewrite, normal sandbox exit and clean core/sync validation. The actual game log hash is unchanged during the smoke. Report: build/m2-component-actual-config-dist.json. The earlier complete regression applies to these identical binaries; no rebuild or repeated full suite. Settings: probe/resources on, image hashing/profiling off, explicit component-7 package enabled, full unmap/WC defaults retained. No launcher edit or game start.

Work is paused. On an explicit resume, the next game checkpoint is a user-started display/LOD0 observation, then actual LOD1: check loaded version, ready/replaced/refusal counters and appearance/animation. Only component 7 is enabled, not the full mod. Do not begin collection or further development while the pause stands. Sections below record earlier checkpoints.

## 0.9.0 first hooked component ready for deployment after game exit

Implemented an opt-in, bounded component package and hooked replacement. It checks the exact index region and supported state, requests a variant asynchronously, imports both poses from mapped source bytes, appends private VS data and emits/restores one indexed draw. Pending/invalid/unsupported requests keep the original. Completed command buffers reuse their arena after a successful legal begin/reset/pool reset; sealed recordings survive resubmission unchanged. Workers and recorded owners release before device destruction. See docs/m2-component-hook.md, T-026 and D-031.

Native mesh checks pass 30; actual-hook runner 45 covers both entries and RenderPass 1/2 with 30 pose updates, original next-draw restoration, reset/pool reuse, live switching, sealed resubmission, unmapped-bone fallback and remap recovery. GPU core/sync validation is clean. Full build/regression passes: unit 88, tracker 144, bones 42, recipes 120, cache 21, snapshots 25, variants 39, M2 45, 17 probes 517-532, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 113. Logs: build/m2-integration-regression-20261012.txt (initial test-only CRC initialization failure), build/m2-integration-regression-final-20261012.txt (corrected native executable and all steps passed).

The unchanged sample component-7 bytes are in build/m2-component7.bin with source/INI fingerprints. Production decoder accepts the package: 40 vertices, 108 indices, bones 266-277 of 499. All ten matching draws across five saved display/LOD0/LOD1 frames meet its draw/declaration conditions (build/m2-component7-capture-check.json). Candidate actual-config smoke passes under validation with normal sandbox exit, backend ready, correct ordinary hashes/capture/rewrite and unchanged game log (build/m2-component-actual-config-next.json).

Candidate cfg: probe, resources on, image hashing/profiling off, m2_package = F:/efmi vulkan/build/m2-component7.bin, m2_enabled = 1; full unmap/WC defaults retained. Default source behavior remains no replacement without explicit package/enable settings. No game restart, launcher edit or deployment yet. Real-character appearance/animation, actual game variant creation and overhead remain unverified; only component 7 is implemented, not the full mod or its textures/image panel. The next run should begin in display/LOD0, then actual LOD1, with counters and an optional live off/on comparison. No duration-only or repeated bone capture is needed. Entries below are historical checkpoints.

## 0.8.3 asynchronous variants and private snapshots verified in the sandbox

VariantCache bounds/deduplicates owned requests, builds outside the drawing thread, retains failed results, cancels retired jobs and joins on shutdown. Recorded command owners retain ready pipelines. DrawSnapshots preallocates aligned per-draw bone/entry slices with full uniform-range padding; seal disallows further writes/recreation. Native cache 21 and snapshot 25 checks pass. The six positive headless GPU configurations use both production helpers, poison input arrays, retire/stop the cache before drawing via retained ownership, and resubmit immutable commands; pixels/restoration and core/sync validation pass. Three negative canaries remain active.

Full build/regression passes (build/variant-cache-regression-20261012.txt): unit 88, tracker 144, bones 42, recipes 120, cache 21, snapshots 25, variant runner 39, 17 probe cases 517-532, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 113. Scope: docs/m2-cache-and-snapshots.md. No live hook integration or game replacement yet; dist/ untouched. Continue implementing the minimal self-contained sample component and command-recording resource ownership.

## 0.8.2 CPU bone import verified offline

Added a bounded, atomic rolling-buffer importer and 256-byte instance patcher, preserving Core's leading source matrix, 512 implicit identities, current/previous slices, remap direction, mesh scale and finer-LOD update priority. Probe hooks do not invoke it. Native tests pass 42 checks. Independent Python replay over the saved display, two LOD0-world and two actual LOD1 frames matches production C++ after every one of 283 records (74 imports, 209 repeats, 407,808-byte master). This is not comparison with a captured DX11 compute-output buffer or a rendered character. Details: docs/m2-bone-import.md and D-030.

Full build/regression in build/next passes (build/bone-import-regression-20261012.txt): unit 88, tracker 144, bones 42, recipes 120, variants 37, 17 probe cases 517-532, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 113. No deployment or new game observation is required for this helper. Continue the M2 backend.

## 0.8.1 actual LOD1 capture succeeds (2026-10-12)

User-started Endfield PID 34644 loaded 0.8.1 probe/inject with resources on, image hashing/profiling off. The user explicitly kept LOD1. Both collections select their first attempt after skipping 8/7 empty present intervals: frames 19506/20414, 818/819 graphics draws plus 60 dispatches each, 35 render passes. All nine LOD1 sections are present, with 47 LOD1 draws (25 color-subpass draws) plus 11 unchanged base-section draws. All 36 matched recipes are complete/preflight-supported. In each frame all 58 entries and both actual VS 12 KiB bone windows agree at record and submit; no missing relevant snapshots. All 147 bundle file fingerprints per frame and the series manifest fingerprints verify.

Evidence: captures/m2-0.8.1-20261012/lod1-a/attempt-01 and lod1-b/attempt-01; report: docs/m2-game-recipes.md. T-023 is closed for this observed scene; exact present producer remains unproven. This supplies LOD1 color-pass record-time availability, not replacement or animation proof. No further game action is needed for offline bone import/remapping. Keep building in build/next while the game holds dist/0.8.1. All earlier entries below are historical checkpoints.

## 0.8.1 deployed after normal exit (2026-10-12, 17:36)

The user reported normal exit; game absence was checked before backup and again before copying. Backed up old dist/ files to build/backups/before-0.8.1-20261012-173645/. Copied the eight exact tested package files and verified every SHA-256 against build/next; both DLLs are 7e311a9f9f358aa50d90c579abfc1384877a8b391aea8ef886d1e87a90ba50d7. Manifest: build/deployment-0.8.1.json. Deployed actual-config inject smoke passes (build/capture-wait-actual-config-dist.json), including the new snapshot wait, five correct hashes before/after rewrite, full recipe and unchanged actual game log. No rebuild in dist/ and no repeated full suite was needed for byte-identical files.

Settings remain probe, resources on, image hashing/profiling off; full unmap/WC defaults retained. No launcher change or game launch. Next requires the user to enter the actual LOD1 state again, then collect with --wait-draws --snapshot mod --follow instance --require-section '*_LOD1' --require-color --attempts 8 --pipeline-limit 128. The recipes and scene corrections from this round are already saved. No LOD0 recapture or duration-only test; LOD1 color-pass bone timing and game effectiveness of the capture fix are still open.

## 0.8.1 capture-window fix: candidate verification before the deployment above

T-023/D-029: capture(wait_draws=True) retains record-time snapshot work across empty present intervals, returns one interval containing draw commands, and reports how many empty intervals were skipped. It does not merge interval contents/statistics, change normal tracking or enable replacements. Default capture remains unchanged; original timeout stops pending snapshot work. Older layers are explicitly refused for the new option. CLI supports --wait-draws; the MCP wrapper does not yet expose it.

Full build/regression passes in build/next: unit 88, native tracker 144, native recipes 120, variant runner 37, 17 probe cases 517-532, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 113. Log: build/capture-wait-draws-regression-20261012.txt. Native tests reproduce empty intervals during recording, including upload/dispatch and a later buffer write; GPU tests cover snapshots through both entries/all descriptor modes. Actual-config inject smoke passes (build/capture-wait-actual-config-next.json), with the real game-log file's hash now recorded and unchanged during that smoke. Candidate DLL hash for both entries: 7e311a9f9f358aa50d90c579abfc1384877a8b391aea8ef886d1e87a90ba50d7.

Next: after observing normal game exit, back up dist/, deploy the exact tested package with the same probe/resources/hash/profiling settings, verify it, then ask for a user-started LOD1 observation. Use --wait-draws --snapshot mod --follow instance --require-section '*_LOD1' --require-color --attempts 8 --pipeline-limit 128. No repeated LOD0 capture or duration test is needed. Game LOD1 bone timing is still unverified; sandbox success does not close it.

## Current 0.8.0 game evidence and corrected LOD labels (2026-10-12)

The user initially entered LOD1, then returned to LOD0 after the agent misinterpreted empty captures. Preserve that correction: display is LOD0; world-rendered-1/2 are the user's LOD0 state, with seven LOD1 sections only in depth passes. They do not establish distant LOD1 color coverage. After explicitly returning to LOD1, lod1-recipes/attempt-01 captures all nine LOD1 sections including color passes. Details and evidence limits: docs/m2-game-recipes.md.

Display: 45 matched draws, 39 complete/supported recipes. LOD0 world: 61 draws, 45 recipes; all 61 entries and both VS bone windows equal at record/submit in two frames, including 14 LOD1 depth draws. Actual LOD1 world: 58 matched draws, 36 complete/supported recipes, but no buffer snapshots. All matched exports exist; unrelated omitted world pipelines are explicit. Current-session VS rewrite/spirv-val checks pass 21/21/19 modules for those scenes.

T-023: 0.8.0 ends capture at the next present even if that interval contains no scene draws. A 40-status LOD1 sample has 30 empty intervals, while total draws increase; no-snapshot captures obtain graphics, but 36 full-snapshot attempts yield none. The exact present producer is unproven. The bounded collector preserves attempts and distinguishes depth-only matches from color-subpass coverage (109 sandbox evidence checks at that checkpoint). The opt-in wait is now implemented and verified as the 0.8.1 candidate above. No game restart or DLL replacement has occurred.

Correction: the 0.8.0 temporary actual-config smoke's historical-log check used the wrong filename and compared missing-file values. It did not prove the log unchanged; the recipe/settings/hash checks remain valid. The script is corrected for future runs. See docs/m2-game-recipes.md.

## M2 owned pipeline variants and restoration: sandbox verified, 0.8.0 deployed (2026-10-12, 16:34)

Continued independent development after restoring memory and deploying 0.7.5. Added full owned classic RenderPass 1/2 and graphics pipeline recipes, C++ VS-only buffer re-pointing, a private pipeline/layout builder and a direct indexed-draw helper that restores pipeline/vertex/index state. The sandbox now proves replacement geometry with RGBA16_UINT bone index 257, uint32 indices and nonzero instance offsets while FS reads the original buffers; the next original draw works without application rebinding. Core/sync validation is clean in both render-pass versions through no layer/layer/inject. Private creation objects are rebuilt from owned metadata on a worker after the source shader/render-pass handles die. Details and limits: docs/m2-pipeline-variants.md, D-028, T-022.

The layer's draw hooks still only observe. No variant request queue/cache, in-flight replacement resources, sample asset/bone import, texture patching or real-character rendering exists. New pipeline exports include recipe, layout_recipe and CPU-only variant_preflight; complete/preflight is not a driver build result. Unknown extensions/flags/layouts fail explicitly. The initial variant uses static vertex input and requires no new game device feature.

Full build and regression in build/next passes: unit 88, native tracker 126, recipe native 120, variant runner 37 (9 headless GPU processes), 17 probe cases 514-529, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 78. Log: build/m2-recipes-regression-20261012.txt. All 21 historical character VS rewrites match the Python reference exactly and pass spirv-val (build/variant-shaders-l0syviji/report.json). These are sandbox and historical offline results, not this round's game evidence.

Game absence checked twice before replacing dist/ with the exact tested package. Backup: build/backups/before-0.8.0-20261012-163359/. Both DLLs SHA-256 6e95b328e5ef019ad3e6d025cad966f2e3b326b97cd34d2b0270155610d73f3d. Actual-config inject smoke passes on candidate and dist/; full pipeline recipe, five correct hashes before/after rewrite, historical game log unchanged (build/m2-actual-config-next.json, build/m2-actual-config-dist.json). dist/efmi_vk.cfg: probe, resources=1, image_hash=0, profile_tracker=0; unmap defaults and WC fix retained. No game/launcher launch or launcher edit.

Next: user starts Vulkan with the sample character visible, preferably display then world. Confirm loaded 0.8.0/settings, collect with --pipeline-limit 128 --snapshot mod --follow instance, inspect every matched pipeline's recipe/preflight and LOD1 bone/instance evidence. Historical captures have no full depth/blend/render-pass recipes and cannot answer this. Stop here for that observation; no duration-only or 30-minute run is needed. M2 is not complete.

Reviewed on 2026-10-12, 16:47 (second agent, at the user's request): independent rebuild and full suite pass with the same counts, the 21 shader rewrites verify again, the deployed hashes and the launcher's Vulkan values are as stated, and reading the recipe, variant and re-pointing code found no defect. Only the capture at creation time and the CPU-only preflight run in a game process. Note for the integration in T-022: most character pipelines declare a fifth vertex binding that the tracker does not keep.

## Deployment of the verified 0.7.5 package (2026-10-12, 15:52)

The user requested continuation after restoring memory. Endfield was no longer running. Backed up dist/ to build/backups/before-0.7.5-20261012-155227/ and copied the tested build/next binaries without rebuilding. Both DLLs have SHA-256 f331ff7c3a390affbc7adfe617a1e1364f9d5441821b86f2ecdabf4ff39c7475. The cfg comment now states the restored full transfer-source default; settings remain probe, resources/hash off and profiling on.

Full suite on dist/ with --no-build passes: unit 88, native 126, 17 probe configurations 504-519, modes 40, limits 174, timing 126, validation 76, MCP 47, evidence 78 (build/deployment-0.7.5-regression.txt). Actual-config inject smoke passes, including unmap_snapshot_all=true and five hashes before/after rewrite (build/tests/sandbox-2c8cp1sr/). The game log is unchanged. No game/launcher launch or launcher change. M2 development can continue offline; the 0.7.5 deployment itself does not require another duration-only game test.

## Game session with 0.7.4: stutter gone, the review's attribution refuted, first open-world frames (2026-10-12, from 15:28; started by the user)

Loads in the real game and data captured in the real game. PID 27188 under Vulkan; the log confirms 0.7.4 probe/inject at 15:28:07 with resources and image hashing off, tracker timings on, and the copy at unmap restricted to staging buffers. The user reports that the stutter seems gone and then ran the open-world route. Full account: `docs/unmap-snapshots.md`; evidence in `captures/pacing-0.7.4-20261012/` and `captures/open-world-0.7.4/` (not committed).

- Between two status reads 87,005 present intervals apart (about 12 minutes, the run included): 24 over 16 ms, 3 over 33 ms, 2 over 50 ms. The 0.7.2 foreground sample had 203 / 51 / 17 in 5,093. Last 2,048 intervals at the second read: p50 8.4 ms, p99 10.6 ms, max 14.5 ms. The two are not the same conditions (the 0.7.4 span includes background time; two attempts to catch a foreground window before the run failed), so no foreground-only figure for 0.7.4 exists.
- Copy at unmap in that span: 52,639 buffers, 27.9 GB, 7.7 s, that is 3.6 GB/s against 0.33 GB/s over the 0.7.2 session. Same volume, eleven times faster: the streaming loads of 0.7.3.
- `passed_over` is 0 of 144,103: the staging-only rule of 0.7.4 skipped nothing. The mesh pools are mapped the whole time. The section below, written before this session, attributed the copies to the pools; that was wrong. It read the `generation` counter as a count of copies at unmap although tracked buffer copies increment it too, and it drew conclusions from an inventory of 251 live buffers about a session that had created 280,000.
- Open world, three captures: about 1,190 draws in 35 render passes, one recording thread, two command buffers, four submits; 428 instanced draws with up to 675 instances; one indexed indirect draw; hash source `mapped` for every direct indexed draw (1,154 to 1,185), none without a hash; 19 of the mod's 21 sections match, the 7 LOD1 ones in render pass 5 with two draws each. A capture takes 208 ms round trip.

Source 0.7.5 restores the default to every whole transfer source; stale-shadow invalidation, counts.unmap, set and pinned test settings stay (D-027). It passed the full suite in build/next and was later deployed after game exit, as recorded above. The 0.7.4 measurements show the two unmap rules behaved identically in that session; 0.7.5 itself has not been loaded into the game.

T-020 is closed on this evidence; T-021 is withdrawn. Open: nothing about the stutter. The copies still move about 28 GB in twelve minutes that nothing reads in this game; removing them is not attempted.

## Review of 0.7.1 to 0.7.3 and layer 0.7.4 (2026-10-12; its attribution to the mesh pools was WRONG, see the section above)

Asked by the user to review the three layer versions made for the stutter and to say whether it is solved. It is not known to be solved: 0.7.3 never ran in the game, and the review found that it does not reach the main cost.

- The code of 0.7.1 (present intervals), 0.7.2 (copy savings, tracker timings) and 0.7.3 (streaming loads for write-combined mappings) was read in full and is correct; nothing in it was changed. The diagnosis that copies at unmap under the exclusive resource lock overlap the largest intervals stands.
- New from the same evidence (`captures/pacing-tracker-work-20261012/buffers.json`): 22 pool buffers of 16 MiB (some 8 MiB) with usage index + vertex + storage + transfer, on memory type 4, carry 2,612 generations, 1,491 / 573 / 511 on three of them; the 76 staging buffers on memory type 2 carry 87 generations and 7 MiB in all. The old rule copied every buffer with the transfer-source bit up to 16 MiB, so the gigabytes were whole mesh pools copied each time the game unmapped them while streaming. The display scene does not stream, which is why the first three sessions were smooth.
- 0.7.3's own measurement: type 2 improves from 24.0 to 1.3 ms per 8 MiB, type 4 from 226 to 151 ms. The pools are type 4. Whether its helper is faster on the game's pool pages is unknown; it cannot be relied on.
- 0.7.4: at unmap only transfer-only buffers inside the mapped range and within the limit are copied; any other buffer the mapping touches loses its shadow knowledge (it may have been written unseen; this also closes a case where a stale shadow could be served). `unmap_snapshot_all` restores the old rule; `set` changes both unmap settings in a running process; `counts.unmap` reports copied, passed over and dropped. Description: `docs/unmap-snapshots.md`; D-026.
- Given up: a hash for a draw whose pool is unmapped at the moment of hashing (it is now `null` with source `none`). All 578 hashes of the three game bundles came from the mapping, but in the display scene. The open world is unmeasured (T-021).

Evidence, sandbox only: the full suite against `build/next` (unit 88, native tracker 126, 17 probe configurations of 504 to 519 checks, modes 40, limits 172, timing 126, validation 76, MCP 47, evidence 78), the same suite against the copied files in `dist/` without rebuilding, and a smoke run in `dist/` with the settings file as the game reads it (0.7.4, probe, inject, resources off, profiling on, staging-only copies, five correct hashes before and after the rewrite, `set` works; `build/tests/sandbox-ep3rst7t/`). `dist/` holds 0.7.4, both DLLs SHA-256 `d51f072b9af7eeda527536d4b6a6b1aabda5004cef1d06df9595508a15350535`; the files it replaced are in `build/backups/before-0.7.4-20261012/`. `dist/efmi_vk.cfg` keeps the diagnostic settings of the last sessions (probe, resources and image hash off, profile_tracker on), with the unmap settings at their new defaults. No game process has loaded 0.7.4; the game log is unchanged.

Next requires the user: the same open-world route under Vulkan. Order of work in that session is in `docs/unmap-snapshots.md`: counters, tracker timings, present intervals against the 0.7.2 sample, a few captures for the hash sources, and only with the user's agreement a short switch back to the old rule.

## Layer 0.7.3: mapped-read fix after measured 0.7.2 stalls (confirmed in the later 0.7.4 session)

User-started PID 41044 (14:22:01) loaded 0.7.2 probe/inject with resources/hash off and profiling on; the user reports renewed stutter. Initial background data is not a valid pacing comparison. After explicit readiness, the 14:44:14.810-14:44:38.840 foreground sample has 240/240 active polls, 5,093 intervals, 203/51/17 over 16/33/50 ms and a retained 129.125 ms peak. No captures or image-hash work. Unmap tracking copies 3.46 GiB and accumulates 11.625 seconds of wall time; the peak overlaps three 43-44 ms unmaps, with a zero-byte copy-record call waiting about 44 ms. res_mx is exclusive during the snapshot and shared during copy recording. This strongly identifies a contributor, not every spike's cause. Histories roll over; missing overlap is not exculpatory. Evidence: captures/pacing-tracker-work-20261012/, 21 fingerprints verified; report: docs/pacing-tracker-session.md.

0.7.3 checks the mapped range's OS page policy once at map time. On SSE4.1 CPUs, PAGE_WRITECOMBINE reads use bounded streaming loads with fences; cached/unknown ranges retain memmove. Unmap snapshots, record fallback, submit priority and buffer visibility are preserved. Added status.mapped_read and buffers[].mapped_write_combined. In a separate headless Vulkan process on this GPU, an 8 MiB type-2 upload copy falls from 24.023 to 1.270 ms; cached type 3 is unchanged, type 4 remains expensive (226.258 to 150.952 ms). All bytes match. These are local copy timings, not a game speedup. D-025 / BUG-004.

Full build/regression passes: unit 88, native 118, 17 probe configurations 503-518, modes 40, limits 148, timing 126, validation 76, MCP 47, evidence 78. First build caught a JSON writer method typo; corrected before the successful suite. Log: build/mapped-read-regression-final-20261012.txt. Native tests cover WC page boundaries/alignment, cached fallback, map offsets/remap, submit priority and complete unmap snapshots.

Observed game exit and checked again before backup/replacement. Deployed the exact candidate from build/candidate-0.7.3-20261012/; prior files are in build/backups/before-0.7.3-20261012-150100/. Both DLL SHA-256: 4cb8fdc1c111530cdbc8d76c9c2924c1cf61ec6fa701ba64a18edc5b36548c85. Actual-config inject smoke passes on candidate and dist/ (build/tests/sandbox-wcxt2ysn/, sandbox-zg_0n_36/), five correct hashes and WC mappings reported. Game log unchanged; no launcher edit or automatic game launch/stop. Diagnostic settings stay probe, resources/hash off, profile on; 16 MiB snapshots and shadow_usage 0x40 retained.

Next requires the user: start through the same Vulkan launcher setup and move along the same open-world route. Confirm loaded 0.7.3 before status-only sampling. A short clean foreground comparison is sufficient; no repeated 30-minute run. T-020 / ISSUE-005 is not closed, and 0.7.3 has no real-game result yet.

## Layer 0.7.2: earlier buffer-copy savings and optional tracker timings

Two confirmed unnecessary costs are removed without changing readable fallback behavior: record-time copies first check source visibility before allocating byte storage; submit writes borrow a readable source under res_mx and copy straight into the destination shadow instead of using a temporary vector. Submit-time contents still take priority; unmap snapshots and record-time fallback remain. An unreadable 64 MiB native-test source now allocates no fallback/destination byte storage. This is not yet a game stutter fix.

Optional profile_tracker = 1 adds timings for buffer unmap, buffer copy recording, buffer submit, submit assembly, command reset and draw/dispatch recording. Default off; clocks/counters can perturb timing when on. A bounded 64-event history retains operations of at least 2 ms, with timestamps on the same QPC clock as present peaks' new host_end_us. Excludes forwarded driver calls, includes tracker lock wait/descheduling; nested/concurrent totals must not be added. Details: docs/tracker-work.md, D-024.

Built into build/next and ran the full suite. The first run failed only a new timing assertion because the ordinary sandbox never unmaps its staging buffer; exercising its existing rewrite path fixed the scenario. Final full run passes: unit 88, native 101, 17 probe configurations 503-518 each, modes 40, limits 148, timing 126, validation 76, MCP 47, evidence 78. Logs: build/tracker-work-regression-20261012.txt and build/tracker-work-regression-final-20261012.txt.

The game had exited by deployment; checked twice before replacing files. Backed up dist/ to build/backups/before-0.7.2-20261012-132253/, then copied the exact candidate package. DLL SHA-256 for both entries: f8b30ce440856995f0c7776b1adf4196d541e651b4cd4705f3f9fde6e394fde6. Candidate actual-config smoke (build/tests/sandbox-bbni7c7u/) and deployed actual-config smoke (build/tests/sandbox-_5s9b_x_/) both confirm 0.7.2 probe/inject, resources/hash off, profiling on, five correct draws and unmap measurements. No settings environment overrides; historical game log unchanged. Unmap snapshot limit 16 MiB and shadow_usage 0x40 remain enabled. No launcher change or automatic game launch.

The requested 0.7.2 comparison has now completed (section above): stutter persists and large unmap snapshots overlap the largest peaks. Its deployment/configuration details here are historical. Passthrough remains the observed smooth fallback, requiring a process restart.

## Passthrough comparison: user reports smooth movement (2026-10-12, 13:04 launch)

User-started PID 14804 began at 13:04:17; the log confirms 0.7.1 passthrough/inject at 13:04:19.597, resources and hashing off, Vulkan device created, seven exports redirected. No probe pipe exists. The user reports the stutter is gone. Evidence: captures/pacing-passthrough-20261012/; follow-up in docs/pacing-resources-off-session.md. This is a subjective game result, without a host/GPU/display timing trace. It implicates probe-only work as a group; it does not identify one routine or prove all Vulkan workloads smooth.

Offline follow-up is now built and verified as 0.7.2 above. This passthrough result remains the control; the individual source of the probe-mode stalls is still unconfirmed.

## Resources off: spikes persist; passthrough prepared (2026-10-12, 12:57)

The user restarted the game and reports continuing stutter. New PID 33204 started at 12:50:29 and confirms 0.7.1 probe/inject, resources off, hashing off. All tracked images/descriptors/retained shaders, hash counters and capture sequence remain zero. Buffer shadowing, draw records, call counters and frame assembly still run.

The first observation was mostly background; the second had an early focus transition. Kept only its final 30.000261 seconds (12:57:21.745-12:57:51.745), with 300/300 foreground polls and all seven endpoints foreground: 6,766 intervals, 276/74/26 over 16/33/50 ms, largest retained 105.000 ms at frame 53703. Final window p50 2.887 ms, p99 36.888 ms. Host timings are not GPU/display times; differing world workloads prevent attributing the larger counts to the settings change. Evidence: captures/pacing-resources-off-20261012/; report: docs/pacing-resources-off-session.md. T-020 / ISSUE-005 stays open.

After measurement, backed up the cfg to build/backups/before-passthrough-20261012-125909/ and changed only mode to passthrough. PID 33204 still uses probe until it exits; nothing was hot-switched or restarted. Actual-config inject smoke passes with no pipe, 546 frames and normal exit (build/tests/sandbox-m_8zuprp/). DLL unchanged at 729d914e1f4a1ac66ae26ee2d421707d898b7426e52c756a5997cec50dfe0b7e. Next: a short user-started same-world movement comparison in passthrough. It removes remaining draw/buffer tracking and probe service, but keeps injection/setup/present accounting; it is not a no-DLL baseline. No interval data can be queried without the pipe.

## Hash-off comparison: open-world spikes confirmed (2026-10-12, 12:38-12:39)

User-started PID 31124 loaded 0.7.1 in probe/inject mode; resources on, image hashing off. Initial display readings were taken while the game was background, and the user said its mostly static character cannot reveal the problem. Switched to user-confirmed open-world movement: nine status reads over 39.992 seconds, 400/400 foreground polls, no captures, 11,681 host present intervals. Counts over 16/33/50 ms: 142/27/12. Largest interval 111.204 ms at frame 69089; final window p50 2.989 ms and p99 24.590 ms. Hash work and capture count remain zero. Evidence: `captures/pacing-hash-off-20261012/`; account: `docs/pacing-hash-off-session.md`.

Spikes remain with hashing off; resource/descriptor tracking has not been isolated from game/driver/presentation work. A supplementary 20-second start/end-only observation has a focus confound (78/200 foreground polls) and is not a clean comparison. No layer/config/launcher change or game launch/stop occurred during measurement. No stutter fix is claimed.

After preserving the observations, prepared `dist/efmi_vk.cfg` with resources = 0 and image_hash = 0 for the next user-started process. Previous cfg: `build/backups/before-resources-off-20261012-124818/`. The running PID 31124 retains resources on; no hot switch or restart occurred. Actual-config inject sandbox passes with resources/images/descriptors off, draw capture and pacing available (`build/tests/sandbox-a_60l8x8/`). Next: compare the same foreground open-world movement after restart; the resource-off configuration is not yet game-verified.

## Third probe: texture identities resolved; stutter remains open (2026-10-12)

User-started Vulkan PID 19264 loaded 0.7.0 at 12:05. Captured frame 22563 in `captures/third-probe/`: all 12 LOD0 components, 45 matched draws, all 114 pipelines exported, no query or descriptor gaps. Verified every one of the manifest's 172 file fingerprints. Pairing with the saved DX11 baseline gives 19 placed and 37 undeclared mod slot assignments, none open; all four former ambiguities resolve by content. Metadata profile: `docs/arcane-texture-slots.json`; full evidence and pipeline creation requirements: `docs/third-probe-session.md`.

The user describes intermittent drops during loading and steady display. Upload processing accumulated about 8.1 seconds for 2.55 GiB; a later 3.000533-second sample advances 360 presents with only 596 microseconds added to that timer. This establishes costly loading work, not the cause of every spike. 0.7.0 lacks per-present timing. T-020 remains open; do not claim a game-tested performance fix.

After collection, set `dist/efmi_vk.cfg` to `image_hash = 0` for the next process (old cfg: `build/backups/before-image-hash-off-20261012-121047/`). The user then confirmed normal game exit. Confirmed no target process remained, backed up the old package/log/settings to `build/backups/before-0.7.1-20261012-122110/`, and copied the exact tested 0.7.1 package to dist/. DLL SHA-256: `729d914e1f4a1ac66ae26ee2d421707d898b7426e52c756a5997cec50dfe0b7e`. Launcher settings were not touched.

0.7.1 adds a fixed 2048-entry host present-interval ring, recent p50/p95/p99 and peaks, lifetime threshold counts, and hash/capture tags (`docs/frame-pacing.md`). No per-frame allocation or resource lock; sorting occurs on the probe thread outside the frame lock. It measures host boundaries, not GPU/display latency. Full build/regression against build/next passes: unit 88, native 85, all 17 probe configurations (503-518), modes 40, limits 148, timing 94, validation 76, MCP 47, evidence 78 (`build/pacing-regression-20261012.txt`). A separate dist/ inject smoke test reads the actual cfg without mode/hash/resource environment overrides: 0.7.1 probe, resources on, image hashing off, five captured draws and host timing available (`build/tests/sandbox-un722puo/`). Game log unchanged.

The planned hash-off comparison and subsequent resource-off comparison have since completed (sections above); both retain open-world spikes. No repeated 30-minute run. M2 now has the sample texture table and pipeline requirements, but no replacement rendering has been implemented.

## DX11 baseline of the sample mod (2026-10-12, 10:09; game under DX11, started by the user)

Data captured in the real game under DX11, PID 9264 (`Forcing GfxDevice: Direct3D 11` in `Player.log`), with XXMI's `d3d11.dll`, the DX11 probe 0.1.6 of the sibling project and this project's DLL in passthrough (it redirected the exports of `vulkan-1.dll` and saw one instance of another component; nothing else). One full frame, 30344, with constant buffer contents: `captures/dx11-baseline/frame-a.*` (not committed). Taken and summarised with the new `scripts/dx11_baseline.py`, which uses the sibling project's client as a library without writing there, and paired with `captures/first-probe` by `server/efmi_vk/dx11_pair.py`. Full account: `docs/dx11-baseline.md`. A second frame of the same session (`frame-b`, 150191), taken with the script's own `capture` command, has the same XXMI figures (113 calls, 12 and 45 compute runs).

- 346 calls, 113 by XXMI: 56 indexed draws of six mod sections and 57 dispatches of the core's two compute shaders. 18 of the 21 objects both frames contain pair draw for draw (41 draws); component 0 is drawn 7 times under both APIs.
- Instance entry: 16 constants; constant 3 the translation (-300, 300, -300), constant 5 (bone offset, previous bone offset, 258, 1.0f). The game's values (504633, 502851) become (1533, 3030) in the 256-byte copy the mod draw uses. Same structure as read under Vulkan. The second compute step ran 45 times; the Vulkan frame has 45 draws of the 12 original components.
- Under Vulkan the instance window is set 2 binding 0 in 38 of 38 paired draws; under DX11 it is `vs-cb1`, `vs-cb2`, `vs-cb3` or `vs-cb6` depending on the shader. The bone buffer (8,413,184 bytes on both sides) is a storage buffer of set 0 at binding 7, 9, 18 or 19.
- Texture slots: no common order, no names on either side, and the table belongs to the Vulkan fragment shader: one DX11 pixel shader is drawn with two Vulkan fragment shaders that place the same slot at different bindings. Component 1 main pass: `ps-t15` diffuse is set 1 binding 6, `ps-t16` light map binding 2, `ps-t17` normal map binding 5, `ps-t18` binding 3.
- Vertex input: identical except the blend indices (mod RGBA16 uint in a stride of 16, game RGBA8 uint in a stride of 12); the mod's indices are 32-bit.
- Under DX11 only the vertex stage gets the merged skeleton and the patched entry; the pixel shader keeps the game's and reads them. One Vulkan binding serves both stages (T-019).

Mods off and on again (10:29, after the user restored the game window): the DX11 probe pressed EFMI's F11 once, `frame-off` (170064) was saved with EFMI's `Mods Display: Disabled` in it, a second press gave `frame-on` (170102) with `Mods Display: Enabled`. The mods are on as before. No other key was sent.

- With the mods off the game draws the 12 components 7, 4, 4, 5, 4, 2, 3, 2, 2, 4, 4, 4 times; the Vulkan frame has the same numbers. All 45 draws pair; the instance window is set 2 binding 0 in 56 of 56 paired draws.
- The pairing now uses only the slots a shader declares (the declarations are read from the running game and saved as `<label>.shaders.json`), since DX11 leaves earlier bindings in place.
- Slot table over the four frames: 111 entries for 36 Vulkan fragment shaders, no contradiction. Of the slots the mod's draw sections set, 15 are placed, 4 are one of two (T-018), 37 are not declared by the shader of that pass.
- The mod assigns its slots in every pass. For component 0 this puts its diffuse map over a 1x1 volume texture in one pass and its normal map into the diffuse slot of render pass 24; that is how it looks under DX11, so the translation has to be by slot and pass (fact 20).

A third pair of F11 presses at 10:40 gave `frame-off-tex` (249580, mods off; the mods were confirmed on again afterwards) with content hashes of mip 0 of the 65 file-backed textures of the scene (`--textures`; DX11 half of T-018). In that frame every component is drawn once more than at 10:29, so it pairs with nothing of the mod and is used for the hashes only: pairing by draw count needs the same state of the scene.

Scope: one scene, one character, the mod's own key states as found, two sessions (window sizes differ, so screen-sized textures do not pair).

Checks of this round: `sandbox/unit_test.py` 75 of 75 (31 for the pairing), `scripts/check_text.py` clean. The layer was not changed and not rebuilt; the full suite was not run.

## Texture content hashes and pipeline creation inventory: layer 0.7.0 in `dist/` (2026-10-12; sandbox only)

T-018 is built. The layer hashes mip 0, layer 0 of an image when a buffer copy covers the whole level, at submit, rows without padding, and reports state, CRC-32C, size and upload count with every sampled image of a descriptor; the status counts uploads per outcome, bytes and time. `scripts/dx11_baseline.py summarize` pairs textures by content first (`--textures` names the DX11 hashes) and lists where each slot the mod sets lands (`mod_slots`: placed, open, no counterpart, undeclared). With the existing Vulkan bundles, which carry no contents yet, the result is unchanged: 15 placed, 4 open, 37 undeclared.

For M2 the same build counts what the game's graphics pipeline creation uses (`api_usage.pipeline_creation`) and reports per pipeline its flags, subpass, dynamic states, specialization, set layout ids and push constant ranges. Description: `docs/probe-resources.md`, section 0.7.0; data path: ARCHITECTURE.

The sandbox uploads its texture three times (a 2x2 part, the whole 4x4 level packed, the whole level with padded rows); the value reported is the CRC of the last one as the sandbox computes it itself.

Evidence, sandbox only: the full `sandbox/run_all.py` against `build/next` had two failures outside the layer (an expectation of `mcp_test.py` that still said 1x1, and a start-up race of the harness, ISSUE-004); both tests passed there after the corrections, and the full suite then passed against `dist/` after the build there: unit 88, native tracker 78, 17 probe configurations (503 to 518 checks each), modes 40, limits 148, timing 84, validation 76, MCP 47, evidence 78; all passed. `dist/efmi_vk_layer.dll` and `dist/efmi_vk_inject.dll` are 0.7.0, SHA-256 `8c5657ea2f81ba9ed89714448577fb7f1dcb6824360fbc57dd8dff45fe9cb1ca`; `dist/efmi_vk.cfg` is `mode = probe`. No game process has loaded 0.7.0.

What a game session has to show: that content hashes of the DX11 side and of the Vulkan side meet at all (the DX11 values are of mip 0 as read back; the Vulkan values of mip 0 as uploaded), what the hashing costs while a scene loads (22,150 image uploads were counted in the second session), and what pipeline creation uses. The command sequence is in `docs/first-probe-session.md`, third session.

## Vertex-stage bindings for the pipeline variant decided (2026-10-12; offline, from game data)

T-019 is decided as D-022. `scripts/stage_usage.py` on `captures/second-probe-all` and on `captures/first-probe` gives the same result: 45 draws of the 12 components, 26 programs, 21 vertex shaders; every vertex shader has exactly one storage buffer and it is the bone buffer; all read the instance window at set 2 binding 0; 20 draws have a fragment shader that reads the bone buffer, 29 one that reads the window; three sets are bound. The variant therefore re-points the vertex module's two variables to bindings 0 and 1 of set 3 and leaves the fragment module alone. `server/efmi_vk/spirv.py` has the reference implementation (`buffer_variables`, `repoint`); applied to the 21 real vertex shaders it changes four words each and `spirv-val` accepts every result. Offline evidence only: no pipeline was created from a rewritten module. Unit tests 84 of 84, text check clean; the layer was not changed.

## Second probe session in the real game: G3 b and c (2026-10-12, 11:10 to at least 11:35; started by the user)

Loads in the real game and data captured in the real game; nothing drawn, nothing verified on a character. PID 36420, Vulkan (no forced DX11 in `Player.log`), layer 0.6.0 by the launcher's injection (`efmi_vk_layer 0.6.0 loaded into Endfield.exe, mode=probe, entry=inject`), XXMI's `d3d11.dll` not injected. 120 frames per second, working set 11.8 GB and flat, `shaders_not_retained` 0, `unknown_writes` 0. Bundles `captures/second-probe` (45 draws of the 12 components with contents) and `captures/second-probe-all` (all 210 indexed draws with contents, all 117 pipelines); about 1,100 further captures for series. Full account: last section of `docs/probe-snapshots.md`.

- G3 b, instance entry: equal at record and at submit in 3,882 of 3,882 comparisons; same structure as under DX11 (constant 3 translation, constant 5 the two bone offsets, 258, 1.0f).
- G3 c, bones: final at submit (240 of 240 windows equal a read 1 to 3 ms later); a component's own palette is complete when its first draw is recorded (component 4: 55 bones equal in 492 of 492 windows), the bytes behind a palette are not (one bone more differs in 21 of 60 frames, always in the shadow pass, always the current offset).
- The bone count per component is in the mod (`VertexGroupCounts`: 45, 86, 42, 15, 54, 16, 8, 12, 80, 117, 23, 1; 499 in all) and equals what the game's meshes use, 12 of 12.
- The palettes are adjacent in a ring buffer, about 1782 elements per frame, in an order that changes between frames.
- All other uniform ranges of sets 0 and 2: 29,967 comparisons, none different. Selection by hash at record time never failed.
- The DX11 pairing was repeated against this session's frame with every pipeline: 36 fragment shaders by SPIR-V hash, 118 entries, none contradictory, same result for the mod's slots.

D-021 follows: D3 holds for this scene without a GPU read-back. Open: other scenes (open world, combat, several instances, LOD1), T-018.

## Contents at submit and the record-time comparison: layer 0.6.0 in `dist/` (2026-10-12; sandbox, then the game)

T-017 is implemented. A capture can be asked to keep, for the draws whose region hash is listed (or for all indexed draws), every uniform range of their sets as it is at `vkQueueSubmit`, the first bytes of one chosen range as the draw's own entry, and windows of the draw's storage ranges located by 32-bit values read from that entry. Each item also carries a comparison with a read taken inside `vkCmdDrawIndexed`: `equal`, `different`, `other_range`, `unreadable` or `not_recorded`. Kept bytes are bounded by a budget (32 MiB by default) and fetched with the new `snapshot` command. Description: `docs/probe-snapshots.md`; data path: ARCHITECTURE.

For the game the rule is set 2 binding 0, values at byte 80, 16 bytes per element (`scripts/collect_probe.py --snapshot mod --follow instance`): the instance entry, the two bone offsets of its constant 5 and 12,288 bytes of the bone buffer at each. The command for the session is in `docs/first-probe-session.md`.

The sandbox now gives each uniform slot 96 bytes with a frame number written before recording, a second one written for odd slots after recording and before submit, and two element numbers into its storage ranges, so that both comparison outcomes occur in every frame.

Evidence, sandbox only: full `sandbox/run_all.py` against `build/next` and again against `dist/` after the build there: unit 75, native tracker 78, 17 probe configurations (485 to 500 checks each, with secondary command buffers and all four descriptor modes), modes 40, limits 132, timing 84, validation 76, MCP 46, evidence 78; all passed. `dist/efmi_vk_layer.dll` and `dist/efmi_vk_inject.dll` are 0.6.0, SHA-256 `dfbe6fb440d261b99ff649acfe92e822188fd4d41c9f3d108ef49197437dd5fa`; `dist/efmi_vk.cfg` is `mode = probe`. The game loaded it at 11:10 the same day (section above).

Not covered: non-indexed and indirect draws, texel buffers, push constants; bytes written between submit and execution; what a compute pass produces. The MCP tools do not expose the snapshot yet (the client and the collector do).

## Machine back in its Vulkan configuration (2026-10-12 10:43; game and launcher closed by the user)

After the DX11 session the user closed the game and the launcher and had this project switch back: `d3d11_mode_cmd_args` from `-force-d3d11` to empty and `xxmi_dll_inject_mode` from `DIRECT` to `SKIP`, two lines, copy of the file before the change in `build/backups/launcher-config-before-vulkan-20261012-104327.json`; the signed library list was not touched. `dist/efmi_vk.cfg` is `mode = probe` again. `dist/` held layer 0.5.1 at that point; 0.6.0 replaced it later the same day (section above). The next launch through the launcher is a Vulkan session with the probe, which stays the user's decision.

## Machine set up for the DX11 baseline (2026-10-12 10:01; historical, reverted at 10:43)

At the user's request this project switched the launcher's EFMI profile back to DX11: `d3d11_mode_cmd_args` from empty to `-force-d3d11` and `xxmi_dll_inject_mode` from `SKIP` to `DIRECT`, two lines, with the launcher and the game not running and a copy of the file saved in `build/backups/`. `dist/efmi_vk.cfg` was `mode = passthrough` for that session, because the launcher injects this DLL under DX11 as well. The launcher log also narrowed BUG-001: XXMI's `d3d11.dll` under Vulkan is the cause, not the DX11 probe.

## First probe session in the real game (2026-10-12, 09:47 to at least 09:55; started by the user)

Launcher configuration of run 3 (Vulkan, XXMI DLL injection mode Skip, only `efmi_vk_inject.dll` injected). `Endfield.exe` PID 8920, layer 0.5.1 in probe mode with full resource tracking. The character of `arcane fes 1.2` was on screen in what looks like a character display scene (objects placed around (-300, 300, -300)). Everything below was read through the probe pipe; nothing was written to the game. Evidence, local and ignored by Git: `captures/first-look/` (status and call counters twice, 5 s apart), `captures/first-probe/` (collector bundle of frame 30630: 84 files, `frame.json` 4.7 MB, 74 shaders, manifest `complete_with_gaps` because 68 pipelines were over the limit of 48), `captures/g3bc/g3bc.json` (two more captures with live buffer reads).

**The probe in the game.** The DLL arrived 4.8 s before `vulkan-1.dll`; 93 of 127 exports redirected; 5 instance creations and 1 device creation seen; no passive call. About 120 frames per second over a 5 s interval with full tracking (possibly the game's cap; no figure without the probe was taken). Process working set 11.5 to 11.6 GB, private 8.4 GB, flat over 7 minutes; the passthrough run of 2026-10-11 had 11.2 GB after 6 minutes. Probe holdings: 22.9 MB of SPIR-V for 1106 shaders (budget 512 MiB, nothing dropped), 2888 live descriptor sets, 6472 images, 22 MB of shadow. No crash, dialog or disconnect was reported by the user in these minutes.

**G2, recording and submission (answered for this scene).** One recording thread. Per frame: 2 command buffers, 4 submits, 28 classic render passes, no secondary command buffers, no dynamic rendering; 238 to 244 draws (215 to 221 indexed, 23 non-indexed, none indirect) and 47 or 48 compute dispatches, all outside render passes. All draws and dispatches of the captured frame are in one command buffer and one submit. Maxima since start (loading included): 3 threads, 8 command buffers, 30 submits. `Player.log` says `jobified=0`. Entry points in use: `vkQueueSubmit`, `vkCmdBeginRenderPass`, `vkCmdBindVertexBuffers`, `vkCmdBindIndexBuffer`, `vkCmdDrawIndexed`; `vkCmdDrawIndexedIndirect` and `vkCmdDispatchIndirect` have been called earlier but not in this scene.

**Descriptor path.** Per frame about 190 `vkAllocateDescriptorSets`, 188 `vkUpdateDescriptorSetWithTemplate`, 2 `vkResetDescriptorPool`, 281 `vkCmdBindDescriptorSets`: sets are allocated and written through core 1.1 templates every frame and released by pool reset. No ordinary `vkUpdateDescriptorSets` and no push descriptors in the interval, although `VK_KHR_push_descriptor` is enabled. All sets of the captured frame were tracked; 1.7 MB of descriptor contents, nothing omitted.

**G3 a, index hashes (passes).** `arcane.ini` has 21 overrides with a hash. All 12 LOD0 entry points (components 0 to 11) match draws of the frame by region hash, first index and index count; the 9 that do not match are the LOD1 entries, which are not drawn with the character close. Every one of the 215 indexed draws was hashed from the application's own mapping (`src: mapped`); no shadow was needed and `unknown_writes` is 0. Geometry lives in 16 MiB pool buffers (usage vertex, index, storage, transfer) in host-visible memory that stays mapped; 5 of them serve the frame, all with 16-bit indices and non-zero bind offsets, as under DX11. Each component is drawn in up to four passes (per pass: 10, 11, 17 and 7 character draws in render passes 5, 8, 21 and 24).

**G3 b, instance config (strongly indicated).** Every character draw binds, as set 2 binding 0, a dynamic uniform buffer of exactly 65536 bytes (4096 constants, as EFMI checks under DX11) in a mapped scratch buffer that alternates between two buffers from frame to frame. A live read right after a capture gave, for 8 of 12 components, floats 12 to 14 = (-300, 300, -300) and entry 5 = (bone offset, second bone offset, 258, 1.0f).

**G3 c, bones (strongly indicated).** The vertex shaders of the character use a storage buffer of 8413184 bytes (buffer 37, mapped) besides the instance config. At `entry5.x * 16` and `entry5.y * 16` bytes it holds rows of four floats that form 3x4 matrices: an orthonormal 3x3 part and a translation of about (-300, 301.4, -300), the same for both offsets up to the fourth decimal. This is the layout EFMI's bone importer reads (`src_offsets.x + 3 + bone * 3`), with x and y as two skeletons (current and previous frame).

Why b and c are not "passed": the reads were taken after the frame, from buffers the engine rewrites every frame. In the first capture 4 of 12 components already read as zeros or other data, in the second capture 11 of 12. The values that were read are consistent with each other, but only a snapshot taken by the layer at submit can show the bytes a draw really used, and nothing yet measures whether they are in place when the draw is recorded. Indirect support: all per-frame data reaches the GPU through mapped memory (no buffer copies and no unknown writes in a steady frame).

**G3 d, textures (open, with one decision forced).** In the main colour pass the material textures of the character are in set 1 (for component 1: six textures, BC7, BC7 sRGB and BC5 at up to 2048 x 2048 plus two small RGBA8 lookups); set 0 holds 31 pass-level textures. The depth-only pass has none in set 1, the pass at render pass 8 has two. None of the 74 exported shaders contains a single `OpName`: the game's SPIR-V is stripped, so mapping by resource name (first choice of plan decision D7) is not possible. The mapping to DX11 registers t12 to t18 needs the modded DX11 baseline (T-008).

**G3 e, vertex streams (as expected, to be compared with DX11).** Four bindings with strides 16, 12, 12, 16 in one pool buffer, binding 3 at the same offset as binding 0. Attributes: position R32G32B32 float plus one float in binding 0; R32G32 float and R8G8B8A8 snorm in binding 1; R16G16B16A16 unorm (weights) and R8G8B8A8 uint (indices) in binding 2. The mod's blend stream has stride 16 with 16-bit indices, so a pipeline variant with other vertex input is needed, as plan decision D8 assumes.

**G3 f, draw shape (passes for this scene).** Direct `vkCmdDrawIndexed` with instance count 1 and first instance 0 for every character draw.

What this session does not establish: long-duration stability; behaviour in the open world or in combat, with several instances, LOD1, or indirect draws; any overhead figure; anything about rendering a mod.

## Review before the first probe session; layer 0.5.1 in `dist/` (2026-10-11; sandbox only)

The 0.4.0 / 0.5.0 work of the sections below was reviewed before any of it goes into the game (session log, D-017). The suite passed as recorded and nothing the tracker reports for the sandbox was found wrong. What was changed is how the probe behaves in a process much larger than the sandbox:

- SPIR-V is kept up to `shader_budget` (512 MiB by default), not without bound; hash and size stay for every module.
- Destroying a module or pipeline, and resetting a descriptor pool, no longer scan every retained shader or every descriptor set.
- A capture keeps at most `descriptor_budget` (64 MiB by default) of descriptor contents and counts what it left out.
- `resources = 0` is a tracking level between full probe and passthrough.
- Mesh pipelines, a collector crash on an unknown shader stage and inline modules under reduced tracking were corrected.

Verification: whole suite against `build/next/`: Python 44, native tracker 78, the 17 probe configurations unchanged, modes 40, limits 100 (new, `sandbox/limits_test.py`), timing 84, validation 76, MCP 46, evidence 52. The tested files were copied to `dist/` (DLL SHA-256 `ce676f264ae4345535fda649766aa23f34a5241a6a9f90123d88ae3a4d009ca8`); the 0.5.0 package is in `build/backups/before-0.5.1-20261011-230051/`. A sandbox run against `dist/` with the settings file as it is (inject entry, push templates, two threads) reported 0.5.1 in probe mode and captured five draws with matching hashes; the historical game log is byte-identical before and after.

Not established: anything about 0.5.1 in the game. The cost of full tracking there is unknown (LIMIT-014); `docs/first-probe-session.md` says what to read first and how to fall back. The numbers of the sections below that mention 0.5.0 describe that version.

## First-probe exporter ready; game-data boundary reached (2026-10-11)

`scripts/collect_probe.py` now saves raw frames, API counters/status before and after, INI fingerprints/matches, G2 summaries and bounded retained shader/pipeline exports. Explicit PID/target labels prevent treating a sandbox capture as game evidence; output cannot overwrite an earlier bundle. Missing post-capture objects and pipeline limits are explicit gaps; a manifest with file SHA-256 values is written last. The collector never launches or injects anything.

Sandbox verification: 52 checks through both entries, push templates and secondary buffers, including failed-query retention, evidence hashes, wrong-scope and overwrite refusal. All current M0/M1 offline prerequisites (T-007/T-012/T-015 and capture preparation) are complete. The next backend decision needs real G2/G3 data; see `docs/first-probe-session.md`. Dist remains 0.5.0 probe with the corrected sandbox; real-game loading of this version and real-character capture remain unverified. Historical run 3 remains accepted initial G1 evidence; no repeated 30-minute run is required.

Final regression against `dist/` passes: Python 44, native tracker 62, all 17 probe configurations, modes 40, timing 84, core/synchronization validation 76, MCP 46 and evidence export 52 (`build/final-offline-regression.txt`). Native tests now write only to their own `build/tests/` log. A test startup line appended to the historical game log was identified and preserved separately, and the exact original bytes were restored; the corrected native run passes 62 checks without changing that log (BUG-002). DLL and sandbox hashes match the tested `build/next/` copies.

## Compatibility inventory complete (2026-10-11; static evidence)

T-007 is complete: docs/compatibility-inventory.md plus the metadata-only compatibility-inputs.json pin 46 Core files, 86 mod files, game binary/content indices and the matching XXMI source. The local 1.2.2.0 DLL exactly matches the official v1.2.2 archive; source commit a7847eae82aaac32175c9a7d199673d5c9323bcd. Game engine version is 2021.3.34.0; no marketing/content version label was recovered, so exact selected-file hashes identify the build. The reproducible inventory check matches all saved metadata.

Corrections: arcane.ini configures 13 slots but has entry points for ids 0-11, and the folder also loads k/1.ini, an interactive HLSL image panel. Complete folder compatibility must include it; it was missing from the original inventory. D-016 retains XXMI engine hosting behind an explicit adapter after static dependency analysis; no compiled host prototype is claimed. Remaining backend decisions require real G2/G3 observations, not more duration-only loading tests.

## Vulkan validation (2026-10-11; sandbox only)

T-015 is complete. Built Khronos VVL 1.4.321 under `F:/vulkan`; the regression suite now requires it. Core/synchronization validation passes 24 positive configurations and three negative canaries: 76 checks. Fixed the sandbox's read-only SPIR-V decoration and shader cleanup; added upload barriers and per-image presentation semaphores. Layer DLL stays 0.5.0, unchanged.

Full suite passes (unit 44, native 62, 17 probe configurations, modes 40, timing 84, MCP 46); final targeted validation passes 76/76. The corrected sandbox is in `dist/`. Repeated 36 benchmark runs: at 2000 draws the median probe increment is 0.386 ms primary / 0.744 ms secondary. These supersede the earlier numbers below. Details: `docs/validation.md`, `docs/probe-performance.md`. An initial VVL run wrote a 36-byte default system temp cache; subsequent child caches are confined to project test directories and that file remains unchanged. No registry/launcher/game change. Next: T-007 inventory, then the real G2/G3 data boundary.

## Many-draw performance measurement (2026-10-11; sandbox only)

T-012 is complete. The sandbox accepts `--draws`, records raw host timings with configurable warmup/sample counts, and `sandbox/benchmark.py` compares no layer, passthrough and probe through the inject entry with rotated trial order. The layer binary is unchanged at 0.5.0. Timing output separates FIFO waiting from recording/submitting. At 2000 draws the median probe increment is 0.424 ms (primary) / 0.553 ms (two secondary recording threads); this does not establish game overhead or the M2 5% target. Full method, limitations and fingerprints: `docs/probe-performance.md`; raw evidence: `build/tests/benchmark-owy1jn92/report.json` (36 runs).

The 37-draw timing/capture regression passes 84 checks. The full suite against `build/next/` passes (Python 44, native 62, existing 17 probe configurations, modes 40, timing 84, MCP 46). Test baselines now clear inherited layer activation; failed startup also cleans up its process. The tested sandbox is copied to `dist/`; the DLL, cfg and historical game log are unchanged. Next: T-015 validation and T-007 inventory.

## Templates and push descriptors (2026-10-11; sandbox only)

The user requested continued development without entering the game. T-011 now decodes descriptor update templates and direct/template push descriptors. Template definitions retain their layouts and copy their entries; offsets/strides, arrays, buffer/image/texel descriptors and immutable samplers share the ordinary-write decoder. Core/KHR and maintenance6 entry points feed the tracker. Unknown templates and unsupported layouts still discard stale knowledge.

Push contents are immutable per recording update, retained by each draw and separate across command buffers and graphics/compute bind points. Partial writes retain compatible elements; disturbance/reset discards them. Immutable samplers must be explicitly pushed. Secondary draws retain their push contents; executing secondaries invalidates primary descriptor bindings. JSON distinguishes `snapshot: record` for pushes from `snapshot: submit` for ordinary sets. Resource metadata and buffer visibility remain submit-time observations (D-015).

- **Built/verified**: 0.5.0 under `build/next/`; full `sandbox/run_all.py` passed. Python 44, native tracker 62, modes 40, MCP 46. Final probe tests pass 17 configurations: ordinary layer 416 / 418 / 418, inject 418 / 420; templates layer 420 / 422, inject 422 / 424; direct pushes and push templates each layer 427 / 429, inject 429 / 431. New paths run with 0 and 2 recording threads through both entries.
- **Prepared in `dist/`**: copied the exact tested DLLs, manifest and sandbox/native executables and checked SHA-256 equality. Old 0.4.0 package/settings/log: `build/backups/before-0.5-20261011-214412-992/`. DLL SHA-256: `c9a8b41db3234ecebce64300d0c1dd34de259b6ebd23827516001df4c89a8005`.
- **Configuration smoke test**: `dist/` inject entry, actual cfg with no mode override, push templates and 2 threads; reported 0.5.0 probe, captured five draws, exited 0 (`build/tests/sandbox-f6c4cx5r/`). The historical game log is unchanged. No game process was running before replacement; no game was launched or injected.
- **Limits**: GPU coverage is core 1.1 templates plus KHR push commands on the existing NVIDIA GPU. Newer aliases and compute/texel pushes have tracker coverage only where described in `docs/probe-resources.md`; no wrapper/GPU coverage is claimed for them. Descriptor buffers, specialized descriptor types, dynamic inline pipeline layouts and full reflection remain open. No validation-layer, visual or overhead result was established.

Next offline work: T-012 overhead measurement, T-015 validation layers, T-007 compatibility inventory. Real-game capture remains a separate user-chosen session; it is not required to continue these tasks.

## Prior game evidence accepted; 0.4.0 probe prepared (historical, 2026-10-11)

The user instructed the project to use the previous run's data. Do not require another 30-minute passthrough run before continuing development or preparing a short probe capture. Run 3 remains historical evidence of loading and scene changes; its exact duration and frame total are unknown. Its passthrough log contains no captured draws or descriptor data.

- **Prepared**: rebuilt layer 0.4.0, both entries and the sandbox in `dist/`, with `mode = probe`. No game process was present before replacing the DLL. The old DLLs, manifest, settings and game log are preserved in `build/backups/before-probe-20261011-212452-288/`.
- **Verified this round, sandbox only**: full `sandbox/run_all.py` against `dist/`; Python 44, native tracker 37, layer probe 385 / 387 / 387, inject probe 387 / 389, modes 40, MCP 46. All passed. A separate inject sandbox read the actual `dist/efmi_vk.cfg` without a mode environment override, reported 0.4.0, captured five draws and exited normally (`build/tests/sandbox-xm23wqvx/`).
- **Evidence preserved**: the historical `dist/efmi_vk_layer.log` is byte-for-byte unchanged. Test logs stayed under `build/`. The two DLL entries have identical bytes; SHA-256 `ab54243ae1db4072169e4587686a7df2c71115c7f4303fe2f116363305e07cf0`.
- **Next game step**: when the user chooses to launch a short probe session, use run 3's launcher configuration, collect API usage and a frame for G2/G3 a with the sample character visible. No game was started this round, and 0.4.0 has not yet loaded in the real game. Long-duration stability is a separate follow-up, not a prerequisite.

## Ordinary descriptors, shader modules and images (2026-10-11; sandbox only)

T-011's ordinary descriptor path is implemented. Draws contain submit-time descriptor contents, dynamic offsets separated by set/binding/array element, buffer ids/ranges/visibility, and image/view/sampler identities, formats and dimensions. Writes, copies, immutable samplers, variable counts, layout disturbance, set/pool lifetimes and handle reuse are tracked. Graphics and compute binding state is separate.

Pipelines retain stage entry points and SPIR-V after module destruction. `efvk_shader` reads or exports the bytes and extracts declared names and direct set/binding decorations. This is limited metadata, not full reflection. Bytecode is reclaimed after its last module/pipeline reference disappears.

- **Built**: layer, sandbox and native tracker test in `build/next/` (0.4.0).
- **Verified during implementation**: full `sandbox/run_all.py`, using `D:/Python312/python.exe` and process-local `EFMI_VK_DIST=F:/efmi vulkan/build/next`; Python 44, native tracker 37, probe 385 / 387 / 387 (layer) and 387 / 389 (inject), modes 40, MCP 46. All passed. Test artifacts now stay under `build/tests/`.
- **Not established**: game loading of 0.4.0, game captures, real-character data, G3 record-time CPU readability, validation-layer cleanliness, visual correctness or overhead.
- **Follow-up**: templates and push descriptors were subsequently implemented in 0.5.0 above. Descriptor buffers, swapchain image metadata and full reflection remain limited; see `docs/probe-resources.md` and LIMIT-006.
- **Deployment follow-up**: the initial implementation left `dist/` at 0.2.0 passthrough. D-014 supersedes that hold: 0.4.0 is now prepared in `dist/` with probe mode, as recorded above. A new game run still requires the user's on-the-spot instruction.

## Call counters, initially staged in `build/next/` (2026-10-11; sandbox only)

Layer 0.3.0 counts every intercepted device function and adds about forty functions that are only counted (descriptor paths, image and shader creation, uploads to images, multi-draw and others), the probe command `api_usage` and the tool `efvk_api_usage`. Purpose: the first probe-mode session in the game then shows which descriptor update path, which submit, copy, map and render pass variants and which draw forms the engine really uses, which decides what T-011 has to track and which untested hooks (LIMIT-011) matter.

- Built into `build\next\` because the running game (run 3) holds `dist\efmi_vk_inject.dll`. `sandbox/run_all.py` with `EFMI_VK_DIST=build\next` passes: unit 36; probe 168 / 170 / 170 (layer entry) and 170 / 172 (inject entry); modes 40; MCP 39.
- **Follow-up completed under D-014**: the user accepts historical run 3 as initial loading evidence. The counters are included in 0.4.0, now rebuilt and verified in `dist/` with probe mode configured. No real-game counter data has been collected yet.

## First runs in the real game (2026-10-11 20:27 and 20:29; started by the user through the launcher)

The user added `efmi_vk_inject.dll` to the launcher's injected libraries and launched twice. The DLL was in passthrough mode (`dist\efmi_vk.cfg`). Evidence: `dist\efmi_vk_layer.log`, the launcher log, `Player.log` / `Player-prev.log`, `F:\EFMI MCP\dist\efmi_probe.log`.

**Run 1, 20:27, DX11** (`cmd_args='-force-d3d11'`, PID 30628; injected: XXMI `d3d11.dll`, `efmi_probe.dll`, `efmi_vk_inject.dll`):

- The DLL loaded, registered the loader notification, and `vulkan-1.dll` was mapped 5.5 s later; 7 of 7 exports redirected. One instance creation was seen (`app=vkFoo`, the engine's probing instance; no device, as expected under DX11).
- The game ran for about 78 s and exited normally (`Player-prev.log` ends with the usual memory statistics, no crash).

**Run 2, 20:29, Vulkan** (`cmd_args=''`, PID 24040; same three libraries injected):

- The DLL loaded 4.6 s before `vulkan-1.dll` was mapped; 7 of 7 exports redirected. Instance creations seen: `vkFoo`, three without application info (API 1.1), then `app=Endfield engine=Unity` (API 1.3). Device creation seen at 20:29:37 on the NVIDIA GPU with 19 extensions.
- About 5 s later the game crashed. Stack from `Player.log`: `unityplayer` -> `vulkan-1` -> `nvoglv64` (four frames inside the NVIDIA Vulkan driver). `efmi_vk_inject.dll` is not on the stack. The last line before the crash is the engine's swapchain attempt (`attempt: 0 useWideColorColorspace: 0 hdrProbe: 0)`). The modules loaded immediately before are D3D ones (`nvwgf2umx.dll`, `d3d11on12.dll`, `dcomp.dll`, `D3DSCache.dll`), which fits the NVIDIA driver setting up DXGI-based presentation for the swapchain.
- In the same process XXMI's `d3d11.dll` (3Dmigoto, which hooks D3D11 / DXGI creation) was loaded, and the DX11 probe had redirected `D3D11CreateDevice` of the system `d3d11.dll` at 20:29:28.

What this establishes:

- **Timing of the launcher's injection is early enough.** Twice the DLL was in the process several seconds before `vulkan-1.dll`. The engine's instance and device creation went through the hooks. This answers the timing and resolution questions of LIMIT-013.
- **The DLL loads and redirects in the real game under DX11 without a visible effect** (one run, 78 s; not the 30 minutes G1 asks for).
- **`jobified=0` under Vulkan as well**: `Player.log` of run 2 says `GfxDevice: creating device client; threaded=1; jobified=0`. A first answer to G2: this run did not use graphics jobs, despite `boot.config`. To be confirmed by counting recording threads once a frame is captured.
- Implicit layers the game listed under Vulkan: `VK_LAYER_NV_optimus` (disabled by the engine), `VK_LAYER_NV_present`, EOS overlay, Steam overlay, Steam Fossilize, Rockstar Social Club.

What it does not establish: the cause of the crash. Suspect, unproven: the D3D11 hooks of XXMI and of the DX11 probe inside a process whose Vulkan driver creates D3D / DXGI objects for presentation. Not excluded: this DLL (absent from the stack, and it hooks only creation and present in passthrough mode), or the game's Vulkan path failing by itself on the current game and driver versions. G1 is therefore neither passed nor failed.

**Run 3, 20:38, Vulkan, only this DLL injected** (`cmd_args=''`, PID 1660; the launcher log lists `efmi_vk_inject.dll` alone; XXMI DLL injection mode Skip, the DX11 probe removed from the list):

- The DLL loaded 4.1 s before `vulkan-1.dll`; 7 of 7 exports redirected; the same sequence of instance creations as in run 2; device creation seen at 20:38:29.
- The game went past the swapchain attempts where run 2 crashed (three `attempt: 0 ...` lines in `Player.log`, no crash) and was still running normally 50 s later, loading assets, when the logs were read. `Player.log` again says `jobified=0`.

Reading of runs 2 and 3: the same Vulkan launch crashes with XXMI's `d3d11.dll` and the DX11 probe injected and does not crash without them. That points at the DX11 hooks, not at this DLL and not at the game's Vulkan path. It is one run each, so "strongly indicated", not proven; which of the two DX11 libraries is responsible is unknown and does not matter for this project. The configuration for every Vulkan session from now on is run 3's.

**Run 3 outcome, reported by the user**: exited before 30 minutes, with scene changes. No `Endfield` process was present when checked. The layer log ends at device creation and has no destruction/frame-count record; `Player.log` has shutdown memory statistics and a JobTempAlloc allocation warning at its end. Exact duration and total frames cannot be inferred from the layer log. The user subsequently accepted this as sufficient initial loading evidence to continue (D-014); it does not establish long-duration stability.

## Inject entry (2026-10-11; sandbox only, game not involved)

The user asked whether the DLL can get into the game by injection, as with d3d11, instead of a registry entry. It can; the entry was added (D-011, architecture in `ARCHITECTURE.md`).

- `dist/efmi_vk_inject.dll` is the same binary as `efmi_vk_layer.dll`; the file name selects the entry. `dist/efmi_vk.cfg` configures a process that cannot be given environment variables.
- `sandbox/run_all.py` passes: unit 36; probe end to end 156 / 158 / 158 with the layer entry (0, 2, 5 threads) and 158 / 160 with the inject entry (0, 2 threads); modes 40; MCP 35. The inject runs establish: with the DLL loaded before Vulkan, everything the layer entry reports is reported identically (draws, hashes, read-back), `vulkan-1.dll` was found after the DLL, at least 40 exports were redirected, no passive call. Loaded after the device exists, the sandbox runs on unaffected, no device is tracked and a capture is refused. Loaded late with the device functions resolved again through the exports, more than 100 calls go through the passive device and the sandbox still runs to its end. Passthrough works in both entries with agreeing frame counts.
- Not established: injection from another process; anything in the game (LIMIT-013).

## Probe, sandbox and MCP server (2026-10-11; sandbox only, game not involved)

- **Built**: `dist/efmi_vk_layer.dll` with manifest, `dist/efmi_vk_sandbox.exe`, both with MSVC from `D:\Microsoft` against the headers in `F:\vulkan\Vulkan-Headers` (tag `vulkan-sdk-1.4.321.0`, commit `2cd90f9d`).
- **Loads in the sandbox**: the loader picks the layer up through `VK_LAYER_PATH` / `VK_INSTANCE_LAYERS` set for the child process only. Device: NVIDIA GeForce RTX 5060 Laptop GPU.
- **Captures in the sandbox**: `sandbox/run_all.py` passed in full when this was written (the current counts are in the section above). What the checks establish:
  - every draw of a frame is reported in order with index buffer id, bind offset, index size, first index, count, instances, dynamic offset, pipeline, recording thread and command buffer;
  - the region hash of each draw equals the value the sandbox computed with an independent CRC-32C, for a buffer read from the application's mapping and for a device-local buffer read from the shadow;
  - a rewrite through a staging buffer that is unmapped before the copy is recorded changes exactly the hash of the rewritten mesh, with no unknown write;
  - draws recorded on several threads into secondary command buffers arrive in execution order with the right thread and command buffer ids;
  - without the layer, and in passthrough mode, no pipe appears; in passthrough mode the layer's frame count equals the sandbox's.
- **Not established**: the rendered picture of the sandbox was not looked at; only that every Vulkan call succeeded on the driver. There is no validation layer on the machine (LIMIT-002). Overhead was not measured.

## Region hash rule checked against real DX11 data (2026-10-11; offline)

`scripts/verify_region_hash.py` on `FrameAnalysis-2026-10-07-123937`: 688 indexed draws in the log, 711 IB files, 336 distinct regions; for all 336 the CRC-32C over `[bind offset + index size * first index, index size * count)` of the dumped buffer equals the region hash in the file name; no mismatch. 23 files had no draw information in the log and were skipped. The dump also shows that the game binds shared index buffers at large byte offsets under DX11 (plan fact 14).

## Workspace (2026-10-11)

`docs/plan.md` is the English v2 plan; `.ai-memory/` and `AGENTS.md` are in place; `scripts/check_text.py` enforces the no-Chinese rule; the Git repository is on branch `main`. The final check found an existing `origin` at `https://github.com/loyal37/efmi-vulkan.git`, superseding the earlier no-remote observation. This round did not configure it or push anything; changes were committed locally. `.mcp.json` registers the MCP server `efmi-vk` for this workspace (a session picks it up when it starts).

## Next

State on 2026-10-12 after the first probe session and the DX11 baseline:

1. T-017 is done, G3 b and c pass for the display scene (D-021). The launcher's EFMI profile and `dist/efmi_vk.cfg` stay at their Vulkan values.
2. T-018, game part: a Vulkan session with 0.7.0 on the display scene (third session in `docs/first-probe-session.md`), when the user starts it.
3. M2 (minimal replacement of one component) with the vertex-stage set of D-022.

Later sessions: open world, combat, several instances, LOD1, indirect draws. M3 must account for the image panel (T-016).

Historical run 3 remains accepted initial G1 loading evidence. A repeated 30-minute passthrough run is not a prerequisite. No real-game launch or injection was performed in this development round.
