EFMI Vulkan
===========

Runs your installed EFMI Mods (XXMI Launcher) for Arknights: Endfield while the game renders with
Vulkan instead of DirectX 11.

Install
-------

1. Close the game and the XXMI Launcher.
2. Unpack the archive into the EFMI folder of your XXMI Launcher, the folder that holds Mods and
   Core. You then have EFMI\EFMI-Vulkan\efmi_vk_inject.dll next to EFMI\Mods. (Unpacked into
   EFMI\Mods it works too.)
3. Open the XXMI Launcher, choose EFMI, open its settings:
   - XXMI DLL Injection Mode: Skip.
   - Advanced, Custom Libraries: tick Inject Libraries and enter the full path of
     efmi_vk_inject.dll, for example D:\XXMI Launcher\EFMI\EFMI-Vulkan\efmi_vk_inject.dll.
4. Close the launcher. Open "XXMI Launcher Config.json" in the launcher's folder (next to the
   launcher program) with a text editor. Under "Importers", "EFMI", "Importer", change
     "d3d11_mode_cmd_args": "-force-d3d11"
   to
     "d3d11_mode_cmd_args": ""
   and save. The game then starts with Vulkan.
5. Start the game from the XXMI Launcher.

Use
---

- Install, enable and disable Mods in EFMI\Mods as usual. Their keys work as with EFMI.
- F10 reloads the Mods folder after you add, remove or edit Mods.
- RabbitFX's anti-dither key (\) works while RabbitFX is installed.
- The Mods' saved settings are in EFMI-Vulkan\settings.

If something goes wrong
-----------------------

- The log is EFMI-Vulkan\efmi_vk_layer.log. Please attach it when you report a problem.
- To go back to DirectX 11: untick Inject Libraries, set XXMI DLL Injection Mode back to Direct and
  set "d3d11_mode_cmd_args" back to "-force-d3d11" (launcher closed).

Licence
-------

GNU General Public License version 3 (LICENSE.txt). Source code:
https://github.com/loyal37/efmi-vulkan. Third-party components: THIRD_PARTY_NOTICES.md and the
licenses folder.
