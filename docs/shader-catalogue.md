# Shared shader catalogue: collection and building

The catalogue is the keystone of T-030 (D-044): fixed `ps-tN` slots, texture-hash overrides,
ShaderOverride and ShaderRegex by game shader hash, and the GPU work inside matched draws that
reads the game's own bindings (typhoeus' chest physics, Si Lunabelle's guard) all need, for the
Vulkan pipeline of a draw, the DX11 shader it stands for and where each DX11 slot is in Vulkan.
The user chose on 2026-10-19 to collect game data for it.

## Method

One scene is captured under DX11 and under Vulkan. `scripts/dx11_baseline.py summarize`
without `--mod` pairs every object both frames draw:

- objects by the region hash of their index data, which is the same number under both APIs;
- their draws by order, when both frames draw the object equally often (otherwise the object
  is reported as not paired);
- what a draw binds by content (CRC-32C of mip 0, both sides) or, weaker, by look (shape, size,
  mips, format); constant buffer windows and buffers by size. A DX11 slot takes part only when
  the shader of that stage declares it.

`scripts/shader_catalogue.py build` merges the summaries of many scenes. It is keyed by Vulkan
program (vertex and fragment shader hash), because binding numbers belong to a program, and
lists for every DX11 slot of every stage where it was found, how often and by which evidence.
A slot found in two places is a conflict and is reported, never resolved by choice; groups that
only look alike and slots without a counterpart are counted. `--check` compares with an earlier
verified table (`docs/arcane-texture-slots.json`). Only metadata goes into the catalogue.

Trial on the data already on disk (arcane's display frame and the two actual LOD1 frames):
3 scenes, 710 draw pairs, 76 programs (48 fragment and 43 vertex shaders), consistent with the
verified arcane table. Texture slots: 329 placed, 144 of their observations by content and 1017
by look (the LOD1 frames had no content hashes); 309 slots only ambiguous. Constant buffer slots
554, all by size; one conflict (`vs-cb3` of one program in two places). Content hashes on both
sides are therefore required, and constant buffers need better evidence than size.

## Collection protocol

Both sessions are started by the user, on the account the user chooses. Same window size,
same graphics quality (shader variants follow it) and the same characters in both sessions.

Session A, DX11, launcher unchanged (XXMI and the EFMI MCP DX11 probe are injected). For each
character: the character display, mods off with F11 (the original draws), then

    py scripts/dx11_baseline.py --out captures/catalogue-<date>/dx11 capture --label <character> --textures --archive-shaders

Session B, Vulkan. The user replaces the injected library in the launcher's interface with
`F:\efmi vulkan\dist\efmi_vk_inject.dll` (a signed value) and asks for the two Vulkan values
(`d3d11_mode_cmd_args`, `xxmi_dll_inject_mode`), which the agent then switches with the game and
launcher closed and a backup first. Layer 1.0.2 as deployed, with `dist/efmi_vk.cfg` set to
`mode = probe`, `resources = 1`, `image_hash = 1`, `m2_enabled = 0` (no replacement, content
hashes on). The same scenes:

    py scripts/collect_probe.py --pid <pid> --target game --out captures/catalogue-<date>/vulkan/<character> --pipeline-limit 128 --snapshot all

Afterwards, offline:

    py scripts/dx11_baseline.py --out captures/catalogue-<date>/dx11 summarize --label <character> --vulkan captures/catalogue-<date>/vulkan/<character>
    py scripts/shader_catalogue.py build --summary <each summary> --out build/catalogue/catalogue.json --check docs/arcane-texture-slots.json

Optionally a character at LOD1 distance in the open world in both sessions. Start with four or
five characters and look at how many new programs each adds before collecting more.

## Open

- Constant buffers are paired by size. The Vulkan snapshot keeps the uniform ranges of the
  selected draws and the DX11 capture the constant buffer windows; comparing static content
  (material constants) would make those pairs strong. Camera and time constants differ between
  sessions and cannot be compared by content.
- Shaders seen under only one API stay outside the catalogue. Structural matching (vkd3d
  translation of the DXBC against the game's SPIR-V interface) can be tested on the collected
  ground truth.
- A game update changes shader bytes; the catalogue is then collected again. Texture identity
  does not depend on it.
