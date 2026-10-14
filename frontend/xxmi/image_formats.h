#pragma once
#include "image_rules.h"
#include <vulkan/vulkan.h>
#include <array>
namespace efmi::frontend {
struct SourceImageFormat {
    VkFormat format=VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspects=VK_IMAGE_ASPECT_COLOR_BIT;
    std::array<UINT,4> bits{},offsets{};
    bool signed_integer=false;
};
SourceImageFormat source_image_format(DXGI_FORMAT);
DXGI_FORMAT source_image_family(DXGI_FORMAT);
}
