# Resource inspection in layer 0.4.0

This read-only diagnostic feature is verified in the sandbox. It has not been
loaded into the game, does not establish G3, and does not make a mod render.

## Descriptor contents

`efvk_get_draw` includes descriptor slots 0 through 3, separately tracked for
graphics and compute. Each slot keeps its existing `set` handle and `dyn` array
and adds a `contents` object:

- `snapshot: "submit"`, allocation id, generation, layout id, `tracked`, an
  unavailable reason when needed, and `pipeline_compatible`;
- bindings in number order, type, allocated array count, stage/binding flags,
  immutable sampler flag, and sparse elements with array index and `known`;
- buffer id, base/dynamic/effective offsets, resolved range, whole-size flag,
  and CPU visibility (`mapped`, `shadow`, `none`);
- image/view/sampler ids, declared image layout, view format and subresource,
  image format, dimensions, mip levels, array layers, samples and usage.

Dynamic offsets split by set, binding number and array element, including
multi-set calls and nonzero `firstSet`. See the
[Vulkan binding specification](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBindDescriptorSets.html).

`vkUpdateDescriptorSets` tracking supports buffer, dynamic buffer, texel buffer,
sampler, combined image sampler, sampled/storage image and input attachment
descriptors. Writes and copies follow array order, including consecutive bindings
and copies after all writes. Immutable samplers and variable allocation counts
are preserved. See
[VkWriteDescriptorSet](https://docs.vulkan.org/refpages/latest/refpages/source/VkWriteDescriptorSet.html).

Set/pool destruction and reset discard allocations. Monotonic ids distinguish
reused handles. Set and pipeline layouts outlive their handles while a tracked
object needs them. Incompatible rebinds disturb tracked sets according to
[pipeline layout compatibility](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html#descriptorsets-compatibility).

## Shader inspection

`efvk_pipeline` includes each directly supplied stage's entry point, shader id,
SPIR-V size and CRC-32C. `efvk_shader` reads that id, lists declared names and
direct set/binding decorations, and optionally saves exact `.spv` bytes in the
server output directory. Bytecode survives module destruction while a live
pipeline references it, and is reclaimed when its final reference disappears.
Inline `VkShaderModuleCreateInfo` is also retained by the tracker.

The pipe command is `shader` with `id` and `code` (0: metadata, 1: binary payload).
Framing remains protocol 1. The additional MCP tool is `efvk_shader`.

## Limits

- Descriptor contents and buffer visibility are from submit time. `read_buffer`
  reads current bytes. Neither proves record-time CPU availability for G3 b/c.
  Update-after-bind writes during GPU execution are not a history of GPU reads.
- Templates, push descriptors and descriptor buffers are not decoded here.
  Intercepted update/bind paths discard stale knowledge and name the unsupported
  path. Unsupported binds conservatively clear all sets at the affected bind
  point, or both points for unimplemented Info/template variants. Core/KHR and
  maintenance6 aliases are intercepted where present.
- Unimplemented descriptor types (inline uniform blocks, acceleration structures
  and others) remain unknown. Unknown layout extensions are not fully modelled;
  compatibility across different handles is conservative.
- Unwritten elements remain unknown. A snapshot emits at most 256 stored elements
  per set per draw, with counts and `truncated`. This is metadata, not image pixels.
- Swapchain images from `vkGetSwapchainImagesKHR` are not tracked as images yet;
  a known view can have `image_known: false`.
- SPIR-V parsing covers direct `OpName`/`OpDecorate` on declared variables. It does
  not resolve decoration groups, types, member offsets, actual shader usage,
  specializations, pipeline-library stages or shader-object pipelines. Stripped
  names are null. This is limited metadata, not full reflection.
- New command aliases, inline modules, compute descriptors and texel descriptors
  do not all have driver-level coverage. Native tests cover state semantics;
  only the paths listed below have sandbox evidence.
- Overhead, visual output and Khronos validation remain unmeasured/unverified.

## Verification on 2026-10-11

Output: `build/next/`. `dist/` retains the historical 0.2.0 passthrough DLL for G1.
Run `D:/Python312/python.exe -B sandbox/run_all.py` with the absolute `build/next`
path in `EFMI_VK_DIST` for the test process only. On this machine `python` resolves
to the Windows app alias; use the explicit interpreter.

| Check | Result |
| --- | --- |
| Build and English text check | Pass |
| Python unit tests | 44 / 44 |
| Offline C++ tracker tests | 37 / 37 |
| Layer entry, 0 / 2 / 5 recording threads | 385 / 387 / 387 checks passed |
| Inject entry, 0 / 2 recording threads | 387 / 389 checks passed |
| No-layer, passthrough and late-injection modes | 40 / 40 |
| MCP end to end | 46 / 46 |

The sandbox actually uses two sets, a dynamic uniform buffer, an array of dynamic
storage-buffer descriptors, and a sampled 1x1 texture with an immutable sampler.
It copies descriptors from a set that is then freed, alternates multi-set and
separate-set binds, destroys shader modules before drawing, and rewrites one
storage descriptor after the trigger. Checks verify ranges, CPU-visible bytes,
texture metadata, binding names and exact SPIR-V fingerprints before/after the
rewrite through both entries. Existing index hashes and recording-thread checks
remain in the same suite.

Native tests additionally cover spanning writes/copies, undefined copies,
variable counts, layout disturbance/lifetimes, pool reset, handle reuse,
unsupported paths and shader reclamation. They make no Vulkan calls.

Test artifacts stay in `build/tests/`; compiler temporary files stay under each
component's ignored `build/tmp/`. No dependency or system configuration changed.
