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

## Original graphics linker, candidate1.2.0

The compiler now retains original DXBC input/output/patch register signatures
and executable SPIR-V IDs. link_graphics validates the source register/component
contract, canonicalizes stage order and relocates consumer locations by source
semantic/index. Names are case-insensitive with optional zero indices. Built-ins
remain untouched; patch and per-vertex arrays are distinguished. Native modules
and dependencies remain available and input program objects are not mutated.

The linker bounds stage, ID, module, signature, dependency and aggregate payload
sizes before copying. It rejects missing/ambiguous producers, stale metadata,
packed components, user interface blocks and specialized/unknown array shapes.
Wider output vectors are allowed only with actual enabled maintenance4 and its
vector rules; other shapes must agree. This does not yet specialize unsupported
interfaces or execute original INI graphics commands.

Native signatures are shared register layouts, not semantic name lookup at draw
time. An initial reference omitted the vertex position member in its pixel input
and therefore read native registers incorrectly. The fixture was corrected and
native register checks added; development failure evidence is retained under
build/graphics-link-native-invalid/. Current valid fixtures retain original
register layout while DXC's case-sensitive alphabetic IO order differs.

The headless GPU reference renders16 RGBA32F pixels. Raw Vulkan produces
(207,311,2,3); linked Vulkan and original DX11 WARP both produce(702,1103,7,11),
byte for byte. Graphics/tessellation/geometry interface fixtures also validate
independently. Complete rebuilt suite:build/next/run_all-1.2.0.out, all pass;
resources4065, compiler172/58 valid modules, core/sync clean, source hashes match.
This host uses private test submissions; it is not production INI draw placement.

Next: source fixed-function state, production graphics/descriptor execution,
coordinate/position-w/ID behavior and matching/directory integration. No game
input needed. No deployment or game action.

Sources: [native signatures](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-signatures),
[Vulkan interfaces](https://docs.vulkan.org/spec/latest/chapters/interfaces.html),
and [DXC SPIR-V conventions](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst).

## Source coordinate and state foundation, candidate1.2.1

Native DX11/Vulkan pixel references exposed BUG-023: pixel SV_Position.w and
vertex/instance IDs were raw Vulkan values. The compiler now uses DXC conversion
options for reciprocal pixel w and nonzero base vertex/instance. Tests cover
non-unit clip w, top-left pixel centers, depth, culling/front face, direct start
offsets, signed indexed vertex bases, first indices and multiple instances.
Negative viewport height and source winding conventions produce matching pixels.
DrawParameters requires actual shaderDrawParameters enablement; the independent
headless host enables it explicitly. Production graphics must check it or lower
these built-ins without silently requiring an unsupported feature.

SourceGraphicsState supplies defaults, replace and bitmask-merge descriptions
using the unchanged source helper. Native merge methods create real WARP states
and agree on effective fields, blend constants, sample masks and stencil refs.
Source pixel-sampler override changes slot0 only. Struct padding is unspecified
and excluded from state comparisons. The bridge does not yet convert or emit these
states in an original INI graphics pipeline. Original native invalid-state
creation/fallback and imported application states are separate executor work.

Full rebuilt regression:build/next/run_all-1.2.1.out, all pass; resources4559,
compiler172/58 modules valid, zero core/sync messages,76 resource/9 compiler hashes
match. No deployment/game action. Continue owned original graphics execution.

Independent native probe build/source-state-probe/samplers.cpp confirms original
comparison-filter samplers with ordinary SampleLevel return normal sampled
values. Shader instruction usage must choose comparison behavior; the source
filter comparison bit alone is insufficient. Integrate this diagnostic into the
production sampler/draw tests next.


## Owned graphics execution (verified candidate 1.2.2)

Production ShaderCommands now records owned VS/PS graphics work with shared
immutable descriptors, private render passes and submission-retained pipeline,
framebuffer, vertex/index/argument and target owners. Direct, indexed, instanced
and indirect draws share the resource domain and actual enabled feature checks.
State/attachment/layout/range validation occurs before recording. No hidden CPU
readback or queue wait is added; GPU indices/counts remain a caller-valid contract.

Native DX11 WARP and Vulkan pixel references agree for distinct stage bindings,
textures, vertex input, blend constants, depth tests, signed indexed bases and
nonzero draw offsets. Tests cover discarded/submitted lifetime and quota rollback.
Complete frozen-source rebuild/regression: build/next/run_all-1.2.2.out, all steps
pass; resources 4749, compiler 172/58 valid modules, zero core/sync messages through
shutdown. All 80 resource and 9 compiler source hashes match; text 316 files clean
before this memory update. This is owned graphics API execution, not original INI
graphics calls, application pass placement or game rendering. Dist 1.0.2 unchanged.

Next 1.2.3: connect original CustomShader stages/state and Draw commands, including
inherited layouts, automatic counts, source samplers and state restoration. Then
finish remaining shader semantics, matching and enabled-directory integration.
Current owned graphics supports VS/PS and triangle/line topologies; geometry,
tessellation, dual-source output lowering and additional attachment/interface
cases remain explicit work. No game input needed; continue without stopping.
PNG paused; LOD0/LOD1 only. No deployment, game or system action by this work.


## Original owned INI graphics (verified candidate 1.2.3)

Original CustomShader now overrides/inherits/restores VS/PS and source graphics
state in the owned executor. Direct, indexed, instanced, indirect, automatic
counts and direct from-caller commands use original argument and buffer rules.
Native state creation canonicalization is applied to inactive depth/stencil/blend
fields; invalid active stencil creation falls back to the native null state.
Null VS does no work; null PS preserves color while accepted depth still writes.
Semantic state keys bound pipeline reuse; descriptors and targets remain retained.

Native DX11 WARP pixel references cover all these draw paths, nested restoration,
constant blending, inherited scissors, depth acceptance, and original point/linear
sampler overrides on PS slot0 without changing slot3. Ordinary sampling ignores
the source comparison-filter bit; actual comparison instructions explicitly refuse.
Root failure restores CPU state and prevents submission of partial graphics work.

Full frozen-source rebuild/regression: build/next/run_all-1.2.3.out, exit0, all
steps pass. Resources5342, compiler172/58 valid modules, core/sync clean through
shutdown; all82 resource and9 compiler source hashes match. Text318 files clean.
This is original INI execution on owned sandbox images, not application resources
or real characters. Dist1.0.2 unchanged; no deployment/game/launcher/system action.
Development fixtures were corrected for original clear syntax and typed depth-view
rules; production constraints were preserved.

Continue1.2.4 with all enabled Mods/Core HLSL declarations from the original parser
and compiler diagnostics for the complete corpus. Then input layouts, remaining
shader/state/resource cases, matching and application/directory integration.
Current frontend still explicitly refuses vertex layouts, geometry/tessellation,
point/line raster rules, depth bias, multisampling, comparison sampling, null
resource emulation and attachment-free graphics. Source state validity beyond the
measured cases remains a compatibility boundary, not a full native fallback proof.
No game input needed; continue without stopping. PNG paused; LOD0/LOD1 only.


## Owned geometry and original Core text (verified candidate1.2.5)

Original CustomShader now executes VS/GS/PS on owned resources. Point, line and
triangle assembly feed triangle-strip geometry output; actual geometryShader,
depthClamp, scalar-layout and draw-parameter enablement are checked. Geometry
primitive compatibility, invocation/output counts and component limits refuse
before Vulkan pipeline creation. The last raster stage must provide position.
Descriptors remain stage-specific and submission-retained. Tessellation, geometry
point/line output, adjacency and standalone point raster rules remain explicit.

The unchanged installed TextPrinter.hlsl and LiberationSans-Bold.dds now run
through original INI resource/compute/graphics commands in the headless sandbox.
Three cases cover ordinary text, newline/wrapping/DPI/centering, and70-character
right/bottom alignment across the64-character geometry chunk boundary. Native
DX11 WARP agrees on exact chunk start/count fields; maximum floating position
and RGBA differences are5.96046448e-08 (test tolerance1e-6). Visible glyph/background
assertions prevent an empty-image false pass. Native default alpha blending is
ONE/ZERO when only the RGB blend key is set. Scalar stride72 requires enablement.
This is the original HLSL/font with controlled owned fixtures, not the complete
Core INI, application backbuffer/window integration or a game rendering result.

Full frozen-source rebuild/regression:build/next/run_all-1.2.5.out exit0, all
steps pass. Resources5644, compiler172/58 valid, full directory37/37 programs,
zero core/sync errors/warnings through shutdown;84 resource,9 compiler,12 corpus
source hashes match. Installed shader/font and corpus sources unchanged.
Dist1.0.2 unchanged; no game/deployment/launcher/system action.

Continue1.2.6 with owned input layouts and original layout-edit lifetime, then
remaining shader/state/resource and matching/directory/application integration.
Independent native probes confirm layouts retain their creation-signature
register map, even after switching VS; APPEND alignment and native failure rules
are recorded in docs/input-layout-audit.md. These probes are not Vulkan layout
execution yet. No game input needed; keep going. PNG paused, LOD0/LOD1 only.


## Owned input layouts and original edits (verified candidate1.2.6)

SourceInputLayout retains immutable element descriptions and the native creation
signature, including required-but-unused semantics. Vulkan input locations follow
the current VS's native registers and the layout's original register mapping.
Creation validity/APPEND alignment, slot rates and source formats are compared to
system DX11 WARP. Native-valid large offsets remain distinct from Vulkan limits.

Original element-offset/format edits run before parameter flush and argument
execution, including zero draws and dispatches. The pinned helper is unchanged,
including its enum-based realignment boundary. Failed native creation keeps the
old layout; unsupported execution refuses instead of faking creation failure.
Edits survive nested CustomShader and Present calls. Intercepted draw completion
restores the original layout on success/failure. Effective layout attributes and
stream rates participate in the pipeline key. Zero instance step adds the native
StartInstanceLocation byte base before using a zero Vulkan binding stride.

Native pixel tests cover switched VS/creation signatures, instance steps0/1 with
nonzero bases, packed R8/R16 APPEND input, component defaults, nested edits,
invalid-creation fallback, original draw restore and failed-root GPU discard.
Full frozen-source rebuilt regression:build/next/run_all-1.2.6.out exit0, all
steps pass; resources6093, compiler172/58, corpus37/37, clean core/sync validation,
source hashes88/9/12 match. Installed inputs unchanged. No game/deployment action;
dist1.0.2 remains unchanged. This is owned execution, not application layout import.

Continue offline with remaining input fetch/divisor/type cases and original
shader matching/directory integration. Native and physical probes show both GPUs
support KHR instance divisors, but neither exposes packed16 BGR vertex formats;
those require lowering, not a blind VkFormat substitution. Type reinterpretation,
null/OOB inputs, divisors above1 and indirect zero-divisor reads remain explicit
boundaries. No game input needed. PNG paused, LOD0/LOD1 only.

## The installed image panel in the owned domain (candidate 1.4.3)

Three source rules were missing for the image panel that about twenty installed
Mods ship as `k/1.ini`:

- **Cached bytecode.** `CustomShader::compile` builds the cache name from the
  HLSL path without its extension, the shader model and the compile flags in
  lowercase hexadecimal (`Draw_2D.vs_5_0.8000.bin`) and loads that file instead
  of compiling when its last-write time equals the HLSL's exactly
  (`load_cached_shader`). It writes caches only with `cache_shaders`. All 19
  enabled Mods with a panel ship both caches with matching times, so the panel
  that runs under DX11 is the shipped DXBC. `GpuShaderRuntime` now follows the
  rule: with `enable_cached_bytecode` such a cache runs through the exact DXBC
  translation of `docs/bytecode-compiler.md`; without the opt-in a valid cache
  refuses instead of running a recompile. Nothing is ever written beside a Mod.
- **Null views.** D3D11 reads a null view as zero, reports zero dimensions and
  drops writes. The panel declares `t101` and leaves it null unless minimised.
  An unbound slot now becomes a null descriptor when the device enables
  VK_EXT_robustness2 `nullDescriptor` (`ShaderHost::null_descriptors`); without
  it the call still refuses. Robust buffer and image access stay disabled.
- **Back buffer and window.** The draw targets `o0 = set_viewport bb` and its
  Present list reads the window, resolution and cursor (1.4.0 to 1.4.2).

`sandbox/resource_panel_cases.h` loads the installed panel of the first enabled
Mod that ships it (copied with its modification times, with a stub declaring the
main INI variables it reads), draws its character mark, presses Ctrl+K and lets
it fade in over 24 frames into a host back buffer. Both CustomShader stages come
from the shipped caches. The reference replays the final draw on native D3D11
with the shipped DXBC, the PNGs through the pinned WIC loader and the owned
run's final IniParams, once on WARP and once on the hardware adapter.

Result on the NVIDIA GeForce RTX 5060 Laptop GPU, which both APIs use: the
owned Vulkan panel differs from native hardware D3D11 in 2 of 360,000 pixels
(largest channel difference 5). WARP differs from that hardware driver itself in
30,979 pixels (largest 2), because the shader is dominated by sin, exp, pow and
atan2 whose precision both APIs leave to the implementation; WARP is therefore
not an exact reference here. The check requires the same adapter for both APIs
and at most 0.01% differing pixels, each within 16 levels.

This is the owned sandbox domain with a test-owned back buffer. The game's swap
chain, the layer and the character generation are step 4 of the panel plan.
