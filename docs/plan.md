# EFMI Vulkan implementation plan (v2)

Date: 2026-10-11.
Status at the time of writing: no code, nothing run inside the game. The facts below come from static inspection of local files and public sources.
Previous version (v1, not in this repository): `D:\XXMI-Launcher-Portable-v2.1.6\EFMI\output\efmi-vulkan-plan-20261011\`.

## 1 Goal and scope

**Goal**: Arknights: Endfield keeps rendering through its native Vulkan path while existing EFMI merged-skeleton mods run on it. The INI, BUF and DDS files of a mod are used byte for byte as they are.

**First acceptance sample**: `Mods\arcane fes 1.2`. It has a single INI, no ShaderOverride and no dependency on RabbitFX, and it still covers a merged skeleton (499 bones, 13 components), LOD remapping, 13 key toggles and textures assigned directly by slot.

**Not in the first version**:

- RabbitFX, and anything else that patches DX11 shader assembly through ShaderRegex or ShaderOverride.
- Shape keys, on-screen text (TextPrinter), hunting mode.
- Replacement on indirect draw paths.

**Never**: any work to evade, hide from or fight the anti-cheat. If loading is blocked, the project stops.

**Unit of a compatibility claim**: the triple (game version, Core version, mod). Every claim needs a test record.

## 2 Verified facts

| # | Fact | Evidence | Effect on the design |
| --- | --- | --- | --- |
| 1 | The game runs Vulkan by default; DX11 is forced by the EFMI launch | Player.log has `Forcing GfxDevice: Direct3D 11`; a 206 MB `vulkan_pso_cache.bin` exists locally | The native Vulkan path is real |
| 2 | The engine is a customised Unity 2021.3.34f5 whose Vulkan backend manages memory with VMA | Version of `UnityPlayer.dll`; `[VmaAllocator]` in Player.log | Buffers are sub-allocations of large memory blocks, so memory mapping and bind offsets must be tracked |
| 3 | `boot.config` enables native graphics jobs; under DX11 the log says `jobified=0` | `gfx-enable-native-gfx-jobs=1` | Under Vulkan, command buffers are probably recorded on several threads; to be measured |
| 4 | The game ships the ACE kernel anti-cheat | `AntiCheatExpert\ACE-BASE.sys` in the install directory | Whether the layer can load at all is the first gate |
| 5 | The IB hash is a pure content hash: `crc32c_hw(0, ptr, size)` over the index bytes the draw uses, starting at bind offset plus first index | `track_region_hashes = 1` in `d3dx.ini`; `GetRegionHash` in XXMI `ResourceHash.cpp`; checked against data with `scripts/verify_region_hash.py`: all 336 distinct index regions of the DX11 dump match | With the index bytes in hand the hash can be recomputed under Vulkan; no mapping table is needed |
| 6 | The two merged-skeleton compute shaders of Core only copy and patch | `MergedSkeleton_BoneDataImporter_RollingBuffer.hlsl`, `MergedSkeleton_InstanceConfigOverrider_RollingBuffer.hlsl` | They can be done on the CPU; no GPU compute is required |
| 7 | For every replaced draw, Core runs a dispatch and a buffer copy inline | `CommandListMergedSkeleton_Apply` and `OverrideComponentSkeleton` in `MergedSkeleton.ini` | Both commands are illegal inside a Vulkan render pass, so the Core INI cannot run as it is |
| 8 | The sample mod assigns textures directly to `ps-t12` through `ps-t18` | `arcane.ini` | A mapping from DX11 texture registers to Vulkan bindings is required |
| 9 | 6 of the 9 local character mods depend on RabbitFX, and those mods pass their textures only through RabbitFX | References to `RabbitFX` in the mod INIs; `liino.ini` has no direct texture assignment | RabbitFX cannot be stubbed out; these 6 mods are outside the first version |
| 10 | The XXMI engine source is public and contains Pool, Region, SpatialHash and ElementFormat | `SpectrumQT/XXMI-Libs-Package`, `DirectX11/CommandList.cpp` and others, last push 2026-10-05 | INI semantics can be reused instead of rewritten |
| 11 | WWMI-DX12 hosts the XXMI engine behind a shim and implements shapekeys, rabbitfx, draw and dispatch as native modules | Directory layout of `lekarsenten/wwmi-dx12`, GPL-3.0 | The hosting approach can be followed; taking over the hard parts with native modules has a precedent |
| 12 | The Vulkan events of ReShade do not include `vkMapMemory` | `map_buffer_region` in `reshade_events.hpp` lists only D3D and OpenGL | ReShade cannot see data uploaded from the CPU and is not a suitable entry point |
| 13 | The existing DX11 frame dump contains no modded character draw | `FrameAnalysis-2026-10-07-123937\log.txt` has zero CustomShader executions | The comparison baseline has to be captured again |
| 14 | Under DX11 the game packs many meshes into shared index buffers and binds them at a byte offset | The same dump: `IASetIndexBuffer(..., Format:57, Offset:2477008)` followed by a draw with `StartIndexLocation:390` | The layer must hash by (buffer, bind offset, first index, count) and keep shadows of large shared buffers, not of one buffer per mesh |

## 3 Design decisions

**D1 The entry point is our own DLL in front of the Vulkan calls, not ReShade.** The project needs every detail of memory mapping, buffer binding, descriptor updates and pipeline creation, and ReShade exposes only part of that. The same sources give two ways in:

- *Inject entry, the one for the game.* The mod launcher loads the DLL with its ordinary injection, the way XXMI brings `d3d11.dll` and extra libraries into the game today (the launcher log shows `d3d11.dll, efmi_probe.dll` injected into `Endfield.exe` about two seconds after the process starts). The DLL then redirects, in memory, the export table entries of `vulkan-1.dll` to its hooks. No registry entry, no system-wide change, no dependence on whether the process is elevated. The condition is timing: the DLL has to be in the process before the engine resolves its Vulkan functions. The status command reports whether it was.
- *Layer entry.* The Vulkan loader loads the DLL as a layer through a manifest. The sandbox tests use it, and it remains the fallback for the game. For an elevated process it needs an implicit layer entry under HKLM, which is a system-wide change.

Settings for a process started by a launcher come from `efmi_vk.cfg` next to the DLL, because such a process cannot easily be given environment variables.

**D2 The compatibility surface is "mod INI plus the EFMIv1 interface", not DX11 semantics.** Mods talk to Core through command lists, variables, callbacks and pools in the `\EFMIv1\` namespace. The project supplies a Vulkan edition of Core (Core-VK below) whose interface names and semantics match Core 1.4.8. Pure logic (the spatial identification state machine, variables, pools) keeps the original INI wherever possible; every command list that touches DX11 slots, compute shaders or buffer copies becomes a native built-in command.

**D3 Two-phase execution; the game's render passes are not split and its commands are not re-recorded.** While recording, the layer does only what is legal inside a render pass: change bindings, issue draws, restore state. All data preparation happens on the CPU, goes into host-visible buffers owned by the layer, and is finished before `vkQueueSubmit` is forwarded. This relies on one premise: instance config and bone data are readable on the CPU when the draw is recorded. G3 verifies the premise; section 7 lists the fallbacks.

**D4 The INI engine reuses the XXMI front end.** Parsing, expressions, variables, keys, persist, pools and command-list control flow come from the XXMI source, hosted the way WWMI-DX12 does it; resource, binding and draw commands are implemented by this project's Vulkan backend. M0 spends three days on assessing how separable the front end is. If it cannot be separated, a reduced interpreter is written that covers exactly the inventory.

**D5 Threading.** The match pre-filter uses an immutable hash table and takes no lock, so a draw that does not match costs almost nothing. A matching draw runs its command list synchronously under one global mod lock, because Core passes arguments between command lists in global variables. The frame boundary is present. During bring-up, if a command-line switch can turn graphics jobs off, the single-threaded mode is used first.

**D6 Hashes rely on shadow copies.** Buffers with index usage keep a CPU shadow, fed from mapped-memory writes or staging copies. The CRC32C of a (buffer, offset, length) triple is computed on first use and cached; a write to the buffer invalidates it.

**D7 Texture slot mapping is built per shader.** The first choice is aligning by resource name: the names in DX11 shader reflection against the names in SPIR-V, provided the SPIR-V still carries debug names. The second choice is aligning by register order and correcting individual shaders through the profile. G3 shows which one works.

**D8 Vertex input uses pipeline variants.** When the vertex stride or `ElementFormat` of a mod differs from the game's pipeline, a variant with dynamic vertex input state is created lazily from the original pipeline and cached. Until the variant is ready the original draw is kept; nothing blocks.

**D9 No run-time HLSL compilation in the first version.** The merged-skeleton path no longer needs shaders. Shape keys later use Core shaders precompiled at build time.

**D10 The licence is GPL-3.0**, because XXMI and 3Dmigoto code is reused.

**D11 Development is supported by a live capture probe.** The read-only diagnostics of the layer are exposed over a named pipe to a Python client and an MCP server, in the manner of the EFMI MCP project for DX11, so that an agent can query the draws, buffers and bindings of a running Vulkan process. The sandbox program gives the same tooling a deterministic target that needs no game.

## 4 Architecture

```text
Endfield (Unity 2021.3, possibly multi-threaded recording)
        | Vulkan calls
        v
+- EFMI Vulkan layer -----------------------------------------+
| intercept  device, resource, descriptor, pipeline, command, |
|            submit interception                              |
| shadow     memory maps, buffer shadows, descriptor set      |
|            contents, pipeline reflection                    |
| match      IB region hash -> TextureOverride candidates     |
|            (lock-free)                                      |
| profile    Endfield adaptation: instance config, bones,     |
|            texture slot mapping                             |
| engine     XXMI INI front end + Core-VK built-ins           |
|            (under the mod lock)                             |
| emit       inside the render pass: rebind, draws, restore   |
| prep       before submit: write this frame's data into      |
|            layer-owned buffers                              |
| diag       logs, frame records, counters, live probe (IPC)  |
+-------------------------------------------------------------+
        |
        v
     GPU driver
```

Order of processing for one replaced draw:

1. The game calls `vkCmdDrawIndexed`. The layer takes the current IB binding, computes the region hash and looks it up. A miss is passed through.
2. On a hit the mod lock is taken and the command list of the matching TextureOverride runs.
3. `Component_ReadConfig` (built-in) finds, through the profile, the binding and offset of the instance config buffer and reads its content from shadow memory.
4. Spatial identification keeps the original INI state machine; only `SpatialHash` changes, reading the instance position from shadow memory.
5. `MergedSkeleton_Apply` (built-in) does three things: it registers the bone import for the component; it writes a 256-byte instance config with patched bone offsets into the layer's config buffer; it replaces the bone buffer binding with the merged skeleton buffer.
6. The mod's callback executes its `ib`, `vbN`, `ps-tN` assignments and `drawindexed`; the layer records them as an emission plan.
7. When the command list has run without error, emit writes the plan into the command buffer in one go: variant pipeline, the mod's VB and IB, patched descriptor sets, the draws, and finally the game's original bindings again.
8. `handling = skip` takes effect only when the replacement succeeded. On any failure the plan is dropped and the original draw goes through.
9. Before `vkQueueSubmit` is forwarded, prep writes the CPU master copy of the merged skeleton into this frame's GPU buffer.

The merged skeleton uses "a CPU master copy plus a GPU snapshot per frame", because with several frames in flight the CPU must not rewrite a buffer the GPU is still reading. At the size of the sample, the snapshot is below 1 MB per mod per frame.

Interface that Core-VK has to keep (taken from what the sample mod actually references):

- Command lists: `RegisterMod`, `Object_ReadConfig`, `Component_ReadConfig`, `MergedSkeleton_DetectBoneDataSource`, `SpatialIdentity_IdentifyComponentInstances`, `Component_DrawInstances`, `InitializeMergedSkeleton`, `MergedSkeleton_AttachComponent`, `OverrideTextures`.
- Callbacks: `Callback_Component_DrawCustom`, `Callback_MergedSkeleton_ConnectComponent`.
- Variables that mods read or write: `component_id`, `gpu_posed`, `lod_level`, `bones_count`, `component_count`, `instance_count`, `custom_mesh_scale`, `bones_data_source`, `instance_config_vs_cb_slot`, `mod_id`, `required_version`, `object_guid`, and all `cfg_*` constants.
- Pools and resources passed in and out: `Input_ObjectSpatialIdentity`, `Input_MergedSkeleton_*`, `Output_MergedSkeleton`, `OutputMergedSkeleton_Template`.

## 5 Gate experiments

All three are done within M1. G1 and G3 are stop conditions; G2 is an input to the design.

### G1 Can the layer load

Procedure: `efmi_vk_inject.dll` in passthrough mode (set in `efmi_vk.cfg`) is added to the launcher's extra libraries, and the game is started through the launcher without the `-force-d3d11` argument, so that it runs on Vulkan. Observe normal play including scene changes. Record from the layer's log whether `vulkan-1.dll` was already loaded when the DLL arrived, how many exports were redirected, and whether instance and device creation were seen. Both launcher settings belong to the user and are changed by the user.

User amendment, 2026-10-11 (D-014): use historical run 3 as initial loading evidence. The user exited before 30 minutes after scene changes; the log confirms instance/device interception. The original 30-minute observation target is deferred and does not block development or a short probe session. This acceptance does not establish long-duration stability, loading of newer builds, or probe-mode capture data. New real-game launches still need the user's on-the-spot instruction.

Pass: the game behaves as it does without the DLL; no dialog, crash or disconnect; and the log shows that device creation was seen, which means the DLL arrived in time. If the DLL arrives too late, the game is unaffected but nothing is intercepted, and the layer entry is tried instead.

Fail: the project stops.

This step by itself carries a risk for the account. Which account is used is the user's decision.

### G2 Threads and submission structure

Procedure: read the `GfxDevice: creating device client` line of Player.log under Vulkan; let the layer count, per frame, recording threads, command buffers, use of secondary command buffers, submits, and whether render passes are classic or dynamic rendering. Also try whether a command-line switch such as `-force-gfx-direct` brings recording back to one thread (whether a release build honours these switches is unverified).

Use: decides the lock granularity and bring-up strategy of D5, and the consistency strategy of the merged-skeleton snapshot when one frame is submitted in several parts.

### G3 Is the data obtainable

Procedure: the layer runs read-only with the sample character on screen. Check item by item:

| Item | What is checked | Pass condition |
| --- | --- | --- |
| a | Recompute the 21 IB hashes of `arcane.ini` from the index bytes seen under Vulkan | All LOD0 components match |
| b | Find the instance config buffer (a window of 4096 constants under DX11) | Readable on the CPU when the draw is recorded; floats 12 to 14 of each instance follow the character's movement |
| c | Find the bone buffer (`vs-t0` with stride 16 under DX11) | x and y of entry 5 of the instance config are offsets into it; readable on the CPU at record time |
| d | Texture bindings of the target pixel shader | They can be matched one to one with t12 to t18 of DX11 |
| e | Stride and attributes of vertex streams 0 to 3 | Same as the DX11 input layout |
| f | Shape of the draw | A direct `vkCmdDrawIndexed`; a rule for emulating `DRAW_TYPE` can be stated |

Handling of a failure:

- a fails: check index width and upload path first. If the bytes really differ, the goal is reduced to "a tool that converts mod hashes automatically".
- b or c fails: the premise of D3 does not hold; go to the fallbacks of section 7 and re-estimate.
- d fails: switch to a hand-maintained profile per shader; the effort of M4 goes up accordingly.
- e or f fails: adjust D8 and the match rule to the real layout; usually not fatal.

## 6 Milestones

Durations are rough estimates for one person with Vulkan experience, to be redone after M2.

### M0 Baseline and inventory (about 1 week, can run in parallel with G1)

- Pin versions and file fingerprints: game version, Core 1.4.8, the XXMI source commit matching `d3d11.dll` 1.2.2.0, the sample mod.
- Capture a new DX11 frame with the sample mod on screen, including one key toggle. Record for each component the IB hash, first index and count, pixel shader hash, the textures at t12 to t18, the vertex layout and the constant buffer sizes.
- Take inventory of every INI construct and command used by `arcane.ini` and Core 1.4.8.
- Assess how separable the XXMI front end is, three days at most, and settle D4.

Deliverables: `compatibility-inventory.md`, the DX11 baseline capture, the version list, the engine decision record.

### M1 Layer skeleton and read-only diagnostics (about 2 to 3 weeks)

- Layer skeleton, sandbox program, live probe (named pipe, Python client, MCP server) and their regression tests. None of this needs the game.
- G1, G2 and G3 in the real game.

Deliverables: the layer, the probe tooling, three experiment reports, the comparison table between DX11 and Vulkan.

### M2 Minimal replacement (about 3 to 4 weeks)

- Complete the shadow system: memory, buffers, descriptor sets, pipelines.
- Without the INI engine, replace one component of the sample by hard-coded logic: the mod's IB and VB, a pipeline variant, texture patches, state restore.
- Add the CPU bone import and the instance config patch so that this component follows the animation.

Pass conditions:

- The component animates correctly, with no wrong frame, in the main pass and in every other pass that hits it (shadow, depth and so on).
- No unexplained error from the Vulkan validation layers on the affected paths.
- With no mod enabled, pass-through adds at most 5% to the CPU frame time (a target, to be revised after measuring).

This is the real gate for the architecture. The schedule from M3 on is estimated only after it.

### M3 First complete mod (about 6 to 10 weeks)

Bring in the INI engine and Core-VK; add keys and persist, pools and spatial identification, LOD remapping, multi-instance decomposition, the `handling = skip` transaction and a basic F10 reload.

Pass conditions:

- The directory of `arcane fes 1.2` is used unmodified.
- Meshes, textures and animation are correct in the open world, in the character screen and in combat; all 13 key toggles work.
- Several instances on screen do not mix bones or appearance.
- With the layer disabled, the existing DX11 plus EFMI launch path is unaffected.

### M4 Extension

In this order, each item accepted against a concrete mod:

1. The two other mods without RabbitFX (ardelia, xaihi). Each has an additional INI containing ShaderOverride sections; these are assessed one by one.
2. Shape keys.
3. On-screen text and version notifications.
4. RabbitFX. It needs an equivalent on the SPIR-V side and gets its own assessment before work starts.

### M5 Stability and release

Resource reclamation across scene changes, hot reload, window changes and repeated start and stop; cache and log optimisation; separate instructions for installing, enabling and restoring; regression with DLSS frame generation on and off.

## 7 Risks and fallbacks

| Risk | Shows earliest at | Handling |
| --- | --- | --- |
| ACE blocks the layer | G1 | Stop the project |
| Instance config or bone data is not readable on the CPU at record time | G3 | Fallback A: do the import on the GPU in a command buffer prepended at submit time, which requires that the game data is produced by an earlier submit. Fallback B: virtualise and replay the game's command buffers; roughly double the effort and a new decision to proceed |
| Parallel recording causes state races that are hard to reproduce | G2, M2 | Mod lock plus aggregation at submit time; single-threaded mode during bring-up |
| Several submits per frame make the merged-skeleton snapshot inconsistent | G2, M2 | Snapshot per frame or per submit, depending on the real submission structure |
| Texture slots cannot be mapped automatically | G3 | Hand-written profile per shader; maintenance cost grows with the number of variants |
| A game update changes the instance config layout or the bone offset fields | M4 and later | Profiles are kept per game version; with an unknown layout the original draw is kept |
| Core is upgraded upstream | Ongoing | Core-VK follows; the `required_efmi_version` check of a mod rejects a Core that is too old |
| First creation of a pipeline variant causes a hitch | M2 | Create asynchronously; keep the original draw until ready |
| Shadow copies cost memory | M2 | Shadow only index buffers and the constant ranges that are hit; consider on-demand read-back after measuring |
| Interaction with Streamline frame generation | M2 | From M2 on, every round is tested with frame generation on and off |

Worth watching: the README of WWMI-DX12 lists EFMI-DX12 as a future plan. Locally there are only DX11 and Vulkan PSO caches, so the game does not seem to have a DX12 path at present and that route does not help for now.

## 8 Repository layout

```text
efmi-vulkan/
  layer/          Vulkan layer: interception, shadow state, capture, IPC
  sandbox/        deterministic Vulkan test program and regression scripts
  server/         Python probe client, CLI and MCP server
  core-vk/        Vulkan edition of the Core INI (from M3)
  profiles/       Endfield adaptation, one file per game version (from M2)
  scripts/        dependency fetch, repository checks, offline analysis
  docs/           plan, inventory, experiment reports, compatibility matrix
  .ai-memory/     project memory for agents
```

Third-party sources (XXMI engine at a pinned commit, SPIRV-Reflect, VMA) are added when the milestone that needs them starts. The Vulkan headers live outside the repository in `F:\vulkan`.

The bone import and the instance config patch get offline tests: the input buffers and the compute shader output from a DX11 frame dump are replayed against the CPU implementation, which has to agree byte for byte with the original HLSL.

## 9 Tasks of the first week

1. Record game version, GPU and driver version; pin the fingerprints of Core, the DLL and the sample mod.
2. Capture a new DX11 frame with `arcane fes 1.2` and extract the table G3 compares against.
3. Take inventory of the INI constructs of `arcane.ini` and Core 1.4.8 and write `compatibility-inventory.md`.
4. Fetch the XXMI source commit that matches the local `d3d11.dll` 1.2.2.0 and start the separability assessment.
5. Write the pass-through layer skeleton, the sandbox, the live probe and the build scripts.
6. Once the user has decided which account G1 uses, run G1.

## 10 Open points for the user

- **Motivation.** If the goal is something that exists only under Vulkan, such as DLSS frame generation, this plan is the right one. If the goal is only better frame times, it is worth measuring DX11 plus EFMI first and then deciding whether months of work are justified.
- **The account for G1.**
- **Scope of the first version.** As scoped now, the first version covers 1 of the 9 local character mods, 3 after the first step of M4; the other 6 wait for RabbitFX.
