# Character Mods straight from the Mods folder (candidate 1.5.3, D-078)

Until 1.5.2 every character needed an offline package: Python scripts drove the original frontend in
a separate host to plan one Mod in isolation (`check_mod_corpus.py`, `package_character.py`,
`package_directory.py`) and wrote a `.bin` with a `.runtime.json` sidecar. From 1.5.3 the layer can
take the EFMI Mods folder itself; no packaging step, and an F10 reload reads the Mod again.

## Configuration

| Setting (environment) | Meaning |
| --- | --- |
| `m2_mods` (`EFMI_VK_M2_MODS`) | the EFMI Mods folder; replaces `m2_package` |
| `m2_core` (`EFMI_VK_M2_CORE`) | the Core folder; default `Core\EFMI` beside the Mods folder |
| `m2_settings` (`EFMI_VK_M2_SETTINGS`) | project-owned folder for the Mods' saved variables; default `settings` beside the replacement module |

Nothing is ever written into the Mods folder (checked by the sandbox test).

## Which INIs

Every enabled INI of the folder (no part of its path starts with `DISABLED`, as the original skips
them) that declares `[CommandListInitializeMergedSkeleton]`, the EFMIv1 merged-skeleton initializer.
Each one becomes a generation of its own, as a package did: the INI, the four Core files and, with
the owned GPU tools present, the other enabled INIs of its folder. All are lazy: their meshes and
images go to the GPU when one of their sections first matches a draw.

## Where the package comes from

The generation is loaded first; `CharacterCore` in discovery mode then reports its contract, and
`CharacterProgram::discover` makes the package around it:

| Part | Source |
| --- | --- |
| mesh sections | the original override registry: sections of the Mod's namespace with a constant `match_index_count` (and optional constant `match_first_index`); the component and LOD are the constants their own command list assigns to `$\EFMIv1\component_id` and the Mod's `$lod_level` |
| hide candidates | such a section without a component; the live plan decides whether it hides the game's mesh |
| left to the game | sections of other INIs, LOD beyond 1, inactive components, non-constant matches |
| bones, components, LOD remaps | the original Core initializer as executed in the generation (`read_skeleton_plan`, `load_palette_files`); components must partition the skeleton, LOD0 is the identity |
| mesh streams | every buffer file the generation declares (index or vertex by its usage, its stride); remap files as remap streams |
| bone streams | found by the live plans: the stream under the blend-index edit (`vb2`), 16-bit indices in its last 8 bytes, weights before them in a 16-byte element (the packager's rule), checked once like a package's |
| image catalogue | every DDS file the generation declares |
| layout edit | the blend-index widening the live plans accept |

The whole package then goes through the same decoder as a package file (bounds, partitions,
remaps), so a malformed Mod is refused as before. There is no static scenario: what a component
draws is always the live plan's. The DX11 pixel slots whose rules are read from the fragment
modules (D-076) follow the live plans as well; a slot a later plan binds for the first time makes
every pipeline read its rules again for the larger set.

## Results (sandbox, the installed Mods folder, 2026-10-19)

The sandbox program draws no character, so this is loading only: 22 character INIs found, 19 load
(akekuri, arcane, arclight, ardelia, avywenna, chengqianyu, ember, estella, gilberta, laevatain,
liino, mifu, perlica, rossiy, si, snowshine, tangtang, typhoeus, zhuangfangyi), in 3.7 s with a
working set of 519 MB (the CPU copies of their meshes). With offline packages 13 of 22 could be
packaged and 12 loaded. Refused:

- xaihi (`ps skip.ini`) and yvonne (`EFMITTL/translucency.ini`): ShaderOverride sections that act
  on the game's draws; they need the DX11 identity of game shaders.
- last rite (until 1.5.5): its namespace `last rite` has a space, which the native Core's name
  check refused. From 1.5.5 a namespace is any lower-case printable ASCII name without
  separators, and an INI without a namespace line is read under its file name, as the runtime
  parses it (the original's implicit namespace by path).

Known limits of those that load:

- chengqianyu edits its blend indices to R32G32B32A32_UINT for one component: accepted per draw
  from 1.5.5 (the element becomes R32G32B32A32_UINT, its bone indices are read as 32-bit). One of
  its LOD1 sections also moves NORMAL and TEXCOORD6 with `ElementOffset`, which needs the DX11
  semantic of a Vulkan attribute; that section stays the game's.
- akekuri, avywenna, estella, mifu, perlica, rossiy, ardelia, liino and typhoeus reference
  RabbitFX, a library in another Mod folder; its references stay original warnings and its
  textures do not bind (LIMIT-004, docs/rabbitfx-assessment.md).
- A blend stream whose element is not 8 or 16 bytes (Typhoeus' 24-byte one) takes its bone
  offsets from the draw's pipeline; offline it keeps no bone mask.
- Sections that match an index hash without an index count (`this = null`, `handling = skip` in
  chengqianyu, last rite, xaihi) are not matched by the renderer.

## Offline plans of the installed Mods (1.5.4)

`scripts/check_mods_dry_run.py` (suite step) runs every character INI through the production
planner (`efmi_vk_character_test --plan-mods`, no GPU, so without the Mods' other INIs and
CustomShader sections), with the Core's source facts of the game's common vertex shaders (D-079):
20 of 22 load there in 1.5.4 (snowshine needs the owned runtime for its CustomShader sections, last
rite's namespace was refused), 21 in 1.5.5; 137 component sections draw (144 in 1.5.5) the Mod's mesh (110 with its own images), 23 hide their
component, 70 hide sections apply, 13 hide sections do not apply (their conditions), and 19 sections
(the first of each Mod) are left to the game until the Mod registers at the next Present, as the
original does. Refused: chengqianyu's two sections under its R32G32B32A32_UINT blend-index edit and
one of ember's (GPU work inside a matched draw, P2). Before D-079 no component section drew
(BUG-027).

## Verification

`sandbox/runtime_test.py` (directory generation): a Mods folder with the fixture's Mod, another
character's Mod and a `DISABLED` copy; the scene's keys, F10 reloads, module exchanges and pixels
pass as with the fixture's package, the other Mod never goes to the GPU, settings land in the
configured folder, and nothing is written into the Mods folder. Not loaded into the game.

## Open

- One engine for the whole folder, as the original has (libraries such as RabbitFX and EFMITTL in
  other folders, keys shared across Mods).
- Implicit namespaces, hash-only index sections, the R32 blend-index edit.
- Releasing characters not seen for a while; reading mesh files only on activation.

## Sections matched by their hash alone (candidate 1.6.0, D-084)

A section of the Mod with an index hash, no `match_index_count` and no `match_first_index`, whose
list skips the draw (`handling = skip`, also inside a branch), hides a game mesh by its index region's
hash whatever its count, as the original matches it. Discovery takes such sections into the contract
with an index count of "any" (`kCharacterNone`); they are never a component. The backend keeps them
apart from the count-indexed sections and, once the character's package is activated, hashes every
indexed draw it sees for them (the region hash is cached per buffer region and re-checked by a short
fingerprint). A hash without a skip is left as before: it may name a texture (D-077).

Installed Mods: five such sections (xaihi 3, chengqianyu 1, last rite 1); the dry run plans all five
as hidden (hide applied 76 to 81). None of their hashes is in the catalogue frames. Runtime check:
the capture replay of arcane with a project-owned copy of the Mod in which one counted hide section
(`9e31e918`, 9546 indices) lost its count and first index gives the same matched and hidden counts as
the original Mod (10439 and 6119 over 360 frames); the replay selects draws for such sections by
their hash alone.
