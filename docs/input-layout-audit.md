# Original input-layout execution audit

This is a read-only source audit and independent native DX11 WARP evidence for
the next owned INI adapter. It is not implemented Vulkan input-layout execution
or a game result. The original source is the pinned XXMI DirectX11 checkout.
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
