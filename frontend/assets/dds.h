#pragma once
#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace efmi::assets {
struct DdsLevel {
    uint32_t width=0, height=0;
    size_t offset=0, bytes=0; // Into the original owned file bytes; no decompression.
};
struct DdsImage {
    VkFormat format=VK_FORMAT_UNDEFINED;
    uint32_t width=0, height=0;
    std::vector<DdsLevel> levels;
    // The file's layers (an array's slices, a cube's faces); `levels` are the first's (D-149).
    uint32_t layers=1;
    bool cube=false;
};
// Bounded 2D DDS, an array or cube of them too: the first layer's levels, which is what a replaced
// draw's 2D slot takes (D-149). Preserves linear/sRGB formats and BC blocks.
// Output stays unchanged on error; no file IO, GPU calls or payload copies.
bool decode_dds(const uint8_t* data,size_t size,DdsImage& output,std::string& reason);
// The same from the file's first bytes only (`available` of them, at least kDdsHeaderBytes or the whole
// file) and its size: a loader reads the payload elsewhere (D-130).
constexpr size_t kDdsHeaderBytes=148;
bool decode_dds_header(const uint8_t* header,size_t available,uint64_t file_size,DdsImage& output,std::string& reason);
// The Vulkan format of a DXGI format the image loaders make (DDS levels, WIC pixels); VK_FORMAT_UNDEFINED
// for any other, typeless ones included.
VkFormat image_vk_format(uint32_t dxgi);
// DirectXTK's MakeSRGB, the DDS loader's forced sRGB (D-140): the format's sRGB twin, else the format.
VkFormat srgb_format(VkFormat);
}
