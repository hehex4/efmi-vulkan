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
};
// Bounded 2D, single-layer DDS. Preserves linear/sRGB formats and BC blocks.
// Output stays unchanged on error; no file IO, GPU calls or payload copies.
bool decode_dds(const uint8_t* data,size_t size,DdsImage& output,std::string& reason);
}
