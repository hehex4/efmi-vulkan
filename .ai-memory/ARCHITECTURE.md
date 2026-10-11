# EFMI Vulkan - Architecture

The target architecture of the product (interception, shadow, match, profile, engine, emit, prep, diag) is described in `docs/plan.md`, section 4. This file describes what exists in the repository: the read-only probe, layer 0.1.0, protocol 1, server 0.1.0.

## Data path of the development probe

`agent -> Python MCP stdio (or CLI) -> named pipe -> efmi_vk_layer.dll inside the Vulkan process -> tracked state / mapped memory / shadow copies -> result`

The layer sits between the application and the driver in the Vulkan loader's layer chain. It forwards every call unchanged; the probe only observes.

## Layer modules

| File | Responsibility |
| --- | --- |
| `layer/src/layer.cpp` | `vkNegotiateLoaderLayerInterfaceVersion`, instance and device creation (walks the loader's link info, fills the dispatch table), `GetInstanceProcAddr` / `GetDeviceProcAddr`, every hook. A hook forwards the call and, in probe mode, feeds the tracker. Pins the module so the pipe thread survives the loader unloading the layer |
| `layer/src/state.h` / `state.cpp` | Device registry; tracking of memory, buffers, pipelines, command buffers; application of buffer writes to shadows at submit time; frame assembly and statistics; the capture state machine; the probe requests and their JSON |
| `layer/src/ipc.cpp` | Named pipe server thread and request dispatch |
| `layer/src/util.cpp` / `common.h` | Configuration from environment variables, log file, CRC-32C (SSE4.2 with a table fallback) |
| `layer/src/json.h` | JSON writer and a reader for flat request objects |

### Hooks and dispatch

`EFVK_DEVICE_FUNCS` in `state.h` lists every intercepted device function once; it generates the dispatch struct and the hook table. Promoted functions and their extension aliases (`CmdBeginRenderPass2` / `...2KHR`, `QueueSubmit2` / `...2KHR`, `MapMemory2` / `...2KHR` and so on) are separate entries with their own next-pointer. `GetDeviceProcAddr` hands out a hook only when the next element of the chain implements the function. In passthrough mode only `DestroyDevice` and `QueuePresentKHR` are hooked.

A device is found from a dispatchable handle through its dispatch key (the first pointer of the handle); a single-device fast path avoids the registry lock.

### Tracked state

- **Memory** (`MemInfo`): size, type index, the current mapping (pointer, offset, size), the buffers bound to it.
- **Buffers** (`BufInfo`): id, size, usage, memory binding, a shadow byte array with an interval set of known bytes, a generation counter, a count of unknown writes.
- **Pipelines** (`PipeInfo`): id, stages, topology, vertex input bindings and attributes.
- **Command buffers** (`CmdBuf`): current bindings (pipelines, index buffer with offset and index size, vertex buffers 0 to 3, descriptor sets 0 to 3 with dynamic offsets), the draws recorded since begin (`DrawRec`, about 200 bytes each, with raw handles), pending buffer writes, the threads that recorded, render pass counters. `vkCmdExecuteCommands` copies the records of the secondary buffers into the primary one, in order, tagged with the secondary's id and thread.

Locks: `res_mx` (shared mutex) guards memory, buffers and pipelines; `cb_mx` guards only the map of command buffers, not their content (one command buffer is recorded by one thread at a time, and read by the submitting thread after recording ended); `frame_mx` guards the frame being assembled and the capture state. Order when nested: `frame_mx` then `res_mx`. The pipe thread holds the registry lock shared while it serves one request, except while it waits for a capture.

### Where buffer bytes come from

`read_buffer_locked` answers from the application's own mapping when the buffer's memory is mapped and the range lies inside the mapping (`mapped`), otherwise from the shadow when the range is known (`shadow`), otherwise not at all (`none`).

The shadow of a buffer is fed at submit time, in recording order, from the pending writes of the submitted command buffers: `vkCmdCopyBuffer(2)` (bytes read from the source at submit time, falling back to what was read when the copy was recorded), `vkCmdUpdateBuffer`, `vkCmdFillBuffer`. Only buffers whose usage intersects `shadow_usage` (default: index buffers) get a shadow. When memory is unmapped, every `TRANSFER_SRC` buffer in it of at most 16 MiB is snapshotted into its own shadow first, so a copy recorded after the unmap still finds its source. A write whose bytes cannot be obtained removes the range from the known set and is counted as an unknown write.

Unmap, free and destroy take `res_mx` exclusively before the call goes down, so a reader never touches a mapping that is going away.

### Frames and capture

`vkQueueSubmit(2)` adds the submitted command buffers to the current frame's statistics. `vkQueuePresentKHR` closes the frame. A capture request arms the state machine; the frame that starts at the next present is recorded in full and published at the present after it. For each draw of a captured frame the handles are resolved to ids and, for a direct indexed draw, the CRC-32C of the index range (`bind offset + index size * first index`, `index size * index count` bytes) is computed.

## Probe protocol

Pipe `\\.\pipe\efmi-vk-<PID>`, one client at a time, started when the first device is created in probe mode. The security descriptor gives system and administrators full control and the interactive user read and write, so that a client without elevation can talk to an elevated process. A message in either direction is `[u32 header_len][u32 payload_len][header JSON][payload]`. Requests are flat JSON objects with `cmd`; replies are `{"ok":true,"result":...}` or `{"ok":false,"error":"..."}`.

| `cmd` | Arguments | Result |
| --- | --- | --- |
| `status` | | Version, protocol, pid, exe, mode, device, frame counter, statistics of the last frame, maxima, counts |
| `capture` | `timeout_ms`, `draws` | Statistics of one complete frame and, unless `draws` is 0, every draw |
| `buffers` | `usage`, `min_size`, `limit` | Tracked buffers |
| `read_buffer` | `id`, `offset`, `size` | Source and generation in the header, the bytes as payload (at most 64 MiB) |
| `hash` | `id`, `offset`, `size` | CRC-32C of the range |
| `pipeline` | `id` | Vertex input, stages, topology |

## Environment variables of the layer

`EFMI_VK_MODE` (`probe`, default, or `passthrough`), `EFMI_VK_LOG` (log file; default `efmi_vk_layer.log` next to the DLL), `EFMI_VK_SHADOW_USAGE` (hex `VkBufferUsageFlags` mask of buffers that get a shadow), `EFMI_VK_UNMAP_SNAPSHOT_LIMIT` (bytes; 0 disables the snapshot at unmap).

## Server modules

| File | Responsibility |
| --- | --- |
| `server/efmi_vk/probe_client.py` | Finds probe pipes, sends requests, raises `ProbeError` |
| `server/efmi_vk/hashes.py` | CRC-32C in Python and the index region rule |
| `server/efmi_vk/mods.py` | Reads the TextureOverride sections of mod INIs (hash, `match_first_index`, `match_index_count`) and matches them against captured draws |
| `server/efmi_vk/tools.py` | `Session` (pipe connection, last capture) and the nine tools; `ToolError` |
| `server/efmi_vk/mcp_stdio.py` | JSON-RPC 2.0 over stdio: `initialize`, `ping`, `tools/list`, `tools/call` |
| `server/run_server.py`, `server/efvk.py` | Entry points: MCP server, command line |

## Sandbox

`sandbox/src/main.cpp` loads `vulkan-1.dll` at run time, opens a 640 x 360 window on the secondary screen without taking the focus, and draws five indexed meshes per frame with one pipeline: three from index buffer A (16-bit, device-local, uploaded once through a staging buffer that stays mapped) and two from index buffer B (32-bit, host-visible, mapped, bound at byte offset 16). Each draw binds the one descriptor set with its own dynamic uniform offset. `--threads N` records the draws on N threads into secondary command buffers. When the trigger file appears it rewrites six indices of buffer A through a second staging buffer that is unmapped before the copy is recorded. It writes the expected draws and hashes, computed with its own table-driven CRC-32C, to `expected.json`.

`sandbox/src/spirv.h` assembles the vertex and fragment shader word by word, because there is no shader compiler on the machine.

## Build and verification entry points

- `scripts/build.py [layer] [sandbox] [--out DIR]`: finds `vcvars64.bat` and the Vulkan headers, compiles with `cl`, writes the layer manifest.
- `scripts/fetch_vulkan_headers.ps1`: clones the pinned header tag into `F:\vulkan\Vulkan-Headers`.
- `sandbox/run_all.py [--no-build]`: build, `scripts/check_text.py`, `unit_test.py` (CRC vectors, INI parser, override matching, MCP message handling), `probe_test.py` (layer plus sandbox with 0, 2 and 5 recording threads: statistics, every draw, hashes before and after the rewrite, buffer read-back, pipeline, reconnect), `modes_test.py` (no layer: no pipe; passthrough: no pipe, frame count agrees), `mcp_test.py` (the MCP server as a child process against the sandbox, every tool).
- `sandbox/harness.py`: `Sandbox` context manager and `layer_env`, which enables the layer for a child process through `VK_LAYER_PATH` and `VK_INSTANCE_LAYERS` and switches other implicit layers off with `VK_LOADER_LAYERS_DISABLE=~implicit~`.
- `scripts/verify_region_hash.py <dump>`: region hash rule against an XXMI FrameAnalysis dump.

`dist/`, the `build/` directories, `captures/` and logs are ignored by Git. A build on disk does not mean a running process has loaded it.
