# Shared shader catalogue: collection and building

The catalogue is the keystone of T-030 (D-044): fixed `ps-tN` slots, texture-hash overrides,
ShaderOverride and ShaderRegex by game shader hash, and the GPU work inside matched draws that
reads the game's own bindings (typhoeus' chest physics, Si Lunabelle's guard) all need, for the
Vulkan pipeline of a draw, the DX11 shader it stands for and where each DX11 slot is in Vulkan.
The user chose on 2026-10-19 to collect game data for it.

The product is general (AGENTS.md, D-075): the runtime must find the correspondence for any
character and Mod, including characters never captured. A catalogue built from captures is
therefore ground truth for deriving and checking a general rule (the correspondence read from
the shaders' own interfaces, see "Open"), not a per-character table the runtime depends on.
Collecting more characters is never an installation step.

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

Optionally a character at LOD1 distance in the open world in both sessions. The number of
scenes is chosen for the variety of the ground truth (passes, material families), not to cover
characters one by one.

## First collection (2026-10-19)

Session A (DX11, PID 12024, probe 0.1.9) and session B (Vulkan, PID 17836, layer 1.0.2 as a probe
without a character package) showed the same 16 characters at 2560x1600 in the same order; every
Vulkan frame matches its DX11 frame by index region hashes. Captures in
captures/catalogue-20261019 (not committed). Two Vulkan frames used more than 128 pipelines; the
rest were queried while the objects were alive and recorded in each bundle's supplement.json.
Catalogue from the 16 scenes: 953 draw pairs, 116 programs (116 fragment and 57 vertex shaders),
113 of the 163 drawn DX11 pixel shaders and 33 of 54 vertex shaders; 800 texture slots placed with
1891 observations by content and 1507 by look, no texture conflict; constant buffers by size with
two conflicts (vs-cb3); consistent with the verified arcane table. Objects drawn once more under
DX11 than under Vulkan (an extra pass in some DX11 frames) are left unpaired; per-pass alignment
would pair them.

## Open

- Constant buffers are paired by size. The Vulkan snapshot keeps the uniform ranges of the
  selected draws and the DX11 capture the constant buffer windows; comparing static content
  (material constants) would make those pairs strong. Camera and time constants differ between
  sessions and cannot be compared by content.
- The general rule: derive the correspondence from the shaders themselves (the DXBC's
  declarations and the vkd3d translation against the game's SPIR-V interface: inputs, outputs,
  sampled images in use order, constant block sizes), so that shaders never captured are
  covered. The 16-scene catalogue is the ground truth to measure it against. Shaders seen under
  only one API need it.
- A game update changes shader bytes; the catalogue is then collected again. Texture identity
  does not depend on it.
