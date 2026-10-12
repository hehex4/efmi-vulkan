# First M2 component through the real draw hook

Layer 0.9.0 connects the previously verified bone importer, asynchronous pipeline
cache and private GPU snapshots to `vkCmdDrawIndexed`. It is disabled by default.
This is one explicit experimental component, not an INI interpreter or full-folder
compatibility. After user-confirmed normal exit, dist/ received the exact tested package
on 2026-10-12 at 18:58. Its actual-config sandbox smoke passes; no game has loaded it.
The user requested a pause after configuration. Resume only when asked.

## Fixed sample and conditions

`scripts/package_m2_component.py` reads the installed sample without changing it.
It requires the reviewed `arcane.ini` fingerprint and copies the four component-7
BUF files unchanged into an uncommitted package. The profile is hash `a3fe5c47`,
108 original indices, first index zero, 40 replacement vertices and 108 uint32 indices.
All weighted indices belong to its own 12-bone palette at merged offsets 266-277
out of 499 bones. Thus this component can import both poses independently at each
draw; it does not require another component to have appeared earlier in a frame.

The fixed vertex streams are 16-byte position, 8-byte texcoord and 16-byte blend,
with position reused at binding 3. Blend indices become RGBA16_UINT; the original
declaration has RGBA8_UINT and stride 12. Higher application bindings are preserved.
The decoder checks exact length, CRC-32C, dimensions, finite coordinates, indices,
normalized weights and palette independence before accepting anything. The package
is an explicit profile, not inference from a similar mod or a replacement of its INI.

Five saved frames (display, two LOD0 world, two actual LOD1 world) contain ten matching
draws. All have the required single-instance, zero-offset shape, vertex declaration
and complete/preflight-supported recipes. Report: `build/m2-component7-capture-check.json`.
This is offline inspection; it does not establish a game driver build or visual result.

The hook additionally requires a primary command buffer, classic render pass, original
mapped index bytes with the exact region hash, mapped instance/current/previous palette
bytes, and two dynamic poses. It refuses extended Bind*2 state, an application set 3,
unsupported pipeline/layout declarations and missing original bindings. Unsupported,
unreadable, pending or exhausted requests retain the original draw. No original draw
is skipped until the complete replacement transaction is ready.

## Ownership and command recording

The worker creates the immutable mesh and a bounded reserve of snapshot arenas.
Each command-buffer owner retains its mesh, ready variants and an arena of 16 private
draw slots. Every replaced draw imports its own two poses and copies a patched 256-byte
entry into a fresh slot. The vertex stage uses private set 3; the fragment stage and
application descriptor sets keep their original data. The emitter restores the original
pipeline and bindings 0-3/index buffer before the next application command.

Arena contents seal before submission and remain unchanged across resubmissions.
Only a successful legal command-buffer begin/reset or pool reset recycles its slots;
Vulkan requires the application to finish GPU uses before those operations. Failed
resets preserve ownership. Completed command buffers retain their arena to avoid
allocation every frame, while free/pool destruction releases it. The backend keeps
at most eight live arenas and two spare arenas, and joins both workers before device
destruction. It does not infer GPU completion from a present interval or timeout.

## Configuration and diagnostics

The candidate configuration names the project-owned package explicitly:

```ini
mode = probe
resources = 1
image_hash = 0
profile_tracker = 0
m2_package = F:/efmi vulkan/build/m2-component7.bin
m2_enabled = 1
```

Environment equivalents are `EFMI_VK_M2_PACKAGE` and `EFMI_VK_M2_ENABLED`.
An omitted package or `off` disables the backend; an omitted enable value is false.
Status advertises `m2_component` and a `replacement` object with configuration,
readiness and match/replacement/pending/refusal/arena counters. `last_reason` is the
last fallback reason, which can remain after subsequent successful draws.

The pipe request `{"cmd":"m2","enabled":0}` retains original draws in future
recordings; `enabled:1` resumes attempts. Without `enabled` it queries the backend.
Already recorded commands are not rewritten. Changing the cfg affects the next
process only. The Python client can issue `client.request("m2", enabled=0)` against
an explicitly selected PID. The general MCP wrapper has no replacement-control tool.

## Verification and next game checkpoint

Native package tests cover corrupt/truncated data, overflowing dimensions, invalid
coordinates/weights/bones, missing files and incompatible declarations. Four headless
GPU runs exercise the actual DLL hooks through layer/inject and RenderPass 1/2.
They verify 30 pose changes, following original draws without rebinding, explicit and
implicit command resets, pool reset, immutable resubmission after disabling, original
fallback for an unmapped palette, recovery after remap and live enable/disable.
All rendering is checked by pixels under core/synchronization validation.

The first full run found that the new native test had not initialized the shared CRC
helper; the DLL initializes it normally and all four GPU runs passed. The test now
uses production initialization. Final suite results belong in CURRENT_STATE and the
session log, including this correction.

Real-character rendering, appearance, animation, visual correctness of retained
fragment inputs and game overhead are still unverified. The next user-started run
must exercise this one component, first in display/LOD0 and then the actual LOD1
state. Confirm loaded version/configuration, ready/replaced counters and fallback
reasons; compare enabled/disabled future recordings if needed. No duration-only
test is required. Other components, texture overrides, remaps between components,
multiple-instance behavior and the image panel remain later M2/M3 acceptance work.
