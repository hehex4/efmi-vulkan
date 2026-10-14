# Original INI GPU resource execution

## Architecture and scope

The next stage after the source-defined language inventory is a common GPU
resource execution backend, followed by CustomShader, shared shader matching
and complete enabled-directory rendering. This order is the user's instruction
on 2026-10-14. Continue offline until actual game evidence is necessary.

`frontend/gpu/resource_commands.*` owns mutable Vulkan buffers and private
command buffers. `frontend/xxmi/gpu_resources.*` is an opt-in production-runtime
extension: the original parser, expressions, branches, command lists, resource
declarations and pool logic remain authoritative. The draw planner mediates
resource commands through this extension; it does not translate INI text again.

This is an execution foundation, not complete directory graphics integration.
The existing character renderer still uses its earlier immutable file plans.
The new backend requires an explicit outside-render-pass scope on one host-
serialized queue. It does not borrow application resources, split a render pass,
change application descriptors or insert blocking work into a draw hook.
Stage slots currently hold logical source-API buffer bindings for the next
shader adapter; a binding is not proof of a Vulkan descriptor bind or draw.

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
- Nonexpiring direct-index pools retain independent scalar/resource state.
  Original reset and per-frame copy counters stay active.
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
guessed format or full-buffer range. Other copy options remain separate work.

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
the underlying resource. Region/spatial hashes and texture-backed queries still
need additional adapters.

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

A failed GPU scope cannot be submitted. Discarding it makes the generation
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
batches/ranges, scalar/resource pool resets, copy limits, generation isolation, retained
owners, negative execution paths and allocation release through shutdown.
Final counts and the full regression checkpoint belong in CURRENT_STATE.md.

## Remaining execution work

Image commands and format conversions; region/spatial hash and texture properties;
fresh typed/raw plain-range views; remaining copy options and invalid source-API
description combinations; expiring/keyed GPU pools; GPU
IniParams; application resource import and command placement; shared shader
contracts; CustomShader draw/dispatch/state; ShaderOverride/ShaderRegex; complete
directory routing and graphical reload. PNG integration remains paused. No
deployment, game load, module swap or character acceptance follows from this test.

## References

Pinned original `DirectX11/CommandList.cpp` and `CommandList.h` at
`a7847eae82aaac32175c9a7d199673d5c9323bcd` define the adapter semantics.

- [Vulkan buffer copy](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBuffer.html)
  defines copy ranges, transfer usage, ownership and outside-render-pass scope.
- [Vulkan buffer fill](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdFillBuffer.html)
  defines alignment and transfer synchronization.
- [Vulkan synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)
  defines the memory dependencies and host visibility rules.
- [D3D11 integer UAV clear](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearunorderedaccessviewuint)
  defines bit-preserving typed clears and first-word raw/structured clears.
- [D3D11 float UAV clear](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearunorderedaccessviewfloat)
  defines float and normalized view conversion behavior.
