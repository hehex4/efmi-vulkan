# Frontend draw plans

frontend/xxmi/draw_plan.cpp mediates backend operations from command trees
compiled by the pinned original XXMI frontend. It contains no component number,
character name, shader hash or asset filename. Expressions, branches and named
calls continue to execute through XXMI. The adapter collects ordered VB/IB/PS
resource bindings, indexed draw arguments and copied layout operations, instead
of calling the reference's D3D context.

This is a CPU intermediate representation, not a ready GPU transaction. It is
not yet linked into the layer. Source/build/next stay 0.9.2, dist stays 0.9.1,
and the existing live hook still selects the explicit component-7 package.

## Contract

Register native command-list boundaries before the original optimisation pass.
They are marked runtime-populated so an empty declaration is not eliminated.
Each invocation remains an explicit External operation with pre/post phase bits;
the Vulkan/Core adapter must resolve it before considering the plan executable.
No missing Core function is silently replaced by an empty command list.

After upstream optimisation, adapt the selected roots and their reachable named
calls/branches. Repeated references are shared. Execution is serial and uses a
fresh CommandListState. It checks the whole result before publishing it to the
caller; failures retain the previous output. No live draw's skip flag is changed.
An empty handling=skip plan deliberately hides geometry. A zero-count draw emits
no draw item. Vertex offsets remain signed; instance counts and ranges retain
their separate meanings. Every draw owns a copy of its layout requests.

Supported bindings are custom-resource references or explicit null to vertex,
index and pixel-resource slots, including the original optimiser's adjacent
texture batches. Unsupported copies, queries, commands, destinations and dynamic
targets fail when reached. Expressions are inspected once for supported CPU
operands before original evaluation; unadapted resource/window/device queries
cannot enter the reference graphics context. Graph/recursion, steps, operations,
draws and layout requests are bounded. Aliases may only enter adapted lists.

Resource pointers borrow from the frontend generation; the future layer caller
must retain it. This adapter does not create GPU resources, ensure their ranges,
resolve material descriptors or submit commands. Variable side effects are not
rolled back on failure. The installed sample draw callbacks have no such writes;
full Core transactions must handle state publication explicitly. Persist/reload,
pool resource copies and native skeleton operations remain separate work.

## Native verification

scripts/probe_xxmi_host.py now builds the planner as well as the CPU prototype.
The original 23 host checks and 28 planner checks pass. Planner checks include
source-order bindings/nulls, arbitrary vertex slot 5, native-boundary retention,
both conditional branches, indexed/instanced arguments, signed base vertices,
per-draw layout copies, deliberate hidden geometry and root aliases. A later
dispatch, invalid count, non-finite expression, recursive call, GPU query, slot
query, excess draw count or unadapted alias rejects the complete output without
touching the original draw. GPU queries are refused before any device access.

The default tests are synthetic and headless. No layer change means the earlier
full 0.9.2 layer regression remains applicable; this checkpoint runs its separate
native frontend build/tests and the project text/diff checks.

## Read-only sample callback execution

```powershell
& 'D:/Python312/python.exe' -B scripts/check_mod_draw_plans.py
```

The tool requires a successful host build and verifies all recorded source and
executable hashes before running it. It reads the actual installed arcane.ini,
declares its file resources through the original parser, compiles its 12 draw
callbacks and preserves OverrideTextures as an unresolved native boundary.
Other Core/entry-point lists, pools, the image panel and present/key handlers
are not compiled or executed by this fixture. The loader preserves the original
Windows text-mode CRLF behavior and refuses parser diagnostics.

Verified source SHA-256:
697d0c98378b83d3aef592a54536470bff4410993f475c1514ed701c4b6f71bd.
The generic fixture runs declared defaults, all persistent scalars at 1, all at
2, then each scalar at 1 with the other defaults, each with LOD 0 and 1. This
is 16 selected scenarios, not every possible key combination or real key input.

Results: 384 plans, 404 indexed draw items, 192 intentionally mesh-empty callback
runs, all 18 unique indexed ranges and 38 distinct bound resource declarations.
Every selected index range and its adjusted min/max vertex index fit the actual
index/vertex files in all four required streams. The original INI hash remains
unchanged. Outputs and resource fingerprints: build/xxmi-host/mod-plans.json and
mod-plan-report.json. No source mod, Core or launcher file is modified, and no
asset payload is committed.

This proves callback selection and file bounds for the observed input. It does
not prove full mod initialization, complete shared skeletons, shader declaration
compatibility, texture interpretation/upload, animation or rendered appearance.
Next integration work must resolve those data-dependent backend requirements
without introducing per-component implementations.
