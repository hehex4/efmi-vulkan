# Original LOD1 material evidence, 2026-10-13

The user positioned the character at actual LOD1 in a user-started DX11 session
(PID 33852). The first capture still contained 594 XXMI calls and mod meshes;
it is retained as `initial-mod-on`, not used as the original-material baseline.
At the user's explicit request the probe sent F11, observed key down/up at frames
35529/35531 and restored focus. No installed configuration was edited.

`captures/dx11-lod1-20261013/lod1-off` is frame 35553: 954 game calls and only the
two XXMI overlay calls, no XXMI indexed mesh draw. All 212 shaders were archived
as DXBC and verified against the captured FNV64 and SHA-256; 247 bound static
textures were read without errors. The manifest fingerprints 452 evidence files.
The probe released its capture after collecting this data. Mod remains disabled.

The raw records reproduce both pairings against saved Vulkan LOD1 bundles
`captures/m2-0.8.1-20261012/lod1-a/attempt-01` and `lod1-b/attempt-01`.
Four original-mesh pass sequences agree on index identity and draw arguments.
The target vertex declarations also agree. The resulting rules are:

| Vulkan fragment | Source DX11 pixel shader | Result |
| --- | --- | --- |
| `310c25c3` | `4349359099f69527` | ps-t17 -> set 1 binding 6; ps-t18 -> set 1 binding 3 |
| `a6d7088a` | `857960e2e308ca7f` | ps-t15, ps-t16, ps-t17 undeclared |
| `0aeef59b` | `ed8cac1c71bcf430` | ps-t15, ps-t16, ps-t17 undeclared |

The two image correspondences use unique dimensions/format/mip count in the
paired draw, not content hashes: the earlier Vulkan sessions had hashing off.
Both saved Vulkan frames reproduce the same mapping and exact shader bytes.
Absent slots are proven by disassembling the captured original DXBC.

`scripts/check_lod1_material_capture.py --check-table` rechecks raw evidence,
source declarations, both pairings and the adopted table. `material_profiles.py`
resolves exact shader bytes from fingerprinted manifests, including this LOD1
source, instead of assuming every shader came from the display capture.

Verification: evidence audit passes; fragment adapter checks pass for eight
shaders/18 image relocations, three expected dimension refusals and ten null
shaders/21 bindings, with SPIR-V validation. The native hosted material replay
accepts 359 of 370 saved custom draws; the remaining eleven are the existing
2D-for-3D limitation (LIMIT-021), with no unknown LOD1 correspondence. The rebuilt
asset suite passes 253 checks and the hosted frontend's native groups pass.
Reports: `build/lod1-material-capture/report.json`,
`build/lod1-source-material-plans.txt` and `build/fragment-images/report.json`.

This proves source correspondence, not Vulkan material rendering. No DLL was
deployed, no new Vulkan session was started and the 0.9.5 flicker fix still needs
game acceptance. Next is texture integration in the existing static character
backend. General Core runtime texture-identity queries remain separate work.
