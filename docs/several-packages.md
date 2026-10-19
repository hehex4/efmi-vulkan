# Every installed character Mod at once (candidate 1.5.0)

A step toward the directory generation (phase 2): packages for the installed character Mods are
built without captures, and the layer runs all of them in one process.

## Packages without captures

`scripts/check_mod_corpus.py` plans every character INI of the installed directory (the original
skeleton initializer and draw callbacks, each Mod with Core in isolation). A file without a
namespace line is named by its file name, as the host parses it. `scripts/package_directory.py`
then builds, for every planned Mod:

- the mesh and material package (`scripts/package_character.py --textures --no-profiles`):
  static pixel bindings of the default scenario and no fragment profiles; the layer derives every
  fragment shader's rules from its module by the DX11 register rule (D-076). A component with
  bones but no entry section gets empty draw lists (nothing of it is replaced); entry sections
  beyond LOD1 are left out (LOD0 and LOD1 are the scope), listed in the report;
- the live sidecar (`scripts/prepare_runtime.py`): the INI and Core are read again at run time;
- a check with the production decoder (`efmi_vk_character_test --inspect`).

The decoder accepts a material envelope without profiles. Result for the directory of
2026-10-19 (22 character INIs, `build/characters/directory.json`):

| Result | Mods |
| --- | --- |
| Packaged (13) | akekuri, arcane, arclight, avywenna, estella, laevatain, mifu, perlica, rossiy, si_lunabelle, tangtang, xaihi, zhuangfangyi |
| Not planned in isolation (8) | ardelia, liino, typhoeus (RabbitFX lists of another file), yvonne (EFMITTL of its subfolder), ember (a resource of another INI), snowshine (a branch whose opening line comes from another file), gilberta (a DDS sRGB check of the planner), last rite (its skeleton initializer under the implicit namespace) |
| Package refused (1) | chengqianyu: a blend-index edit to R32G32B32A32_UINT, the layer widens to R16G16B16A16_UINT only |

The eight are artifacts of planning one Mod in isolation: the whole directory parses with no
diagnostic (`check_ini_folder.py`). The directory generation removes that limit.

## Several packages in the layer

`m2_package` may name a directory. Every `*.bin` in it with its `.runtime.json` sidecar is a
package (sorted by name, at most 32); the host enters character mode for a directory as for a
package file. The module's backend holds one controller per package (each with its own F10
generations). A draw goes to the controllers in order and the first whose section matches records
it, so each package sees only its own character. Present work runs package by package; a presenter
waits for what the one before handed on (`PresentWork`).

GPU residency is lazy for a directory: at start a package is decoded and its INI program loaded on
the CPU; its mesh, arenas and images go to the GPU only when one of its sections first matches a
draw (`character package activated on its first match` in the log). Until that package is ready
the game's own draws stay, as for any new character. An F10 reload of an active package brings its
successor up at once; a package never matched takes over without GPU work. For the 13 packages,
all on the GPU at once would be about 2.7 GB of images and 0.24 GB of meshes; lazily only the
characters on screen are. A single package file keeps the eager preparation it had.

Status: the fields at the top are those of the first package on the GPU (else the first), as
before; with several packages each one is under `packages` with its path, `lazy` and `activated`.

## Verification (sandbox)

`sandbox/runtime_test.py` runs the character scene with a directory of two packages, the fixture's
and another character's whose section hashes match no draw of the scene and which sorts first: the
scene's keys, reloads, module exchanges and pixels pass as with one package, the other package is
never activated and uploads nothing, the fixture's activates on its first match. Native decoder
tests cover the envelope without profiles. None of this was loaded into the game.

## The 13 packages in the sandbox (1.5.1 and 1.5.2)

The sandbox program loads all 13 directory packages (no draw of it matches a character, so none
goes to the GPU): about 2.7 s and 394 MB, normal exit.
- 1.5.0: all 13 refused at INI load, 11 for texture-hash TextureOverrides, 2 for shader sections.
- 1.5.1 (texture identity, docs/texture-identity.md): 10 load.
- 1.5.2: 12 load. zhuangfangyi's folder holds a second INI (`fangyi ul.ini`) with its own mesh
  overrides at objects the package has no part for; the original loads it with the character,
  and its meshes now stay with the game's draws (logged as mesh files outside the package; a
  replaced draw binding one would refuse). perlica's ShaderOverride sections carry only a hash and
  a filter_index nobody reads; such a section runs nothing, and without shader facts nothing can
  read its filter_index (a ps or vs operand is no CPU expression), so it is accepted.
- Still refused: xaihi, whose `ps skip.ini` skips the game's draws of two shaders by their DX11
  hash. That needs the DX11 identity of the game's shaders, which no rule gives from SPIR-V
  (phase 4 shader facts).
- Six of the twelve reference RabbitFX (akekuri, avywenna, estella, mifu, perlica, rossiy), a
  ShaderRegex library in its own Mod folder; those references stay original warnings and their
  RabbitFX textures do not bind (LIMIT-004).

## Open

- Packages that were on the GPU stay there until the process ends; releasing a character not seen
  for a while is not done yet.
- Each package hosts its own copy of Core in its INI generation (separate engines), unlike the
  original's one engine for the whole directory.
- The R32 blend-index edit (chengqianyu) and the eight Mods planning cannot isolate.
