# Texture-only replacement of game draws (candidate 1.5.9, D-083)

Some Mod sections keep the game's mesh and only change its textures: a TextureOverride matched by
an index hash that binds `ps-tN = Resource...` (or runs RabbitFX's SetTextures) and neither skips
the draw nor draws anything itself. Under DX11 the game's draw then runs with those textures bound.
Until 1.5.8 the layer left such draws to the game unchanged: in the offline dry run of the installed
Mods 13 sections of six Mods (Liino, akekuri, arclight, avywenna, rossiy, zhuangfangyi) planned this
way and had no effect.

## How it works

- The planner records how many plan items the pre phase made (`DrawPlan::pre_items`, carried to
  `FilePlan`): those are bound when the original draw runs; post commands come after it.
- `CharacterProgram::plan` with the new `textures` output: a plan that leaves the application's draw,
  binds textures in its pre phase and does nothing else (no buffers, no draws) becomes a list of one
  texture-only draw (`CharacterDraw::texture_only`) carrying those pixel bindings. It goes through
  the same material path as the Mod's own draws: slots, rules from the fragment module (D-076),
  images published per batch (D-081), RabbitFX roles (D-082).
- The backend draws the application's draw again, in its place: its own vertex and index buffers and
  draw arguments (instanced draws included), through a pipeline variant that keeps the application's
  vertex stage (`FragmentPatch::keep_vertex`) and only re-points the fragment images to the private
  set. The private set comes from the same arenas; `DrawSnapshots::append_images` writes its images
  and entry without copying bones (nothing reads them). An application set at the private slot is
  bound again afterwards, as for the Mod's draws. Status counter `textured`.
- Nothing the module can take (no image, no null) leaves the draw as it is; a slot the module cannot
  take refuses only that draw (BUG-030's rule), and the game draws it.

## Checked

- GPU sandbox (pipeline variant scenes, both entries, both render pass versions): the application's
  draw made again through the texture-only variant at a second instance entry shows the private
  green image where the application's own draw shows its blue one; a texture-only variant without
  images is refused; a texture-only arena entry copies no bones.
- Offline dry run of the installed Mods: 4 sections with fixed slots plan as texture-only (arclight,
  zhuangfangyi); the RabbitFX ones need the draw's textures and are seen in the replay.
- Capture replay, 360 frames: texture-only draws in Liino 1694, rossiy 2365, arclight 298,
  zhuangfangyi 1184, without validation messages; every pass with a component draw still shows the
  Mod. More variants compile in the first frames (the new slots make every shader derive again).
  The replay's game meshes have no vertices, so the pictures cannot show these draws.

Not covered: the game itself. A section that also binds vertex or index buffers without drawing (a
geometry change of the game's draw) stays the game's.
