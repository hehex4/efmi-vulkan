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

## Order of work, each step test first

1. The rule and the patch on the captured modules (unit test: the dithered shaders found, the
   cutout and noise-only kills not; the patched modules validate and reach no dither kill).
2. The gate from the INI (frontend test: RabbitFX's sections give IniParams[172].w = `$censor`).
3. Replaced draws' variants with the patch (sandbox scene with a dithered fragment shader).
4. The game's own draws through twin pipelines (sandbox scene; the replay with a dithered pass).
