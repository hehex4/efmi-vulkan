# Vulkan-native Mods: ShaderOverride, hunting and native shader replacement

Route 2 of the project (the user, 2026-11-10, restated 2026-11-12): a version of 3DMigoto for this
game's Vulkan renderer, for Mods written against Vulkan itself, beside route 1 (installed DX11-written
Mods unchanged). Its order: the review's Phase 1 (a draw's lists run against a draw state) and Phase 2
(any Mod replaces meshes), both done; then this step, ShaderOverride by SPIR-V hash and hunting; then
replacing game shaders; then slots of other stages (`vs-cb`, `vs-t`, render targets). This document
covers the implemented shader, resource-binding and capture steps through D-233. Section 8 separates
those deliverables from the remaining architecture and game-acceptance work.

## 1. What 3DMigoto does (read from `F:\vulkan\XXMI-Libs-Package\DirectX11`)

### ShaderOverride

- **The hash.** A shader is named by a 64-bit hash of its bytecode, computed when the game creates it
  (`CreateVertexShader` and the others in HackerDevice.cpp) and printed as 16 hex digits. A section
  reads it with `GetIniHash` (IniHandler.cpp): `sscanf("%16llx%n")`, the whole value or nothing, so
  `hash = 1234abcd` is the 64-bit number 0x1234abcd.
- **Parsing** (`ParseShaderOverrideSections`): every `[ShaderOverride*]` section goes into
  `G->mShaderOverrideMap[hash]`. Sections with one hash share one entry: the later section's commands
  are appended to the same `command_list` and `post_command_list`, `filter_index` is read again from
  each section (default `FLT_MAX`), and `allow_duplicate_hash` decides only whether the duplicate is
  warned about (all must say `true`, or one `overrule`). Keys of the section itself: `hash`,
  `allow_duplicate_hash`, `depth_filter` and `model` (legacy), `disable_scissor` (becomes a `run` of a
  built-in list), `filter_index`; every other line is a command (`handling = skip`, assignments,
  `run`, `checktextureoverride`, bindings, draws), `pre` by default, `post` with the prefix.
- **ShaderRegex links** (ShaderRegex.cpp `link_command_lists_and_filter_index`): a ShaderRegex group
  that matches a shader appends a `run = linked command list` to that hash's entry, after the
  section's own commands, and sets the entry's `filter_index` only when no section set one. The EFMI
  Core's `[ShaderRegexEnableTextureOverrides]` matches every vertex shader with
  `if $enable_mods: if DRAW_TYPE % 2 == 0: CheckTextureOverride = ib`: that is how TextureOverride
  sections run at all.
- **At a draw** (HackerContext.cpp `BeforeDraw`, `AfterDraw`; every `Draw*` call goes through them):
  hunting first (below), then for the bound vertex, hull, domain, geometry and pixel shader in that
  order: look the hash up, remember the entry's post list, run its pre list (`ProcessShaderOverride`:
  the legacy `depth_filter` decides whether the original shader is swapped back in). The draw goes to
  D3D11 unless `call_info.skip` was set (`handling = skip`, by any list of any stage). `AfterDraw`
  runs the post lists in the same stage order. The vertex stage's entry holds the Core's link, so the
  order of one draw is: the Mod's vertex ShaderOverride commands, the TextureOverride sections of the
  index buffer, the hull, domain and geometry entries, the pixel entry; the same order for the post
  lists.
- **`vs`, `ps` as values** (CommandList.cpp `process_shader_filter`): no shader bound reads -0; a
  bound shader without an entry 0; an entry with `filter_index` that value; an entry without one 1.
  This is how a TextureOverride tells passes apart (`if ps == 203`, the outline pass a ShaderOverride
  marked).
- **`checktextureoverride = ps-t0`** (any slot): runs the TextureOverride sections of the hash of the
  resource bound there, in the same draw. The layer has this already for the texture-hash sections.

What D3D11 does for 3DMigoto here: a draw is one call on one context; a list's bindings stay on that
context until changed; not issuing a draw has no further consequence.

### Hunting (Hunting.cpp, HackerContext.cpp, HackerDXGI.cpp, Overlay.cpp)

- **Settings**: `[Hunting]` of d3dx.ini. `hunting = 0` registers no hunting key (XXMI then keeps it
  "soft disabled", 2, for its ShaderRegex), `1` enabled, `2` soft disabled: the keys are registered and
  `toggle_hunting` switches between 1 and 2. `marking_mode` (`skip`, `original`, `pink`, `mono`; key
  `next_marking_mode`), `marking_actions` (`clipboard`, `hlsl`, `asm`, `regex`, `mono_snapshot`,
  `snapshot_if_pink`; default clipboard and hlsl), `repeat_rate`, and per kind a `next_`, `previous_`
  and `mark_` key: pixel, vertex, compute, geometry, domain, hull shader, index buffer, vertex buffer
  (and its slot), render target; `done_hunting`. The installed d3dx.ini has `hunting = 2`,
  `marking_mode = skip`, `marking_actions = clipboard hlsl asm regex`, numpad keys (toggle on 0, pixel
  shaders 1-3, vertex shaders 4-6, index buffers 7-9, `+` done) and `analyse_frame` on Shift+F11.
- **What is visited**: while hunting is enabled, every `*SetShader` inserts the shader's hash into the
  stage's set (`std::set`, so in ascending hash order); `BeforeDraw` registers the draw's index and
  vertex buffer region hashes (with `track_region_hashes`). The sets are cleared after 60 seconds
  without a hunting key (`TimeoutHuntingBuffers` in the Present hook) and by `done_hunting`.
- **Selection** (`HuntNext`, `HuntPrev`): the next or previous element of the set after the selected
  one, wrapping; the overlay shows `VS:3/15 PS:7/40 ...` (position from 1, set size).
- **At a draw**: if the draw's index buffer, any stage's shader, a vertex buffer or a render target is
  the selected one, the draw is "marked": `marking_mode = skip` sets `call_info.skip` and
  `hunting_skip` (the latter keeps a `draw = from_caller` of a list from drawing it again), `pink`
  swaps in a pink pixel shader; the ShaderOverride lists still run. The marked shader's peers (index
  and vertex buffers drawn with it) are collected for the mark's log.
- **Mark** (`MarkPixelShader` and the others): logs `>>>> pixel shader marked: pixel shader hash = %016I64x`
  and the visited peers, copies the hash to the clipboard (`HashToClipboard`, "copied to clipboard"
  on the overlay), dumps the shader's HLSL or assembly to ShaderFixes, takes screenshots, as
  `marking_actions` says.
- **Index and vertex buffers** (BeforeDraw with `track_region_hashes`, ResourceHash.cpp): the index
  buffer's hash is the region hash of the draw's indices (the TextureOverride hash); a vertex buffer's is
  the CRC-32C of its region from `offset + stride * FirstVertex`, `stride * VertexCount` bytes long, or for
  an indexed draw `(3 * IndexCount + 10) / 20 + 3` vertices from the base vertex (the EFMI fork's
  estimate). `next_vertexbuffer_slot` restricts the vertex buffers to one slot (-1, every slot, first) and
  starts their set again. Render targets are the bound views' resources; their mark logs the resource's
  hash and description (`LogRenderTarget`).
- **`hunting` as a value** (CommandList.cpp): `G->hunting`. XXMI never leaves it at 0 (ParseHuntingSection
  sets 2 for `hunting = 0`), so a list reads 2 while hunting is soft disabled and 1 while enabled; the
  optimiser folds it to 0 only when hunting is disabled outright.
- **Frame analysis** (`AnalyseFrame`, FrameAnalysis.cpp): only while hunting is enabled; the next frame
  is logged into `FrameAnalysis-%Y-%m-%d-%H%M%S` beside the DLL (`log.txt`: per draw, numbered with six
  digits, the calls that set its state with the shaders' hashes, then the draw call), and with
  `analyse_options` the bound resources are dumped as files; a second press aborts; the overlay says
  "Frame analysis saved to ...".

## 2. What Vulkan has of it, and what not

- **Has.** Every module's bytes at `vkCreateShaderModule` (the layer keeps them: `ShaderInfo`, the
  probe's `shader` request); every pipeline's stages and their modules at creation (`PipeInfo::shaders`);
  every draw with the pipeline bound for it (`DrawRec::pipeline`), on whichever thread records it,
  primary or secondary command buffer; not forwarding a draw is legal and changes no other state.
- **Not as in DX11.** Stages are not bound one at a time: a pipeline is a fixed set of modules, and a
  module is in many pipelines; one module's bytes may be created as several `VkShaderModule` objects
  (equal bytes, equal hash). There is no DX11 hash of a game shader (D-089: SPIR-V compiled
  separately, nothing derives one from the other). A shader cannot be switched at a draw without
  another pipeline (the next step, shader replacement, needs pipeline variants as the anti-dither
  twins of D-133 are). Bindings are by (set, binding), not by DX11 slot (a later step).
- **Not in this game.** Shader objects (`VK_EXT_shader_object`) and modules given inline to the
  pipeline (`VkShaderModuleCreateInfo` chained into a stage) were not seen; such a pipeline has no
  module hash here and no ShaderOverride acts on it.

## 3. How a Vulkan-native Mod names a shader

- **The hash is the SPIR-V module's CRC-32C**: of the bytes the application gives
  `vkCreateShaderModule` (`codeSize` of them), printed as 8 lowercase hex digits. It is exactly the
  `hash` the layer already shows: the probe's `shader` and `pipeline` requests (`hash_algorithm:
  crc32c`), the MCP tools `efvk_shader` and `efvk_pipeline`, the shader file names of capture bundles,
  the shader catalogue and slot tables (docs/shader-catalogue.md, keyed by vertex and fragment shader
  hash), the sandbox program's `expected.json`. A Mod writes it as 3DMigoto writes a shader hash:
  `[ShaderOverrideOutline]` `hash = 8a1b2c3d`.
- **Why not a 64-bit hash as 3DMigoto's.** The original's parser reads either width into one 64-bit
  value. A 32-bit value is below 2^32, and a 3DMigoto shader hash (64-bit FNV-1a) is below 2^32 with a
  chance of 2^-32: the value alone tells a Vulkan section from a DX11 one at load, which keeps the
  compatibility route's notice exact (section 5), and the hash every existing tool prints stays the
  one a Mod writes. The cost is the chance of two of the game's modules sharing a CRC-32C: about
  1300 modules were alive at once in the 2.1.3 game session, so a pair among them is about 2 in 10000.
  The layer checks it: a module whose hash an earlier module of other bytes already had is a
  collision, logged and counted in the status (`shader_hash_collisions`), and a ShaderOverride on that
  hash acts on both, as a 3DMigoto texture hash shared by two textures does.

## 4. A ShaderOverride on a SPIR-V hash

- **Loading.** The folder generation parses the ShaderOverride sections whose hash is below 2^32 with
  the original's parser and its rules (merging, `allow_duplicate_hash`, `filter_index`, warnings);
  the others are DX11 hashes and stay as today (section 5). D-191 implements `depth_filter` for native
  replacements: `depth_active` requires a depth attachment in the captured subpass, `depth_inactive`
  requires none. A mismatch uses the original stage; the section's command lists still run. `model`
  remains an offline HLSL compiler option, with a notice for the unused INI key.
- **At every draw** (indexed, non-indexed, indirect) of every pipeline whose stage modules include such
  a hash: the entries of the vertex, tessellation control and evaluation (hull, domain), geometry and
  fragment (pixel) module run as the original's, in its order and with its draw call info (`DRAW_TYPE`
  of the Vulkan call: `DrawIndexedInstanced`, `DrawInstanced`, or their indirect forms):
  1. the vertex entry's pre list;
  2. for a draw a Mod section matched (the device index, D-097, D-173): the library's vertex groups and
     the TextureOverride sections, as now;
  3. the hull, domain and geometry entries' pre lists, then the pixel entry's;
  4. the library's pixel groups, as now;
  5. the game's draw unless a list set `handling = skip`; for a matched draw, the Mod's draws as now;
  6. the post lists in the same order.
- **`vs`, `ps`** read `process_shader_filter`'s values with the module's entry: its `filter_index`, 1
  without one, 0 for a bound module without an entry, -0 for a stage the pipeline has not. A vertex
  module without an entry keeps BUG-090's value (the Core's patternless ShaderRegex).
- **A draw no Mod section matched** runs only the ShaderOverride lists, on the folder's generation,
  under its plan lock as a matched draw's. What its lists can do in this step: variables, conditions,
  keys' state, `run` of command lists, `handling = skip`, `vs` and `ps`. A binding or a draw of the
  lists at such a draw is not done: the game's draw stays as it is (also with `handling = skip`, as
  D-162 leaves a draw the host cannot draw to the game), said once per section. The game's textures at
  such a draw (`checktextureoverride = ps-tN`, texture queries) are not answered there yet: the layer
  answers them from a package's material state, which a draw no Mod section matched has not. Both
  need route 2's later steps (slots by (set, binding)).
- **Cost.** With no acting hash in the generation a draw pays one atomic read. With some: per draw the
  pipeline's module hashes (kept per pipeline), and the lists only where one matches.

## 5. Compatibility with the DX11 route

The installed Mods are DX11-written and are route 1's; this step must not change what any of them
does (the user, 2026-11-12: consider the compatibility with the existing DX11 route). The rules:

- **A DX11-hash section stays exactly as today.** A section whose hash is 2^32 or more is a DX11 hash
  (written for 3DMigoto under DX11: Xaihi's `ps skip.ini`, perlica's hair sections). It is not parsed
  into the generation (as today: its lists are not compiled, so no new warning can come from them),
  and the layer says once at load what it says today ("acts on game shaders by their DX11 hash, which
  the Vulkan layer cannot see: inert"); one that only declares (`hash`, `filter_index`,
  `allow_duplicate_hash`) is said nowhere. Only sections with a SPIR-V hash are parsed, with the
  original's parser and its rules among themselves.
- **Nothing else changes for a folder without a SPIR-V-hash section**, which is the installed folder
  today: no list runs at a draw that did not run before, `vs` and `ps` read what they read (0, BUG-090's
  value for `vs`), the order of a matched draw's lists is the same, and a draw pays one atomic read.
  Checked by the plan digests of every installed Mod in every key state (`scripts/check_mod_states.py`:
  the build before and this one on the same folder) and by the notices of the installed folder in the
  sandbox (Xaihi's two lines and nothing new).
- **Both in one Mod, or one Mod for each.** A DX11 Mod's TextureOverride that tells passes by a
  ShaderOverride's `filter_index` (`if ps == 203`, its outline section by DX11 hash) reads 0 under
  Vulkan today. A section with the Vulkan module's hash and the same `filter_index`, in the Mod or in an
  INI of its own beside it, makes the same condition true at the same draws: the bridge from a DX11 Mod
  to its Vulkan port is a few lines, not a rewrite. A DX11-hash and a SPIR-V-hash section in one Mod
  never meet, so neither changes what the other does.
- **Hunting keys.** The installed d3dx.ini registers its numpad keys under XXMI's DX11 (`hunting = 2`);
  under the layer they now do what they do there (toggle on numpad 0; the player under DX11 has the
  same keys). With hunting soft disabled, as installed, no draw is skipped and nothing is visited until
  the toggle; `hunting = 0` in d3dx.ini switches them off for both routes.
- **The compatibility route's own uses of shaders are untouched**: EFMITTL's groups and RabbitFX's gates
  (D-171, D-133) stay native and keep their places in a matched draw's order (section 4, steps 2 and 4).

## 6. Hunting in the layer

- **Settings and keys** are the original's: the installation's d3dx.ini (its own sections join the
  folder generation first, D-154), its `[Hunting]` section, its key strings through the original's
  `RegisterIniKeyBinding` and input dispatcher, so modifiers, `NO_` prefixes and `repeat_rate` mean
  what they mean under XXMI. `hunting = 0` registers nothing. A key's action is queued by the
  generation and carried out by the layer after the presentation it was dispatched in. They are the host's keys, not a Mod's: the checks that press every Mod key
  (`scripts/check_mod_states.py`) leave them out.
- **State** is the layer's, for the process (an F10 reloads `hunting`, `marking_mode`,
  `marking_actions` from the file and keeps the selection, as the original's globals do): the mode
  (1 enabled, 2 soft disabled), the visited sets, the selection per kind, the time of the last key.
- **Visited** (D-218): while enabled, draw module hashes are collected in ascending order. Every
  registered input action, including Mod keys, refreshes the idle timer. Collections clear on every
  idle frame before the first action and after 60 seconds without one, or on `done_hunting`.
- **Persistent counts** (D-218): the host displays VS/PS/CS/GS/DS/HS and selected VB/IB/RT counts
  independently of transient notices, before idle clearing. An active unselected kind shows `0/0`,
  as the original. Disable, F10 and device teardown remove stale text; a staged code module cannot
  publish into the active module's display or analysis session. Existing fenced overlay ownership
  and presentation dependencies also cover this white count line.
- **Selection and skip**: `next_`/`previous_` vertex and pixel shader as `HuntNext`/`HuntPrev`; with
  `marking_mode = skip` every draw whose pipeline has the selected module is not forwarded (a draw a Mod
  replaced keeps the Mod's draws, as `hunting_skip` keeps a list's own draws under the original).
  D-192 adds `original` (restore only the selected VS/PS stage, including its native replacement and
  anti-dither patch) and `pink` (the selected draw's pixel output is float4(1,0,1,1) at location 0).
  Pink supports shader, buffer and target selections, including ordinary Mod mesh variants; a custom
  shader pass such as a translucency capture keeps its own shader. The host restores the previous
  pipeline after each direct/indirect draw, and recording owners retain every temporary twin.
  An incompatible pink output interface leaves the original shader and reports why. `mono` has no
  stereo separation to change in this renderer and is reported as such.
- **Mark**: the original's log lines with the 8-digit hash (`>>>> pixel shader marked: pixel shader
  hash = 8a1b2c3d`, `>>>> Index buffer marked: index buffer hash = ...`, `>>>> Vertex buffer marked:
  ...`, `>>>> Render target marked: ...` with the image's format and size, then the visited peer modules),
  a notice on the overlay, and the hash on the clipboard when `marking_actions` has `clipboard` (the
  player's own clipboard, as XXMI). With `hlsl`, `asm` or `regex` (the original's dump actions) a marked
  module's SPIR-V is written as the game created it, `<hash>-vs.spv` or `<hash>-ps.spv`, into
  `ShaderFixes` in the layer's settings folder (never the installation's, which is the DX11 route's): the
  file to start a replacement from (section 7).
- **Index and vertex buffers, render targets** (D-189): while enabled every indexed draw's index region
  hash, its vertex buffers' region hashes by the original's rule (above; both read from the application's
  mapping or the layer's shadow copy, as the original reads its own copy of a buffer) and its colour
  targets' images (known where the draw
  is recorded inside its render pass instance; draws in secondary command buffers have none) are
  visited; `next_`/`previous_`/`mark_` index buffer, vertex buffer (and `next_`/`previous_vertexbuffer_slot`)
  and render target select and mark as the shaders do, and a draw with a selected one is skipped.
- **`hunting` as a value** (D-189): the generation's `G->hunting` is the layer's mode (2 where no key is
  registered or while soft disabled, 1 enabled), set before the optimiser and at every presentation.
  No installed Mod reads it.
- **Frame analysis, the log** (D-189): `analyse_frame` while enabled records the draws between the next
  two presentations into `FrameAnalysis-<time>/log.txt` in the layer's settings folder: per draw its
  number, command buffer and pipeline, `VSSetShader hash=...` and the other stages, the index region
  hash, the vertex buffers' region hashes, the colour targets, then the call as the original names it
  (`DrawIndexedInstanced(...)`, `DrawInstanced(...)`, the indirect forms); a notice says where.
  D-202 adds completed GPU buffer/image exports and D-203 adds geometry, within the explicit capture
  scope described below. D-213 applies the original global `analyse_options` resource categories;
  D-214 adds requested file encodings; control flags and per-draw analysis commands still need adapters.
- **What the player sees**: a notice per key with the selection (`PS 3/40: 8a1b2c3d`, `IB 2/5: ...`,
  `RT 1/1: image 7`) in place of the overlay's line. The status has a `hunting` object (mode, marking
  mode, the visited counts and selections of the nine kinds, the vertex buffer slot, draws skipped,
  marks, dumps, frame analyses).
- **Not yet**: unrestricted resource capture, the overlay's steady line. D-193 adds compute
  hunting and D-195 adds geometry, hull and domain stages (sections 10 and 12).

## 7. What needs the game

Everything above is checked in the sandbox (`sandbox/runtime_test.py`: the sandbox program's own
modules, its five draws on two recording threads). The game adds: how many draws a ShaderOverride
meets per frame and what its lists cost there; hunting over the game's thousand-odd modules (the
visited sets, the order a player cycles through them, the frame time while enabled); the clipboard and
notices while playing; whether any game pipeline has no module hash (inline modules).

## 8. Delivery status and remaining work

1. Replacing game shaders: D-190 implements the SPIR-V files and an offline HLSL compiler (section 9).
   D-191 adds `depth_filter`; D-192 adds `pink` and `original` marking; D-193 adds compute;
   D-195 completes the classic graphics stages with geometry and tessellation;
   the compiler's explicit `--model` chooses the native HLSL target, not an INI `model` directive.
2. D-196 adds explicit (set, binding) aliases and queries; D-197 executes native draw/dispatch lists;
   D-198/D-199 retain and bind application buffers with persistent context semantics;
   D-200/D-201 add published file buffers, null descriptors and sampled 2D file images.
   D-206 adds guarded retained application sampled images; D-209 adds sampled file arrays/cubes. D-219/D-222 add private writable compute file buffers/images. General cross-queue hazards
   still need an adapter. Render-target rebinding is not implemented.
3. D-202 exports completed GPU buffers/images; D-203 adds vertex/index allocation data and per-draw
   metadata. Scope is new one-time recordings, one application queue, synchronization2 disabled,
   transfer-source resources and supported owned layouts. Broader capture synchronization,
   D-223/D-224 add dynamic geometry/input capture; per-draw analysis commands remain work. D-214/D-215 add file encodings and held analysis sessions.
   D-208 adds indirect argument/count data; D-212 adds bounded mip/layer cells; D-213 adds resource selection.
4. Real-game shader/hunting/replacement acceptance and performance measurements require the user's
   authorization for that session. Sandbox results cannot settle them. See the current state and
   session log for the exact source, binary and historical game versions.

## 9. Native game shader replacement (D-190)

The layer reads `<hash>-<stage>_replace.spv` from `ShaderFixes` inside its settings folder. `hash` is
the original eight-digit CRC-32C from hunting; `stage` is `vs`, `hs`, `ds`, `gs` or `ps`. Hunting's
`<hash>-ps.spv` dumps do not act until explicitly made replacement files. The DX11 installation's
ShaderFixes folder is not read or changed. A native shader keeps the game's entry point, descriptor
set/binding interface, push constants, and stage inputs/outputs. It can stop using inputs/descriptors,
but cannot add resources or widen declared access. Vertex and other producer stages must keep outputs
needed by the next stage, including built-ins. Changed resource bindings belong to the next route-2 step.

An HLSL source can be compiled offline, using the tools installed in `F:/vulkan`:

```powershell
python scripts/compile_game_shader.py shader.hlsl --hash 12345678 --stage ps --output-dir dist/settings/ShaderFixes
```

Replace the example hash with the hunted module's hash. Use explicit `[[vk::binding(binding, set)]]`
and `[[vk::location(location)]]` declarations; this compiler does not apply the compatibility route's
DX11 register shifts. `--entry` defaults to `main`, `--model` to `6_0`, and `--target` to `vulkan1.1`.
The compiler validates its temporary SPIR-V before replacing the destination atomically; compilation
or validation failure leaves the last good file intact. This command only creates a file. F10 loads
it into an already running layer; real-game loading still requires the project's authorization rules.

Files load with the folder generation, initially and on F10. The folder worker validates SPIR-V for
the application's Vulkan version and the enabled features supported by the owned shader runtime. A
known pipeline's interface is checked there too; new pipelines are checked on the variant worker.
An invalid reload keeps the last good module and reports why; deletion restores the game shader.
Unchanged code keeps the same revision and cached variants. An interface mismatch first encountered
on a new pipeline keeps that pipeline's original stage. A refused native stage does not suppress a
character Mod's mesh. ShaderOverride matching and hunting always use the original hash.

A shader can satisfy Vulkan's interface but remove a pattern the compatibility renderer needs for
its bone or image rewrite. If such a replacement prevents a character variant from being built,
that variant retries with the game's original stages and records the reason. This preserves the Mod
mesh; it does not claim that every authored shader can compose with the compatibility rewrites.

Variants are prepared from owned graphics recipes when game pipelines are created or the files reload.
A shader-only variant keeps the application's full layout without adding a private set. Character
variants use the same published shader set before their ordinary vertex/image rewrites. Queued builds
retain a revision snapshot, and recorded command buffers retain their actual pipelines through
completion, including an old revision after F10. Native twins obey the backend's enable switch.

Descriptor layouts and immutable samplers retain the original handles until the application has
requested destruction and all metadata/variant/recording owners are gone. Using the original layout
also avoids a false incompatibility from the pinned validation layer's immutable-sampler pointer
comparison. The original 3DMigoto saves sampler references with `PSGetSamplers`, restores them with
`PSSetSamplers` and then releases them; Vulkan requires this lifetime to be explicit. Native shader
replacement adds no resource upload or write dependency: the replacement must preserve the game's
resource access contract; existing queue order/barriers and the character path's owned-work
synchronization still apply. See the Vulkan specification's
[pipeline layout compatibility](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html#descriptorsets-compatibility).

The status's `shader_fixes` reports loaded files, revision, bind count and file-validation warnings.
Per-pipeline interface/build refusals appear in the overlay and log. A file count alone is not proof
that a replacement was drawn. Current boundaries: graphics pipelines captured by the existing recipe
tracker; a single matching entry point; supported core capabilities and fully tracked layouts. Layout
binding flags, unsupported extension state, and immutable samplers/layouts created with custom host
allocators remain refused. An empty Mods folder supports initial ShaderFixes loading and F10 via the
existing host input fallback (verified in D-192). D-194 loads the installation generation even without a Mod INI, so hunting keys and root ShaderOverride sections remain active with the configured Core.
D-193 adds compute shader replacement and hunting (below). Native arbitrary resource/draw lists remain pending. D-191 publishes
depth filters with the INI/shader generation; a filter-only F10 changes the revision and invalidates
cached variants. Depth presence means an attachment, independently of the pipeline's depth-test switch.

Verification lives in `sandbox/native_shader_test.py`; `sandbox/run_all.py --no-build --only native-shaders`
adds tracker lifetime, pipeline/cache, live INI and validation checks. These are sandbox results, not
real-game acceptance.

`sandbox/hunting_shader_test.py` checks VS/PS original selection, pink shader/index/target selections,
clearing, a Mod's own mesh region, non-indexed and indexed-indirect draws, and the inject entry. A
render-target selection identifies one swapchain image, so another presented image need not be pink;
that case checks selected pipeline binds and validation, while the shader/index cases verify pixels.

The routine pipeline step runs deterministic fixtures. Historical captured-shader rewrites are a
separate `sandbox/run_all.py --no-build --only shader-corpus` step, also in the full suite and the
bones group; it retains the capture checks without scanning the entire capture tree for every native
pipeline change. This separation avoids the old 30-second cold-read timeout.


## 10. Native compute modules (D-193, 2.2.3)

`ShaderFixes/<original-hash>-cs_replace.spv` uses the same file generation, F10 and last-good
rules as graphics. The offline compiler accepts `--stage cs`. Owned compute recipes retain the
entry point, specialization entries/data and module bytes after application shader destruction.
The shared variant queue creates a compute pipeline with the original descriptor layout and push
ranges, without a render pass or private set. Recording owners retain it until completion.

Direct and indirect dispatches run matching ShaderOverride command lists with the original hash,
`draw_type` 8/9 and the `cs` filter value. The existing variables/conditions/run/skip boundary still
applies: arbitrary resource bindings or dispatch commands in these lists need the native slot
adapter and are reported as unavailable. A compute list does not enter the character draw's
EFMITTL hooks or restore a draw's input layout when it ends.

The original's `BeforeDispatch` deliberately bypasses `ProcessShaderOverride`: graphics depth
filters do not apply, and hunting skip returns before the lists. `next_computeshader`,
`previous_computeshader` and `mark_computeshader` select original module hashes; skip omits the
dispatch, original uses the application's shader, and pink has no compute pixel output to change.
Mark dumps the original as `<hash>-cs.spv`; frame-analysis logs include CS identity and dispatches.
The host restores the previous compute binding after a temporarily marked dispatch.

Native compute modules must preserve resource access and the original fixed local workgroup size.
Device limits for dimensions, invocation count and shared memory are checked before publication.
Fixed `WorkgroupSize` constants are resolved because they override `LocalSize`, as defined by the
[Vulkan shader interface specification](https://docs.vulkan.org/spec/latest/chapters/interfaces.html#interfaces-builtin-variables-workgroupsize).
Specialized local sizes, unsupported pipeline flags/extensions, inline modules and shader objects
are not covered. No resource upload or new access is introduced: application barriers/order remain
in force. Under DX11 the runtime supplies hazard tracking, reference-counted deferred destruction
and context order; here these are explicit interface constraints and retained Vulkan owners.

`sandbox/native_compute_test.py` runs real application dispatches, reads the storage buffer only
after a fence and exercises both entry methods, replacement/fallback, commands, hunting and F10.
The native-shaders group includes it. These checks establish sandbox behavior, not game acceptance.


## 11. Empty Mod inputs (D-194, 2.2.4)

An existing empty Mods directory still has an installation configuration. The folder loads the
configured Core and d3dx.ini, registers hunting/F10, and runs root ShaderOverride sections. Adding
or removing the last Mod exchanges that same generation; removal clears its commands while keeping
installation keys. No placeholder INI is generated in Mods. If the configured Core cannot load,
the existing empty-folder fallback still loads shader files and supports the host's F10 fallback;
it does not claim to have loaded installation commands.

`sandbox/native_input_test.py` covers initial empty inputs, removing the last INI, root overrides,
shader/config reload and adding/removing the first Mod, with compute GPU readback and validation.
`--only native-input` runs this plus the existing live INI step and text check. Empty graphics
ShaderFixes/F10 are checked separately with `native_shader_test.py empty f10:empty`.

## 12. Geometry and tessellation modules (D-195, 2.2.5)

Native graphics recipes retain all five classic stages (VS, HS, DS, GS, PS), including the
tessellation patch state. HS and DS must be present together. Ignored tessellation pointers are
not read without that pair; unsupported extension state remains refused. A rebuilt pipeline uses
the original layout and owned shader/entry/specialization data, with no private descriptor set.

`ShaderFixes/<original-hash>-gs_replace.spv`, `-hs_replace.spv` and `-ds_replace.spv` use the same
compiler, generation, F10 and fallback rules as VS/PS. Replacements preserve the original primitive,
patch, output-vertex and invocation contracts and input/output rate decorations. This retains the
application's valid inter-stage/resource interface and synchronization contract. No GPU upload or
new access dependency is introduced. Pipeline and module lifetimes remain explicit Vulkan owners;
the original DX11 runtime instead supplies reference-counted deferred destruction, hazard tracking
and one-context order for its generic SetShader path.

The original `next_`, `previous_` and `mark_` keys for `geometryshader`, `hullshader` and
`domainshader` select original hashes. Original mode restores only the selected stage; pink changes
the draw's pixel shader; marking dumps the original stage's SPIR-V. Status exposes geometry, hull
and domain selections alongside the existing kinds.

`sandbox/native_graphics_stages_test.py` checks real GS/HS/DS pipelines, all five graphics stages
together, replacement, original-hash skipping, original/pink hunting and dumps, F10, and refusal of
a changed primitive contract. GPU image readback and validation establish sandbox behavior only.
Arbitrary native slot bindings/draws, frame-analysis resource dumps and real-game acceptance remain
separate work.

## 13. Explicit descriptor aliases (D-196, 2.2.6)

A native ShaderOverride may repeat `vk_binding = ps-t3, 1, 5, 0`: original logical slot,
Vulkan descriptor set, binding and optional array element (default zero). The alias belongs to
that original SPIR-V hash and stage. All six stages support t/cb aliases; pixel and compute also
support u aliases. These are Mod-authored addresses, never guessed DX11 registers or per-character
runtime profiles. Duplicate identical aliases coalesce; conflicting aliases or invalid metadata
isolate that INI through the normal input loader, including F10. Other valid INIs remain active.

Queries resolve ordinary, template and pushed descriptors, including dynamic offsets and array
elements. They validate the current layout, stage visibility, descriptor type and object identity.
Size is the visible buffer range; image dimensions describe the resource, while Format describes
the bound view. Buffer-only properties return the original's non-buffer sentinel for images.
Native Offset additionally exposes the Vulkan descriptor byte offset for t/u buffers. BindFlags
translates creation usage, with storage buffers exposing SRV and UAV capability. Fuzzy texture
matching uses the resource format, independently of a differently formatted view. References
between logical slots or declarations retain these queries within the current list execution.
As in the original, `ResourceX->bindflags` reads the declaration's own flags even when it holds
a reference to an application resource; direct slot queries read that resource's creation usage.

Only known, read-only image upload content supplies a content hash. GPU-writable resources are
not assigned a recording-time content hash. Resource-description matching works without a hash;
this does not claim that a Vulkan identity is the game's old DX11 hash. Queries borrow metadata
only and introduce no GPU work or resource retention. Binding execution and references beyond
the current execution require the following native execution work.

The original ResourceCopyTarget getters and resource matcher supply the property/sentinel rules.
DX11 provides reference-counted deferred destruction, hazard tracking and one-context order;
those guarantees must be explicit when Vulkan starts executing resource operations. The
`native-slots` regression group exercises real compute and graphics queries, descriptor forms,
dynamic offsets, resource/view formats, refusal, declaration references and F10 in the sandbox.

## 14. Native application calls (D-197, 2.2.7; sandbox verified)

At an unmatched native application call, ShaderOverride lists preserve `draw = from_caller` as
an actual caller replay, including indirect/count-buffer addresses and stride. Explicit direct
draw/drawindexed/instanced calls and dispatches use the bound application pipeline and resources.
The executor places pre calls, the original call (unless skipped), and post calls in that order.
A post `handling = skip` arrives too late to remove the original call. Hunting skip suppresses
custom calls as well. Secondary command buffers retain whether they inherited a render-pass scope.

As in original DrawCommand::run, these commands use the original context's current state. DX11
supplies reference-counted deferred lifetime, resource hazard tracking and one-context order.
Here calls reuse the application's already-recorded object lifetimes, including retained temporary
shader variants. Compute calls insert memory dependencies before, between and after the calls;
no new queue or asynchronous resource upload is involved. Graphics calls require known read-only
shader resources: storage side effects need an explicit compatible render-pass dependency. The
resource-access decision is cached by pipeline identity. Render-pass barriers have restrictions
described in the [Vulkan command specification](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdPipelineBarrier.html).

The entire call plan is checked before GPU recording, including execution domain, compute limits,
index-buffer bounds and unsupported resource/layout work. A refused plan retains the application's
call and reports its reason. Resource binding changes still require the descriptor executor;
they must not silently execute a custom draw with the old bindings. Native automatic/stream-output
draws, explicit indirect-resource commands, and graphics storage-write hazards are not implemented
by this call-only step. `shader_overrides.executed` counts consumed native call plans.

The `native-calls` group contains GPU invocation-count and geometry cases, existing native compute
scenes, tracker, Vulkan validation and text checks. It is sandbox evidence only.
The call scenes pass 60/60, including indirect-count replay with a nonzero count-buffer offset and
a zero GPU count. Six selected regression steps pass: native compute 91, native resource queries
73, tracker 185, character package/replacement 115/377, validation 76 and text.

## Persistent native descriptor bindings (D-198/D-199)

`vk_binding` assignments can retain application buffers in Resource declarations and bind them
at later native calls. Ordinary, template, pushed and push-template descriptors share the same
executor. Pre/original/post order and subsequent calls observe the actual private descriptor
state; application rebinds supersede it. Recordings retain complete resource/allocation chains,
descriptor pools and reproduced compatible pipeline layouts until completion and legal reset.
Custom-resource stride queries use the original declaration's explicit stride, format size or 16.

Ordinary descriptors are snapshotted while recording. The [Vulkan descriptor-update contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkUpdateDescriptorSets.html)
invalidates recordings when ordinary descriptors are updated afterwards; this is not a reason
to refresh them at submission. Late buffer data writes remain valid. Exceptional binding flags
are refused. Compute calls add queue-local barriers. D-221 requires actual pre-pass dependency proof for
graphics application uniform references, retained across classic and dynamic pass boundaries.
Persistent bindings keep later-pass ordering after alias configuration changes. Inherited
secondaries require a parent-aware adapter; immutable file uploads keep publication semantics. Multiple application queues require cross-queue hazard tracking and are
refused explicitly. Application images need subresource layout tracking; file uploads and null
bindings follow separately. The native-bindings group covers GPU destinations, retained refs,
post persistence, caller replay, indirect calls, late data and descriptor variants.

## Native file buffers and null bindings (D-200)

Plain file-backed Buffer declarations can bind to native uniform and read-only storage descriptors.
The declaration's stride must fit the byte count and the shader's structured-buffer contract.
These resources share the existing device cache, budget, upload queue and completed timeline
publication. Changed files get a new revision on F10. D-219 adds writable compute file buffers
with independent pinned storage below. Transformed, pooled, initialized and typed texel-buffer
resources still need their respective adapters and are refused explicitly.
Null bindings use the device's enabled nullDescriptor feature and preserve sampler ownership.

A recording retains its uploaded allocation and completed publication signal, including through
secondary execution and module exchange. Its host submission issues a satisfied timeline wait on
each consuming queue. Read-only uploads may use the tracked graphics family's queues; application
references still need a single application queue until cross-queue hazards are represented.
A primary's reset releases secondary dependencies without resetting the secondary's own resources.
The alternating-queue fixture drains each presentation before changing queues; it does not claim
concurrent presentation coverage. Native-only and empty folder generations can exchange modules
without adding a placeholder character Mod.

### Native sampled file images (D-201, 2.3.1; sandbox only)

A filename Resource, with default type or Texture2D, can bind through an explicit native vk_binding alias. DDS/WIC decoding, color-space flags, shared upload budgets and F10 file revisions follow the existing loader. Uploaded 2D color views retain their allocation and publication through recorded commands and backend exchange. The shader must use a compatible float sampled view; D-209 extends arrays/cubes below; writable or depth-comparison consumers, incompatible explicit formats and missing shader-resource bind flags are reported.

Ordinary and pushed graphics bindings, null with an enabled device feature, and compute/secondary bindings have GPU coverage. Native-only shader twins now preserve retained push layouts. Depth=2 compiler uncertainty is compatible with color sampling when neither module uses depth-reference operations. These changes do not move arbitrary application image layouts or add mutable file textures. No real-game acceptance is implied.

### Completed GPU frame-analysis resources (D-202, 2.3.2; sandbox only)

The analysis directory now includes resources.jsonl, buffer-N.buf and image-N.bin. Every completed record names the command buffer/call, resource identity, binding, byte range and file; image records also give Vulkan format, extent, mip and layer. Bytes come from a distinct GPU staging copy completed through a host-owned timeline. A worker writes completed data, retaining the staging across command reset and backend exchange. The draw log is separate and can be published before resource files finish.

The current scope is new one-time, non-simultaneous recordings on one unprotected application graphics queue, with synchronization2 disabled. Compute snapshots follow the application call and its replacement plan; graphics snapshots are at classic pass end. Only transfer-source resources with retained allocation chains and supported creation/descriptor state are copied. Image descriptors must be statically accessed; known owned single-sample color attachments can use their final layout. Raw .bin images require the manifest's format/extent. This is not per-internal-draw capture or a screenshot.

Refusals appear in the manifest, with no fabricated bytes. Current exclusions include multiple queues (including the real game's current arrangement), stage-limited synchronization2 signals, replay, swapchain ownership, depth, image-descriptor arrays and unsupported external/extension resources. D-212 adds bounded mip/layer view exports below. Source usage flags are never changed. Budgets are 8 MiB per copy, 64 MiB/256 jobs per analysis, and 128 MiB retained staging.

### GPU geometry exports (D-203, 2.3.3; sandbox scope)

At classic pass end, vertex-N.buf and index-N.buf hold complete application IA buffer allocations,
copied once per resource/kind in that pass. The manifest's uses array preserves each direct draw's
recording command buffer, pipeline, binding offset, first/count, instance range and base vertex.
Vertex uses include slot, stride, input rate and attribute locations/formats/offsets; index uses give
the index element size. Only vertex slots used by the pipeline's attributes are exported. Indexed
draw counts are index counts, including in the associated vertex use; operation identifies that
interpretation. A non-indexed draw does not export an index buffer merely because one is bound.

These bytes are end-of-pass data for the original application's bindings, not per-draw snapshots or
the replacement engine's private mesh buffers. Secondary recording identities are retained in uses.
D-208 adds indirect calls below. D-223 captures core/EXT dynamic strides and topology, including
zero stride, null-stride/legacy rebinding and secondary snapshots. The manifest includes bound size.
Static pipelines use their own geometry. Capture completeness never authorizes pipeline cloning.
D-224 adds full dynamic vertex input with owned per-draw declarations, binding/format/offset,
instance rate and divisor, including independent later stride overrides. Unknown declarations and
slots beyond the four tracked slots remain refused. All D-202 ownership, usage, queue and completion restrictions still apply. Buffer copies
preserve every byte, including sizes not divisible by four; VkBufferCopy has no such alignment rule.

### Shader-independent output attachments (D-204, 2.3.4; sandbox scope)

Stored classic-pass output attachments are collected independently of shader descriptor reflection.
Clear-only passes, primary command buffers after secondary graphics execution, and failed descriptor
reflection do not remove independently known output attachments from analysis. A dedicated owned
4x4 fixture checks separate completed green/magenta clears, with missing transfer usage and discarded
stores excluded. All D-202 queue, source ownership, layout and completion boundaries remain.

The consolidated delivery/evidence audit is [native-offline-delivery-2.3.4.md](native-offline-delivery-2.3.4.md).

### Application image layout facts (D-205, 2.3.5)

Native application-image queries enable bounded recording-local image state. Facts are keyed by
image identity, aspect, mip and array layer; explicit barriers and event waits, classic subpass/final
transitions and executed secondary effects update them. Reset starts a new independent record.
Synchronization2 equal-layout barriers do not invent a new layout, even through the legacy API.
Framebuffer layer counts and the union of multiview masks bound implicit transitions; attachment
state outside the current subpass is conservatively unknown. Unknown ownership/extensions fail
closed. No global layout is inferred from CPU recording order.

This is the foundation for D-206 native application-image execution below; existing file-image
bindings keep their publication path. Tracker 252/252, live INI 221/221, native resources 89/89 and Vulkan validation
76/76 pass with the 2.3.5 host/module pair. The execution extension follows below.


### Retained application sampled images (D-206, 2.3.6; sandbox only)

Native references retain the application view/image/memory chain and use current recording-local
subresource layouts. Each descriptor keeps its desired image and original application fallback.
Every application call refreshes persistent requests, including shaders without an override;
changed descriptors are cloned and rebound. Application rebinding supersedes the request.

GENERAL and SHADER_READ_ONLY_OPTIMAL bind directly. Known transfer/color layouts may be temporarily
sampled outside render passes, then restored after compute or the pass. Transition cells are
deduplicated and their owners survive until recording completion. New in-pass transitions, active
attachment overlap and unknown scope/layout refuse or suspend the replacement. Original statically
used descriptors can establish a valid-application layout precondition, never a circular proof from
the native override. Allocation failures do not forward calls with stale descriptors.

The supported scope is one application graphics queue, synchronization2 disabled, retained optimal
non-depth typed sampled 2D, array and cube views with supported creation/usage state (D-210/D-216).
Cross-recording layout and queue/signal graphs, mutable file images and other view types remain
separate engineering.
Tests check actual distinct image values across GPU writes, temporary layout restoration, GENERAL
descriptor refresh, unmatched pipelines, application rebinding, push/secondary paths, rendered
pixels, F10 and code-module exchange. Exchanged ShaderFixes locate the validator through the owned GPU toolchain directory. No game acceptance or deployment is implied.

### Native compute storage images (D-216, 2.4.6; verification in CURRENT_STATE)

Retained application 2D and array color views can replace native compute storage images. The view
must have STORAGE usage, one view mip and the exact declared SPIR-V storage format and numeric
class. Original UAV GetResource/CSSetUnorderedAccessViews retains the view and persistent binding;
DX11 supplies deferred lifetime, hazard tracking and context order. Here descriptor clones and
recording owners retain the allocation, and explicit compute dependencies make writes visible.

Each cell uses GENERAL for storage access. Known layouts can transition outside a render pass
and are restored after the call, including persistent references used by unmatched pipelines.
Every native descriptor reserves its selected layout even when no transition is needed; a later
alias cannot invalidate an earlier descriptor. Application rebinding still replaces the request.
The same one-application-queue and synchronization2 boundaries apply. D-217 adds formatless
storage checks below. Graphics storage writes and mutable file images need further adapters;
D-219 covers writable file buffers below.

The storage fixture checks fenced GPU values for ordinary/push/secondary calls, explicit dispatch,
post restore, arrays, temporary layouts and original rebinding. Tracker cases cover conflicting
sampled/storage aliases. Vulkan rules: [storage images](https://docs.vulkan.org/spec/latest/chapters/descriptors.html)
and [format compatibility](https://docs.vulkan.org/spec/latest/chapters/interfaces.html).

### Native formatless storage images (D-217, 2.4.7; verification in CURRENT_STATE)

An Unknown SPIR-V image format can bind when the device's enabled read/write-without-format
features and that view format's optimal-tiling features cover its executable accesses. Queries
require Vulkan 1.3 or enabled KHR_format_feature_flags2 and are cached per device and format.
Read-only and write-only uses require their respective capabilities independently. Numeric class,
view shape, usage, retained ownership, queue and layout checks remain unchanged.

The bounded storage provenance analyzer uses a private copy with normalized descriptor identities;
the executable and its native set/binding interface stay intact. Returned access identities use
the original sets, including repeated binding numbers in different sets. Unknown provenance is
refused. A ShaderFix cannot add formatless reads or writes beyond the original executable: the
resource compatibility cache describes the original pipeline. Vulkan requires a declared format
for image atomics; this adapter does not treat Unknown-format atomics as valid.

GPU checks cover ordinary/push/secondary, arrays, post restore and core/extension query paths.
Tracker checks independently cover device features, per-format support, read/write separation,
numeric/typed format mismatches and ShaderFix access expansion. Rules:
[format features](https://docs.vulkan.org/spec/latest/chapters/formats.html) and
[shader image interfaces](https://docs.vulkan.org/spec/latest/chapters/interfaces.html).

### GPU indirect argument snapshots (D-208, 2.3.8; sandbox only)

Analysis exports indirect-N.buf and, when used, indirect-count-N.buf as full GPU allocations.
Their uses record command buffer/call, operation, argument/count resource IDs and offsets, stride,
command_bytes and max_draws. Indirect vertex/index uses carry the same association and omit unknown
first/count/instance values. Decode the GPU command at its recorded offset to interpret geometry;
max_draws is a recording-time limit, not a fabricated GPU count. Zero counts still retain arguments.

Graphics arguments are snapshots at classic pass end; compute arguments follow each indirect dispatch.
Changes made to arguments inside the pass cannot be reconstructed as earlier per-draw values.
All transfer-source, retained-owner, queue, synchronization and geometry recipe limits still apply.
The121 checks cover indexed/non-indexed/count/zero/secondary forms, dispatches and usage refusals.

### Native sampled file arrays and cubes (D-209, 2.3.9; sandbox only)

Native filename resources retain all DDS array slices or cube faces and selected mip levels.
2D-array, cube and cube-array views must match the shader consumer; float sampled color access
remains read-only. Cube arrays require the application's enabled imageCubeArray feature and all
shapes respect device limits. [Vulkan view requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html)
define the cube-compatible flag and feature requirement.

The cache distinguishes this mode from legacy character materials, which keep their first-layer
2D view. Shared allocation budgets, F10 file revisions, timeline publication and retained command
ownership still apply. Both upload staging paths retain the original DDS layer stride when lower
mips are selected. Standalone checks read every compressed mip/layer back from the GPU; native
fixtures sample each layer/mip with distinct values and cover push, secondary and F10 paths.

Descriptor clones preserve standalone immutable samplers by relying on allocation initialization;
combined samplers still receive image writes and push descriptors explicitly initialize samplers.
The current evidence and exact builds are in CURRENT_STATE; file-shape support does not extend the
application-image layout adapter or frame-analysis capture scope.

### Retained application array/cube views (D-210, 2.4.0; sandbox only)

Application resource references retain the exact view shape and its mip/layer range. Sampled 2D,
2D-array, cube and cube-array consumers must match; partial views preserve their base mip and layer.
Up to1024 view cells are checked individually, temporarily transitioned where needed, then restored.
Persistent bindings refresh after GPU writes and the application's own rebinding supersedes them.
The existing queue, synchronization2, ownership, attachment and unknown-layout restrictions remain.

The53 new checks read distinct values from every view cell, including partial ranges, push and
secondary commands, and verify shape mismatch refusal. Existing references76, file shapes33,
descriptor bindings48, GPU analysis55, tracker260 and Vulkan validation76 pass on the same pair.
This changes application binding support; frame-analysis image capture retains its narrower scope.

### Narrow DDS formats (D-211, 2.4.1; sandbox only)

Native and legacy file loading now accepts R8_UNORM, R16_UNORM and RGBA16_UNORM DDS, in addition
to the existing RGBA/BGRA and BC formats. Mip/layer copy starts use four-byte staging alignment,
as required on transfer-only queues by [Vulkan copy rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBufferToImage.html).
The file's bytes and original layer strides stay intact; padding is inserted in place in staging.
Both upload paths charge the padded allocation and retain the same completion/publication owners.
Legacy material views still select the first layer; native views preserve the complete file shape.

Headless checks compare every texel after both staging paths and mip demotion, with independent
readback packing. Native shaders cover narrow arrays/cubes, F10, push, secondary commands and two
application queues. These published read-only file uploads do not broaden the application-image
queue adapter or enable integer, depth or writable consumers. CURRENT_STATE records exact evidence.

### GPU color mip/layer captures (D-212, 2.4.2; sandbox only)

Each retained color view cell can produce a completed GPU file with its original mip/layer index,
format, extent and byte count. This covers arrays, cube faces and partial views; remaining-range
constants are normalized. Views exceeding256 cells refuse, and all existing allocation/frame/job,
queue, synchronization2, replay and transfer-usage constraints remain. Every copied cell's layout
is restored. Files describe view subresources, not an invented snapshot of the entire allocation.

Stored attachments export only referenced framebuffer layers or used multiview layers. Extra layers
exposed by a view are left untouched, including layers still in undefined layouts. The rules follow
[Vulkan render-pass transitions](https://docs.vulkan.org/spec/latest/chapters/renderpass.html).
Classic [multiview creation state](https://docs.vulkan.org/refpages/latest/refpages/source/VkRenderPassMultiviewCreateInfo.html)
is now retained and reconstructed with owned masks, dependency offsets and correlations. Unknown
chains and incompatible counts still refuse; application features are never silently enabled.

Image checks47 compare each GPU cell and metadata, including remaining ranges, budget refusal,
push/secondary paths and unsupported queues/usage. Target checks43 cover clear-only, prior graphics,
secondary, exact framebuffer/multiview layers, unused attachments and store/usage refusals. Pipeline
recipe/recreation and existing capture regressions pass; CURRENT_STATE records the exact binaries.

### Analysis resource selection (D-213, 2.4.3; sandbox only)

The installation's original `analyse_options` parser now selects native captures. `dump_rt` selects
output attachments and UAVs, `dump_tex` selects SRVs, `dump_cb` selects uniform buffers, and
`dump_vb`/`dump_ib` select geometry independently. Depth remains an explicit capture refusal.
SPIR-V readonly declarations distinguish storage-buffer SRVs from UAVs; an unknown classification
is reported. No resource flags, including an absent setting or `log`, means a draw log with no GPU
copies. F10 loads the new selection, frozen for each analysis session. Indirect argument snapshots
accompany selected captures as native call metadata. Clear-only passes do not export stale shader
descriptors from an earlier pass.

D-213 exports native raw files and reports unsupported requested encodings/control flags in
`resources.jsonl`. D-214 below adds the format encoders. D-215 adds hold/release below; clear and per-draw command-list options
remain separate work. Existing capture ownership, completion, queue and layout guards remain.

### Completed GPU export formats (D-214, 2.4.4; verification in CURRENT_STATE)

Canonical `.bin` files keep the completed GPU bytes. The same manifest record now has an `exports`
array for the requested tool formats, with independent file names and completion/refusal outcomes:

- `dds`: a typed DX10 DDS for each captured color cell, preserving its bytes and dimensions. The
  original resource mip/layer remains in the native manifest; the file itself is one 2D cell.
- `jpg`, `jpeg` or `jps`: mono JPEG through WIC for supported normalized color/grayscale formats.
  Alpha is omitted and JPEG is lossy. Typed integer or unsupported conversions are reported.
- `jpg_dds` and aliases, or no image format: JPEG first, lossless DDS when JPEG is unavailable.
- `buf`: an exact binary buffer span; `txt`: generic native words with byte offset, hex, unsigned
  integer and float interpretations. It does not invent DX11 input-layout semantic names.
- `desc`: JSON describing the native captured resource, span and use in a `.dsc` file.

Absent buffer formats default to text when CB/VB/IB categories are requested, following the original
format selection. Files are produced on the completion worker after the existing GPU synchronization;
encoders issue no Vulkan commands. Unsupported format mappings preserve the canonical raw data and
report the failed requested artifact. Control flags and per-draw options remain separate adapters.

The layout follows [Microsoft's DDS header definitions](https://github.com/microsoft/DirectXTK/blob/main/Src/DDS.h),
and JPEG uses [WIC frame encoding](https://learn.microsoft.com/en-us/windows/desktop/wic/-wic-imp-iwicbitmapframeencode).
Tests verify actual GPU bytes inside DDS, decoded JPEG colors, text words, descriptions and fallback,
plus existing capture and selection behavior. CURRENT_STATE records the exact verification result.

### Held analysis sessions (D-215, 2.4.5; verification in CURRENT_STATE)

`hold` keeps recording while the configured analysis key is down. The original key release callback
ends the session; without `hold` the existing single-presentation interval remains. Options are fixed
for that session. F10 closes and saves the old session with a configuration-change marker, so a
replaced input generation cannot leave it active indefinitely. An abort preserves the partial log.

Logs append bounded chunks per presentation, including frame and terminal markers. GPU metadata
records the presentation interval as `frame`; file IDs remain unique throughout the session.
The frame-byte budget resets each interval while the aggregate resident budget still includes all
retained in-flight jobs. Stopping recording does not discard copies already submitted to the GPU.
Status adds `analysis_frames` and `active_options`. Existing queue, one-time recording and layout
guards remain, and control flags other than hold remain explicit adapters to implement.

### Mutable native file buffers (D-219, 2.4.9; verification in CURRENT_STATE)

Plain Buffer files with inferred or explicit unordered-access bind flags can initialize native
compute storage buffers. Each declaration owns an independent allocation, even when path, usage
and file revision match another declaration. References to the same declaration share its GPU
contents. F10 creates fresh writable declaration storage; immutable files retain cache sharing.

Writable allocations stay charged and resident until their last declaration is forgotten. They
cannot be evicted and reconstructed from the original file, which would lose GPU changes.
Recorded descriptor owners retain allocations and completed upload publication independently of
frontend or module lifetime. Budget exhaustion leaves new loads pending rather than discarding
writable contents. File IO, allocation and upload remain on the existing worker path.

Exact byte/structure stride, descriptor usage/range, one application queue and compute scope remain
enforced. D-235 adds scoped direct-compute support with synchronization2 below. Dispatches with persistent mutable descriptors receive
explicit dependencies even without a new override plan; recording reset and secondary execution
preserve the dependency lifetime. D-222 adds writable file textures below. Transformed/pool/initialized resources and graphics
writable bindings require separate adapters. CPU dependency evidence, actual GPU results and
validation observations are distinguished in CURRENT_STATE; none is real-game acceptance.

### Mutable native file images (D-222, 2.5.2; verification in CURRENT_STATE)

DDS/WIC declarations with inferred or explicit unordered-access flags can initialize native compute
storage images. Equal-file declarations own independent allocations; references to one declaration
share its GPU writes. F10 recreates writable contents from the file. Every mip and layer remains
resident until the declaration is forgotten, while in-flight command owners can retain the image.

Uploads query actual storage capabilities and publish GENERAL. Sampled views cover the file's mip
chain; storage views address mip zero and preserve array layers, with cube faces exposed as a 2D
array for storage. Native typed and formatless access retains exact format, shape and enabled-feature
checks. Readonly views are never promoted to writable access. Private images remain outside the
application's layout tracker, and all their compute aliases use GENERAL.

Persistent bindings receive compute dependencies even without another override plan. One application
queue remains required; D-235 adds scoped direct-compute synchronization2 support. Attachment binding, graphics
writable access, transformed/pooled/initialized resources and cross-queue graphs need further adapters.
Sandbox results cover actual completed bytes and allocation/publication lifetime; no game acceptance
is implied. CURRENT_STATE distinguishes failures in the feature baseline from fixture/integration corrections.

### Native shader twins with dynamic geometry (D-225, 2.5.5)

Native ShaderFixes and hunting can keep core/EXT dynamic stride/topology and full dynamic vertex
input. The owned twin reproduces the exact dynamic declaration and the original static state;
full dynamic input has no invented static layout. Its creation mode forbids a vertex override.
Mesh replacement retains its earlier completeness and binding-state restrictions. Unknown
extended dynamic states still refuse. D-226 adds core1.3 raster/depth/stencil dynamics and
WITH_COUNT viewport/scissor. Dynamic discard retains required rasterization structures even when
its static placeholder is TRUE. D-227 adds primitive restart to native twins and exports its static/dynamic per-draw value
with captured geometry. Unhandled extensions remain explicit gaps.

The scoped GPU fixture proves replacement pixels, original restoration, F10 change/removal,
secondary recording, injection, module exchange and original/pink hunting. CURRENT_STATE separates
the failing baseline, matching build/ABI and exact verified area. No game verification is implied.

### Native output resource sources (D-228)

`o0` through `o7` and `oD` can supply native resource properties, property-based
TextureOverride matches and retained `ref` sources without a descriptor alias. Color locations map
through the current subpass; imageless and dynamic attachments use their runtime views. Resource
extents and view format remain distinct, including DSV-specific depth formats. Null slots preserve
the original missing-resource answers. A resolve destination is not the color source.

`ResourceSaved = ref o0` retains an owned view/image/allocation and can feed a later compatible
sampled descriptor after the application establishes its readable layout. Current attachment
conflicts and unsupported queues/synchronization2 still refuse. Swapchain views allow metadata
queries but cannot supply retained allocation ownership. Destroyed/reused objects and inherited
secondary attachment context refuse explicitly. This source adapter does not implement `oN = ...`
output rebinding. Actual owned-source/push/module-exchange sandbox pixels are covered by
`sandbox/native_target_test.py`; none of this is new game evidence.

D-229 also supplies native `rt_width`/`rt_height` from output0's full2D resource, following
original ProcessParamRTSize. A mip view does not shrink this answer; framebuffer/render-area sizes
are not substituted. No target or an array view answers -1 as the original operand does. Unknown
inherited context remains explicitly unavailable. `sandbox/native_target_size_test.py` checks actual
pixels and absence of operand degradation in classic/inject and changing owned/main passes.

D-230 makes late-bound imageless views authoritative for the pass's implicit layouts and native
attachment-conflict checks. Each pass receives immutable metadata; reusing a framebuffer with new
views cannot alter earlier facts or the device template. Missing or unsupported Begin metadata
remains unavailable. `sandbox/native_imageless_test.py` verifies actual later sampling, push
descriptors, injection and module exchange with core and synchronization validation.

### Dynamic-rendering native shader twins (D-231)

Native ShaderFixes can recreate dynamic-rendering graphics pipelines with exact retained color,
depth/stencil formats and view masks. The twin preserves original geometry and raster state; no
classic render pass is invented. Empty/unused attachments and ignored state follow Vulkan's
declarations. Unknown extensions and library pipelines remain unavailable. This native-only route
does not grant mesh replacement permission or implement output destination rebinding.

`sandbox/native_rendering_shader_test.py` verifies actual core/KHR entry points, geometry/raster
dynamics, secondary/inject, reload/removal/module exchange and original/pink pixels. CPU recipe
tests separately verify multiview and format-array ownership. No new game evidence is implied.

D-232 preserves native pre-pass dependency proof across same-recording suspended/resumed segments
without inserting forbidden boundary barriers. A fresh recording cannot inherit proof it did not
record. Temporary image layout changes across such segments remain unavailable until submission
restoration is modeled; already-readable resources and published immutable files keep existing
rules. CPU command-emission tests prove the boundary behavior; actual segment uniform pixels and
secondary fallback are checked by `sandbox/native_rendering_boundary_test.py`.

D-233 enables retained owned output sampling in ordinary dynamic-rendering passes once the
application establishes a readable layout. Exact color/depth/stencil and enabled resolve views
drive attachment conflict proof; null attachments and disabled resolves contribute no image.
Unknown chains, incomplete snapshots and segmented scopes remain guarded. Actual dynamic target
queries, sizes, later sampling, KHR push/module exchange and fallback paths are covered by
`sandbox/native_dynamic_target_test.py`; this does not implement output destination rebinding.

## Actual dynamic depth filters (D-234)

Dynamic-rendering ShaderFixes evaluate `depth_filter` against the actual depth/stencil views supplied to BeginRendering. Depth-test/write enable and the pipeline's allowed attachment formats do not determine presence. Pipelines bound before Begin, or reused with a different attachment context, are refreshed there; the native variant caches separate unknown, absent and present contexts. Inherited secondary recordings without actual parent views conservatively keep filtered stages original. Classic subpass filters and compute bypass retain their existing behavior. As in ProcessShaderOverride, the legacy filter does not suppress command lists.

The optional sandbox fixture enables `VK_EXT_dynamic_rendering_unused_attachments` for legal reuse of a D32 pipeline with a null view, with depth testing disabled. This checks runtime presence rather than declared compatibility; it grants no image-layout or application-allocation ownership permission. Native runtime output masks receive the same actual presence facts.

## Private compute resources with Submit2 (D-235)

Private mutable file buffers and images can be used by direct compute calls with synchronization2 enabled. Their complete allocation/publication owners stay retained, uploads are already completed when published, and all mutable accesses remain on one application queue. Image aliases use GENERAL throughout. Repeated command-list calls and persistent bindings receive compute-to-compute dependencies, including uniform reads of original application buffer aliases. The application keeps its own dependencies for unchanged descriptors; the layer adds no private-compute ordering at graphics Begin.

New application resource references and indirect-call replay still require separate resource/argument stage adapters in this mode. This does not enable general application image accesses, cross-queue hazards, output rebinding or analysis readback with synchronization2. Legacy synchronization retains its existing behavior.

The sandbox now submits actual core/KHR Submit2 work with a fragment-stage timeline wait and color-stage signal. GPU tests cover independent private declarations, persistent writes, push/secondary paths, F10/module exchange and the guarded cases. A storage/uniform alias test separately verifies three dependent dispatches return3: an intermediate missing UNIFORM_READ mask returned1 despite clean validation. The excluded host-release experiment also stalled without replacements and is not evidence of a layer deadlock or a supported future-wait graph.

## Resolved imageless frame analysis (D-236)

Frame analysis accepts the immutable actual-view instance resolved at BeginRenderPass. Unresolved imageless templates remain unavailable. Existing stored-attachment, owned allocation, transfer usage, format/sample, final-layout and exact layer/multiview checks still apply. The optional imageless sandbox suite verifies exported GPU bytes across clear-only, earlier graphics/secondary, discard/usage and layered/multiview cases; no game verification is implied.

## Dynamic-rendering frame analysis (D-237)

Ordinary dynamic pass ends now export actual stored outputs after driver EndRendering, preserving Begin layouts through explicit copy/restore. Clear-only passes and prior primary/secondary draw state are distinguished. Enabled resolve destinations are stored independently of the multisample source storeOp; the adapter captures supported single-sample color resolves and explicitly refuses unsupported source shapes. Actual active attachments are excluded from descriptor-layout capture. Exact view mip/layer and multiview ranges are retained.

Suspended/resumed or unknown attachment contexts cannot emit capture commands. Existing retained-allocation, transfer-source usage, format/layout, one-time recording, single-queue and synchronization2 restrictions remain. GPU tests verify actual green/magenta bytes, core/KHR calls, array/multiview cases, stored/discarded MSAA sources and clean segmented fallback. This is sandbox capture evidence, not real-game verification or output destination rebinding.

## Native dynamic color destinations (D-238)

An output assignment can redirect the current single color slot to a retained application image view with the same format and sufficient extent. This uses actual rendering attachments. Original stored contents and clear operations complete before an explicit dependency and LOAD continuation; introduced destination layouts are restored at application End. Pre/post list order and persistence through later draws are preserved. Exact current views also drive subsequent source queries and attachment conflicts.

The initial adapter requires an ordinary primary single-sample dynamic pass with STORE, no depth/stencil or resolve, one application queue and legacy synchronization. Query/transform-feedback boundaries, unknown layouts, overlapping image-view aliases, mixed descriptor/output plans and persistent native image bindings remain guarded. Null outputs, additional colors, depth, classic passes and broader queue/resolve scopes require separate adapters. No pass split is emitted until all events validate.

Sandbox evidence uses a full-size owned destination and independent GPU readbacks of both targets. It covers core/KHR/inject, one-time assignment persistence, post order, F10, module exchange and actual active-query refusal. The host completes layout restoration across an exchanged module; complete owners remain retained until recording completion. These are offline results.

Output splitting also requires a complete native pipeline attachment contract. Dynamic local-read attachment-location/input-index commands invalidate its proof for the recording, since a private Begin resets those mappings. Unknown static pipeline chains remain refused; the adapter does not silently replace an application mapping with identity.

## Actual null output destinations (D-239)

Within the ordinary single-color scope, `o0 = null` starts a continuation with an actual null color view when the application enabled `dynamicRenderingUnusedAttachments`. The original color-format contract survives unbinding, so a later retained destination can be restored with exact pipeline compatibility. Introduced targets restore their layout when unbound; null views own no image and contribute no attachment conflict. The layer enables no device feature on behalf of the application.

The original `BuiltInCommandListUnbindAllRenderTargets` also executes here: the complete single-color/no-depth context proves that its other color slots and depth are already absent. Unsupported nonnull slots still refuse before any command records. Actual GPU readbacks cover core/KHR/inject, repeated/once/post unbinding, null then restore, the original built-in and feature-disabled fallback. No real-game verification is implied.

## Proven absent outputs (D-240)

Null assignments to outputs already proven absent emit no rendering, ownership, layout or dependency work. This lets the original all-output unbind run in compute lists, including non-inherited secondary recordings and supported Submit2 calls. Complete classic unused references and dynamic null slots supply the same proof. Unknown/inherited attachment contexts, unmodeled mappings and external-format null-source resolves remain unavailable.

The entire plan is checked for nonnull writes before removing any null event. An initially null slot that is bound and then unbound keeps both ordered changes. Proven no-ops can accompany descriptor work; actual output changes retain the pass and alias restrictions. GPU coverage verifies three dependent dispatches produce3 after the built-in unbind, plus core/KHR/inject/push/secondary and legacy descriptor assignments.

## Preserved depth during color changes (D-241)

Ordinary single-color output changes can preserve a known stored single-sample depth attachment without depth resolve. Every private continuation keeps its exact view/layout and uses LOAD, including after null color assignments. The complete pipeline depth format must match the actual attachment; a declared but absent depth format needs the application's enabled unused-attachments feature. The unchanged depth image remains in attachment-conflict checks, and a new color target cannot alias it.

Active depth rebinding, stencil, discarded depth and unsupported pass/queue scopes remain guarded. The optional depth fixture writes actual D32 values and reads them back after completion. Repeated post-list null/restore boundaries must preserve all earlier depth geometry; color readbacks independently verify the destination and command order.

## Multiple color destinations (D-242)

Supported ordinary dynamic passes keep independent original/current state and exact pipeline format contracts for each color slot. Changing one slot preserves every other stored color and depth attachment through LOAD continuations. The host restores all introduced active images to their distinct earlier layouts at the application's End. Initially null slots retain declared format proof without an invented original image; the complete pipeline must explicitly use single-sample rasterization.

Every event validates against the ordered full context before any GPU command records. A target cannot overlap another active color or depth image; alternate views of original or saved images remain guarded. Real depth/stencil changes, resolve/classic passes and broader synchronization scopes still need adapters. The two-output fixture reads the screen and two owned images independently, covering each slot, null/restore/post order, injection/KHR and alias refusal. CPU checks separately verify simultaneous destinations and exact host layout restoration.

## Preserved stencil during color changes (D-243)

Ordinary dynamic color-output changes preserve stored single-sample stencil attachments without resolve, including stencil-only S8 and combined depth/stencil. Every actual preserved aspect uses LOAD on continuation and retains its exact pipeline format, view/layout and conflict identity. When both depth and stencil attachment pointers exist they must share the same view. Color destinations cannot alias either aspect. Actual depth/stencil rebinding still requires its own adapter.

The optional stencil fixture checks complete fenced stencil bytes and exact geometry, alongside separate color outputs. Combined depth/stencil barriers transition both aspects together; no separate-layout feature is assumed. These are sandbox results, not real-game acceptance.

## Actual null depth/stencil outputs (D-244)

`oD = null` removes both actual depth and stencil views in supported ordinary dynamic passes, including the original built-in all-output unbind. Remaining colors continue with LOAD; their introduced-resource ownership and exact host restoration stay active. Repeated null entries are ordered no-ops after the first change. Nonnull depth/stencil rebinding and unsupported pass/queue contexts remain guarded.

The native ShaderFixes depth filter follows ProcessShaderOverride: it tests actual DSV presence before this draw's command list. A list can unbind depth for the draw while that draw retains its earlier shader choice. Subsequent application draws refresh from the new actual output context even without another pipeline bind.

## Zero-color dynamic outputs (D-245)

Ordinary depth/stencil passes may have zero color attachments. Native DSV null and built-in output unbind keep the actual zero count through preflight, continuation and host End. They retain the same exact pipeline, feature, sample, query and queue requirements as color-bearing passes. No synthetic color attachment is introduced.

When an originally null color slot receives an actual image, the executor explicitly sets no-resolve LOAD/STORE semantics and clears the previously ignored resolve handle/layout/metadata. A valid null attachment's ignored fields must not become a new resolve operation.

## Retained depth/stencil destinations (D-246)

Supported ordinary dynamic passes can bind a retained application depth/stencil view through `oD`, restore the original view, and redirect again. Introduced DSVs keep their owners until completion and return to their exact earlier layouts when replaced, nulled or finished at the application's End. Original and introduced color state remains independent. Zero-color passes retain the same DSV state across multiple plans.

Each destination must satisfy exact declared depth/stencil formats, sample count, extent, subresource and alias contracts. Combined images require one proven layout across both format aspects and transition both together, even when the view mask names only depth. Nonnull assignments add missing roles only from defined pipeline formats; adding a role forces an actual continuation even if the view handle matches. Every new actual attachment explicitly disables previously ignored resolve state.

Independent original/source aspect readbacks verify pre/post binding order and restoration. A completed sandbox module exchange checks the shared state boundary. Classic/resolve/segmented passes, broader queue graphs and persistent-image conflicts retain their explicit guards.

## Classic output destinations (D-247)

An ordinary primary classic pass with one stored single-sample color attachment can redirect that output to a retained same-format application view. RenderPass2 and resolved imageless attachments use the same exact contract. Preparation validates the whole ordered list before commands, then creates compatible LOAD passes and framebuffers. Published metadata remains classic, including the actual framebuffer and pass recipe used by source queries and pass-end analysis.

The first split accounts for the application's implicit final transition; private continuations use explicit dependencies and color-optimal initial/final layouts. Switching away restores introduced views to their saved layouts, while the original returns to its declared final layout. Returning to the original transitions it back before LOAD, and host End restores its final layout again. A bounded per-backend cache revalidates live identities; command owners retain cached or evicted objects and their view resources until completion, including across module exchange. Null/depth/MRT/resolve/segmented passes and broader queue or persistent-image contexts retain guards pending their own adapters.

## Stored classic attachment combinations (D-248)

Ordinary one-subpass classic continuations retain all stored single-sample color and depth/stencil attachments. Color slots map through unique subpass attachment references; combined depth/stencil moves both aspects together. The first private End performs all original final transitions, so preserved attachments also return from their original final layouts before LOAD. Later changes restore outgoing views and transition incoming views without changing other active layouts. Host End restores every current attachment, including preserved originals.

Cached framebuffers revalidate and retain every view, not only the changed destination. Preserving the application's original image stays within its existing acquired/render-pass scope; it does not make a swapchain reference transferable or remove the allocation-owner requirement for new resource bindings. Null, resolve, segmented and broader queue scopes keep separate guards.

Original classic layout restoration follows image identity across all original attachments. Moving an original into another vacated compatible slot does not replace its own implicit final-layout contract with the new slot's contract (BUG-111).

## Exact selected stages and classic null outputs (D-249 / D-250)

Application-geometry twins retain the final selected stage bytes and compatible layout metadata. Classic null-output continuations rebuild those exact stages against the actual private pass; shader replacement, hunting and depth filtering are not reevaluated during this adaptation. Filtering refreshes on subsequent application draws from actual output presence.

Supported stored single-sample classic passes can remove color or depth/stencil outputs, including MRT, combined aspects and all-output unbind. Private passes omit unused attachment descriptions and compact active references, because even unused descriptions would retain implicit layout transitions. Empty framebuffers use the original valid render area. Returning outputs and host End restore image layouts by identity, and host End restores the selected source pipeline.

All preparation happens before the first End. Later binds also require a compatible pipeline; failed or unknown selections block the affected draw. Bounded caches retain immutable recipes and live views, and recording owners retain evicted objects until completion. Complete shared cache object layouts participate in the module ABI, including pipeline-variant metadata; an incompatible module is refused before exchange.

Sandbox coverage includes independent original/source colors and aspects, shader replacement, hunting, following draws without another list, RenderPass2, imageless attachments, injection and completed module exchange. These results do not establish real-game acceptance. Mod mesh and image-interface variants in masked classic passes remain explicitly refused pending the next compatible-interface adapter. Resolve, segmented and broader queue contexts retain their separate guards.

## Mod interfaces in classic null passes (D-251)

Merged-skeleton, texture-only and own-mesh variants can draw in the supported masked classic passes. Every successful graphics variant freezes its exact final VS/FS words, actual vertex input and full private descriptor interface. Compatible pass pipelines recreate that interface while the draw transaction keeps the original variant's layout, private set index, image/bone descriptors and geometry. Preparation finishes before draw commands; recording owners retain cached or evicted pipelines through completion. Application shader selection and the final pipeline/geometry restoration remain separate.

Independent sandbox readbacks verify moved geometry, native replacement and hunting shaders, and completed module exchange. A second scene verifies merged bone movement and texture-only Mods through classic and RenderPass2/injection. Output commands inside a matched Mod plan remain a separate executor limitation; the tests establish the output state before those plans. Private translucent captures retain their own passes, while their compositor still requires actual color0 and depth; missing-target readiness and actual depth-filter selection for Mod variants remain follow-up work. None of these sandbox checks establishes real-game acceptance.

## Actual depth filtering in Mod draws (D-252)

Mod variants snapshot actual output depth presence before the current command lists, matching ProcessShaderOverride. Own-mesh, texture-only and merged-skeleton shader selection inherit that snapshot, including the merged fast cache key. A list changing outputs does not retroactively select another shader for the same draw; following draws observe the new state. Nested draws restore the enclosing selection and compute bypasses the graphics depth filter.

Independent GPU readbacks verify both active/inactive filters on a Mod triangle moved away from its original location, with the application's current and following draws checked separately. These are sandbox results. Missing-target transparency and output operations inside matched Mod plans remain separate work.
