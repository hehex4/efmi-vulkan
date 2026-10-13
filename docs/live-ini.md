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
Syntax/contract failures, failed image preparation and upload timeout retain the
active generation and expose a reason. Unexercised dynamic expressions can still
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
IniParams upload, cursor/window queries, alpha blend state, viewport and safe
backbuffer drawing/synchronization. It is T-016, not an already supported part
of this runtime. Passing it as an input currently refuses explicitly.

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

These are sandbox and offline results. Actual-character key handling, F10 visual
behavior and persistence across a game restart require a user-started session.
