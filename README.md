# EFMI Vulkan

Runs the installed EFMI (XXMI) Mods of Arknights: Endfield on the game's native Vulkan renderer,
through a Vulkan layer of our own, instead of 3DMigoto on the DX11 renderer. The bar is 3DMigoto's
experience: the first frame shows the Mod, nothing flickers or stands in, and the frame rate does
not drop.

Work in progress. The [current state](.ai-memory/CURRENT_STATE.md) records the deployed version, what
was verified where (build, sandbox, captured frames, the game) and what is open; the
[known issues](.ai-memory/KNOWN_ISSUES.md) and the [tasks](.ai-memory/TASKS.md) go with it.

## State (1.9.1, 2026-10-26)

- The layer reads the EFMI Mods folder and Core itself (read-only) and runs every character Mod's
  INIs through the original XXMI engine. In game sessions of 2026-10-26 it replaced Si
  NightveilTrancer, Tangtang, Yvonne, arcane and others with their meshes, textures and bones, ran
  their key bindings, key panels and persistent settings, and drew no game mesh before a Mod's (the
  layer's counters from the game and what the user saw there).
- Open: the game froze once with the Mods' Ctrl+K key panels under DLSS Frame Generation (BUG-058);
  1.9.1 removes the per-frame driver allocations behind it, and what a panel creates the first time
  still runs under the engine's locks. A Mod's first use of very large textures (64 MiB 8192x8192
  maps) holds its draw while they load. The layer's share of the render thread is still being
  reduced (D-121).

## How it works

- **Interception.** One DLL, loaded either by the Vulkan loader as a layer (`efmi_vk_layer.dll`) or
  injected by the XXMI launcher (`efmi_vk_inject.dll`, redirecting the exports of `vulkan-1.dll`).
  It tracks every Vulkan object the game creates and computes the index-region hash XXMI matches
  on (CRC-32C of the draw's index range).
- **The original engine.** The pinned XXMI source (parser, expressions, command lists, pools,
  texture-override matching, keys, F10 reload) is compiled into the layer; each Mod folder is a
  generation of it. A matched draw's command lists are planned into Vulkan draws of the Mod's
  meshes with the game's own pipelines (variants for the Mod's vertex layouts). The EFMIv1 Core's
  native lists (merged skeleton, spatial identification) run on the renderer with the Core's
  semantics. DX11 facts a Mod asks of the game's shaders come from their SPIR-V by one register
  rule, checked against paired DX11/Vulkan captures ([source slots](docs/source-slots.md)).
- **Owned GPU work.** CustomShaders, Present lists (the key panels), resource copies and compute
  run on an owned runtime on the game's device: HLSL through DXC, shipped DXBC caches through
  vkd3d-shader, ordered against the game's work by semaphores ([owned Present](docs/owned-present.md)).
- **Assets.** Meshes in device memory per package, textures loaded on first bind and kept as the
  original keeps them ([texture residency](docs/texture-residency.md)), bones imported on the GPU.
  EFMITTL translucency and RabbitFX texture roles are supported as libraries.
- **Exchangeable module.** The replacement logic is a module the loaded layer can exchange while
  the game runs ([hot swap](docs/hot-swap.md)); the hooks and the tracker stay.
- **Probe.** Status, frame capture and an in-layer sampler over the named pipe
  `\\.\pipe\efmi-vk-<PID>`, with a command-line client and an MCP server.

[How 3DMigoto does it](docs/3dmigoto-flows.md) compares each flow of the original with the
layer's; [the plan](docs/plan.md) has the gates, decisions and evidence history.

## Using it in the game

The game gets the DLL through the XXMI launcher: the EFMI profile in its Vulkan configuration, with
`dist\efmi_vk_inject.dll` as the injected library. Both are the user's settings in the launcher.
`dist\efmi_vk.cfg` configures the layer:

| Setting | Meaning |
| --- | --- |
| `m2_mods`, `m2_core` | the EFMI Mods folder and Core (`...\EFMI\Mods`, `...\EFMI\Core\EFMI`) |
| `m2_enabled` | replacement on (1) or the probe alone (0) |
| `m2_gpu_bones` | merged skeleton imported on the GPU (1) or on the CPU (0) |
| `settings_save_seconds` | how often changed persistent variables are saved, as `settings_auto_save_interval` (60) |
| `mode = passthrough` | forward every call untouched, no tracking, no pipe |

Persistent Mod variables are kept per Mod folder in `dist\settings\`, never next to a Mod; the
installation and the Mods stay read-only. F10 reloads the Mods folder as in XXMI. The layer's log is
`dist\efmi_vk_layer.log`; a stall watchdog in it names the threads that hold up a presentation.

## Layout

| Path | Content |
| --- | --- |
| `layer/` | The Vulkan layer (C++17, MSVC x64): hooks, tracker, character backend, owned presenter, probe; `layer/module/` the exchangeable module |
| `frontend/xxmi/` | The original XXMI engine's host: generations, draw plans, owned GPU runtime, Core natives |
| `frontend/gpu/`, `frontend/assets/` | Vulkan resource and shader commands, register rules, DXBC/HLSL compilers; DDS/PNG and the resource cache |
| `sandbox/` | A Vulkan test program, the replay of captured frames and the regression suite (`run_all.py`) |
| `server/` | Python probe client, command-line tool and MCP server |
| `scripts/` | Build, checks against captures and the installed Mods, profiling, deployment helpers |
| `docs/` | Design documents and evidence |
| `.ai-memory/` | Project memory for agents; `AGENTS.md` is the working agreement |

Captures (`captures/`), build output (`build/`, `dist/`) and game assets are not in the repository.

## Requirements

- Windows x64 with the MSVC build tools (found through `vswhere`, or `VCVARS` set to `vcvars64.bat`).
- Python 3.12, standard library only.
- Under `F:\vulkan` (`EFMI_VK_VULKAN_ROOT`): Vulkan headers (`scripts/fetch_vulkan_headers.ps1`), the
  pinned `XXMI-Libs-Package` source, DXC 1.9.2609, vkd3d-shader 2.1 built by
  `scripts/build_bytecode_compiler.py`, SPIRV-Reflect, and the Khronos validation layers built by
  `scripts/build_validation.py` ([validation](docs/validation.md)).

## Build and test

```bat
python scripts\build.py
python sandbox\run_all.py
```

`build.py` builds the layer, the module, the sandbox program and the test tools into `dist\`
(`--out DIR` for another folder while a running game holds `dist\`). `run_all.py` builds and runs the
suite: text and language checks, the original engine's tests, the installed Mods' offline plans,
captured frames replayed through the layer with real game pipelines and real Mods
([capture replay](docs/capture-replay.md)), the owned GPU runtime, the sandbox end-to-end tests and
Vulkan validation. The layer is enabled only inside the child processes the tests start; nothing is
registered system-wide.

## Diagnosing a running game

```bat
python server\efvk.py status
python scripts\profile_game.py <PID> --seconds 30
python scripts\symbolize_crash.py dist\efmi_vk_layer.log dist layer\build layer\build\module
python scripts\hot_swap.py --pid <PID> --out build\next
```

`status` reports the replacement counters, texture residency, bone import, owned work and the
engine lock; `profile_game.py` samples the threads that call into the layer and names where their
time goes ([profiling](docs/profiling.md)). MCP for agents: `python server\run_server.py`
(registered in `.mcp.json`).

## Licence

GPL-3.0. The layer compiles the XXMI / 3DMigoto source, which is under that licence.
