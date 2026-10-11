# EFMI Vulkan

Work in progress. The goal is to run existing EFMI mods of Arknights: Endfield on the game's native Vulkan renderer, through a Vulkan layer of our own.

Nothing here has been loaded into the game yet. What exists is development tooling that runs against a sandbox program.

## Layout

| Path | Content |
| --- | --- |
| `docs/plan.md` | The implementation plan: verified facts, design decisions, gates, milestones, risks |
| `layer/` | The Vulkan layer (C++17, MSVC x64): interception, shadow state, frame capture, named-pipe probe |
| `sandbox/` | A deterministic Vulkan test program and the regression scripts |
| `server/` | Python client for the probe, a command-line tool and an MCP server |
| `scripts/` | Dependency fetch, repository checks, offline analysis |
| `.ai-memory/` | Project memory for agents; `AGENTS.md` is the working agreement |

## Requirements

- Windows x64, MSVC build tools (found through `vswhere`, or set `VCVARS` to the full path of `vcvars64.bat`).
- Vulkan headers. `scripts/fetch_vulkan_headers.ps1` clones the pinned tag of `KhronosGroup/Vulkan-Headers` into `F:\vulkan\Vulkan-Headers`. A Vulkan SDK is used instead when `VULKAN_SDK` is set or one is installed in `F:\vulkan`.
- Python 3.12 for the client, the MCP server and the tests; standard library only.

## Build and test

```bat
layer\build.bat
sandbox\build.bat
python sandbox\run_all.py
```

`run_all.py` builds both, then runs the offline tests and the end-to-end tests against the sandbox. The layer is enabled only inside the child processes the tests start; nothing is registered system-wide.

## Licence

GPL-3.0. Later milestones reuse code from XXMI / 3Dmigoto, which is under that licence.
