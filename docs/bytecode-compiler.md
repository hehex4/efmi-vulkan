# Original bytecode translation

ShaderRegex modifies native DXBC assembly. Recompiling the original HLSL would
discard these modifications. The bytecode compiler translates the exact original
or reassembled DXBC with the unchanged vkd3d-shader2.1 library. It keeps the
source bytecode in the resulting program; it does not infer application shader
identity or connect unrelated SPIR-V to native shader hashes.

The input limit is4MiB, output32MiB and diagnostics1MiB. Native scanning reads
descriptor declarations and input/output signatures without relying on optional
RDEF/debug chunks that may be absent after original signature-aware assembly.
Register classes and stage offsets use the existing binding contract. Scanned
signatures preserve native output-mask inversion. Reflected IO is associated
with native registers; packed/component-sharing interfaces currently refuse.

The library is loaded by explicit absolute path and its version is checked.
Ambient VKD3D_SHADER_CONFIG and dump-path overrides refuse, rather than changing
the process environment or allowing external shader dumps. No shader cache is
created. A failed compilation leaves the previous caller program intact.

Native error ownership is significant: vkd3d's SPIR-V failure path frees its
temporary output but leaves the returned pointer non-null. Only successful code
and scan results are owned by the wrapper. An initial double-free in the wrapper
was exposed by the first unsupported float-mode test and corrected at this
boundary, without editing upstream source.

DXBC float32 denormals require flush-to-zero semantics. The compiler emits the
original mode using SPV_KHR_float_controls. GPU execution requires actual logical
extension/core enablement and the matching physical float32 property. Unsupported
widths/modes refuse. Disabling the mode merely to make compilation succeed would
change the source contract. Application feature import remains separate from
owned sandbox device enablement.

Local properties in build/float-controls/result.txt show that the RTX5060 Laptop
GPU exposes neither float32 preserve nor explicit flush-to-zero, while the Intel
RaptorLake-S device supports both. Candidate1.3.4's strict translated execution
therefore uses the explicitly selected Intel device. Existing resource/INI tests
remain on their original NVIDIA device. This is not evidence that the game GPU
can execute a module declaring the unsupported mode.

For the next adapter, unchanged upstream libs/vkd3d/state.c itself overrides
denormal modes to ANY. A separate exploratory NVIDIA run removed only the strict
mode declarations from a translated module. Sixteen positive/negative boundary
inputs agreed with native WARP for copies, addition, multiplication and zero
comparison, including signed zero. Evidence:build/float-controls/default_mode
.cpp/.txt. This is a small default-mode experiment, not broad conformance or a
change to the strict candidate. Next expand the reference matrix and distinguish
explicit device mode support from an explicitly measured default-mode policy.

The next reference matrix follows the original [Direct3D floating-point
rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/floating-point-rules):
arithmetic flushes denormals with sign preserved, data moves retain bits, and
min/max have specific allowed variations. [Vulkan runtime SPIR-V
requirements](https://docs.vulkan.org/refpages/latest/refpages/source/RuntimeSpirv.html)
forbid declaring a denormal mode when its physical property is false. The
[Vulkan SPIR-V environment](https://docs.vulkan.org/spec/latest/appendices/spirvenv.html)
allows implementation-dependent default denormal behavior. Small positive test
results do not establish every arithmetic operation or a different driver.

Buffer representations can differ from DXC: vkd3d maps raw/structured SRVs to
R32_UINT texel views and buffer UAVs to scalar SSBO storage. Native structured
stride must not be confused with the executable's scalar array stride. Owned
descriptor tests are distinct from adapting imported INI view metadata.

## Dependencies

scripts/build_bytecode_compiler.py builds only under F:/vulkan, validates the
unchanged release/header trees against pinned official archives, generates the
parser/configuration out of tree and links an explicit source list. Its report
records source/tool hashes and the exact commands. Tool locations and provenance:

- vkd3d2.1: https://dl.winehq.org/vkd3d/source/vkd3d-2.1.tar.xz
  SHA2567510146aff2adfb4ae07ab890701a607e5ff7c66e57100cfc9f630ec92eeda6a.
  Source/library under F:/vulkan/vkd3d-2.1 and vkd3d-2.1-build. Upstream LGPL2.1+
  notices and COPYING remain with the unmodified source; no binary is deployed.
- Portable GCC14.1.0 in w64devkit1.23.0:
  https://github.com/skeeto/w64devkit/releases/download/v1.23.0/w64devkit-1.23.0.zip
  SHA2565c7dce6762be3e0dba648a9317790444c0e2f1ef3e677315c115727d7a549539.
- winflexbison2.5.25:
  https://github.com/lexxmark/winflexbison/releases/download/v2.5.25/win_flex_bison-2.5.25.zip
  SHA2568d324b62be33604b2c45ad1dd34ab93d722534448f55a16ca7292de32b6ac135.
- Khronos SPIRV-Headers SDK1.4.321.0:
  https://github.com/KhronosGroup/SPIRV-Headers/tree/vulkan-sdk-1.4.321.0
  Archive SHA2565bbea925663d4cd2bab23efad53874f2718248a73dcaf9dd21dff8cb48e602fc.

This compiler alone does not publish regex links, swap application shaders or
establish game compatibility. Runtime publication must associate each successful
translation with the exact original source and preserve generation lifetimes.


The expanded exploratory sign/absolute-value case disproved broad default-mode
equivalence: NVIDIA retained subnormal bits in6/16 rows where native WARP flushed
to signed zero; Intel strict mode matched16/16. Files:build/float-controls/
sign_mode.cpp, sign_nvidia.txt, sign_intel.txt. Continue with source-correct
software lowering for this unsupported control case, not unconditional ANY.

Verified1.3.4 full rebuilt frozen-source suite:build/next/run_all-1.3.4.out,
all steps passed; runtime475/resources9908, compiler172/58, corpus37/37,
source hashes108/9/12 match and core/sync clean through device shutdown. Current
strict GPU execution uses Intel explicitly. Installed inputs/dist1.0.2 unchanged;
no game or deployment. Regex runtime publication still remains separate.
