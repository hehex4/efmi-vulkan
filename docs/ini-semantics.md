# How the layer handles a Mod's INI (D-137)

The user, 2026-10-28, after tangtang Ramune v1.9 drew a broken arm: the INI handling keeps running into
problems, Mods are written in every way, think it through and handle it generally, never per Mod.

## Why problems kept appearing

1. A subset, and refusal outside it. The original's command lists are adapted into a narrow plan
   (vertex, index and pixel-resource bindings, indexed draws; docs/frontend-draw-plans.md): what lies
   outside "fails when reached" and the whole draw keeps the game's mesh. The subset grew from what the
   installed Mods used (the census of docs/ini-language-coverage.md), so a Mod written differently meets
   its edge. On 2026-10-28: about 170 refusal points in the frontend's planning code, 36 in the layer's
   plan conversion, 14 refusal counters in the backend.
2. Failure the other way round. The original contains a failure to its own unit: a resource whose load
   fails stays null and every assignment of it binds nothing (CustomResource::LoadFromFile), a command
   that cannot act does nothing, a warning goes to the overlay, and the draw goes on. The layer refused
   the draw: the user saw a broken model and no message (BUG-076).
3. Checks compared the layer with itself (a Mod alone against the folder generation) in the default
   state, not with the original in every state a player reaches.

## The rule

- The original is the specification, not the installed Mods. Its command language is finite: 28
  command classes in CommandList.h, resource targets of a fixed set of kinds (vertex, index and constant
  buffers, shader resources, unordered access views, render and depth targets, custom resources, swap
  chains, pseudo-resources) per shader stage, with a fixed set of copy options. A Mod's way of writing is
  a combination of these; the combination is executed by the original's own interpreter, which the
  layer runs. Each command is implemented with the D3D11 meaning it has, composable as D3D11 state is.
- Every command ends in one of three ways: implemented; degraded as the original degrades (a missing or
  unloadable file is a null resource; a command that cannot act does nothing; the draw goes on);
  impossible under Vulkan, listed, and degraded visibly. "Refuse the whole draw" is not one of them, nor
  is a budget the original does not have.
- What is degraded is shown, as XXMI shows it: the overlay's warnings at load, at F10 and when they first
  happen, naming the Mod and the section; and the log.
- General: no Mod name, hash or file name in the code. Tests are generated from the language and run over
  every installed Mod in every state its keys reach.

## Order of work

1. Every refusal point classified against the original: where the original degrades, degrade the same way
   and warn. The overlay notices (load, F10, warnings).
2. Checks with the original as the judge: every installed Mod in every state its keys and LODs reach,
   planned and compared with what the original executes; and small INIs per command run under DX11 with
   XXMI and under Vulkan with the layer.
3. The gaps those checks show, closed by implementing the commands' D3D11 meaning for replaced draws, the
   way owned work (Present, CustomShader) already does, not by special cases.

## The overlay's notices (2.1.2)

As 3DMigoto's LogOverlay (Overlay.cpp): notices drawn at the top left of the presented image, a line each
in the level's colour (DIRE red 20 s, WARNING orange-red 10 s, NOTICE orange 5 s, INFO lime green 2 s,
the time from the first showing), on a black backing of 75 %, at most ten, in Arial (3DMigoto's
notifications font) drawn by GDI, so a Mod's name shows in any script. `layer/src/notices.cpp` draws them
with a render pass of its own on the presenting queue after the presentation's other work (the Mods'
Present work), the presentation then waiting for it; three slots, never a wait for the GPU in the
presentation (BUG-062): with all three in use the notices wait a presentation. The notices are kept by
util.cpp (`notice()`, also the module's through HostApi).

- F10: `> Reloaded config in N.NNNs`, as the original after ReloadConfig (IniHandler.cpp), timed from the
  key to the exchange of the folder generation.
- A refused F10 (the layer's; the original always loads what it can): `> Reload failed, the Mods loaded
  before stay: ...` as a warning.
- What the layer cannot do as the original does (a package's refusal reason): once per Mod and reason for
  the process, as a warning naming the Mod. These are the layer's gaps, shown whatever `show_warnings`
  says; the original's own warnings would follow `show_warnings` (EFMI's d3dx.ini: 0), and none is routed
  to the overlay yet.

Checks: owned_present_test (a notice drawn into presented images, LimeGreen text at the top left and
none in the middle, clean synchronization validation), runtime_test (an F10 that reloaded queues the
original's text).

## Every installed Mod in every key state (check_mod_states.py)

The folder generation offline (`efmi_vk_character_test --plan-folder-states`), as the original loads the
folder: two presentations for the default state (EFMI's exports register at a Present), then each key
action's keys as the original parsed them (`IniRuntime::key_combos`: the VK codes of its buttons that are
not inverted) held for a presentation and released, up to eight presses or until the variables repeat a
state. The original's own dispatch decides what a key changes (cycle, toggle, hold, conditions such as
`$active0 == 1`); nothing of the key syntax is read by the check. After each hold and each release every
Mod's sections are planned; a refusal fails the check with the first state it appeared in. The offline
stand-ins' limits (no game pipeline: "offline ...") are not counted.

2026-10-28 on the installed folder: 316 key actions, 2407 states, 18 Mods, every section plans in every
state, 7 s. With BUG-076's fix taken out it reports tangtang Ramune's component 0 at LOD 0. A run_all step.

## Classification of refusal points (2026-10-28)

About 230 refusal texts in the planning code (frontend draw_plan, resource_plan, vertex_plan, material_plan,
draw_transaction, file_draws; the layer's character_program): 41 internal invariants (an adapter not
installed, a cyclic graph), 39 budgets, 56 constructs the plan path does not take, the rest per-value checks.
Worked through by impact: first what drops a whole Mod at load, then what the original degrades, then budgets,
then the gaps (step 3).

- Done: BUG-076 (a missing texture binds nothing), BUG-077 (a missing, short or twice-declared buffer file
  degrades per draw as the original; the cache of distinct draw lists starts again when full instead of
  refusing; check_mod_degradation.py). The every-state check also runs 1200 presentations after the keys
  (a Present that changes state every frame).

## Freezes (the user's other half, 2026-10-28)

The watchdog's lines in every game log (dist/efmi_vk_layer.log): the presentation stopped inside the
layer's Present work for minutes in 1.9.4 (PID 14500, 285 s) and 1.9.5 (PID 11492, 416 s), BUG-062 and
BUG-063, fixed in 1.9.6 and by D-129's generations with their own engine state; in 2.0.7 (PID 30992,
22 s) Unity's crash handler ran after BUG-067, fixed in 2.0.8. In the 2.0.3 and 2.0.8 sessions, today's
included, no presentation took 1.5 s or more. Most stall lines are threads that did not enter Vulkan for
a while (idle), not freezes. Each freeze the user meets is to be traced from these lines.

## Status

- BUG-076 (2.1.1 tree): a texture file that is not there binds nothing, in a Mod's own generation and in
  the folder's alike; was a refusal in the folder generation and a bound missing file alone.
