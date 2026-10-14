# Original input-layout execution audit

The audit below is read-only source and independent native DX11 WARP evidence.
Candidate1.2.6 now implements the measured owned layout paths; its validation
and remaining boundaries are recorded at the end. None of this is a game result. The original source is the pinned XXMI DirectX11 checkout.
All diagnostic files and binaries are under build/source-state-probe/.

## Register correspondence

HackerInputLayout retains the original input elements and the bytecode used to
create the layout. CreateCustomInputLayout uses that retained signature when an
INI layout edit creates a new layout; it does not use the currently bound VS.

input-layout.cpp/.txt creates two shaders with ALPHA and BETA in opposite
signature-register order. Input values are (1,2,3,4) and (10,20,30,40); the shader
evaluates ALPHA*100+BETA. Matching layout/shader signatures produce
(110,220,330,440). Switching the shader while retaining the other layout produces
(1001,2002,3003,4004). Therefore current-semantic-only reassociation would change
native behavior. The owned adapter needs the layout's creation-register mapping
and the current VS's original input signature before selecting Vulkan locations.

## Native creation and offsets

input-layout-offsets.cpp/.txt and input-layout-limits.cpp/.txt establish:

- APPEND uses the previous element of the same slot, aligned to min(4, format
  byte width). R8 followed by R16G16 starts at byte4; R16 followed by R8G8B8A8
  starts at byte4; a new slot starts at byte0. Explicit misaligned offsets fail.
- Mixed vertex/instance classification or different instance step rates in one
  slot fail. Per-vertex step1 fails. Per-instance step0 succeeds.
- Slot31 succeeds and slot32 fails at the tested feature level11_0.
- Aligned offsets2048,2052 and0xfffffffc all create successfully. Do not classify
  a Vulkan device-limit refusal as native CreateInputLayout failure. These
  extreme-offset diagnostics never draw or access an out-of-range buffer.

input-layout-creation.cpp/.txt establishes that required signature elements must
be present even when their native ReadWriteMask is0. An unused additional element
is accepted, but its invalid UNKNOWN format still makes creation fail. Semantic
names are case insensitive. Missing a required semantic fails rather than
silently binding another semantic.

## Override lifetime and command order

CommandList.cpp UpdateInputLayout obtains the current HackerInputLayout. A null
layout returns without consuming pending overrides. Otherwise it applies the
unchanged ApplyInputLayoutOverrides helper to the stored element descriptions,
recreates with the stored signature, defers a successful replacement and clears
pending edits. Failed native creation keeps the previous layout.

DrawCommand::run performs the edit and activates the deferred layout before
parameter flushing and argument evaluation, including zero-count draws and
dispatches. Hunting skip returns before any of this. CustomShader state restore
does not restore the input layout. HackerContext saves the original layout once
and restores it in AfterDraw after all post lists. Present/key command roots are
not interchangeable with that intercepted draw boundary. These details must be
preserved by the owned adapter and its tests; per-CustomShader restoration would
be incorrect.

## Remaining work

Connect owned immutable layout descriptions, original element edits and native
register matching to vertex streams and pipeline keys. Distinguish proven native
creation failures from unsupported Vulkan execution cases. Test nested calls,
zero-count/dispatch consumption, frame persistence and intercepted-draw restore.
Format/signature type reinterpretation, null/OOB fetch semantics, arbitrary
instance divisors, imported application layouts and packed native registers need
explicit handling rather than guesses from successful syntax parsing.

Primary references:

- [ID3D11Device::CreateInputLayout](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createinputlayout)
- [D3D11_INPUT_ELEMENT_DESC](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_input_element_desc)

## Owned execution checkpoint1.2.6

The source creation-register model and original offset/format edit helper now
back owned INI vertex draws. Native references in resource_layout_cases.h verify
creation rules across formats0..115, current/creation VS permutations, instance
steps0/1, nonzero starts, packed R8/R16 APPEND fetch, missing component defaults,
zero draw/dispatch consumption, nested/Present persistence, intercepted-draw
restore and failed-root discard. Native large offsets remain valid descriptions
until actual Vulkan execution checks its limits. Source creation failure retains
the current layout; an unsupported adapter case refuses explicitly.

Additional diagnostic input-layout-instance.cpp/.txt proves that step0 still
uses StartInstanceLocation. The direct fallback offsets the source buffer by
that base before binding with stride0. Indirect step0 and divisors above1 require
an enabled divisor path. vertex-features.cpp/.txt finds KHR divisors, zero divisor
and nonzero first-instance support on both local GPUs. Packed16 BGR VkFormats
lack vertex-buffer support on both devices; retain an explicit lowering boundary.

The unchanged original RealignInputLayoutOffsets uses original_format plus its
byte size as a boundary, rather than original_offset. Tests deliberately preserve
that behavior: shrinking R32G32B32A32_FLOAT to R32G32_FLOAT leaves another explicit
attribute at offset16. This is compatibility with the pinned helper, not a claim
that its algorithm is ideal. Complete regression:build/next/run_all-1.2.6.out,
resources6093/core+sync clean, all steps pass. No application layout import/game.


## Enabled divisor checkpoint1.2.7

Owned KHR/core vertex divisors preserve firstInstance + floor(instance/divisor),
with divisor0 fetching firstInstance. Native pixel comparisons cover0/2/3/max
for direct/indexed/indirect/indexed-indirect commands. Actual enablement, zero
support, device maximum and supportsNonZeroFirstInstance are explicit host facts.
Indirect bases require the latter property without a CPU readback; direct zero
fallback still offsets the buffer before stride0. Divisors enter pipeline keys.
Complete regression:build/next/run_all-1.2.7.out, resources6653/core+sync clean.

Native diagnostic input-layout-reinterpret.cpp/.txt additionally confirms that
float/int/uint signature reinterpretation preserves fetched bits, absent W uses
the format's scalar class, and BGRX always supplies float alpha1. Vulkan lowering
for those cases is the next increment; this diagnostic is only native evidence.

- [Vulkan vertex fetch](https://docs.vulkan.org/spec/latest/chapters/fxvertex.html)
- [Divisor properties](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceVertexAttributeDivisorProperties.html)
