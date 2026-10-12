# Third Vulkan probe session: texture identity and reported stutter

2026-10-12, user-started Endfield PID 19264, layer 0.7.0 in probe/inject mode.
The user reported intermittent frame-rate drops during loading and while the character display
was otherwise steady. A high average frame rate does not contradict that report.

## Preserved evidence

- `captures/third-probe-observation/`: two status reads 3.000533 seconds apart and API counters.
- `captures/third-probe/`: frame 22563, manifest complete, all 172 file hashes verified.
  176 graphics draws and 45 dispatches; 153 indexed draws; one recording thread, four submits,
  two command buffers, 28 classic render passes. All 12 sample LOD0 components match, 45 draws.
  All 114 queried pipelines were exported; no descriptor omission or query gap.
- `captures/third-probe-pairing/frame-off.summary.json`: comparison with the earlier DX11
  frame 170064 and its saved shader declarations, using the 65 DX11 texture hashes from
  `frame-off-tex.textures.json`. Earlier comparison files were preserved.
- `docs/arcane-texture-slots.json`: metadata-only sample slot profile with source fingerprints.
  It includes no shader bytecode or texture assets.

## T-018 is answered for this scene

Texture pairs include 144 content matches and 101 unique size/format matches. Of the sample
mod's assignments, 19 are placed and 37 are not declared by that pass's shader; none remain open.
The four formerly ambiguous entries are all resolved by CRC-32C of mip 0:

| Component | Vulkan fragment hash | DX11 slot | Vulkan set.binding |
| --- | --- | --- | --- |
| 0 | 09e0be38 | ps-t16 | 1.3 |
| 0 | 09e0be38 | ps-t17 | 1.1 |
| 0 | 2e2750b8 | ps-t16 | 0.38 |
| 4 | 3983573e | ps-t17 | 1.8 |

There are no conflicting binding assignments in the table. Two unrelated UI fragment shaders
are shared by several DX11 shader hashes; that is not a binding conflict. The reporting script
now counts these identities separately. CRC equality is evidence, not a collision-proof identity.
The remaining mappings marked `look` retain their weaker size/format evidence.

## Pipeline creation requirements for M2

At observation time, 936 graphics pipelines had been created. All used two stages, flags 0,
subpass 0 and one sample; 935 used triangle lists and one a triangle strip. Dynamic viewport,
scissor and stencil reference occur in all 936; dynamic depth bias in 117. Raster depth bias
is enabled in 124 and depth clamp in 97. No tracked pNext chain, specialization, pipeline base
handle or non-main entry point was reported. These are observed counts, not support guarantees
for other scenes or future game versions.

The 45 matched draws use 39 pipeline objects but only 26 shader pairs (21 vertex and 26 fragment
shaders). All 39 use three set layouts, no push constants, flags 0, subpass 0, no specialization
and triangle lists. Eight include dynamic depth bias; the remaining 31 use viewport, scissor and
stencil reference only. D-022's extra vertex-stage set remains applicable. Recreating these
pipelines and rendering a replacement are still M2 work; nothing was replaced in this session.

## Performance evidence and fallback

Status A: 22,583 successful upload hashes, four unreadable uploads, 2,734,191,086 bytes hashed,
8,121,313 microseconds accumulated. Status B: 22,943, four, 2,734,191,806 bytes, 8,121,909
microseconds. These are upload counts, not distinct textures. The timer covers the upload
processing routine, including reading/copying bytes, CRC and resource locks; it is not a
measurement of the CRC instruction alone or an exact increase in total loading time.

Across this interval, 360 presents advanced in 3.000533 seconds (about 120/s), while hash work
increased by only 596 microseconds for 720 bytes. The roughly 8.1 seconds accumulated earlier
establish substantial work during loading; this steady interval cannot establish the cause of
the user's intermittent stutter. Layer 0.7.0 has no per-present timing history, so tail latency
cannot be reconstructed from these average counters. No hash-off comparison has run yet.

After saving the bundle, `dist/efmi_vk.cfg` was changed to `image_hash = 0`. The previous file is
in `build/backups/before-image-hash-off-20261012-121047/`. It takes effect in the next process,
not in PID 19264. Resource tracking remains enabled for the first comparison. If stutter remains,
the existing `resources = 0` and passthrough fallbacks can isolate broader probe overhead.
No launcher setting was changed and no process was launched or stopped by the agent.

T-020 tracks the unresolved frame-time spikes. The 0.7.1 diagnostic is described in
`docs/frame-pacing.md`; it has not been loaded in the real game by this session.
