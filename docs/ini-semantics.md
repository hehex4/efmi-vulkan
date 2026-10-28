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

## Status

- BUG-076 (2.1.1 tree): a texture file that is not there binds nothing, in a Mod's own generation and in
  the folder's alike; was a refusal in the folder generation and a bound missing file alone.
