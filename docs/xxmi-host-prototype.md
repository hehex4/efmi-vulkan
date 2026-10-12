# Native XXMI CPU host prototype

The D-016 prototype now compiles, links and executes the pinned XXMI frontend in
a standalone headless process. It passes 23 CPU checks. It is not linked into
either layer DLL, does not load a real mod, and creates no graphics device,
window, input hook or game process. Source/build/next remain 0.9.2; dist remains
0.9.1. The next work is connecting frontend operations to Vulkan resources and
shared character state, not adding per-component implementations.

## Reproduce

```powershell
$env:VCVARS = 'D:/Microsoft/VC/Auxiliary/Build/vcvars64.bat'
& 'D:/Python312/python.exe' -u -B scripts/probe_xxmi_host.py
```

Inputs are the existing read-only checkouts under F:/vulkan:

- XXMI-Libs-Package: a7847eae82aaac32175c9a7d199673d5c9323bcd (1.2.2).
- WWMI-DX12 reference: 113f42cc2dc5702dbba624ee16abac96d68ed3b5.

The script rejects another revision or tracked changes, stages the upstream
PRISTINE list under F:/vulkan/efmi-xxmi-host-prototype, and verifies copied bytes.
The original parser/expression/pool sources are not edited. Reference host
headers provide compile scaffolding; its DX12 implementation is not linked.
Local wrappers expose original parser entry points and substitute synthetic
button state. Standalone shims provide logging/locks and reject their missing
graphics/hook operations. The reference headers still contain unused no-op
graphics methods, so they are not an accepted production Vulkan adapter.

Every run compiles all translation units and links anew; it never executes an
old binary after build failure. Build/runtime failure returns nonzero. Outputs:
build/xxmi-host/report.json (pins, input fingerprints, executable hash, result),
build.txt, runtime.txt and host_probe.exe. No output goes to installed mods,
Core, launcher settings or dist. No system-wide environment change is made.

## Executed checks

The fixture invokes the original C++ implementation, not a Python reimplementation:

- Arithmetic, precedence, comparisons, boolean operators and remainder.
- Source-ordered assignment, both conditional branches and separate post phase;
  changing a variable affects the next execution without reparsing.
- Independent ring-pool values, positive/negative wrapping, aliases and cycle
  rejection; FIFO expiration boundary and reset-to-template behavior.
- Mutable command-list references and cycle rejection.
- Root-file duplicate Present rejection versus included global Present append.
- Region expression arguments retaining byte offset/length.
- ElementFormat parsing plus execution, retaining slot, semantic name, format
  and the evaluated semantic index in the requested layout operations.
- Duplicate key registrations, both receiving exactly one down/up edge from
  synthetic state; no real key polling or input injection.
- Explicit refusal when a missing GPU adapter is requested.

Two initial fixture expectations were incorrect. Root-file duplicate sections
do not append: only global sections from namespaced included files do so. Also,
ElementFormat evaluates its semantic index in LayoutElementOperation::run, not
when parsed. Expectations were corrected against the original implementation;
the engine was not changed. Promotion also caught a wrapper named input.cpp
including itself and per-unit NOMINMAX requirements; the reproducible full build
fixes both. The final run passes 23 checks with zero failures.

## Scope and attribution

This closes the bounded compilation/CPU-semantic feasibility checkpoint of
D-016. It does not establish all expressions, resource views/Region reads,
spatial pools, persisted settings, file includes/reload, Core callbacks, actual
key conditions/cycling, shader execution, textures, panel behavior or whole-mod
compatibility. The Region test inspects evaluated arguments, not a GPU copy.
The layout test creates an operation, not a Vulkan pipeline. Callback tests
exercise the underlying reference objects, not full installed Core execution.

XXMI and the reference host are GPL-3.0 sources; their original notices remain
in their checkouts (XXMI LICENSE.GPL.txt, reference LICENSE). DirectXTK retains
its MIT license, and the original CRC/PCRE notices remain with those inputs.
No upstream source, library binary or game/mod asset is copied into Git. Any
future distribution must include the applicable notices and corresponding
source; this local prototype is not a distribution package.
