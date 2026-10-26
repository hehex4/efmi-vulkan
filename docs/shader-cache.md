# Shader cache (1.9.2, D-126)

Requested by the user on 2026-10-26: 3DMigoto keeps a compiled CustomShader beside its HLSL
(`<name>.<model>.<flags>.bin`) and reads it on the next start; the layer should keep its own work
the same way.

## What the layer translates at load

Every generation compiles its Mod's CustomShaders when its INI loads, at the game's device creation
and again at every F10 reload. A stage comes from one of two paths:

- an HLSL source, compiled by DXC to SPIR-V (with FXC's bytecode for reflection, and as the fallback
  when DXC refuses what FXC took);
- a shipped 3DMigoto cache (`.bin`, used when its write time equals the HLSL's, as the original
  does), translated by vkd3d-shader (D-070).

Measured in the sandbox with the installed Mods and the actual configuration on 2026-10-26 (22
packages): 8 DXC compiles (0.59 s; the Tangtang panel's two stages alone 0.57 s on the first
measurement) and 62 translations (0.18 s), of a load of 2.4 s with the file system cache warm.

## The cache

`frontend/gpu/shader_cache.cpp`, folder `settings\shader-cache\` beside the persistent variables
and the variant pipeline cache (`m2_settings`, default the module's folder). One file per stage,
`<key>.efvkshader`: a header (format, the tools' identity, the key) and the whole `HlslProgram`
(SPIR-V, the reference bytecode, bindings, interfaces, signatures, the includes with their bytes,
thread counts, diagnostics).

- The key is a SHA-256 over the tools' identity, the path, the stage, flags, Vulkan version, include
  settings and the source's bytes (HLSL), or over the bytecode's bytes, stage and float mode (a
  shipped cache).
- The tools' identity covers DXC, vkd3d-shader, the system's FXC and the module the code is part
  of, by path, size and write time: a new build or another tool finds nothing, and the first cache a
  process opens on the folder removes the entries no build of these tools can read.
- An HLSL entry holds the includes as the compiler read them; one that changed or went makes the
  entry stale and the stage compiles again.
- A missing, stale or damaged entry is a miss; writing is best effort (temporary file and rename)
  and never fails a load. `shader_cache = 0` (`EFMI_VK_SHADER_CACHE`) turns it off.
- Identical shipped panels in many Mods share one entry, so even the first load translates each
  once.

The owned runtime's own pipelines (the CustomShaders' graphics and compute pipelines) are created
through the device's variant pipeline cache, which the layer writes to disk at shutdown.

## Measured

Sandbox, actual configuration, 22 packages, file system cache warm: 2.40 and 2.43 s without the
cache, 1.65 and 1.67 s with it (nothing compiled or translated), 35 entries, 7.4 MiB. The status
reports per package `owned_present.shaders`: `compiled`, `translated`, `cached` and their
microseconds.

## Tests

- `sandbox/shader_compile.cpp` (suite step "original HLSL Vulkan compiler"): a program serializes
  whole and reads back unchanged, a truncated one is refused, an empty cache misses, a stored stage
  reads back as compiled, another flag or source is another key, a changed include is stale, a
  damaged entry misses, shipped bytecode keys by bytes, stage and float mode, and a folder's first
  opening removes another build's entries and keeps its own.
- `sandbox/owned_present_test.py`: the same Mod folder started twice; the second start reads both
  panel stages from the cache, compiles nothing and draws the same picture (failed before the cache:
  2 stages compiled again).
