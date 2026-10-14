# Original HLSL to Vulkan compilation

The CPU compiler accepts the original shader file, stage, compiler flags,
recursive_include setting and an explicit working directory. It reads source
files without modifying them or writing caches beside installed Mods. This is
the compiler foundation for CustomShader; INI shader execution is still separate.

## Source contracts

Pinned XXMI CommandList.cpp uses main, shader model 5.0 and an empty-valued stage
macro for each of VS/HS/DS/GS/PS/CS. The adapter preprocesses with D3DPreprocess and
the original include policy, then compiles the same expanded text with both the
original D3DCompile and DXC. Original DXBC reflection supplies source register
facts. DXC's independently assigned register numbers are not assumed identical.

Includes are relative to the initial shader directory for modes 0 and -1, with
an explicit legacy working-directory fallback. Other nonzero modes use the
current includer directory. No process working directory or environment changes.
Limits are 32 MiB per file, 64 MiB total, 1024 dependency reads and 256 nesting
levels. Exact dependency bytes are retained, including empty files. A failed
candidate leaves the caller's existing program untouched. Native D3DCompile
accepts zero-byte includes, whereas standalone D3DPreprocess returns E_INVALIDARG
without diagnostics. For preprocessing only, an empty include is represented by
one newline; dependency bytes stay empty. A native comparison covers this case.

Each original register class has a distinct Vulkan binding range in set zero:
stage index (v,h,d,g,p,c) times 1024, plus b=0, t=128, s=256, u=384 or UAV
counter=512, plus the source slot. Descriptor arrays retain the original base
slot/count, including optimized-away leading elements in FXC's flattened array
reflection. Source register spaces other than zero refuse. Device descriptor
limits must still be checked by the execution adapter.

SPIRV-Reflect retains descriptor types, image dimensions, structured stride,
constant block sizes, IO semantics, thread dimensions and capabilities. Metadata
for source semantics/UAV counters is saved before removing reflection-only
Google annotations/extensions from the executable. DXC uses DirectX buffer
layout and alphabetic stage IO ordering. Graphics coordinate conversion and
interstage compatibility belong to the later pipeline adapter.

## Dependencies and evidence

- XXMI-Libs-Package a7847eae82aaac32175c9a7d199673d5c9323bcd.
- Existing DXC 1.9.2609 under F:/vulkan/dxc-1.9.2609; explicit absolute DLL path.
- SPIRV-Reflect SDK 1.4.321.0, e55086b044225f9b511ae44dbf9b079d3625943f,
  installed unmodified under F:/vulkan and compiled into the host.

scripts/check_shader_compiler.py exercises all six stages, source flags,
includes, arrays, resource classes and counters. Every generated module is
independently checked by spirv-val. Its report records source, dependency,
executable and generated-module hashes. The targeted compiler result is 116
passing checks, 33 independently valid modules. The resource GPU fixture compiles an
unchanged-style Texture1D/structured-UAV shader through the production compiler,
then compares actual Vulkan output to native D3D11 WARP DXBC output and the
65-float4 input. This proves the tested compute shader's values and remapped
bindings, not full CustomShader or game rendering.

## Explicit remaining differences

- Native shader-model-5 compilation rejects force_vs_software_no_opt,
  force_ps_software_no_opt and no_preshader flags. Those native failures remain.
- enable_backwards_compatibility is accepted by native FXC for modern source,
  but DXC refuses -Gec with -spirv. The adapter reports the failure; it does not
  silently drop the option. Legacy HLSL compatibility needs further work.
- ieee_strictness succeeds in the tested DXC path targeting Vulkan 1.2; the
  Vulkan 1.1 target fails DXC's own validation. The caller selects the target
  explicitly and receives its capabilities. Compilation is not proof of enabled
  logical-device float-control features/properties.
- Older partial-precision and DXBC validation/optimization hints are passed to
  the original compiler. SPIR-V validation is always retained. No equivalence of
  compiler optimization decisions or all floating-point edge cases is claimed.
- Storage image format specialization/unknown-format feature gating, null
  descriptors, graphics state and application placement remain executor work.
  Candidate1.1.7 adds owned direct compute, device/format checks, retained
  descriptors and production layout validation; see custom-shaders.md. Compiler
  checks are now117, including native warning plus DXC error visibility.

The original includes and compile settings are audited against local pinned
source. Compiler behavior is documented by the official
[DXC SPIR-V guide](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst)
and reflection data by
[SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect).

## Native graphics convention correction in1.2.1

The compiler now requests -fvk-use-dx-position-w,
-fvk-support-nonzero-base-vertex and -fvk-support-nonzero-base-instance. The first
option preserves source pixel position w; the others remove Vulkan draw offsets
from source vertex/instance IDs. Actual original DXBC/WARP and Vulkan output
comparisons expose each missing conversion and pass after correction. The pinned
DXC executable documents all three options in its local help. Shader modules may
now require DrawParameters; execution must use actual shaderDrawParameters
logical-device enablement or a separate lowering fallback. Compilation alone is
not an execution capability promise. See BUG-023 and docs/custom-shaders.md.
