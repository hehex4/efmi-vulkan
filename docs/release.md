# The playable release (D-135)

The user, 2026-10-27: release under GPLv3; the release text tells players how to install and use it;
nothing for development goes into it, players hold a playable version.

## What it is

`EFMI-Vulkan-X.Y.Z.zip`, one folder `EFMI-Vulkan` the player unpacks into the EFMI folder of the XXMI
Launcher (next to `EFMI\Mods`):

- `efmi_vk_inject.dll`: the layer with the inject entry, built with `EFVK_NO_PROBE` and without
  `layer/src/ipc.cpp`: no probe pipe (its access list let any interactive user's process drive the
  elevated game, module exchange included), no sampler, nothing outside the game reads or changes it.
- `dxcompiler.dll`, `SPIRV-Tools-shared.dll`, `efmi-vkd3d-shader.dll`: loaded from this folder by full path
  when a Mod's HLSL or DX11 bytecode is compiled. `dxil.dll` is not shipped (SPIR-V only, and its
  Microsoft licence does not go with a GPL package).
- `efmi_vk.cfg`: `m2_mods = auto` (the first folder upward from the DLL's that holds `Mods` and
  `Core\EFMI`, so EFMI\EFMI-Vulkan and EFMI\Mods\EFMI-Vulkan both work; 2.0.9's `../Mods` found nothing
  in the latter, BUG-073; a named folder may be relative to the DLL's folder), replacement on, the
  settings the game was played with. The layer's own folder is never part of the Mods folder's content.
- `README.txt` (install and use), `LICENSE.txt` (GPLv3), `THIRD_PARTY_NOTICES.md`, `licenses\`.
- Optional `settings/shader-inputs.bin`: generated whole-corpus semantic metadata for exact shader
  identities (D-294/D-295), selected with `--shader-inputs`. It contains no shader code, character
  names, capture data or saved player settings. The packager validates bounded framing, checksum,
  unique fields and canonical encoding before building or replacing an output folder.

Not in it: tests, the sandbox, the replacement module files, the layer manifest, symbols (kept in
`layer/build/release` for crash logs), logs, saved player settings or generated GPU caches.

Why not one DLL: DXC exists only as Microsoft's DLL (a static build of it is unsupported and large);
vkd3d-shader is built with MinGW GCC, the layer with MSVC; loading DLLs from memory is how cheats
hide code and is never done here. SPIRV-Tools could be linked statically if one file less matters.

## Steps

1. `python scripts/build.py layer sandbox release`: the development build (for the checks) and the
   release in `build/release/EFMI-Vulkan`, `build/release/EFMI-Vulkan-X.Y.Z.zip` and its `.sha256`.
   To include semantic metadata, append `--shader-inputs build/shader-inputs.bin`, using the checked
   output of [the asset metadata pipeline](shader-input-metadata.md). An omitted cache does not enable
   arbitrary semantic edits. Generated metadata is never committed to the source repository.
2. The touched regression areas, then `python scripts/check_release.py build/release/EFMI-Vulkan-X.Y.Z.zip`
   (contents, no probe in the binary, the release injected into the sandbox with the installed Mods, and
   unpacked into an EFMI folder of its own and into its Mods folder).
   The checker also verifies an included semantic cache and confirms that the no-probe binary loads it
   in the full Mods-folder check and both unpack layouts. These remain sandbox loading checks.
3. Commit and fast-forward main. The archive remains an unpublished local candidate. Remote publication
   is separate and requires a repository named by the user; game loading remains subject to Gate G1.
