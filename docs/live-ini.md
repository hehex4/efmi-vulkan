# Live original INI runtime

## Scope

1.0.2 links the pinned original XXMI parser, expressions, command lists, pools,
texture-override matcher and input dispatcher into both the injected layer and
the exchangeable backend. This character's main `arcane.ini` runs at draw and
frame boundaries. Its keyboard conditions, cycle lists (including duplicate
listeners), conditional meshes, alternate DDS binding and persistent variables
are evaluated at runtime. The interpreter does not rewrite the installed mod.

This is still a scoped adapter, not complete DX11 emulation or compatibility with
every INI in the mod folder. Native identity, merged bones and Vulkan recording
remain the verified renderer. Core registration, attachment and draw-instance
boundaries validate the original evaluated skeleton against that renderer's
profile. The existing single-character, LOD0/LOD1 limits remain. There is no
component-specific replacement code.

The registry accepts this profile's mesh override entries. Additional source-image
hash overrides, fuzzy overrides, arbitrary shader overrides, recursive includes,
RabbitFX, controller input and new mesh/semantic profiles require adapters. The
native OverrideTextures boundary has no image overrides to execute for this
accepted registry; it does not invent DX11 image hashes. Unavailable rendering
commands refuse before emitting replacement commands.

## Configuration and source files

`scripts/prepare_runtime.py` copies the verified material package into
`build/character/arcane-live.bin` and creates its `.runtime.json` sidecar. The
sidecar records the original main INI, four original EFMI Core inputs, namespace,
settings name and the file corresponding to each mesh/remap stream. The old
package stays available for rollback. Without a sidecar the static package path
keeps its previous behavior.

The package now supplies renderer correspondence: index matching, bone ranges,
mesh stream formats and observed fragment binding profiles. Existing profiled
BUF files are read afresh, realigned and passed through the complete package
validator. Command ranges and image choices come from the live INI. All reachable
file references in both command branches are catalogued before GPU publication;
dormant declarations do not demand files. This includes the tenth, alternate
stocking DDS absent from the fixed scenario's nine-image package. New DDS paths
can be loaded on F10. A new BUF path or skeleton/semantic structure outside the
profile requires profile preparation, not just an unchecked bind.

Set `m2_package` in the project-owned `dist/efmi_vk.cfg` to the live package and
keep `m2_enabled = 1`. The launcher needs no change.

## Input and settings

The host samples keys only while its process owns the foreground window. A loss
of focus supplies released keys, including release behavior for hold bindings.
Original XXMI parsing and dispatch determine modifiers, conditions, ordering and
duplicate listeners. No global input hook or injected desktop input is used.

The current main INI has thirteen listeners on ten physical key combinations:

| Keys | Original variables |
| --- | --- |
| Ctrl+5, Ctrl+6, Ctrl+7, Ctrl+8 | swapkey0, swapkey1, swapkey2, swapkey3 |
| Alt+5 | swapkey4 |
| Alt+6 | swapkey5 and swapkey7 |
| Alt+7 | swapkey6 and swapkey8 |
| Alt+8 | swapkey11 and swapkey12 |
| Alt+9 | swapkey9, a three-value cycle |
| Ctrl+L | stocking |

These keys require the character's original `$active0` condition. The runtime
adds F10 with no modifiers for configuration reload. Variables declared persist
are written by the worker to `build/character/settings/arcane_fes.ini`, with
fully qualified names and original INI Constants assignments. A flushed temporary
file is atomically renamed over the previous settings. Normal backend shutdown
also flushes pending changes. No write goes to the installed mod, Core, d3dx.ini
or DX11 user settings. There is no inheritance of the DX11 saved outfit.

## F10 and ownership

F10 schedules a candidate generation on a worker. It rereads the INI, profiled
BUF files and referenced DDS files, parses and validates, then creates a new
mesh, texture cache and variant cache. The active generation keeps drawing while
that work and queue publication finish. A candidate is adopted only when Ready.
Original syntax warnings do not stop it (D-067): as after F10 under DX11, the
broken line is skipped, the rest takes effect and the warnings are in the log and
in `ini.diagnostics`. Contract failures (a construct without an adapter), failed
image preparation and upload timeout retain the active generation and expose a
reason. Unexercised dynamic expressions can still
refuse an individual draw later; this is not proof of all possible mod branches.

At exchange, the latest persistent values replace any stale parse-time snapshot,
the old settings writer is suspended, and held keys are suppressed until release.
Old command buffers retain immutable meshes, palettes, descriptors and image
owners. An immutable generation token, rather than a reusable backend address,
identifies those command owners. Retirement runs off the draw path. As with a
backend module exchange, identity and pipeline warmup can temporarily show the
game's model; seamless visual transition is not claimed.

Probe `status.replacement` adds `ini_generation`, `ini_reloads`,
`ini_reload_rejected`, `ini_reload_pending`, `ini_reload_error` and an `ini`
object with execution counts, key edges, settings state and variable values.
The module generation and INI generation are separate counters. File reload is
distinct from replacing the DLL's code through the existing module command.

## HLSL panel

Vulkan consumes SPIR-V. HLSL can produce it with DXC; it does not have to be
translated into GLSL. See the [Khronos HLSL guide](https://docs.vulkan.org/guide/latest/hlsl.html).

`scripts/check_panel_hlsl.py` compiles the unmodified installed
`k/Draw_2D.hlsl` with DXC 1.9.2609 to Vulkan 1.1, then validates both stages.
The vertex and fragment interfaces agree; image bindings 100 and 101 and the
1D IniParams image at binding 120 remain explicit. The DXC archive is installed
only in `F:/vulkan/dxc-1.9.2609`, upstream release `v1.9.2609`, archive SHA-256
`ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1`.

This is compilation evidence, not panel rendering. `k/1.ini` is not among the
runtime inputs. Its CustomShader still needs shader/pipeline creation, image and
IniParams upload, alpha blend state, viewport and safe backbuffer
drawing/synchronization. It is T-016, not an already supported part of this
runtime. Passing it as an input currently refuses explicitly.

### Window, resolution and cursor operands (1.4.0)

The panel's [Present] list reads `window_width`, `window_height`, `res_width`,
`res_height` and `cursor_window_x`/`_y`. The original reads them with
GetClientRect, GetCursorInfo and ScreenToClient on its hWnd, and keeps the swap
chain size in `mResolutionInfo`. The host now supplies one sample per present
in `RuntimeInput` (client rectangle and its screen origin, cursor screen position
and visibility, swap chain size); the original functions read that sample, and
a generation's hWnd is only a token that never reaches a real window API. Draws
between presents read the latest sample. The bridge accepts `res_*`,
`window_*`, `cursor_showing`, `cursor_screen_*`, `cursor_window_*` and
`cursor_x`/`_y`; cursor images stay refused (see the next section for the
other operands).

Original fallbacks are kept: without a window, `window_width` takes the
resolution and then the fallback screen size, and window positions equal screen
positions. A hosted generation has no reference render target, so the
heuristic continues as the original does with none bound. The fallback size is
IniHandler's default 1920x1080, because a generation reads no d3dx.ini; the
user's d3dx.ini sets 2560x1600 in [System], a difference that is reached only
when neither a window nor a swap chain size is known. The layer does not
supply the sample yet: that comes with the swap chain integration of the panel.
`sandbox/runtime_window_cases.h` covers the semantics (13 checks).

### Remaining operands (1.4.7)

Probing every named operand in a draw and in [Present] (`runtime_test.exe
--probe-command`) showed which ones a CPU generation still refused:

- `hunting`, `frame_analysis`, `sli`, `stereo_active`, `stereo_available` already
  worked: with hunting disabled, as in every hosted generation, the original
  optimiser turns them into the constant 0.
- `fps` reads the original FPS counter in the generation's globals. The host
  passes its steady clock in microseconds (`RuntimeInput::ticks_us`, the
  original's GetSystemTicks) and the counter advances at each presentation
  before the Present list, as in HackerSwapChain::RunFrameActions (first tick
  starts it, smoothing 0.1, pauses over a second ignored). Zero leaves it.
- `effective_dpi` and `cursor_hotspot_x`/`_y`: the original calls Win32 directly
  (GetDpiForMonitor on the window's nearest monitor with the thread made
  per-monitor aware, GetIconInfo on the cursor). The bridge answers them from
  the host sample (`RuntimeInput::effective_dpi`, `Cursor::hotspot_x/_y`) through
  `DrawPlanAdapter::host_values`; the original's floor of 96 is applied. The
  layer samples both at each presentation (`OwnedPresenter::sample`), keeping
  a cursor's hotspot while its handle stays the same.
- `Resource->region(...)` as an expression has no case in the original's
  `process_texture_filter` and returns 0 without the resource; it is accepted.

In the owned GPU domain (Present lists executed on the device):

- `rt_width`/`rt_height` read o0 through `DrawPlanAdapter::render_target_size`:
  the size of the Texture2D behind a 2D or 2D multisample render target view,
  otherwise the original's -1 (a Texture2DArray view reports none). As in
  ProcessParamRTSize, the size is cached in the command list state once found
  and asked again while it is -1; the window heuristic falls back to it.
- `->hashregion` and `->spatialhash` keep the original's sentinels (null -2,
  image -3). On a buffer the original hashes its CPU copy of application data
  (`cache_resource_data`), which a Mod-owned buffer never has, so the result is
  UNKNOWN (-1). A vertex or index buffer outside a draw is refused, where the
  original would read a missing draw call.

Still refused: `rt_width`/`rt_height` in replaced draws and CPU generations (the
game's render target is not known there), `scissor*_*` (the original reads the
application's rasterizer state), cursor images (`cursor_mask`, `cursor_color`),
`->lastframe` on pools, and buffer hashes outside the owned domain other than
the Core spatial identification adapter. Their installed users all read game
resources (Si Lunabelle's guard, Yvonne's translucency capture), which needs
the shader catalogue. Covered by `sandbox/runtime_window_cases.h` (host values,
12 checks) and `sandbox/resource_operand_cases.h` plus the property cases in
`sandbox/resource_gpu.cpp` (owned domain).

## Verification

1.0.2 passes the full suite in `build/next/run_all-1.0.2.out`: original runtime
45 checks and live GPU runner 28 checks, in addition to the existing regressions.
Candidate and deployed actual-config smokes upload all ten images through both
entries with two enabled graphics queues (queue 1 submits), with no validation
errors or warnings. See `build/character-assets-config-next-1.0.2.json`,
`build/character-assets-config-dist-1.0.2.json` and `build/deployment-1.0.2.json`.

`scripts/build_xxmi_runtime.py` exercises isolated original-engine generations,
keyboard conditions, duplicate keys, hold release, persistence, refused parsing,
held F10 and the actual main INI. Actual input checks translate every LOD0/LOD1
entry through the renderer catalogue and exercise all thirteen listeners. They
verify the default nine body ranges, the combined two-range outfit and the
alternate stocking image.

`sandbox/runtime_test.py` uses the existing independently checked, three-component
GPU scene through both loader/injection entries, with one and two graphics queues.
It checks actual pixels after Ctrl+5, malformed-INI rollback, invalid-DDS rollback,
successful F10, persistence, old sealed command resubmission after retirement,
application-state restoration and held-F10 suppression. The key fixture file is
recognized only inside `efmi_vk_variant_gpu.exe`; tests never synthesize input to
the desktop or game. All checks run under core and synchronization validation.

Real-game follow-up on 2026-10-13: user-started PID 31108 loads 1.0.2 and the
user confirms working outfit keys. Logs record successful F10 exchanges at
22:53:43.879 and 22:53:49.113, generations 1 and 2. Read-only status snapshots
confirm two reloads, no reload rejection/error/pending work, ten images Ready and
published, and all thirteen project-saved variables equal to live state. Settings
are clean; stocking is 1 and outfit switches are 0. Evidence:
`captures/live-ini-1.0.2-20261013-225835-31108/`.

The user cannot see an F10 change; unchanged inputs with retained persistent
choices can produce the same appearance. No pre-reload snapshot was taken, so
preservation across these two exchanges is not independently measured. Restart
persistence and visual coverage of every key remain untested. Replacement
continues in the sampled interval, but historical missing-buffer variant refusals
also increase; this is F10 evidence, not a clean effect/stability acceptance.
