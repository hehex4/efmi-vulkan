# Sandbox Vulkan validation

## Installation and reproduction

`scripts/build_validation.py` builds Khronos VVL and its pinned `known_good.json` dependencies
under `F:/vulkan`, using the existing MSVC/CMake/Ninja tools. Optional `--proxy` affects child Git
processes only. No SDK installer or registry registration is used. Compiler scratch stays there.

```powershell
& 'D:/Python312/python.exe' -B scripts/build_validation.py
$env:EFMI_VK_DIST = 'F:/efmi vulkan/build/next'
& 'D:/Python312/python.exe' -B sandbox/validation_test.py
```

Source: [Khronos build instructions](https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/vulkan-sdk-1.4.321.0/BUILD.md).
Tag `vulkan-sdk-1.4.321.0`, commit `fee7b2b4a926355e48668e29570be129eb82cb36`.
Manifest/DLL: `F:/vulkan/validation-1.4.321.0/bin/`.
DLL SHA-256: `71d93886750ea08219f31375a23d5ec36d8c251de9aa2b9c2a25077aad97b8c5`.
SPIRV-Tools (including `spirv-val`) lives under VVL's `external/Release/64/` tree. This is not a full SDK.

The sandbox explicitly enables VVL and a debug-utils callback. The harness uses child-only
`VK_LAYER_PATH` (`EFMI_VK_VALIDATION_DIR` overrides the default directory).
`VK_LAYER_VALIDATE_SYNC=1` adds synchronization validation to core checks, following
[LunarG's guide](https://vulkan.lunarg.com/doc/view/1.4.321.1/windows/synchronization_usage.html).
All sandbox child cache/temp paths now point into their `build/tests/sandbox-*/` directory.

## Evidence and fixes

On 2026-10-11, **76 checks passed**: 24 positive configurations covering no EFMI, loader entry
and inject entry; ordinary/template/push/push-template descriptors; primary/two-thread secondary
recording. Every run rewrites index/material data. Both EFMI entries also capture under validation.
Each entry has a negative canary: a zero-sized buffer produces `VUID-VkBufferCreateInfo-size-00912`
and is aborted by the callback before reaching the driver. Only that canary error is expected.

Positive runs report zero validation warnings/errors. Two specific loader configuration notices
are counted separately: disabling implicit overlays and enabling EFMI through the environment.
Their full text is retained; no VUID or synchronization diagnostic is filtered.

The first run found missing `NonWritable` on the sandbox fragment storage buffer
(`VUID-RuntimeSpirv-NonWritable-06340`). Fixed it and shader cleanup on pipeline creation failure.
Review also added transfer-to-index barriers and one render-finished semaphore per swapchain image,
following [Khronos' semaphore guidance](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).
The EFMI layer binary did not change.

Full regression passed: unit 44, native 62, all 17 probe configurations, modes 40, timing 84,
validation (initially 68 checks), MCP 46. Final targeted validation adds both EFMI canaries and
cache confinement: 76/76. Evidence: `build/regression-validation.txt`, `build/validation-final.txt`,
and per-run stdout. Performance was remeasured after the fixes (`docs/probe-performance.md`).
Corrected sandbox SHA-256: `0d97c52094b83e28ed1b9d00887d6240aef111dc1efbccb649b329088ffe81e5`.

## Limits and temporary-cache incident

Coverage is core/synchronization validation on the existing NVIDIA device. GPU-assisted validation,
other GPUs, visual correctness and the real game are not established. Untested API variants remain
listed in `docs/probe-resources.md` and LIMIT-011.

Before cache redirection, VVL created/updated the 36-byte file
`C:/Users/MR/AppData/Local/Temp/shader_validation_cache.bin`. This was an unintended external write,
not an SDK installation or registry change. It was left untouched because the directory is outside
the write boundary. Later runs write caches inside the project and do not modify that file.
VVL checks `XDG_CACHE_HOME` before TMP/TEMP even on Windows; the harness now sets all for its child.
