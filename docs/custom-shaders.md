# Original CustomShader execution

Candidate1.1.8 extends the opt-in GpuShaderRuntime introduced in1.1.7. It reuses the pinned
original CustomShader section keys, compiler-flag/state parsers, expressions,
command lists and per-frame counter objects. The compiler boundary produces
source-reflected SPIR-V as described in hlsl-compiler.md. All declared shader
sources, including dormant ones, are compiled before accepting a generation.
Unlike the original loader's log-and-remove behavior, a compilation failure
rejects the whole candidate and preserves a separately retained old generation.

## Owned compute execution

ShaderCommands creates actual Vulkan compute pipelines, descriptor sets,
buffer views and samplers. A dispatch receives explicit reflected bindings;
it does not infer original slots. Supported low-level descriptors include
uniform/storage buffers, uniform/storage texel buffers, sampled/storage images,
samplers, fixed descriptor arrays and separate UAV counter buffers. Format
scalar types and storage-image format facts come from the pinned registry.
Each dispatch owns immutable descriptor contents. Fence tickets retain pipeline,
pool, view, sampler and resource owners after an INI generation retires.

Commands share ResourceCommands' single host-serialized queue and explicit
outside-render-pass domain. Barriers order preceding transfers/shader writes
before later consumers. The executor never implicitly submits or waits. The
original store adapter can explicitly submit/wait for same-list feedback.
Resident/staging budgets remain shared; shader objects have a separate shared
count budget. Samplers also use a shared host-reserved quota bounded by the
physical sampler limit; application samplers consume physical capacity too.
Discard releases unsubmitted invocation objects.

The host supplies actual properties and enabled features. A pinned SPIRV-Tools
validator DLL is loaded from an explicit absolute path. Every executable is
validated before pipeline creation using the actual scalar/uniform block-layout
enablement. DXC's DirectX layout option alone does not prove a module is usable
on an arbitrary Vulkan device. Workgroup, descriptor, buffer alignment/range,
image format/type and dispatch-count limits are checked before recording.

The original INI adapter currently maps direct/indirect compute dispatches, constant
buffers, structured/raw/typed resources and image bindings into this executor.
It preserves compiler source-path namespace/root fallback; include fallback uses
the explicit legacy working directory (default captured from the actual process
working directory), with no global CWD changes. Null compute stages produce no
work. Nested CustomShader phases share abort/dirty state and have no extra root
parameter flush. Original limits and reset_per_frame_limits use the same counters.

On return, including abort or failure, the adapter restores the compute stage,
logical viewport and OM RTV/DSV/pixel-UAV slots. Ordinary SRV/CB/VB and compute-UAV
bindings survive, as in the original source. Root post execution has its existing
independent abort state. Failed nested execution poisons its GPU scope.

## Verification and boundaries

Headless tests read actual buffer/image values, compare the production parameter
dispatch to native DX11 WARP, verify descriptor snapshots across multiple
recorded dispatches and check counter atomics. They cover source slot mapping,
typed/raw buffers, arrays, linear sampling, storage images, lifetime/discard,
budgets, wrong types, missing descriptors and malformed modules. A packed float3
fixture refuses without scalarBlockLayout and executes on a separately enabled
device with its native 12-byte stride. Original INI tests cover nested/inherited/
null compute, pre/post, per-frame reset, viewport/OM restoration, retained
ordinary bindings, abort, recursion, failed candidates and old-generation reuse.

Review reproduced an earlier adapter omission: only indexed draw arguments were
guarded. A direct dispatch using iniparams->width reached the unadapted source
query and failed. The common argument walker now covers every parsed draw and
dispatch argument plus indirect resource targets. The original INI compute test
uses this resource-query dispatch and passes; before-fix evidence is retained in
build/resource-commands/dispatch-query-before-fix/. An earlier full run was
interrupted for this correction (run_all-1.1.7-interrupted-review.out).

The final frozen-source rebuilt suite passes in build/next/run_all-1.1.7.out:
3633 resource checks,117 compiler checks,33 independently valid modules and
every other suite step. Core/sync stays clean through shutdown; all recorded
source hashes match. This includes the corrected dispatch-query fixture.

## Compute follow-up in1.1.8

Bounded executable analysis measures fixed workgroup scalars, vectors, matrices,
arrays and structures before pipeline creation. Native DX11 and Vulkan tests
produce identical neighbour-exchange/atomic results in independent workgroups.
Explicit/aliased/initialized shared layouts and specialization-dependent sizes
still require device-specific adapters. The analyser caps module size, id count,
type depth, provenance entries and work; it refuses unknown resource provenance.

Actual OpSampledImage constructors now determine image/sampler descriptor pairs,
including constant array elements. Fetch-only images no longer inherit unrelated
sampler restrictions. Dynamic indices conservatively expand to possible elements;
cyclic or non-inlined resource provenance refuses rather than losing uses. A
mixed integer-fetch/linear-sampling GPU fixture proves the earlier false refusal
is removed. Comparison samplers do not require ordinary linear-filter capability.

Indirect compute reads its argument buffer directly on the GPU. Shader/transfer
writes are ordered before indirect reads; fences retain argument owners. The
original explicit byte offset is used, independently of resource/view offsets.
Null source is a no-op. GPU counts must obey device limits, as in Vulkan itself;
there is no hidden CPU readback, wait or count rewriting. Tests use GPU-produced
counts, zero counts, invalid ranges/usages/domains and source-query expressions.
The original parser splits indirect operands at the first comma, so a region
with multiple arguments must be referenced through a previously assigned alias.

Owned source sampler slots default to the D3D11 null-sampler state: linear
filtering, clamp and the full signed LOD range. Native DX11 comparison covers
interpolation, edges and negative LOD. An explicit host API snapshots Vulkan
sampler state for source slots; null restores the original default. It is not
application-state import or new INI syntax. Shared sampler quotas prevent
unbounded owner creation across generations and roll back after failure.

Source audit: original CommandList.cpp FillOutBufferDesc(UAV) explicitly leaves
append/counter flags as TODO; SetResource also preserves the current counter
with -1. Consequently owned INI buffers must not invent append/counter views.
Low-level counter execution remains tested; importing existing application
counter views is separate integration work. The original CustomShader sampler
override affects pixel s0 only and remains part of graphics-state work.

Targeted1.1.8 resource checks:3720, zero core/sync messages through shutdown.
The complete frozen-source rebuilt suite also passes in
build/next/run_all-1.1.8.out;69 resource and6 compiler source hashes match.
No deployment/game action. Native binding probes found BUG-022: source read/write
conflicts, batched ranges and OM restore effects still need adaptation. Fix next;
passing current tests is not proof of those semantics. The distinct-resource
compute/native output comparisons remain valid.

This is owned offline compute execution. The following remain explicit work:

- CustomShader graphics stages and fixed-function state, drawing and graphics
  coordinate conventions; these paths refuse when executed.
- Additional capabilities/float controls and workgroup layouts, null resource
  semantics and buffer-view offset
  adaptation when native alignment is less restrictive than Vulkan's.
- Original pixel sampler overrides and imported application counter/sampler
  state; owned compute defaults and explicit host sampler slots now work.
- Storage formats that need specialization or unknown-format feature gates.
  Current storage views must match the executable's declared format.
- Additional resource-provenance forms and dynamic-index correlation, beyond
  the bounded constructor/array/selection analysis above.
- Shared shader matching, enabled-directory rendering, application resources,
  real command placement and render-pass integration. No game load, deployment
  or character result is implied. PNG remains paused; LOD0/LOD1 only.

References: pinned XXMI CommandList.cpp/IniHandler.cpp;
[Vulkan dispatch requirements](https://docs.vulkan.org/spec/latest/chapters/dispatch.html),
[descriptor buffer ranges](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorBufferInfo.html),
[DXC buffer layouts](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst),
[source null sampler state](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-cssetsamplers),
and [Vulkan sampler limits](https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCreateInfo.html).


## Source binding follow-up, candidate 1.1.9

BUG-022 is fixed for owned resources. Source bindings now resolve read/write
hazards across every shader stage and IA slot. A buffer is one D3D11 subresource
regardless of view byte ranges. Texture mip and array-layer ranges can coexist.
Source slot ranges materialize all views/copies before one atomic binding update;
conflicting output batches preserve the previous state as native D3D11 does.

RTV/DSV assignments validate complete attachment compatibility. Depth dimensions
may exceed color dimensions. Every such assignment clears the pixel UAV set.
Pixel UAV calls replace the whole pixel UAV set, unlike compute UAV calls, and
also remove RTV registers at/after the first assigned UAV register, including
null UAV assignments. CustomShader OM restoration clears conflicting inputs and
compute UAVs. The original save helper only captures pixel slots 0-7; complete
restoration clears higher slots. The original optimizer removes empty shader
calls, so an empty list does not exercise restoration.

Native WARP references cover these state transitions, disjoint texture ranges,
invalid batches, attachment sizes and high registers. Invalid native calls are
isolated because they can affect later observations even after ClearState. Older
fixtures now unbind outputs before reading, preserving all data checks. Full
rebuilt regression: build/next/run_all-1.1.9.out, all steps pass; resources 4006,
core/sync clean through shutdown, 71 resource and 6 compiler hashes match.
No application binding import or graphics drawing is implied. No game action.

Native contracts and observed source behavior are checked alongside
[OM target binding](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets)
and [OM UAV binding](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargetsandunorderedaccessviews).
