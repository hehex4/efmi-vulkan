# EFMITTL compatibility

2026-10-24, candidate 1.7.7. The requested library is
`D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Mods/EFMITTL`, development 0.5.2
(`$version = 0.52`). Its installed files are read-only. This work does not
enable transparency in the game yet.

## Actual support boundary

| Part | Owned execution | Current game integration |
| --- | --- | --- |
| Original INI, resources, expressions and command lists | Executed unchanged by the original frontend | The character loader loads each character's folder and Core, not the external library's shared frame state |
| HLSL and supplied DXBC caches | Compile/translate and execute on Vulkan; derivative capability fixed in 1.7.7 | Custom shader execution exists for owned resources |
| Opacity, mask, shadow, front/back layers and composite | Pixel/depth tests against independent expected values | Borrowed game vertex/pixel programs, targets and ordered work inside the application's passes are not wired |
| Four pattern-only ShaderRegex groups | Native shader hook machinery exists separately | D-089/LIMIT-036 keeps game shader hooks inert; SPIR-V is not DXBC assembly |
| Render target identification | Added capture metadata with native and end-to-end tests | Requires the new layer in a user-started Vulkan session; historical frames lack these facts |

Do not advertise this candidate as fully supporting EFMITTL. Parsing an INI,
compiling all seven shader programs, or injecting an opaque fallback does not
establish the requested transparent rendering. The new fixture deliberately
supplies pass signals and synthetic geometry; it does not prove recognition of
the game's shaders or correct placement of the game composite.

## Executor fix

The installed alpha pixel shader's DXBC cache translates to SPIR-V with
`DerivativeControl` (capability 51). The executor rejected it even though it is
a core Vulkan 1.0 capability. Source-only compilation had not exposed this
runtime refusal. The allowlist now accepts this capability; the SPIR-V
validator and existing stage/instruction checks remain. Refusals identify the
capability and shader stage. No other capabilities or optional device features
were enabled. See the capability table in the
[Vulkan SPIR-V environment specification](https://docs.vulkan.org/spec/latest/appendices/spirvenv.html).

## Reproducible GPU checks

Run `python scripts/check_efmittl.py`. It builds `sandbox/efmittl_gpu.cpp` against
the production original INI/GPU executor, copies the unchanged library into
fresh project-owned fixtures and records hashes of every installed input.
One run keeps the supplied DXBC caches; a second omits only the copied caches
and exercises compilation from the unchanged HLSL sources. Both use Vulkan
core and synchronization validation in headless owned processes.

The cases cover:

- Alpha 0, alpha 1, the default alpha, one/four layers and the fifth call sharing
  the last layer; every front/back surface is checked against sorted CPU alpha
  composition. Scene alpha must survive composition.
- Complete/partial scene-depth occlusion, all four mask channels, scaling and
  inversion, negative/repeating UVs, bilinear interpolation and derivative-based
  selection of a nonuniform mip with other mips holding different values.
- Plain and fading shadow depth, positive/negative fade, a nonzero instance
  index, masked, invisible and opaque calls; every texel is compared with the
  opacity/noise/fade reference.
- Four-frame missing-hook fallback and recovery, target resizing, neutral
  screen-space mask fill and restoration of the previous targets/mask.
- Public argument/IniParams restoration, layer consumption, repeated frames
  and release of every resident/staging/object budget after generation shutdown.

Reports and logs are in `build/efmittl-gpu/` in the development worktree. The
report distinguishes cached and source runs, records executable/input hashes
and lists the cases actually executed. The full regression suite includes this
step. This is owned execution only, not a real character or live-game result.

Current result: 24 cases/356 checks in each path, zero validation errors/warnings.
Each fixture checks actual cache selection (eight translated shader stages in
cached mode, zero in source mode). All 14 installed input hashes remain unchanged.

## Attachment capture

`capture` now includes `render_targets`. A draw's `targets` is a one-based index
into that array, or zero when no context was recorded. Each context reports its
record-time framebuffer/render-pass identity, subpass, render area, layers and
attachment views/images, formats, usage, sample count, extent, subresource range
and attachment layout. Inputs and resolves preserve image aliasing. Dynamic
rendering includes its flags, view mask, color/depth/stencil and resolve targets.
Swapchain images receive identities even though they have no `CreateImage` call.

Classic and imageless framebuffers, render-pass-2 next-subpass calls and dynamic
rendering are covered. Secondary draws inherit a primary pass's targets or
rebase their own contexts. Snapshots own metadata, never application Vulkan
objects; handle reuse/destruction cannot change earlier identities. Unsupported
extensions or absent facts are marked unknown. This is not a synchronization or
layout-transition ledger and must not be used as permission to borrow a target.

Context collection is armed only for a capture request, with limits of 4,096
contexts per command buffer and 16,384 per capture. It adds no per-draw GPU call
and no context allocation during ordinary rendering. Already-recorded command
buffers and an exhausted limit can produce `targets = 0`; do not interpret that
as no attachments. Request completion/timeout disarms collection. Framebuffer
creation retains a bounded metadata description throughout its normal lifetime.

## Next integration step

Historical catalogues contain shader/resource facts but no attachment identity
or scene-color/depth aliasing around the game's depth-copy draw. The structural
audit (`build/efmittl-role-audit.json` in the main project) finds several small
explicit-LOD image shaders; none is a proven compositing boundary. DXBC register
declaration masks also differ from the Vulkan output declarations. No shader
hash table, character profile or fabricated DXBC disassembly was introduced.

Use a user-started Vulkan session with 1.7.7 to capture one visible consumer and
its surrounding passes. Validate scene targets and the depth-copy ordering,
then implement shared-library frame ownership, general pass recognition and
ordered capture/composite execution using those facts. Library frame state must
be shared across characters, with one composite and one reset per rendered
frame, and preserve original draw/descriptor state. Follow with an owned replay
of the captured pass sequence and then real-character visual acceptance.

The launcher, installed library/Mods/Core and current game session are unchanged.
The version on disk is not evidence of the version loaded into a process.

## Candidate verification record

Built in `F:/efmi vulkan/build/efmittl-next`. The full 36-step run completed with
35 passing and one obsolete probe assertion failing: it expected one image,
excluding the now-tracked swapchain images. The assertion now includes the
reported swapchain size, and all 17 affected probe configurations pass on rerun.
The first full log remains unchanged; a second all-green full run is not claimed.
Updated Python reader units also pass 117/117. Both capture replays, native 178,
GPU resources 11189, character 361, live INI 76, Vulkan validation 76 and MCP 50
pass. Logs: `build/efmittl-run-all-1.7.7.out` and `build/efmittl-probe-1.7.7.out`
under the main project.

Candidate source is on `codex/efmittl-support` in `build/performance-worktree`.
The main tree's 21 older WIP files are preserved, including an overlapping
shader executor edit. Implementation commit: `776e22e`. After the game and
launcher had closed, two process checks preceded deployment of all 18 tested
files; hashes match and the cfg is unchanged. The old 1.7.6 files/config/log are
in `build/backups/before-1.7.7-20261024-213603/`. Actual-config sandbox injection
loaded 1.7.7 with 23 packages, captured a known attachment context and exited
normally (`build/efmittl-smoke-1.7.7.json`). No game load is claimed. The launcher
still reads DX11/DIRECT and was not changed. When collecting the new data, use
the candidate's client/server code or raw `ProbeClient.capture`;
an already-running older MCP server does not acquire the new reader automatically.
