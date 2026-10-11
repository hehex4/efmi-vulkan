# EFMI Vulkan - Current development state

> Last updated: 2026-10-11. Layer 0.2.0 (probe, protocol 1), server 0.1.0. The read-only probe works end to end against the sandbox through both entries, as a Vulkan layer and injected with export redirection. The inject DLL has been in the real game twice in passthrough mode (once under DX11 without incident, once under Vulkan where the game crashed in the NVIDIA driver with the cause still open); gates G1 to G3 are open.

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

Next test, to be run by the user: Vulkan again with the launcher's XXMI DLL injection mode set to Skip and only `efmi_vk_inject.dll` in the injected libraries. If that crashes too: Vulkan with nothing injected (start from the official launcher).

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

`docs/plan.md` is the English v2 plan; `.ai-memory/` and `AGENTS.md` are in place; `scripts/check_text.py` enforces the no-Chinese rule; the Git repository is on branch `main` with no remote; `.mcp.json` registers the MCP server `efmi-vk` for this workspace (a session picks it up when it starts).

## Next

1. T-011: track descriptor set contents, shader modules and images, which gate items G3 b, c and d need.
2. T-007: the M0 inventory of `arcane.ini` and Core 1.4.8, and the XXMI front-end separability assessment.
3. T-008: a DX11 baseline capture with `arcane fes 1.2` on screen (needs the game under DX11 with EFMI; the EFMI MCP tools can take it).
4. T-009: G1 in the real game through the launcher's injection, once the user has decided on the account and gives the go-ahead.
