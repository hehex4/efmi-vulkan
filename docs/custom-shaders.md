# Original CustomShader execution

Candidate 1.1.7 adds an opt-in GpuShaderRuntime extension. It reuses the pinned
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
count budget. Discard releases unsubmitted invocation objects.

The host supplies actual properties and enabled features. A pinned SPIRV-Tools
validator DLL is loaded from an explicit absolute path. Every executable is
validated before pipeline creation using the actual scalar/uniform block-layout
enablement. DXC's DirectX layout option alone does not prove a module is usable
on an arbitrary Vulkan device. Workgroup, descriptor, buffer alignment/range,
image format/type and dispatch-count limits are checked before recording.

The original INI adapter currently maps direct compute dispatches, constant
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

This is owned offline compute execution. The following remain explicit work:

- CustomShader graphics stages and fixed-function state, drawing and graphics
  coordinate conventions; these paths refuse when executed.
- Indirect dispatch, workgroup shared-storage layout/size proof, additional
  capabilities/float controls, null resource semantics and buffer-view offset
  adaptation when native alignment is less restrictive than Vulkan's.
- Source sampler/counter state adapters. Low-level execution supports these
  descriptors, but original INI slot-state mapping is not connected yet.
- Storage formats that need specialization or unknown-format feature gates.
  Current storage views must match the executable's declared format.
- Exact sampler/image usage pairs. The current validation conservatively checks
  all sampled views against all bound samplers, which can reject a legal shader
  containing both filtered and unrelated unfilterable images.
- Shared shader matching, enabled-directory rendering, application resources,
  real command placement and render-pass integration. No game load, deployment
  or character result is implied. PNG remains paused; LOD0/LOD1 only.

References: pinned XXMI CommandList.cpp/IniHandler.cpp;
[Vulkan dispatch requirements](https://docs.vulkan.org/spec/latest/chapters/dispatch.html),
[descriptor buffer ranges](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorBufferInfo.html),
and [DXC buffer layouts](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst).
