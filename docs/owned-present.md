# Original Present work on the game's swap chain (phase 1, P1)

Completing the INI in the game starts with the work the original runs outside replaced draws:
the Present list, key lists, Constants and reloads. The image panel shipped in about twenty
installed Mods is one user of it; this document describes the general mechanism (D-072).

## One generation, two execution paths

A character generation in the game hosts the original engine with the character extension
(Core boundaries, the file plan of replaced draws inside the application's render pass). When
owned GPU execution is available it also carries `GpuShaderRuntime`, combined by
`frontend/xxmi/owned_generation.cpp`:

- the owned callbacks apply only while the host has a GPU scope open; replaced draws open
  none and keep the plan path, and a CustomShader inside one refuses until step P2;
- declaration references executed by the GPU side are mirrored into the plan;
- the native preparation runs with the scope closed, and the natively executed Core
  CustomShaders are not compiled.

With owned execution the generation's inputs also include the Mod folder's other enabled INIs
(recursively; `DISABLED` names and the generation's settings folder excluded), as the original
loads them, so the panel's `k/1.ini` shares the main INI's variables. Image files other than
DDS stay out of the character package; only the GPU side reads them, and a replaced draw that
binds one refuses.

`frontend/xxmi/owned_runtime.h` is the interface the layer uses, so the original engine's
headers stay out of the layer's translation units.

## Scopes and submission

Loading runs on a background thread, which must not use the application's queue. The
generation therefore loads inside a deferred scope (no queue), and the scope is suspended
after loading: recorded work stays pending, and replaced draws see a closed gate. At each
presentation the module (`layer/src/character_present.cpp`, `OwnedPresenter`):

1. samples the window (client rectangle and origin of the swap chain's surface window), the
   cursor and the swap chain size for the INI (D-069);
2. imports the presented swap chain image as the back buffer (`bb`, `r_bb`, `f_bb`), valid in
   PRESENT_SRC before and after, with the DXGI format of its Vulkan format and the
   shader-resource bind flag only for sampled use;
3. opens (or resumes) the scope on the presenting queue, runs the Present lists, and submits
   with the application's present wait semaphores, signalling a semaphore of that image;
4. hands the presentation that semaphore in place of the application's.

A refusal discards the scope; the generation's GPU side then stays failed (the first reason
is logged), while replaced draws continue through the plan. Submissions are retained until
their fences complete and waited for when the generation retires.

## What the layer adds to the application's objects

Only in character mode, and only what the application did not enable:

- VK_EXT_robustness2 with `nullDescriptor` alone, when supported (D-071);
- `shaderDrawParameters` (core 1.1), when supported: shaders compiled from HLSL use the base
  vertex for D3D vertex IDs. The application's own Vulkan 1.1 or draw-parameter feature
  structure is set for the duration of vkCreateDevice and restored; otherwise the layer adds a
  structure of its own;
- color attachment usage on a swap chain that lacks it (every swap chain supports it).

The status reports each of these (`null_descriptors_added`, `draw_parameters_added`, per swap
chain `usage_added`), and the swap chains' formats and sizes.

## Tools beside the layer

`scripts/build.py` copies DXC (`dxcompiler.dll`, `dxil.dll`), the exact DXBC translation
(`efmi-vkd3d-shader.dll`) and the SPIR-V validator (`SPIRV-Tools-shared.dll`) from `F:\vulkan`
next to the layer. Without them a generation stays a CPU generation and the log says why.

## Verification

`sandbox/owned_present_test.py` (suite step "owned Present on a swap chain") runs the sandbox
program with a real swap chain in both entries, the synthetic character fixture and a second
INI whose Present list draws an opaque quarter through a CustomShader into `bb`. A test-only
copy of the 20th owned presentation (`EFMI_VK_TEST_PRESENT_READBACK`, honoured only by
`efmi_vk_sandbox.exe`, with `--readable-swapchain`) shows the quarter drawn and the
application's own picture elsewhere; core and synchronization validation are clean.

Not yet verified in the game: the game's swap chain format and usage, its device features,
and the installed panel there. Those need a Vulkan session, which only the user starts.
