# What the original Core reads off a vertex shader (candidate 1.5.4, D-079)

Every EFMI export draws its mesh only through this guard in `CommandList_Component_DrawInstances`:

```ini
run = CommandList\EFMIv1\Component_ReadConfig
run = CommandList\EFMIv1\MergedSkeleton_DetectBoneDataSource
if $\EFMIv1\bones_data_source != 0 && $\EFMIv1\instance_config_vs_cb_slot != -1
    handling = skip
    ...
    run = CommandList\EFMIv1\Component_DrawInstances
endif
```

Under DX11 the Core sets both from what the draw binds (Core `API.ini`, `MergedSkeleton.ini`):

- `instance_config_vs_cb_slot`: the first of vs-cb2, vs-cb1, vs-cb3 bound with 4096 vectors, the
  per-instance window; -1 without one.
- `bones_data_source`: the rolling bone buffer when vs-t0 is a structured buffer of 16-byte
  elements and such a window exists (cb3/cb4 sources of FX shaders are not taken by the renderer).

The layer replaces those Core lists natively (`CharacterCore`) and had no answer for either, so
both stayed "unknown" and the guard left every real Mod's mesh to the game (BUG-027). The sandbox
fixture has no such guard, so the scene never showed it; the offline dry run of the installed Mods
did.

## The rule

`efmi::gpu::core_source_facts` (C++) and `core_facts` (`server/efmi_vk/source_slots.py`) apply the
Core's own rules to the registers the vertex module's SPIR-V gives by D-076: the block size of each
cb register (the declared block) and the element stride of the buffer at t0. The backend keeps the
facts only where they name what it reads itself: the window at set 2, binding 0 (where it takes the
instance entry) and the bone buffer its bone site found. Otherwise the facts stay unknown and the
Core leaves the mesh to the game, as before. They are cached per pipeline; status
`source_facts_unknown` counts pipelines without rolling bones.

## Checked

`scripts/check_core_facts.py` (suite step): for the 57 vertex shaders of the 2026-10-19 catalogue,
the facts the Core computed from each DX11 draw's bindings against those derived from the paired
Vulkan module (`docs/core-facts-truth.json`, metadata only):

- the replacement decision agrees for all 57;
- for the 49 with rolling bones, the window slot agrees and the window is at set 2, binding 0;
- of the 8 without, 3 differ in the slot only (the Core reads the bound view's size, the module
  gives the declared block); neither side replaces there.

The C++ and Python implementations give the same facts for all 57 modules. Unit checks cover the
rule on a DXC-compiled vertex shader and on synthetic register lists.

## Effect, offline

`scripts/check_mods_dry_run.py` (suite step) runs every installed character Mod through the
production planner (`efmi_vk_character_test --plan-mods`): before this rule 0 component sections
drew; with it 137 do (110 with the Mod's own images), 23 hide their component, 70 hide sections
apply, and the first section of each Mod is left to the game as the original does until the Mod
registers at the next Present. Not loaded into the game.
