// The DX11 resource hash of a game image (D-077, docs/texture-identity.md), as the original computes it
// for the same texture: crc32c of the 44-byte D3D11_TEXTURE2D_DESC, seeded with the data hash of the
// first subresource's initial data (zero for an image created without data). The data hash comes from
// the tracker's first upload of mip 0, slice 0 (ImageInfo::dx11_data_crc).
#pragma once
#include "resources.h"

namespace efvk {

// The DXGI format the game's DX11 build creates for this Vulkan format; 0 when not known.
uint32_t dxgi_format(VkFormat format);

// False when the hash cannot be computed: image content tracking off, a 3D or 1D image, an unknown
// format, or an upload the tracker could not read whole. An image never uploaded from a buffer has
// the hash of a texture without data.
bool dx11_texture_hash(const ImageInfo& image, uint32_t& hash);

} // namespace efvk
