# What the player can do around F10, and what the layer does

The user, 2026-10-27, after 2.0.7 ended the game at an F10: consider every operation a player may
perform before and after an F10 refresh. This is that list for the Mods folder's generation (D-131),
each case against the original and with its check.

## The original

3DMigoto's F10 (`ReloadConfig`, IniHandler.cpp) runs in Present on the game's thread, under
`G->mCriticalSection`: no draw sees a half-reloaded configuration. It saves the persistent variables
to d3dx_user.ini, parses d3dx.ini and every Mod's INI again (they read the saved values back), runs
[Constants] again (the other variables start from their initial values), makes new custom resources
(each loaded from its file at its first use, `CustomResource::Substantiate`), marks every shader for
the ShaderRegex pass again, and releases what the old configuration held; the D3D11 runtime destroys
each object once, when its last reference goes and the GPU is done with it. Every F10 key press sets
`gReloadConfigPending` and the next frame reloads: a press is never lost.

## The layer

F10 starts a successor generation in the background (the live one draws on); the packages the
frames draw bring their successors up; then, at one presentation, every package is exchanged at
once, a Mod added becomes a package, a Mod gone stops, and the old generation stops on a thread of
its own (BUG-062). The successor inherits the live persistent values at the exchange.

## The cases

| Before F10 the player... | Original | Layer | Check |
|---|---|---|---|
| changes nothing | everything reloaded, files read again | meshes kept, textures not read again (BUG-061) | scene: F10 with unchanged files |
| edits a Mod's INI | new logic next frame | new logic once the successor is ready | scene: green to red |
| leaves a broken line in an INI | line reported and skipped | the same | scene: broken line |
| replaces a texture with a broken file | that resource fails at first use, the rest reloads | the successor is refused, the live one draws on (deviation, LIMIT-043) | scene: broken DDS |
| changes a mesh file | new buffer | uploaded again | character test (same_payload) |
| adds a Mod folder | loaded | a package, matched from the next frame | scene: re-enabled Mod (2.0.8) |
| removes or disables a Mod that is drawn | released | its package stops once, off the presenting thread; 2.0.7 stopped it twice at once and ended the game (BUG-067) | scene: disabled drawn Mod (2.0.8) |
| removes a Mod never drawn | released | its package (no worker) stops | the same path |
| enables again a Mod removed earlier | loaded | 2.0.7 took a new slot each time and kept the old package's objects: 64 slots in all, so toggling Mods ran out (BUG-068); 2.0.8 reuses the Mod's slot and drops what a stopped package held | scene: re-enabled Mod (2.0.8) |
| renames a Mod folder, or swaps one Mod of a character for another | old released, new loaded | one package stops, one is added, in one exchange | the two rows above |
| presses F10 again while the first reload loads | each press reloads | 2.0.7 dropped the press (the request was taken while a load ran, and lived in the old generation's runtime); 2.0.8 keeps it and loads again after the exchange (BUG-069) | scene: two F10 in a row (2.0.8) |
| holds F10 | one reload | one reload | scene: F10 held |
| toggles a Mod's key, moves a panel, before or while it loads | saved, read back | the successor inherits the live values at the exchange | scene: panel settings |
| removes or adds RabbitFX | ShaderRegex sections gone or applied | the gate follows the live generation; 2.0.7 set it from a candidate while it loaded (BUG-070); with RabbitFX added by F10 the existing pipelines' twins are prepared then (D-133) | scene: RabbitFX disabled and enabled (2.0.8) |
| presses F10 in a menu (no character drawn) | reloads | exchanged at once, packages activate when drawn | scene |
| presses F10 with the character on screen | reloads at once | the live generation draws until the successor's images and meshes are up (at most 30 s, then refused) | scene |
| deletes a Mod's files while its reload reads them | the files that are missing fail | the successor may fail its package and be refused after 30 s (LIMIT-044) | none |
| quits the game while a reload loads or an old generation stops | n/a | teardown waits for both; a package stopped by both stops once | scene ends after an F10 |
| has more than 64 character Mods | no limit | the first 64 (LIMIT-040) | none |

The table is the Mods folder's generation, the configuration in use. The older one-generation-per-Mod
mode (`m2_folder_generation = 0`) keeps its own F10 handling and is not covered here.
