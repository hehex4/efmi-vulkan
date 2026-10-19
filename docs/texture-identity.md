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

## In the layer (1.5.1)

- Data hash. At each complete upload of mip 0, slice 0 the tracker hashes the same bytes twice: the
  whole level for its own content CRC, and for `ImageInfo::dx11_data_crc` the first
  `W * H * layers` bytes when that is shorter, else the whole level (rows without padding). The
  tracker keeps the latest complete upload; the original (`track_texture_updates = 0` in EFMI's
  d3dx.ini) keeps the hash of the creation data. The captures cannot tell the two apart; the 345
  matches above used the latest upload.
- Description. `texture_identity.cpp`: DXGI format from the Vulkan format, width, height, mip and
  array counts, sample count, usage 0, bind flags from the Vulkan usage (sampled 8, colour
  attachment 0x20, depth 0x40, storage 0x80), CPU access 0, misc TEXTURECUBE for cube-compatible
  images. An image never uploaded from a buffer (a render target, an image the GPU writes) gets the
  hash with seed 0, as the original gives a texture created without data. An asset the game would
  fill another way (an image-to-image copy) would get that hash too; not seen, not checked.
  With image content tracking off (`image_hash = 0`) no image has a hash.
- Per draw. A replaced draw's INI plan asks for the texture at a DX11 pixel slot: the pipeline's
  fragment module gives the register (D-076), the draw's tracked descriptor the image. A slot the
  module does not declare is empty. A buffer SRV, an array, an untracked descriptor, an unknown
  image, a partial or unreadable upload or an unmapped format answers "bound, no hash": no texture
  override matches it and the draw is still replaced. Status: `texture_queries`,
  `texture_unknown` and `texture_unknown_reason` per package; the capture's image list carries
  `dx11_hash`.
- A Mod's own resource bound to a slot answers "bound, hash 0": the original creates custom
  resources outside its hash table. Inside the owned presenter's scopes the GPU runtime's resource
  matcher answers instead; the two coexist since 1.5.1.
- With the host answering, CharacterCore lets Core's OverrideTextures run (CheckTextureOverride on
  ps-t0 to ps-t22) and accepts the Mod's texture-hash sections. Mesh sections the package has no
  part for (LOD levels beyond the packager's scope) are accepted as well and stay the game's.
- Sandbox: the runtime fixture's `[TextureOverride_AppTexture]` uses the description-only hash of
  the scene's sampled image and sets `$texture_seen`; a section with the same description over other
  data must not fire (runtime_test, all four configurations). Not yet seen in the real game, and the
  prefix rule for uncompressed formats is not yet checked against data.
