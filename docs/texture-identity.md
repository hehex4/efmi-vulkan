# DX11 texture hashes from what the layer sees (D-077)

Mods name the game's textures by their DX11 (3DMigoto) resource hash: `[TextureOverride...]
hash = ...` with `this = Resource...`, and Core's OverrideTextures runs `CheckTextureOverride` on
ps-t0 to ps-t22. Under Vulkan the same texture is an image the game uploaded. Its DX11 hash can be
computed from that upload and the image's description, for any texture and any character.

## The original hash

ResourceHash.cpp (XXMI): `hash = crc32c(D3D11_TEXTURE2D_DESC, seed = data_hash)` over the 44 bytes
Width, Height, MipLevels, ArraySize, Format, SampleDesc.Count, SampleDesc.Quality, Usage,
BindFlags, CPUAccessFlags, MiscFlags (screen-sized textures substitute their size; not asset
textures). `data_hash` (texture_hash_version 0) covers the first subresource's initial data:
`W * H * ArraySize` bytes from its start when that is not longer than the subresource, else the
whole subresource with row padding skipped. For BC7, BC5, BC6H and R8 that is the whole mip 0, for
BC1 and BC4 too (the second case); for RGBA8 it is the first quarter of mip 0.

## Checked on the 2026-10-19 captures

- DX11 side: for every game Texture2D the 16 DX11 frames read (540), the formula with the probe's
  `data_hash` gives the recorded hash for all shader-resource textures (usage 0, bind flags 8,
  misc 0: 506 of 506), and for most render targets; the three exceptions are screen-sized render
  targets, which the original renames by the screen size.
- Vulkan side: for every content-hashed image of the 16 Vulkan frames in BC7, BC5, BC1 and R8, the
  formula with the image's own description (Vulkan format mapped to DXGI, usage 0, bind flags 8,
  CPU access 0, misc 0) and the layer's CRC-32C of mip 0 gives the DX11 hash of the DX11 texture
  with the same content: 345 of 347 where both frames had the texture. The two others are
  explained: a cube map (misc TEXTURECUBE), and one texture uploaded under Vulkan with one mip
  level where DX11 has its full chain. 15 Vulkan images had no DX11 counterpart in the frames.
- Uncompressed formats (RGBA8, RGBA16F) need the first `W * H` bytes of mip 0, which the captures
  do not keep; the layer computes that at upload time. Not yet checked against data.

## In the layer

A tracked image's DX11 hash comes from its first upload of mip 0, slice 0: the data hash by the rule
above, then the description of the Vulkan image (DXGI format from the Vulkan format, mip and array
counts, TEXTURECUBE for cube-compatible images, bind flags shader-resource). An image without such
an upload has no hash; a query for it refuses rather than guessing.
