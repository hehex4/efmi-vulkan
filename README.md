# EFMI Vulkan

Work in progress. The goal is to run existing EFMI mods of Arknights: Endfield on the game's native Vulkan renderer, through a Vulkan layer of our own.

The read-only probe has captured real character data and resolved the sample's texture slots.
Earlier 0.7.2 timings correlated open-world spikes with expensive copies at unmap and lock contention.
Layer 0.7.3 made those copies eleven times faster by reading write-combined memory with streaming
loads; in the game, with 0.7.4, three of 87,005 present intervals exceeded 33 ms and the user reports
the stutter gone. A review that blamed the game's mesh pools instead was wrong and is corrected.
The 0.9.0 candidate adds an opt-in [single-component replacement](docs/m2-component-hook.md)
through the actual draw hook, verified in the GPU sandbox. Full mod rendering and real-character
replacement are not yet verified. See the [current state](.ai-memory/CURRENT_STATE.md)
for the deployed package, next-launch configuration and evidence boundaries.

## Layout

| Path | Content |
| --- | --- |
| `docs/plan.md` | The implementation plan: verified facts, design decisions, gates, milestones, risks |
| `layer/` | The Vulkan layer (C++17, MSVC x64): interception, tracked state, frame capture, named-pipe probe |
| `sandbox/` | A deterministic Vulkan test program and the regression scripts |
| `server/` | Python client for the probe, a command-line tool and an MCP server |
| `scripts/` | Build, dependency fetch, repository checks, offline analysis |
| `.ai-memory/` | Project memory for agents; `AGENTS.md` is the working agreement |

## Requirements

- Windows x64, MSVC build tools (found through `vswhere`, or set `VCVARS` to the full path of `vcvars64.bat`).
- Vulkan headers. `scripts/fetch_vulkan_headers.ps1` clones the pinned tag of `KhronosGroup/Vulkan-Headers` into `F:\vulkan\Vulkan-Headers`. A Vulkan SDK is used instead when `VULKAN_SDK` is set or one is installed in `F:\vulkan`.
- Khronos validation layers for the full regression suite: `scripts/build_validation.py` builds the pinned source under `F:/vulkan` using MSVC/CMake/Ninja. See [validation](docs/validation.md).
- Python 3.12 for the build script, the client, the MCP server and the tests; standard library only.

## Build and test

```bat
python scripts\build.py
python sandbox\run_all.py
```

`build.py` writes `efmi_vk_layer.dll`, its manifest `efmi_vk_layer.json`, the copy `efmi_vk_inject.dll` and `efmi_vk_sandbox.exe` to `dist\`. `run_all.py` builds, then runs the text check, the offline tests and the end-to-end tests against the sandbox. The layer is enabled only inside the child processes the tests start (`VK_LAYER_PATH`, `VK_INSTANCE_LAYERS`); nothing is registered system-wide.

## The probe

With the experimental M2 package disabled (the default), probe mode forwards every
Vulkan call unchanged and records what it sees:

- memory maps, buffers, pipelines, command buffers, submits and presents;
- ordinary/template descriptor set contents, dynamic buffer ranges and recorded push descriptors; image/view/sampler metadata; retained shader SPIR-V;
- for index buffers a CPU shadow fed from staging copies, so that the CRC-32C hash of the index range of a draw can be computed. This is the hash XXMI calls the region hash and that mod INIs match on;
- per frame: every draw with its bindings, recording thread, command buffer and render pass.

It serves this over the named pipe `\\.\pipe\efmi-vk-<PID>`. `EFMI_VK_MODE=passthrough` switches the tracking and the pipe off.

The DLL gets in front of the Vulkan calls in one of two ways, selected by its file name:

- `efmi_vk_layer.dll` is loaded by the Vulkan loader as a layer, through the manifest.
- `efmi_vk_inject.dll` is loaded with `LoadLibrary` by whatever injects libraries into the process, and redirects the exports of `vulkan-1.dll` in memory. It has to arrive before the application resolves its Vulkan functions; the status says whether it did.

Settings can also be given in `efmi_vk.cfg` next to the DLL (`mode = passthrough`, `log = ...`), for processes that cannot be given environment variables. Two of them bound the probe in a large process: `shader_budget` (bytes of SPIR-V kept, default 512 MiB) and `resources = 0`, which keeps draws, buffers and hashes but no descriptor, image or shader metadata.

Command line, one or more tools sharing a session:

```bat
python server\efvk.py status
python server\efvk.py capture_frame -- list_draws kind=indexed limit=20
python server\efvk.py capture_frame -- match_mod path="D:\path\to\a mod"
```

MCP server for agents: `python server\run_server.py` (registered for this workspace in `.mcp.json`). Tools: `efvk_status`, `efvk_capture_frame`, `efvk_list_draws`, `efvk_get_draw`, `efvk_find_hash`, `efvk_list_buffers`, `efvk_read_buffer`, `efvk_pipeline`, `efvk_shader`, `efvk_api_usage`, `efvk_match_mod`.

See [resource inspection](docs/probe-resources.md) for the descriptor snapshot schema, SPIR-V export, verified paths and remaining limits. Ordinary set values resolve at submit; push values are retained when recorded. Resource visibility remains a submit-time observation, not evidence of record-time buffer availability.

The layer also reports bounded [host present intervals](docs/frame-pacing.md), including tail
latency and capture/upload tags. The [resource-off and passthrough comparisons](docs/pacing-resources-off-session.md)
narrow the reported stutter to probe-only work as a group. Layer 0.7.2's optional
[tracker timings](docs/tracker-work.md), enabled by `profile_tracker = 1`, measure selected paths
without treating host intervals as displayed-frame timings. The [latest session and 0.7.3 candidate](docs/pacing-tracker-session.md)
identify expensive snapshot reads as a contributor and verify a local copy optimization. [The copy
at unmap](docs/unmap-snapshots.md) has the game result that confirms it, the first open-world frames,
and the record of a wrong attribution made in between.

`scripts/collect_probe.py` saves a complete frame observation and bounded pipeline/shader exports
from an explicitly selected running probe. See [the first probe session](docs/first-probe-session.md)
for evidence boundaries and the next game-data step. It never launches or injects a process.

## Offline analysis

`python scripts\verify_region_hash.py <FrameAnalysis folder>` checks the region hash rule against an XXMI frame dump: for every indexed draw it recomputes the CRC-32C of the index range and compares it with the hash in the dump's file name.

See [the compatibility inventory](docs/compatibility-inventory.md) for the pinned sample/Core/source inputs, and [probe performance](docs/probe-performance.md) for synthetic measurements. `scripts/inventory_compat.py --check-snapshot docs/compatibility-inputs.json` verifies the saved local input metadata.

## Licence

GPL-3.0. Later milestones reuse code from XXMI / 3Dmigoto, which is under that licence.
