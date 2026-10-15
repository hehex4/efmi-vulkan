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

DXBC float32 arithmetic uses one explicit denormal policy. Hardware mode declares
DenormFlushToZero32. Software mode inserts integer bit operations before/after
scalar/vector arithmetic to replace subnormals with signed zero, leaving pure
data moves intact. Both modes require SignedZeroInfNanPreserve32 and
RoundingModeRTE32. GPU execution checks actual logical enablement and physical
properties before creating a pipeline. Application feature import remains
separate from owned sandbox device enablement.

The bounded lowering pass retains original result IDs and transfers
NoContraction to the arithmetic instruction. It accepts generated scalar/vector
float32 operations and refuses unsupported matrix, specialization or extended
arithmetic shapes without modifying the caller's program. The input/output and
identity budgets also bound expansion. Software flushing does not implement
missing infinity/NaN or rounding device support.

GLSL Fract can round small negative normal inputs to1. Native [frac requires
values below1](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-frac).
Both paths bound that result to the largest float below1 with an ordered compare
and select; NaNs are not replaced with a numeric clamp. Native WARP comparisons
cover scalar/vector boundaries, positive/negative denormals, signed zero,
negation/absolute value, arithmetic, comparison, rounding, saturation and frac.

Local properties in build/float-controls/result.txt show that the RTX5060 Laptop
GPU exposes neither float32 preserve nor explicit flush-to-zero, while the Intel
RaptorLake-S device supports both. Candidate1.3.5 tests software denormal handling
on NVIDIA and strict hardware handling on Intel. Both have the required NaN/
infinity/signed-zero and RTE properties. Software mode never declares unsupported
flush-to-zero support. These are owned tests, not application feature enablement.

Unchanged upstream libs/vkd3d/state.c itself overrides
denormal modes to ANY. A separate exploratory NVIDIA run removed only the strict
mode declarations from a translated module. Sixteen positive/negative boundary
inputs agreed with native WARP for copies, addition, multiplication and zero
comparison, including signed zero. Evidence:build/float-controls/default_mode
.cpp/.txt. This is a small default-mode experiment, not broad conformance or a
change to the strict candidate. Expanded negation/absolute-value cases below
demonstrated why this default-mode experiment was insufficient.

The reference matrix follows the original [Direct3D floating-point
rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/floating-point-rules):
arithmetic flushes denormals with sign preserved, data moves retain bits, and
min/max have specific allowed variations. [Vulkan runtime SPIR-V
requirements](https://docs.vulkan.org/refpages/latest/refpages/source/RuntimeSpirv.html)
forbid declaring a denormal mode when its physical property is false. The
[Vulkan SPIR-V environment](https://docs.vulkan.org/spec/latest/appendices/spirvenv.html)
allows implementation-dependent default denormal behavior. Small positive test
results do not establish every arithmetic operation or a different driver.

Buffer representations can differ from DXC: vkd3d maps raw/structured SRVs to
R32_UINT texel views and buffer UAVs to scalar SSBO storage. Candidate1.3.6 adds
bounded SM4.x/5.0 buffer declaration contracts using the original token header.
The native scan API omits structured stride and cannot distinguish typed uint
buffers from structured ones. The supplementary reader cross-checks scanner
registers/raw flags; it does not parse INI or modify upstream source.

Original INI views retain their byte offsets and ranges. Structured allocations
must match the source declaration's stride; raw and typed view classes must also
match. The executable's runtime-array stride comes from reflection, including
the type-description fallback, and is distinct from the source structure size.
Source MiscFlags determines whether StructureByteStride is meaningful: native
typed buffers can contain a nonzero ignored stride field.

When a program declares a typed buffer UAV, all its buffer UAVs use texel views.
A scalar SSBO would lose typed-format conversion and component width. Typed UAV
reads use unknown SPIR-V format; actual per-binding image read/write/atomic
provenance determines required format support. Logical feature enablement plus
enabled core1.3/KHR_format_feature_flags2 queries are checked, including the
per-format read/write bits and atomic support. Unused declarations do not imply
an access. Known shader formats still require exact view-format agreement.

Owned tests compare the exact reassembled DXBC and unchanged INI view metadata
with native WARP:12/16-byte structures, raw sources/destinations, typed sources,
nonzero regions and normalized RGBA8 read/write conversion mixed with structured
output. Missing features/query support and incompatible structure strides refuse
before submission, preserving earlier output. This is separate from application
buffer import. Unrepresentable Vulkan offsets still refuse; original owned INI
views have no imported UAV counter, so counter access remains an explicit gap.

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
sign_mode.cpp, sign_nvidia.txt, sign_intel.txt. This failure motivated the
source-correct software lowering in1.3.5.

Verified1.3.4 full rebuilt frozen-source suite:build/next/run_all-1.3.4.out,
all steps passed; runtime475/resources9908, compiler172/58, corpus37/37,
source hashes108/9/12 match and core/sync clean through device shutdown. Current
strict GPU execution uses Intel explicitly. Installed inputs/dist1.0.2 unchanged;
no game or deployment. Regex runtime publication still remains separate.


Float32 bytecode lowering now preserves signed denormals in arithmetic while
leaving data moves intact. Software handling runs on NVIDIA, hardware handling
on Intel. Both explicitly require NaN/infinity/signed-zero preservation and RTE;
Fract retains the native result below1. Native WARP comparisons cover eight
boundary/vector cases plus compute, graphics and descriptor execution.

Full rebuilt frozen-source run:build/next/run_all-1.3.5.out. All code/GPU steps
pass (runtime475, resources10650, compiler172/58; core/sync clean). Two installed
corpus steps fail because fangyi ul.ini contains identical duplicate resource
sections (INPUT-001); do not label the full run green. A source-only project
copy removes those two blocks:70 declarations,55 distinct HLSL programs all
compile/validate. Evidence:build/corpus-1.3.5/manifest.json and hlsl/report.json.
Source hashes110/9/12 match; all88 copied installed inputs remain unchanged.
Text358 clean. No game, deployment, launcher or system action; dist1.0.2 unchanged.

Continue offline with exact native buffer-view contracts and regex executable
publication. Do not equate scalar SPIR-V storage stride with the original
structured stride. Source identity, command publication and executable lifetime
must stay associated atomically. No game input needed; keep going. PNG paused,
LOD0/LOD1 only. See D-065, LIMIT-033 and docs/bytecode-compiler.md.
