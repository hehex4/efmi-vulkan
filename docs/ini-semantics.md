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
The same on copies of the user's ten disabled Mods (without their DISABLED_ prefix, with the installed
libraries; 2026-10-28, after D-139): 179 key actions, 2398 states, every section plans.

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

## Sections matched by a resource's description (D-138)

A TextureOverride without `hash` but with match_* keys (match_type, match_width, match_height,
match_format, match_byte_width, match_bind_flags, match_usage, match_mips, match_msaa ...) is a
FuzzyMatchResourceDesc of the original (IniHandler.cpp parse_texture_override_fuzzy_match): it matches
every resource whose D3D11 description meets its predicates, `match_usage` DEFAULT unless given, the
types its keys can apply to (update_types_matched), and its draw-context keys. The original looks them
up wherever it looks up a resource's sections (ResourceHash.cpp find_texture_overrides_for_resource:
the whole-resource hash's sections, then these, each filtered by the draw): a CheckTextureOverride of a
pixel slot (EFMI's Core runs one on ps-t0 to ps-t22 in a Mod's draws), `CheckTextureOverride = ib` (the
Core's ShaderRegexEnableTextureOverrides, every vs_4/vs_5 draw of the game: the region hash's sections,
then the index buffer's description's), and when the game creates a resource (HackerDevice
process_texture_override: format, width, height, their multipliers). The original's own resources
(CustomResource, the back buffer) have no handle info and meet none.

The layer, before: any such section refused the whole Mod ("Fuzzy/image overrides require a
source-image identity provider"). Now:

- A section that can match a 2D texture runs at a CheckTextureOverride of a pixel slot, against the
  description of the game's image the layer already makes for its texture hash (D-077: Vulkan format to
  DXGI, usage DEFAULT, bind flags from the image's usage, TEXTURECUBE for cube-compatible images; the
  hash from it matched DX11's on 345 of 347 textures, the two others a cube map and a mip chain the
  game uploads shorter under Vulkan). The original's matcher and order (find_texture_overrides_for_desc
  after the hash's sections) run unchanged in the frontend (`described_resource_matches`). An image
  the layer cannot describe (1D, 3D, a format without a DXGI counterpart) meets none. A Mod's own
  resource meets none, as in the original.
- A section that can match a buffer would also run at the game's draws by their index buffer's
  description. The layer finds the game's draws by their index region's hash (D-097) and has no D3D11
  description of the game's buffers (Unity's Vulkan buffers are not shown to be the DX11 build's): it
  matches none of them. One for 1D or 3D textures likewise.
- A section that changes how the game creates, maps or copies a resource (format, width, height,
  width_multiply, height_multiply, a buffer size with allow_buffer_resize, deny_cpu_read,
  expand_region_copy), matched by hash or description: the layer leaves the game's resources as the game
  made them.

The last two are the layer's gaps: the section is inert, the rest of the Mod loads, and the Mod is told
(CharacterDiscovery::gaps, the original's warning in the log, the first three per Mod as overlay notices
naming the section). No installed Mod has such a section (census 2026-10-28: no match_* key and no
creation-time key in any enabled TextureOverride). Checks: texture_query_test (the original's parse of
a Texture2D and a Buffer section; a described texture meets the Texture2D one after its hash sections,
pre lists before post lists; another description, an undescribed texture and the generation's own
resource meet none) and check_mod_degradation's two variants (a section by a texture's description
loads and plans in every state; one by a buffer's description and a creation-time key load and are
reported), both refused before.

## Image files (D-139)

The original (CustomResource::LoadFromFile) takes a file named .dds with DirectXTK's DDS loader and every
other file with its WIC loader (CreateWICTextureFromFileEx: frame 0, its WIC-to-DXGI table, the sRGB rule
of the container: PNG's sRGB chunk, another's System.Image.ColorSpace 1; one mip). A file either loader
makes nothing of leaves the resource null, with an overlay warning, and every assignment of it binds
nothing; the same for a buffer file it cannot open or read (LoadBufferFromFile).

The layer, before: a replaced draw that bound anything but a .dds refused ("Pixel file binding requires
an unmodified DDS image", the game's mesh drawn); a texture file present but broken refused the draw when
its load failed (C_IMAGE, the game's mesh) and refused an F10 candidate (LIMIT-043); owned work (Present,
CustomShader) took PNG only and refused the Present for any file it could not load. Now:

- `assets::decode_wic` is the PNG decoder made general: any WIC container, frame 0, the same table, the
  container's sRGB rule. Owned work and the replaced draws' loader (ResourceCache, both its own staging
  and the staging arena) take every file not named .dds through it.
- The character program checks at load whether each texture file loads (CharacterProgram image_loads:
  a .dds by its header, another by WIC without decoding the pixels, within the loader's 128 MiB). One
  that does not is the original's null resource: what binds it binds nothing, as for a file not there
  (BUG-076), and the Mod is told (its gaps: log, notice). Such a file is never declared to the residency,
  so it no longer refuses an F10 candidate.
- Owned work: a texture or buffer file the loader makes nothing of is null with the original's warning;
  the Present goes on.

Checks: resource_png_cases (BMP, JPEG, JPEG with forced sRGB and TIFF fixtures, written by WIC's own
encoder, decode to the bytes and format the pinned DirectXTK loader gives on WARP; a broken file fails
there and binds nothing in the owned Present, which goes on), resource_image_ini_cases (a missing PNG and
a truncated DDS bind nothing, the Present goes on; were refusals), asset_gpu wic_tests (a PNG through
both of the loader's paths arrives as decoded; a broken file fails), check_mod_degradation (a bound
texture as a PNG: the Mod's sections refused before, plan now; a broken DDS is reported and binds
nothing), and a vk04 replay of SiModGB's copy with one texture a PNG and one a broken DDS (every matched
draw planned, validation clean). The live INI scene's F10 with its texture file broken was refused
before (the candidate dropped, LIMIT-043); now it goes through as the original's, the pixel reads
the null texture's zero, and an F10 after the file is repaired draws it again; a prepared package's own
images are checked the same way and one that does not load is never declared.

## A texture declaration's keys (D-140)

What the original does with a [Resource] that a replaced draw binds at a pixel slot and that names a file:
LoadFromFile passes the declaration's bind and misc flags and its colour space (the DDS loader's
forceSRGB for `color_space = sRGB`; WIC's FORCE_SRGB or IGNORE_SRGB) and nothing of its description:
width, height, mips, array, msaa, the multipliers and the stride do nothing to a texture DirectXTK makes
from a file. The binding's view takes the declaration's format (CreateCompatibleView: OverrideOutOfBandInfo,
FillOutTex2DDesc): a typed texture's view must have its format, or CreateShaderResourceView fails and the
slot binds nothing; without a format the view is the texture's own. Bind flags without shader_resource
give no view either.

The layer refused every one of these keys ("Pixel file binding requires an unmodified image", the game's
mesh drawn). Now the plan carries the colour space as loader flags, the view format and whether the view
can be made (FileResource); an image is keyed by its file and its loader flags, so a file declared in two
colour spaces is two textures, as in the original; the loaders apply the flags (DDS: the sRGB twin of its
format, DirectXTK's MakeSRGB; WIC: the decoder's flags); the size keys are ignored; and a view format the
loaded texture does not have binds nothing.

Checks: check_mod_degradation's two variants (a bound texture with `color_space = sRGB` and `width = 64`;
one with `format = R32_FLOAT`), 6 sections refused each on the build before, planned now; asset_gpu (one
BC7 file declared plain and sRGB loads as two textures, BC7_UNORM and BC7_SRGB, the blocks as the file
holds them); resource_plan_test (an sRGB declaration was a refusal case; it reaches the loader as its flag).

## The character contract's sizes (D-141)

The original Core has no limit of its own on a character: the Mod sets `$component_count` and
`$bones_count`, and its pools and the merged skeleton are sized by them (MergedSkeleton.ini, the Mod's
[Pool_MergedSkeleton_Component_*] `pool_size = $component_count`). The layer refused a Mod with more than
32 components (a 32-bit mask per stream and per list), 256 mesh sections, 256 mesh files, or (in a
prepared package) 4096 bones. Now a component is a bit of a 64-bit mask (the stream's, the list's, the
identity's components met), and the bounds are the widths of the layer's own indices: 64 components,
65535 bones (16-bit indices after the blend-index edit; the merged skeleton takes 65536), 4096 mesh
sections and files, 4096 Mods in a folder generation. The installed and disabled Mods reach 17 components
and 865 bones. Check: character_test (a package of 40 components loads and its list requires the 40th's
bones; refused on the build before). LOD 2 and 3 sections stay the game's: the user, 2026-10-28, does not
want them taken.

## The draw commands of a replaced draw (D-142)

The original's DrawCommand::run issues DrawIndexed, DrawIndexedInstanced, Draw and DrawInstanced with
their arguments; `draw = from_caller` the matched draw's own call again (an indexed or a non-indexed one,
instanced or not); `draw = auto` Draw(vb0's whole elements from its offset, 0); `drawindexed = auto`
DrawIndexed(the index buffer's whole elements, 0, 0); `drawindexedinstanced = auto` that with the caller's
instance count and first instance; and a count it cannot determine draws nothing. The layer planned
DrawIndexed and (planned, then refused) DrawIndexedInstanced only.

Now the plan carries the instance count and first instance, whether the draw is indexed, and whether
its count is the bound buffer's; the program takes them into the draw (a non-indexed draw without the
index stream, its vertices from `first`; an automatic count from the bound stream's whole elements of
the file, an owned buffer's from its size) and the emitter draws a non-indexed one with vkCmdDraw.
dispatch, the indirect draws and drawauto inside a replaced draw are what remains of DrawCommand (the
first runs the game's last compute shader, which a render pass cannot; the others read their arguments
from GPU buffers).

Checks: check_mod_degradation's four variants (a section's first draw as two instances, `draw = 3, 0`,
`draw = from_caller`, `drawindexed = auto`), each refused on the build before, planned now; a vk04 replay
of SiModGB's copy with three draws made two instances, non-indexed and automatic (validation clean, every
matched draw planned).

## Slot ranges in a replaced draw (D-143)

`ps-t[a:b] = X` binds every slot of the range: to none, to one resource every slot takes, or to the
elements of a pool range of the same size (another size: a warning and nothing); `unless_null` keeps a
slot whose source is none; `Pool[a:b] = ref ps-t[c:d]` fetches the slots into the pool's elements
(SlotRangeCopyOperation::run, RunBind, RunFetch). The original runs a single slot operation per slot for
anything but a plain reference (SlotOp). The plan refused every range. Now it resolves the range with the
original's own code (FrontendSlotRange beside the pinned CommandList.cpp: ResolveRange, pool_element) and
runs one ResourceCopyOperation per slot through the plan, as `ps-tN = ...` goes. A pool element nothing
assigned or wrote, of an untyped pool, binds nothing, as the original's has no resource.

Checks: check_mod_degradation's three range variants (to none, to one texture, from a pool range), refused
on the build before ("Unsupported frontend operation"), planned now.

## CheckTextureOverride of other targets in a replaced draw (D-144)

The original looks up a resource's sections wherever the target is (find_texture_overrides_for_resource,
the region path for an index or vertex buffer). The plan answered pixel slots only and refused any other
target. Now: a Mod's own resource (a declaration, or one the plan bound to the slot) has no handle info and
meets nothing, as in the original; an empty slot is not bound; the matched draw's own index buffer meets
the sections of the region hash the draw was matched by (region_hash_matches: the original's region path
with track_region_hashes, else the hash map the generation matched the draw through);
the game's vertex buffers and the other stages' resources have no DX11 identity the layer knows (their
Vulkan buffers are not shown to be the DX11 build's), so they meet nothing and the original's warning
channel says so once per target.

Checks: check_mod_degradation's two variants (`checktextureoverride = ib` and `= vb0` before a section's
first draw), refused on the build before ("Texture query requires an adapted single pixel slot"),
planned now.

## A view D3D11 does not make, in owned work (D-145)

CreateCompatibleView returns null where D3D11 makes no view: a typed texture under another format, a
typeless one without a format (image_source_reference checks both on WARP); SetResource then binds the
null view and the list goes on. Owned work (Present, CustomShader) refused the whole Present there. Now
the slot binds nothing. Checks: resource_image_ini_cases's three such cases were refusals (the test
asserted it, passing on the build before); they bind nothing now and the Present goes on.

## Commands a replaced draw does not issue (D-146)

`dispatch` runs the game's last compute shader in the original, which a Vulkan render pass cannot hold;
`drawauto` draws stream output, which the layer does not make; the indirect draws read their arguments
from a GPU buffer, which a replaced draw's plan does not take. Each refused the whole draw; now the
command does nothing, the original's warning channel says so once, and the draw's other commands go on.
Check: check_mod_degradation's `dispatch = 1, 1, 1` variant (refused before, planned now).

## store and clear in a replaced draw (D-147)

The original's `store = $v, Resource, i` maps the resource at once (a staging copy and a Map that waits
for the GPU) and the value is there for the list's next command; `clear` clears a resource's view. In a
replaced draw the layer's owned work is recorded while the game records its own command buffer and is
submitted before the game's submission: nothing of it has run when the list goes on, and waiting for the
GPU on the game's recording thread is what froze the game before (BUG-062). So, as the user prefers no
freezes to the exact frame: a store there copies its value into a host buffer with that submission and
takes, at a later execution of the same store, the value of the newest one completed (the value of a
frame or so before); before the first, the variable keeps its value, as the original's does for a null
source. `clear` of the Mod's own resources runs as owned work there, as its copies do. Both reach only
the Mod's resources (the game's are not owned work's). A generation without the owned GPU runtime
(offline) keeps the variable and clears nothing, with the warning once.

Check: live INI rendering's fixture Mod stores `ResourceStoreSource` (data 7) in its sections: refused
on the build before (the whole character stayed the game's), `$stored` is 7 now in every scene.

## Draws past their files (D-148)

D3D11's input assembler reads index 0 past an index buffer and zeros past a vertex buffer. The layer
refused a list whose draw reached past its index file, or whose index file (all of it) reached past a
vertex file. Now a draw keeps its whole triangles inside the index file (the ones past it are one vertex
three times in the original and show nothing; only a triangle across the end differs); a draw's own index
range is bounded by per-64-index block extrema and read exactly from the file only when that bound reaches
past a vertex stream (no false refusal for another draw's indices); a draw that does read past a vertex
file shows vertices of zeros in the original, which the layer leaves out, the list's other draws going on,
logged once.

Checks: check_mod_degradation's variants (a draw's count far past its index file; a position file cut in
half), refused before, planned now; a vk04 replay of SiModGB's copy changed that way (validation clean,
the draw reading past its vertex file left out).

## DDS arrays and cubes in a replaced draw (D-149)

DirectXTK's DDS loader makes an array or a cube texture of such a file. The layer can re-point only a
fragment shader's nonarray 2D images to the Mod's (fragment_images.cpp), and D3D11 leaves undefined what
a 2D slot reads from a view of another dimension (drivers commonly read the first slice). The layer's
loader refused these files (the slot bound nothing since D-139). Now a DDS array or cube (every face; DX10
arrays of either) loads as its first layer, the levels the 2D slot takes; a volume still does not load.
Check: asset_gpu (a two-slice array and a cube, on the worker's own staging and through the arena, load
and read back as their first layer; failing before, 297/297 now).

## Sections matching the game's buffers by description (finding, 2026-10-28)

The DX11 catalogue capture (char01) shows the game's index data in shared pools: five 16 MiB DEFAULT
buffers bound as vertex and index buffers (and one 4 MiB DYNAMIC); a draw's index and vertex offsets are
the same under Vulkan (draw 130: index offset 44128, vertex offsets 13664 and 35424 in both), so Unity's
pooling is the API's. A section matching buffers by description (match_type = Buffer, match_byte_width
...) therefore meets nearly every draw of the game in the original, and the layer would have to run the
INI generation on every draw to follow it. The Vulkan captures do not record buffer sizes, so the pools'
descriptions are not shown equal. These sections stay inert and reported per Mod (D-138); no installed or
disabled Mod has one.

## The original as the judge (D-150)

D-137's step 2: small INIs run under the original itself and under the layer, and what they leave is
compared. scripts/check_dx11_judge.py (a run_all step) extracts the pinned XXMI 3DMigoto (d3d11.dll
1.2.2, the version of the user's EFMI installation, from F:\vulkan\XXMI-PACKAGE-v1.2.2.zip) into
build/dx11-judge/xxmi with a d3dx.ini of its own whose [Loader] names the judge's D3D11 program
(sandbox/dx11_judge.cpp: a hidden window, a swap chain made through the original, frames presented); the
user's installation is never touched. Each case of sandbox/judge_cases runs its commands in [Present]
and puts results in `global persist` variables, which the original saves to d3dx_user.ini (an assignment
must mark them: a store alone does not); the same case runs under the owned GPU runtime
(resource_gpu.exe --present-ini) and its variables are compared.

Cases: expressions; buffers (copy, reference, Region copy, store in and past range, ->size); compute (a
CustomShader dispatch writing a RWBuffer, store; a clear of a buffer without the UAV bind flag); queries
(->width/height/mips/array of a Texture2D declared without `array` and of one with it, bb->width, a
structured buffer's stride, `=== null`).

What it found, both refusals of the layer where the original degrades, both fixed:
- A Texture2D declared without `array` (and other descriptions D3D11 does not create): the original's
  SubstantiateTexture leaves the resource null with a notice, queries give -2 (RESOURCE_NOT_FOUND); the
  layer refused the Present. Now the resource is null and the list goes on (texture and buffer
  substantiation alike).
- A clear of a resource without a view of the cleared kind (a buffer without the UAV bind flag, an image
  without the view's bind flag): the original makes no view and clears nothing; the layer refused. Now
  nothing is cleared, and a view of a kind the bind flags leave out is the original's null view.
resource_image_ini_cases's six cases that asserted those refusals now check the null resource.

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
