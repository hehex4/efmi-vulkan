# Resource inspection in layer 0.5.0 and 0.5.1

This read-only diagnostic feature is verified in the sandbox. It has not been
loaded into the game, does not establish G3, and does not make a mod render.

## Descriptor contents

`efvk_get_draw` includes descriptor slots 0 through 3, separately tracked for
graphics and compute. Each slot keeps its existing `set` handle and `dyn` array
and adds a `contents` object:

- `kind: "set"` with `snapshot: "submit"`, or `kind: "push"` with
  `snapshot: "record"`; `resource_snapshot: "submit"` in both cases;
- allocation id (0 for pushes), generation, layout id, `tracked`, an unavailable
  reason when needed, and `pipeline_compatible`;
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

## Update templates and push descriptors

The tracker copies each template's entries and retains its layout definition.
Updates decode each entry's byte offset and stride, including padded application
structures, zero stride, array ranges and consecutive bindings. Supported types
are the same as for ordinary writes. Template destruction removes the decoder
without erasing descriptors already written; a reused handle gets a new decoder.
Unknown templates/layouts discard stale state. See
[template entries](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorUpdateTemplateEntry.html).

Push descriptors are stored separately for each command buffer and graphics or
compute bind point. Each update creates immutable contents retained by later
draws until another update. Partial writes retain compatible untouched elements;
layout disturbance discards them. Immutable samplers remain undefined until
explicitly pushed. No descriptor set is allocated (`set: "0x0"`, `id: 0`) and no
dynamic offsets apply. Secondary draws keep their recorded contents when copied
into a primary; executing secondaries invalidates the primary's descriptor
bindings. Reset releases current push state and recorded draws. See
[push semantics](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdPushDescriptorSet.html).

Push templates use the bind point and set number specified at template creation.
They share the same decoding and immutable recording path as direct pushes.
Core/KHR and maintenance6 Info entry points are forwarded separately and feed
these shared trackers. Dynamic pipeline layouts supplied only through `pNext`
are not decoded; they report unknown. See
[push templates](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdPushDescriptorSetWithTemplate.html).

## Shader inspection

`efvk_pipeline` includes each directly supplied stage's entry point, shader id,
SPIR-V size and CRC-32C. `efvk_shader` reads that id, lists declared names and
direct set/binding decorations, and optionally saves exact `.spv` bytes in the
server output directory. Bytecode survives module destruction while a live
pipeline references it, and is reclaimed when its final reference disappears.
Inline `VkShaderModuleCreateInfo` is also retained by the tracker.

The pipe command is `shader` with `id` and `code` (0: metadata, 1: binary payload).
Framing remains protocol 1. The additional MCP tool is `efvk_shader`.

## Bounds added in 0.5.1

A review before the first probe session in the game found that several parts scaled with the
size of the observed process. None of this changes what is reported for the sandbox.

- **SPIR-V budget.** Code is kept up to `shader_budget` bytes in total (default 512 MiB;
  `EFMI_VK_SHADER_BUDGET` or the settings file). A module created past the budget keeps its
  CRC-32C and size, `efvk_pipeline` shows `code_retained: false` for it, and `efvk_shader`
  refuses to return code for it. The total is given back when the last reference dies. The
  status shows `counts.retained_shader_bytes` and `counts.shaders_not_retained`.
- **Destruction cost.** Destroying a module or pipeline looks only at the shader ids
  involved. Resetting or destroying a descriptor pool looks only at that pool's sets.
- **Capture budget.** The capture request takes `descriptor_budget` in bytes (default 64 MiB,
  0 for none); `efvk_capture_frame` takes `descriptor_budget_mb`. Past the budget a set's
  `contents` is `{"tracked": false, "reason": "capture_descriptor_budget"}`. The capture
  reports `descriptor_bytes` and `descriptors_omitted`. Draws, hashes, set handles and
  dynamic offsets are not affected by the budget.
- **Reduced tracking.** With `resources = 0` (`EFMI_VK_RESOURCES`) nothing in this document
  is kept: bound sets report `reason: "resource_tracking_off"`, and the dynamic offsets of a
  bind call are recorded with its first set, as before 0.4.0. Draws, buffers, hashes, pipeline
  vertex input and call counters are unchanged.
- A pipeline with a mesh or task stage does not have its vertex input and input assembly
  pointers read.

Verified by `sandbox/limits_test.py` (100 checks through both entries) and 16 native tracker
checks. How these costs behave in the game is unmeasured.

## Image content and pipeline creation (0.7.0)

Sandbox only so far.

**Content of a texture.** Two textures of the same size, mip count and format cannot be told apart
by their description, and an image handle means nothing in another process or under another API.
From 0.7.0 the layer follows `vkCmdCopyBufferToImage` (also `2` and `2KHR`) to mip 0, layer 0 of
an image. When the command buffer is submitted, a copy that covers the whole level has its source
bytes read (mapped memory, or the copy taken at unmap) and hashed with CRC-32C, row by row
without the padding a `bufferRowLength` or `bufferImageHeight` adds, so that the value is that of
the packed level. A sampled image element of a descriptor then carries

```json
"content": {"state": "hashed", "crc": "5f3a9c21", "bytes": 4194304, "uploads": 1}
```

`state` is `hashed`, `none` (no such copy was seen), `partial` (the last copy covered a part),
`unreadable` (the source was not CPU-visible at submit), `unsupported_format` (the layer does
not know the texel size; it knows the 8, 16 and 32-bit colour formats, packed 32-bit formats,
D16, D32 and BC1 to BC7) or `too_large`. `uploads` counts the copies to that level. The status
has `counts.image_content` with the number of uploads per outcome, the bytes hashed and the
time spent in microseconds.

The same value is computed on the DX11 side by `scripts/dx11_baseline.py capture --textures`
(mip 0 read back through the DX11 probe), and `summarize` pairs textures by content first.

Cost and bounds: the work happens once per upload, on the submitting thread, with the resource
lock held shared while reading. `image_hash_limit` (default 64 MiB, `EFMI_VK_IMAGE_HASH_LIMIT`)
bounds one upload; `image_hash = 0` (`EFMI_VK_IMAGE_HASH`) switches the feature off, and so does
`resources = 0`. Upload memory can be slow to read; the time counter is there to see it.

Not covered: images filled by a blit, an image copy, a clear or rendering; other mips and
layers; a copy recorded in one frame and never submitted.

**What pipeline creation uses.** To replace a draw the layer will have to create pipelines like
the game's. `api_usage` therefore has `pipeline_creation`: the number of graphics pipelines
created and a counter per use, for example `flags:0`, `stages:2`, `dynamic:0` (one per dynamic
state), `topology:3`, `samples:1`, `next:<sType>` and `stage_next:<sType>` for chained
structures, `specialization`, `base_pipeline`, `tessellation`, `sample_mask`,
`depth_stencil_given`. Only state that has to be valid is read; depth-stencil and colour blend
state, which a pipeline without such attachments may leave dangling, are counted by presence.
The `pipeline` command adds `create_flags`, `subpass`, `specialized`, `dynamic_states`,
`layout_sets` (ids of the set layouts) and `push_constants`.

Verified by `sandbox/probe_test.py` (the texture is uploaded as a 2x2 part, as the whole level
with packed rows and again with padded rows; the reported value is the CRC of the last one,
computed independently) and `sandbox/limits_test.py` (switched off; limit below the texture).

## Limits

- Ordinary descriptor contents and all buffer visibility are from submit time;
  pushed descriptor values are from recording. `read_buffer` reads current bytes.
  Recording a buffer handle/range does not prove record-time byte availability for G3 b/c.
  Update-after-bind writes during GPU execution are not a history of GPU reads.
- Descriptor buffers remain unsupported. Intercepted unsupported update/bind
  paths discard stale knowledge and name the gap. Unknown push templates clear
  both bind points because their creation bind point is unavailable; known but
  unsupported push layouts clear only the affected point. Only graphics and
  compute bind points are tracked, not ray tracing.
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
- GPU coverage uses core 1.1 template creation/update/destruction and KHR push /
  push-template commands. KHR template lifecycle/update aliases, core 1.4 push,
  maintenance6 Info commands, compute pushes and texel pushes have no driver
  coverage. Native tests cover shared state semantics; they do not validate wrappers.
- Overhead, visual output and Khronos validation remain unmeasured/unverified.

## Verification on 2026-10-11

Built and verified in `build/next/`, then the exact tested binaries and manifest
were copied to `dist/` with SHA-256 equality checked. `dist/efmi_vk.cfg` selects
probe mode. Run
`D:/Python312/python.exe -B sandbox/run_all.py` with the absolute output path in
`EFMI_VK_DIST` for the test process only. `EFMI_VK_LOG` directed the native tracker
log into `build/tests/`; other test logs already use their
own scratch directories. The historical game log is unchanged. On this machine
`python` resolves to the Windows app alias; use the explicit interpreter.

| Check | Result |
| --- | --- |
| Build and English text check | Pass |
| Python unit tests | 44 / 44 |
| Offline C++ tracker tests | 62 / 62 |
| Ordinary sets, layer 0 / 2 / 5 threads | 416 / 418 / 418 checks passed |
| Ordinary sets, inject 0 / 2 threads | 418 / 420 checks passed |
| Templates, layer 0 / 2; inject 0 / 2 threads | 420 / 422; 422 / 424 checks passed |
| Direct pushes, layer 0 / 2; inject 0 / 2 threads | 427 / 429; 429 / 431 checks passed |
| Push templates, layer 0 / 2; inject 0 / 2 threads | 427 / 429; 429 / 431 checks passed |
| No-layer, passthrough and late-injection modes | 40 / 40 |
| MCP end to end | 46 / 46 |

A separate sandbox loaded `dist/efmi_vk_inject.dll` without a mode environment
override, reported version 0.5.0 and probe mode from the actual settings file,
captured five draws using push templates and two recording threads, and exited
normally. This verifies the prepared package, not the real game. The previous
0.4.0 package/settings/log are in `build/backups/before-0.5-20261011-214412-992/`.

The sandbox actually uses two sets, a dynamic uniform buffer, an array of dynamic
storage-buffer descriptors, and a sampled 1x1 texture with an immutable sampler.
It copies descriptors from a set that is then freed, alternates multi-set and
separate-set binds, destroys shader modules before drawing, and rewrites one
storage descriptor after the trigger. Checks verify ranges, CPU-visible bytes,
texture metadata, binding names and exact SPIR-V fingerprints before/after the
rewrite through both entries. Existing index hashes and recording-thread checks
remain in the same suite.

`--descriptors ordinary|template|push|push-template` selects the material path.
Template cases use padded data, copy to another set, destroy the initial template
and copy source before drawing, then update one element using a second template.
Push cases use non-dynamic storage descriptors with changing per-draw offsets,
so a later push overwriting an earlier draw's contents fails the range checks.
Direct pushes split buffer and texture updates to verify incremental retention.
Both push modes require `VK_KHR_push_descriptor`; unsupported devices fail
explicitly instead of silently skipping this part of the suite.

Native tests additionally cover spanning writes/copies, undefined copies,
variable counts, layout disturbance/lifetimes, pool reset, handle reuse,
unsupported paths and shader reclamation. Added cases cover template strides,
overlapping entries, handle reuse, immutable push samplers, partial updates,
separate command buffers/bind points, push snapshot retention and secondary
execution/reset. They make no Vulkan calls.

Test artifacts stay in `build/tests/`; compiler temporary files stay under each
component's ignored `build/tmp/`. No dependency or system configuration changed.
