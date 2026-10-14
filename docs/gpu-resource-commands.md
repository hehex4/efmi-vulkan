# Original INI GPU resource execution

## Architecture and scope

The next stage after the source-defined language inventory is a common GPU
resource execution backend, followed by CustomShader, shared shader matching
and complete enabled-directory rendering. This order is the user's instruction
on 2026-10-14. Continue offline until actual game evidence is necessary.

`frontend/gpu/resource_commands.*` and `resource_images.cpp` own mutable Vulkan
buffers/images and private command buffers. `frontend/xxmi/gpu_resources.*` is an opt-in production-runtime
extension: the original parser, expressions, branches, command lists, resource
declarations and pool logic remain authoritative. The draw planner mediates
resource commands through this extension; it does not translate INI text again.

This is an execution foundation, not complete directory graphics integration.
The existing character renderer still uses its earlier immutable file plans.
The new backend requires an explicit outside-render-pass scope on one host-
serialized queue. It does not borrow application resources, split a render pass,
change application descriptors or insert blocking work into a draw hook.
Stage slots currently hold logical source-API buffer/image bindings for the next
shader adapter; a binding is not proof of a Vulkan descriptor bind or draw.

## Mutable image foundation (1.1.2)

The low-level executor creates owned optimal-tiling 1D/2D/3D images, arrays,
cubes and complete mip chains. Image views retain the underlying allocation,
format, dimension, subresources and usage. Mutable-format views use the Vulkan
compatibility classes; a matching byte size alone does not authorize a view.
An actual physical-device capability query checks each description/usage, and
the logical device's enabled features control compression, cube arrays and
multisampled storage. Dedicated allocation requirements are honored. Resident
images and transfer staging share the same budget ledgers as buffers.

Operations include pitched buffer-image upload/download, compatible image
region copies, color and depth/stencil clears, and color MSAA resolve. Transfers
cover arrays, volume slices, selected mips and compressed edge blocks. Depth and
stencil buffer transfers use their distinct aspect footprints. Buffer offsets
must meet both four-byte and aspect texel-block alignment. Host image transfers
use zero buffer offset and the exact required span through the last texel;
row/slice padding in a downloaded staging buffer is unspecified. Explicit
image-to-buffer copies preserve destination bytes outside the written texels.

Format facts for all 184 non-undefined Vulkan 1.0 core formats are generated
from the installed pinned Vulkan-Headers registry. The regression verifies the
generated snapshot against that registry. This is a footprint/compatibility
inventory, not a claim that a device supports all formats. Extension/multiplanar
formats, sparse/imported resources, 2D views of volume slices, copies between
different dimensionalities or block extents and depth/stencil resolve remain
explicit gaps. Images require Vulkan 1.1 and a serialized graphics queue.

Each batch tracks its private image layouts, uses transfer-optimal layouts
where possible, and returns touched images to GENERAL before submission ends.
Only a successful queue submission publishes the resulting layout state.
Discard and failed submission preserve the previous submitted state. Allocation
owners remain with fence tickets after executor retirement. View creation alone
does not initialize pixels, emit descriptors or make an image ready for a shader.

These operations were first exercised directly through the owned GPU executor.
The 1.1.3 adapter below connects original INI commands to this same image domain.
The existing immutable 2D DDS uploader and deployed renderer remain unchanged.

Source rules followed by the INI adapter, from the pinned CommandList.cpp:

- SubstantiateTexture1D/2D/3D starts from a zero description, then applies the
  original OverrideTexDesc. Do not invent default dimensions, array counts or
  sample counts. The source 3D depth override assigns Height rather than Depth;
  preserve/report its actual behavior instead of silently fixing the language.
- RecreateCompatibleTexture makes the description typeless, changes destination
  bind flags, resolves the 2D sample count when requested, removes generate-mips,
  then applies the destination overrides. Copy caches must remain independent
  of aliases and keyed by the complete source-API description, as with buffers.
- FillOutTex1D/2D/3DDesc defines fresh views. SRVs span the mip chain; writable
  views select mip zero. Retained source/cached views may have different ranges.
  Export the original CPU helpers rather than maintaining a second set of rules.
- ClearViewCommand prefers an existing view. An SRV does not become writable
  because its allocation has UAV flags. With no view, explicit depth/stencil or
  integer clear controls selection, otherwise DSV/UAV/RTV priority applies.
  Clear values use the selected DXGI view format, whereas vkCmdClearColorImage
  uses the allocation format. Typeless aliases, integer bit patterns and sRGB
  reinterpretation therefore need explicit adaptation and pixel-result tests.

## Original image commands (1.1.3)

`frontend/xxmi/gpu_images.cpp` consumes the original command objects. Texture
declarations call the original OverrideTexDesc helpers; allocation contents stay
undefined until initialized. Texture inline `data` is not an initializer in the
pinned source. Because its 3D depth override writes Height, a new empty Texture3D
declaration cannot establish Depth; the adapter refuses the resulting invalid
description. Valid volume DDS resources and copies of existing volumes work.
Validation applies only to fields consumed by that dimension's original
overload; unused height/depth/sample options on a 1D declaration stay ignored.

The bounded DDS path reuses pinned DirectXTK format decoding and surface sizing.
Legacy and DX10 files can supply arrays, cubes, volumes, mip chains and supported
BC formats. Packed D24/S8 and D32/S8 payloads split into Vulkan depth/stencil
planes. The file determines dimensions/format and cube-ness; declaration width
and format overrides do not resize/reinterpret that payload. Original sRGB and
bind/misc options still apply. Declaration property overrides retain their own
precedence over allocation facts. Non-DDS loading is an explicit gap; PNG stays
paused. Files are bounded to 128 MiB before allocation and subresource traversal.

Image bindings share the buffer adapter's original reference/null, range,
whole-pool and per-frame counter rules. Owned copy pools are keyed by complete
source descriptions and remain independent of aliases. `copy_desc` restores a
previous allocation without replacing its bytes. Original dimension, mip, array,
format, identity and metadata queries inspect the actual owner/view where the
source does; buffer-only properties retain original sentinels. Image `Region`
metadata is not a texel rectangle. Original `store` requires a buffer source:
its source implementation casts to ID3D11Buffer and calculates byte ranges.

Fresh views use original FillOutTex helpers. Native headless WARP tests establish
that UNKNOWN view format inherits a fully typed image's format, but cannot
choose one for a typeless resource. A typed resource requires that exact view
format; a typeless resource requires the same DXGI family. This is stricter than
Vulkan's format compatibility classes. The original allocation MakeTypeless
switch, including its linear BGRA omission, is preserved separately.

Clears follow the selected source view, not just the allocation format. SRV
clears do nothing; writable views cover mip zero and every selected array layer
or volume slice. Integer UAV clears pack low channel bits without float
conversion, including NaN payloads and small packed formats. A reusable compatible
UINT image receives those bits before a GPU copy. A differently formatted float
or sRGB view similarly uses a correctly typed scratch clear and bit copy. Scratch
owners share the resident budget and are reused by description. These are
correctness paths; their memory/performance has not been accepted in the game.

Depth clears use the original flag priority and clamp to [0,1]. Stencil-only
clears never submit an invalid unused depth value (BUG-018). Color MSAA resolves
operate on complete array subresources. `set_viewport` records the original full
destination dimensions, including zero height for 1D resources. SRV/UAV/RTV/DSV
slots and that viewport are logical state for the next shader adapter; they do
not yet emit application descriptors, render targets or viewport commands.

Remaining image cases include unmapped DXGI formats (including alpha-only,
BGRX and video formats), multisampled color/depth family copies, nonzero MSAA
quality/sample patterns, other source
misc flags, application imports and render-pass placement. Generate-mips flags
do not imply mip generation. Source-API invalid descriptions/views are explicit
execution refusals rather than reproductions of the original driver's log-and-
continue behavior. Valid shader use and volume-slice attachments need the later
shader/render-target adapter. This is not complete Vulkan language execution.

### Integer RTV and color/depth conversions (1.1.4)

Integer RTV clears now convert the original floating-point clear values before
packing their channel bits. Fractions truncate toward zero, NaN becomes zero,
and out-of-range values saturate to the destination channel's signed/unsigned
range. The calculation bounds a double before integer conversion, avoiding
undefined casts at infinity and the UINT32 endpoint. Original `int`/hex options
do not change RTV behavior; they still affect UAVs as before. Typeless aliases
clear using their selected view format and a compatible owned scratch image.

This is an observed native-API compatibility policy, not a claim that every
driver follows one overflow rule. Headless WARP and hardware both truncate the
tested fractions, despite the archived D3D11 DDI specification's general
nearest-even conversion wording. WARP saturates finite values outside the
32-bit range; this machine's hardware can instead wrap a UINT result or yield
the negative SINT endpoint. The adapter deliberately follows WARP saturation.
The repeatable test compares actual WARP pixels against all 19 supported integer
RTV formats plus two differently typed allocation/view cases over nine input
sets. It covers fractions, signs, channel saturation, large integers, infinity,
NaN, signed zero, denormals and original clear flags. Initial hardware/WARP
observations are under build/resource-commands/dx11-clear-{hardware,observation}.txt;
the production adapter never creates a D3D11 device.

Single-sample R16/R32 color/depth family copies now use a resident GPU buffer
between the two image aspects. Original whole-image copies iterate every mip
and array layer. The lower-level command also handles selected rectangles and
layers with independent source/destination offsets. A shared-budget scratch
buffer grows when necessary and is reused; submission tickets retain it with
the images. This records transfers without CPU mapping, staging allocations,
implicit submission or waiting. It is still an owned outside-render-pass path.

Opaque color bits entering D32 require the host to report that the logical
device actually enabled VK_EXT_depth_range_unrestricted. Physical support alone
does not suffice. Without the extension, that copy refuses and poisons the INI
scope, while `copy_desc`, D32-to-color copies and both R16 directions remain
available. The headless fixture creates a separate device with the extension
enabled and checks arbitrary float bit patterns, including NaN payloads. Ordinary
low-level buffer-to-depth calls still require the caller to ensure valid source
depth values when the extension is disabled.

Source review also found BUG-019: float depth uploads previously accepted values
outside Vulkan's permitted range without the extension. Host uploads now check
the copied D32 texels before recording, respecting pitch and ignoring padding.
The ordinary packed-depth DDS fixture now contains valid depths; the unrestricted
fixture separately verifies unusual float bits. Clean core/sync validation alone
cannot prove the validity of the bytes copied into a depth image.

## Buffer commands

The adapter supports explicit Buffer, StructuredBuffer and ByteAddressBuffer
declarations with inline data, files and zero padding. It invokes the original
description/stride override methods. File byte size and allocation byte width
remain distinct. Resources are instantiated on first use, within shared budgets.

- `ref` shares allocation; aliases retain their previous resource when another
  alias changes. Dynamic stage slots and optimizer bind/fetch batches execute in
  original command order. Explicit null and `unless_null` have distinct behavior.
- Whole-buffer `copy` copies bytes to a compatible cached destination.
  `copy_desc` creates/reuses its description without asserting initialized data.
  Copy pools retain their own allocation per description, separately from a
  current resource alias. A ref followed by copy cannot overwrite the referenced
  source. Returning to a previous description restores that owned allocation.
- UAV clear supports raw/structured views and one/two/four-component 32-bit typed
  float/integer views. Typed float conversion and exact uint bit patterns differ.
- `store` reads the actual GPU through staging and a fence. It uses the original
  structured-range calculator and output-variable writer: indices count four-
  byte values, out-of-bounds reads become zero, null leaves the old variable, and
  a result can control the next branch in the same command list.
- Direct, FIFO and spatial pools retain independent scalar/resource state.
  Original key lookup, slot reuse, expiry, optional read refresh/reset and
  per-frame copy counters stay active. Replacing a key does not by itself clear
  an existing resource. Expiry invalidates the pool entry when configured, while
  earlier stage bindings and submissions retain their resource owners.
- Original slot ranges bind/fetch pools and broadcast resources, including
  dynamic bounds, explicit null, unless_null and per-slot copies. Plain fetch
  bypasses single-copy counters and custom overrides, as upstream does.
- Buffer copy regions preserve the original CB-window and end-boundary rules,
  stride truncation, CB rounding and view-less vertex-to-structured conversion.
  Unwritten tails retain existing cached bytes; newly allocated undefined tails
  are not promised to be zero. Incompatible overrides refuse rather than emit an
  invalid copy. A source view and a buffer offset without a view are distinct.
- `raw` creates a raw view only when the allocation permits it; it does not
  change a compatible borrowed source view. `no_view_cache` drops the cached
  view while retaining live slot bindings and copy-pool ownership.
- Whole-pool assignment (`Pool = Resource` and `Pool[*] = Resource`) retains
  the source allocation/view before any destination changes. Each element has
  its own copy limit, metadata overrides and owned description cache; original
  proxy and mixed-element behavior stays active. A local destination preserves
  the original command across repeats and failure. As upstream does, source
  stride metadata is read per destination even though the GPU source is fixed.
- Buffer `mono` and `set_viewport` preserve the source engine's no-op modifier
  behavior. Buffer `resolve_msaa` creates/reuses a destination without copying
  bytes; it takes precedence over `copy` and is still not an image resolve.
  Existing initialized cache contents survive. These modifiers also execute
  through per-slot ranges and whole-pool assignments.

Original source views matter independently of allocation bind flags. A resource
fetched from an SRV slot carries an SRV view; original `clear` does not clear that
view merely because the same allocation could also support a UAV. A reference
into a UAV slot selects a UAV view. Keep this distinction in bindings, including
references back into custom declarations and reuse of compatible view caches.
The backend calls the pinned `FillOutBufferDescCommon` through a small exported
CPU helper. Its `buf_src_size` is an end boundary when building an SRV/UAV,
whereas a constant-buffer region uses a byte count. These source behaviors must
not be normalized into one assumed offset/length interpretation. A referenced
existing view also keeps its own format/range despite changes to out-of-band
resource metadata. A raw-capable allocation can still have a typed float view.

Plain ranges have their own source behavior. Fresh structured SRV/UAV range
views expose one element because the original helper receives zero metadata;
a compatible view fetched earlier from a slot keeps its full range. A plain
constant-buffer range binds the whole buffer, but unless_null preserves an
existing region. Pool indices use the pinned engine's unsigned size_t remainder,
including its surprising negative-index results for non-power-of-two sizes.
Nonfinite or out-of-int-range bounds refuse before the original integer cast.
Fresh typed/raw plain-range views without a compatible source view still refuse
explicitly. Their original view descriptor is not silently replaced with a
guessed format or full-buffer range. Image copy options remain separate work.

The host supplies frame duration to both original Present and draw contexts.
The frame_time operand and spatial pool radius use this same value; the original
ceil scaling is preserved. The existing character backend forwards its existing
measured duration, bounded to 1-250 ms. A frontend caller that supplies none keeps
the original startup zero rather than an inferred rate. Nonfinite/negative duration and overflowing
spatial radius refuse before the source engine's unsigned conversion.

## Resource properties

Original resource identity, stride/source_stride, size, format, bind flags,
offset, width, height, array and mip queries now use the owned buffer domain.
Declaration overrides take precedence, even on an unassigned alias; those
queries need no GPU allocation or active scope in Constants. Other queries need
an explicit execution scope. Missing resources, unknown values and non-texture
buffers preserve the distinct original sentinels. Vertex/index offsets use the
original helpers with an actual draw context; Present does not invent one.

View metadata is target-specific: GetViewInfo reads stage views, not a view
stored in a custom resource by plain range fetch. The latter retains its zero
format/stride metadata until the original rules derive other facts. Null keeps
the cached allocation/view, while resetting visible binding metadata; original
source_stride survives null and direct range fetch. A declaration containing
only format/stride overrides remains unassigned until something binds it.
The `this` target retains its requested property/region arguments when resolving
the underlying resource. Texture properties use the same original precedence
through 1.1.3; region/spatial hashes still need additional adapters.

## Synchronization and lifetime

Every owned allocation is charged against a shared resident or staging ledger.
Command submissions retain all resources they use through their fence, even
after the INI generation and executor retire. A readback waits explicitly; the
normal submission ticket can be polled. Mutable owners preserve allocation,
not historical contents. Reload generations create independent mutable state.

Transfers use memory dependencies and transfer usage bits. Host writes flush
noncoherent allocations; host reads wait for transfer-to-host visibility and
invalidate noncoherent allocations. Mapped ranges cover the complete allocation,
so noncoherent atom alignment is preserved. Resources from another execution
domain, invalid/overlapping ranges and budget failures refuse before recording
the offending command. This is a conservative synchronization implementation;
no performance or multi-queue acceptance is claimed.

A failed GPU scope cannot be submitted. Original-runtime Present/draw failures
notify the GPU extension even when a CPU expression fails outside its callbacks.
This prevents submitting earlier GPU commands from a partially executed INI list.
Discarding it makes the generation
unusable, because earlier original CPU assignments or store submissions cannot
generally be rolled back. A prepared prior generation remains independent.

## Verification

`scripts/check_resource_commands.py` builds the production original runtime and
a headless Vulkan fixture. It runs core/synchronization validation only in the
child process and writes `build/resource-commands/report.json` and `runtime.txt`.
It is part of `sandbox/run_all.py`.

Tests compare GPU bytes and original variable results, not merely command counts:
copy/clear order, aliases, same-list readback branches, description copies,
typed/structured clears, region references, buffer-file padding, optimized slot
batches/ranges, whole-pool/proxy assignments, fixed source ownership with changing
indices, scalar/resource pool resets, copy limits, generation isolation, retained
owners, negative execution paths and allocation release through shutdown.
Final counts and the full regression checkpoint belong in CURRENT_STATE.md.

Image fixtures additionally compare all DDS subresources, real format-specific
clear pixels, independent copy-cache bytes, depth/stencil planes, MSAA resolve,
native view-format acceptance and image/view lifetime. Negative descriptions,
incompatible DXGI families, truncated DDS and paused PNG loading refuse without
submitting partial work. Source hashes include both direct and INI image fixtures.
Conversion fixtures add native integer-clear comparisons, native color/depth bit
copies, mip/array/partial-region round trips, enabled/disabled extension paths,
pitched depth upload rejection, explicit submission counts, scratch reuse/budgets
and retirement after the last fence ticket releases its owners.

## Remaining execution work

Remaining image format/conversion cases above; region/spatial hash;
fresh typed/raw plain-range views; invalid source-API
description combinations; GPU
IniParams; application resource import and command placement; shared shader
contracts; CustomShader draw/dispatch/state; ShaderOverride/ShaderRegex; complete
directory routing and graphical reload. PNG integration remains paused. No
deployment, game load, module swap or character acceptance follows from this test.

## Next source audit: GPU IniParams

This is implementation guidance, not a completed parameter/shader adapter.
The pinned HackerDevice::CreateIniParamResources creates a single-mip, single-
layer R32G32B32A32_FLOAT Texture1D, with width equal to iniParamsReserved; an
empty parameter vector creates no image. HackerContext::InitIniParams publishes
zeros before Constants, then executes the original pre/post initialization.

ParamOverride::run evaluates into the original CPU vector and sets the current
CommandListState::update_params only when float comparison says the value
changed. Preserve that rule: changing positive zero to negative zero alone is
not dirty, whereas NaN compares unequal. The GPU copy contains the complete
vector when a flush occurs, including other components whose bits changed.

CommandListFlushState uploads before injected DrawCommand execution (including
dispatch) and at RunCommandListComplete boundaries. Nested calls share their
command state. Do not substitute one unconditional upload at Present or one
final vector snapshot for these ordering rules. The read-only `iniparams`
resource target and configured shader slot refer to the published GPU image,
which can lag the CPU vector before a flush. Test Constants, pre/post, nested
commands, signed-zero bits, reload lifetime and actual GPU consumption. The
current owned executor still requires an explicit serialized queue scope;
application binding and render-pass placement remain separate integration work.
The current RuntimeExtension::prepare runs after Constants, so it is too late
for zero publication before those commands. Add an explicit initialization
boundary rather than silently moving existing native Core preparation. The
current DrawPlanAdapter also finishes root pre/post lists without an extension
flush callback; cover root completion and each injected draw/dispatch separately.

## References

Pinned original `DirectX11/CommandList.cpp` and `CommandList.h` at
`a7847eae82aaac32175c9a7d199673d5c9323bcd` define the adapter semantics.

- [Vulkan buffer copy](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBuffer.html)
  defines copy ranges, transfer usage, ownership and outside-render-pass scope.
- [Vulkan buffer fill](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdFillBuffer.html)
  defines alignment and transfer synchronization.
- [Vulkan synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)
  defines the memory dependencies and host visibility rules.
- [Vulkan image creation](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageCreateInfo.html)
  defines image dimensions, usage and enabled-feature constraints.
- [Vulkan image views](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html)
  defines compatible formats and view subresource ranges.
- [Vulkan buffer-image transfers](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBufferToImage.html)
  defines block alignment and pitched transfer regions.
- [Vulkan image copies](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyImage.html)
  defines copy-compatible formats, overlap and sample constraints.
- [Vulkan color clears](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdClearColorImage.html)
  defines clearable formats; compressed images are not color-clear targets.
- [Vulkan image resolves](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdResolveImage.html)
  defines multisample color resolution and matching formats.
- [D3D11 integer UAV clear](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearunorderedaccessviewuint)
  defines bit-preserving typed clears and first-word raw/structured clears.
- [D3D11 float UAV clear](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearunorderedaccessviewfloat)
  defines float and normalized view conversion behavior.
- [D3D11 depth/stencil clear](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-cleardepthstencilview)
  specifies depth clamping and view-wide clears.
- [Vulkan depth/stencil clear value](https://docs.vulkan.org/refpages/latest/refpages/source/VkClearDepthStencilValue.html)
  requires an in-range depth field even for a stencil-only operation.
- [Vulkan color clear value](https://docs.vulkan.org/refpages/latest/refpages/source/VkClearColorValue.html)
  defines allocation-format conversion; a source view format cannot be ignored.
- [D3D11 render-target clear](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearrendertargetview)
  uses float inputs and does not promise arbitrary integer bit-pattern clears.
- [D3D11 resource copy](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource)
  permits compatible formats in one typeless family without scaling/conversion.
- [Archived D3D11 specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
  sections 3.2 and 5.2.3 describe conversion/clearing; the native reference results
  above document the observed integer RTV rounding/overflow differences.
