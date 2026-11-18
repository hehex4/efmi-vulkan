#include "texture_identity.h"
#include <cstring>
#include <dxgiformat.h>

namespace efvk {

uint32_t dxgi_format(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R32G32B32A32_SFLOAT: return 2;
    case VK_FORMAT_R32G32B32A32_UINT: return 3;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return 10;
    case VK_FORMAT_R16G16B16A16_UNORM: return 11;
    case VK_FORMAT_R16G16B16A16_UINT: return 12;
    case VK_FORMAT_R16G16B16A16_SNORM: return 13;
    case VK_FORMAT_R32G32_SFLOAT: return 16;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return 24;
    case VK_FORMAT_A2B10G10R10_UINT_PACK32: return 25;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return 26;
    case VK_FORMAT_R8G8B8A8_UNORM: return 28;
    case VK_FORMAT_R8G8B8A8_SRGB: return 29;
    case VK_FORMAT_R8G8B8A8_UINT: return 30;
    case VK_FORMAT_R8G8B8A8_SNORM: return 31;
    case VK_FORMAT_R16G16_SFLOAT: return 34;
    case VK_FORMAT_R16G16_UNORM: return 35;
    case VK_FORMAT_R32_SFLOAT: return 41;
    case VK_FORMAT_R32_UINT: return 42;
    case VK_FORMAT_R8G8_UNORM: return 49;
    case VK_FORMAT_R16_SFLOAT: return 54;
    case VK_FORMAT_R16_UNORM: return 56;
    case VK_FORMAT_R8_UNORM: return 61;
    case VK_FORMAT_R8_UINT: return 62;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return 71;
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: case VK_FORMAT_BC1_RGB_SRGB_BLOCK: return 72;
    case VK_FORMAT_BC2_UNORM_BLOCK: return 74;
    case VK_FORMAT_BC2_SRGB_BLOCK: return 75;
    case VK_FORMAT_BC3_UNORM_BLOCK: return 77;
    case VK_FORMAT_BC3_SRGB_BLOCK: return 78;
    case VK_FORMAT_BC4_UNORM_BLOCK: return 80;
    case VK_FORMAT_BC4_SNORM_BLOCK: return 81;
    case VK_FORMAT_BC5_UNORM_BLOCK: return 83;
    case VK_FORMAT_BC5_SNORM_BLOCK: return 84;
    case VK_FORMAT_B5G6R5_UNORM_PACK16: return 85;
    case VK_FORMAT_B8G8R8A8_UNORM: return 87;
    case VK_FORMAT_B8G8R8A8_SRGB: return 91;
    case VK_FORMAT_BC6H_UFLOAT_BLOCK: return 95;
    case VK_FORMAT_BC6H_SFLOAT_BLOCK: return 96;
    case VK_FORMAT_BC7_UNORM_BLOCK: return 98;
    case VK_FORMAT_BC7_SRGB_BLOCK: return 99;
    default: return 0;
    }
}

namespace {
struct DepthFormats {
    DXGI_FORMAT family, target, depth, stencil;
};

DepthFormats depth_formats(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_D16_UNORM:
        return {DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_UNKNOWN};
    case VK_FORMAT_D32_SFLOAT:
        return {DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_UNKNOWN};
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return {DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT,
                DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_X24_TYPELESS_G8_UINT};
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return {DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
                DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT};
    default: return {};
    }
}
}

uint32_t dxgi_application_format(VkFormat format, VkImageUsageFlags usage)
{
    const auto depth = depth_formats(format);
    if (!depth.target) return dxgi_format(format);
    return (usage & VK_IMAGE_USAGE_SAMPLED_BIT) ? depth.family : depth.target;
}

uint32_t dxgi_depth_target_format(VkFormat format)
{
    return depth_formats(format).target;
}

uint32_t dxgi_view_format(VkFormat format, VkImageAspectFlags aspect)
{
    if (aspect == VK_IMAGE_ASPECT_COLOR_BIT) return dxgi_format(format);
    const auto depth = depth_formats(format);
    if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT) return depth.depth;
    if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT) return depth.stencil;
    return DXGI_FORMAT_UNKNOWN;
}

bool dx11_texture_desc(const ImageInfo& image, uint32_t desc[11])
{
    if (image.type != VK_IMAGE_TYPE_2D) return false;
    const uint32_t format = dxgi_format(image.format);
    if (!format) return false;
    // The DX11 description of the same texture: default usage, no CPU access, bind flags from what the
    // Vulkan image is used for (a game asset: shader resource only), a cube map flagged as such.
    uint32_t bind = 0;
    if (image.usage & VK_IMAGE_USAGE_SAMPLED_BIT) bind |= 0x8;                    // D3D11_BIND_SHADER_RESOURCE
    if (image.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) bind |= 0x20;          // D3D11_BIND_RENDER_TARGET
    if (image.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) bind |= 0x40;  // D3D11_BIND_DEPTH_STENCIL
    if (image.usage & VK_IMAGE_USAGE_STORAGE_BIT) bind |= 0x80;                   // D3D11_BIND_UNORDERED_ACCESS
    const uint32_t misc = (image.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) ? 0x4u : 0u; // D3D11_RESOURCE_MISC_TEXTURECUBE
    // D3D11_TEXTURE2D_DESC: Width, Height, MipLevels, ArraySize, Format, SampleDesc {Count, Quality},
    // Usage, BindFlags, CPUAccessFlags, MiscFlags.
    const uint32_t fields[11] = {image.extent.width, image.extent.height, image.levels, image.layers, format,
                                 image.samples ? image.samples : 1u, 0, 0, bind, 0, misc};
    memcpy(desc, fields, sizeof(fields));
    return true;
}

bool dx11_texture_hash(const ImageInfo& image, uint32_t& hash)
{
    // Without content tracking no upload is seen, and an asset would look like a texture without data.
    if (!g_cfg.image_hash || !g_cfg.track_resources) return false;
    uint32_t desc[11];
    if (!dx11_texture_desc(image, desc)) return false;
    uint32_t seed = 0;
    if (image.content == IMG_HASHED) seed = image.dx11_data_crc;
    else if (image.content != IMG_NONE) return false; // a partial, unreadable or oversized upload
    hash = crc32c(seed, desc, sizeof(desc));
    return true;
}

} // namespace efvk
