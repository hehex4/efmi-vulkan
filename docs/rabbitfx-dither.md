# RabbitFX's anti-dither under Vulkan (D-133, LIMIT-036)

## What the original does

RabbitFX (`Mods/RabbitFX -ENDMI-/RabbitFX.ini`, 2.8) is a ShaderRegex library. 3DMigoto disassembles
every pixel shader the game creates, applies each `[ShaderRegex*]` section's pattern and replacement,
assembles the result and uses it in place of the game's shader; a section's command list runs like a
ShaderOverride's whenever such a shader draws.

The game's character pixel shaders fade the character out when the camera is close (and in a dash)
by a dither: interleaved gradient noise of the pixel position (`dp2` of the position halved and
floored with (0.067111, 0.005837), `frc`, times 52.982918, `frc`) compared with the instance's fade
value in `cb2[instance + 4]`, then `discard`. RabbitFX's main, plain shadow, Maegami shadow and
LOD-skin shadow patterns capture that discard (`${discard}`) and put it back inside

    ld_indexable(texture1d) ini.xyzw, l(172, 0), t120.xyzw   ; IniParams[172]
    if_z ini.w
        discard ...
    endif

and the main, LOD shadow and LOD-skin shadow sections assign `w172 = $censor`, a persistent toggle
on the `\` key (KeyCensor). With `$censor` 1 the dither never discards: the character stays whole
however close the camera is. The face, transparent and simple shadow patterns leave their discards.

## Why it does nothing under Vulkan

The game's shaders are SPIR-V; no DX11 text exists to match, and the layer cannot tell which
Vulkan pipeline a DX11 shader hash means (LIMIT-036). RabbitFX's sections load inert, its
`$censor` toggles (key events work) and nothing reads it.

## The SPIR-V counterpart of the pattern

Ground truth: the DX11 shaders archived with RabbitFX active (catalogue 2026-10-19) show the
wrapped discard in 7 pixel shaders, each a dither of the form above; none of them has a captured
Vulkan twin in the catalogue. The Vulkan dithered shaders are in the flicker captures of 2026-10-13
(each the profiled shader of the same component and pass with the dither added).

Rule: a kill (OpKill, OpTerminateInvocation, OpDemoteToHelperInvocation) whose selecting branch
condition depends on a multiplication by the noise constant 52.9829 and on a load from the uniform
at set 2 binding 0 (the character instance window, DX11 cb2, D-079). Over the 263 captured
fragment modules: 38 have such a kill (the dithered character shaders), 4 have a noise kill without
the instance window (left alone: not a character's fade), alpha-test kills match neither.

## The design

- **Facts from SPIR-V.** For a fragment module: its dither kills by the rule above.
- **The patch.** A module whose dither branches never take the kill side (the condition replaced
  by a constant), everything else unchanged; validated before use.
- **What gates it, from the library's INI.** The replacement text names the IniParams element the
  discard waits for (`l(172, 0), t120`, `if_z ini.w`: 172.w); the sections' lists assign it
  (`w172 = $censor`). The folder generation evaluates those assignments at each presentation, as
  the original runs them whenever a patched shader draws (they are plain assignments), and the
  layer reads the element: nonzero means no dither.
- **Where it applies, as the original's replaced shader does: every draw.** The game's own draws
  of a pipeline with a dither kill bind a twin pipeline with the patched fragment module while the
  gate is on (made once, off the recording thread; until it exists the game's pipeline is bound).
  Replaced draws take the patch in their pipeline variants.

## 2.0.8: the camera fade, and the states made ahead

In the game (2.0.7) the anti-dither turned the field's grass and bushes into solid quads: their kill is
`min(LOD cross-fade - noise, alpha - cutoff) < 0`, the cross-fade read from the instance window too, so
the rule took it and the patch dropped the alpha test with it. RabbitFX's patterns never match those
shaders: they need the character materials' declarations and the camera fade (`max r, cb.x, cb.x`
then `add r, -r, l(1.0)`) before the discard. The rule now needs that fade: the larger of two values
read from the instance window in the kill's condition. Over 626 distinct captured modules (with the
field frame of 2026-10-27, captures/vk-2026-10-27-field) 45 match, all patched and valid; ten that
2.0.6 took (LOD cross-fades, three with an alpha test) and every alpha-tested field module do not.

The key also took a second or two: twins and the replaced draws' other state compiled when the gate
turned. As the original's replaced shader exists from the game's shader creation, the twin of a
dithered pipeline is now prepared when the game creates it (or when a gate appears), and a replaced
draw prepares its other state with its first; both build behind the variants draws wait for.

## Order of work, each step test first

Status 2026-10-27 (2.0.6): all four steps done. Steps 3 and 4: `FragmentPatch::no_dither`; replaced and
texture-only draws take it while the gate is set; the application's own draws bind the pipeline's twin
(a variant with the application's vertex stage, no images, the dither dropped) in vkCmdBindPipeline
(`CharacterModule::bind`), kept by the recording until it completed (`TwinOwner`); code that binds its
own pipelines in between binds back what the driver had (`DrawRec::bound`, `CmdBuf::gfx_bound`). The
gate is read once after the load as well, so the first frame sees it, and the first bind of a pipeline
waits for its twin (bounded, 4 s), as a replaced draw waits for its variant (D-103). The twins follow
RabbitFX's gate alone, not the layer's replacement switch. Scene check: the Mods-folder scene with a
dithering application fragment shader (EFMI_VK_TEST_DITHER) and the saved `$censor` 1 draws every
picture as without the dither (96/96); with `$censor` 0 its picture checks fail.

Earlier: step 2's mechanism done (IniRuntime::discard_gates; the Mods-folder scene's RabbitFX-shaped library reports IniParams[172].w = $censor; status `folder_generation.discard_gates`, `dither_off`). The installed RabbitFX is not loaded as an INI: the layer's RabbitFX interface (D-082, frontend/xxmi/rabbit_textures.h) stands in for it and lacks the anti-dither contract (`$censor` not persistent, no `\` key, no gate); RabbitFX itself now: the installed file's key and ShaderRegex sections join the interface (RuntimeConfig::native_library_files), its `$censor` is persistent; the vk03 replay with the installed folder reports 172.w = 0. Step 1 done (frontend/gpu/dither_kills.cpp; recipe_test: the fixture's dither found, its alpha test and noise-only kill not; 38 of 473 distinct captured modules dithered, all 38 patched into valid SPIR-V without a dither branch).

1. The rule and the patch on the captured modules (unit test: the dithered shaders found, the
   cutout and noise-only kills not; the patched modules validate and reach no dither kill).
2. The gate from the INI (frontend test: RabbitFX's sections give IniParams[172].w = `$censor`).
3. Replaced draws' variants with the patch (sandbox scene with a dithered fragment shader).
4. The game's own draws through twin pipelines (sandbox scene; the replay with a dithered pass).
