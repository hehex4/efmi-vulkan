# EFMI Vulkan

Work in progress. The goal is to run existing EFMI mods of Arknights: Endfield on the game's native Vulkan renderer, through a Vulkan layer of our own.

The current 0.5.0 read-only probe is verified against the sandbox and available in `dist/`, with probe mode configured. It now tracks descriptor update templates and direct/template pushes. Historical game launches used 0.2.0 passthrough; the user accepts run 3 with scene changes as initial G1 loading evidence and defers the 30-minute observation. Long-duration stability, loading of newer builds in the game, probe-mode game data and mod rendering remain unverified. See the [current state](.ai-memory/CURRENT_STATE.md).

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

In probe mode the layer forwards every Vulkan call unchanged and records what it sees:

- memory maps, buffers, pipelines, command buffers, submits and presents;
- ordinary/template descriptor set contents, dynamic buffer ranges and recorded push descriptors; image/view/sampler metadata; retained shader SPIR-V;
- for index buffers a CPU shadow fed from staging copies, so that the CRC-32C hash of the index range of a draw can be computed. This is the hash XXMI calls the region hash and that mod INIs match on;
- per frame: every draw with its bindings, recording thread, command buffer and render pass.

It serves this over the named pipe `\\.\pipe\efmi-vk-<PID>`. `EFMI_VK_MODE=passthrough` switches the tracking and the pipe off.

The DLL gets in front of the Vulkan calls in one of two ways, selected by its file name:

- `efmi_vk_layer.dll` is loaded by the Vulkan loader as a layer, through the manifest.
- `efmi_vk_inject.dll` is loaded with `LoadLibrary` by whatever injects libraries into the process, and redirects the exports of `vulkan-1.dll` in memory. It has to arrive before the application resolves its Vulkan functions; the status says whether it did.

Settings can also be given in `efmi_vk.cfg` next to the DLL (`mode = passthrough`, `log = ...`), for processes that cannot be given environment variables.

Command line, one or more tools sharing a session:

```bat
python server\efvk.py status
python server\efvk.py capture_frame -- list_draws kind=indexed limit=20
python server\efvk.py capture_frame -- match_mod path="D:\path\to\a mod"
```

MCP server for agents: `python server\run_server.py` (registered for this workspace in `.mcp.json`). Tools: `efvk_status`, `efvk_capture_frame`, `efvk_list_draws`, `efvk_get_draw`, `efvk_find_hash`, `efvk_list_buffers`, `efvk_read_buffer`, `efvk_pipeline`, `efvk_shader`, `efvk_api_usage`, `efvk_match_mod`.

See [resource inspection](docs/probe-resources.md) for the descriptor snapshot schema, SPIR-V export, verified paths and remaining limits. Ordinary set values resolve at submit; push values are retained when recorded. Resource visibility remains a submit-time observation, not evidence of record-time buffer availability.

## Offline analysis

`python scripts\verify_region_hash.py <FrameAnalysis folder>` checks the region hash rule against an XXMI frame dump: for every indexed draw it recomputes the CRC-32C of the index range and compares it with the hash in the dump's file name.

## Licence

GPL-3.0. Later milestones reuse code from XXMI / 3Dmigoto, which is under that licence.
