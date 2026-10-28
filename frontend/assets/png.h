#pragma once
#include "dds.h"
#include "allocation_budget.h"
#include <memory>

namespace efmi::assets {
// One decoded plane. The reservation is destroyed after the pixel storage.
// Move-only so a copied vector cannot silently exceed the shared source budget.
struct PngPixels {
    std::shared_ptr<void> reservation;
    std::vector<uint8_t> bytes;
    DdsImage metadata;
    PngPixels() = default;
    PngPixels(PngPixels&&) = default;
    PngPixels& operator=(PngPixels&& other) noexcept
    {
        reservation.swap(other.reservation); bytes.swap(other.bytes);
        std::swap(metadata, other.metadata); return *this;
    }
    PngPixels(const PngPixels&) = delete;
    PngPixels& operator=(const PngPixels&) = delete;
};
// The pinned DirectXTK WIC_LOADER_FLAGS this decoder honours; the original passes a
// declaration's color_space (CustomColorSpace) as these values.
constexpr uint32_t png_force_srgb = 0x1, png_ignore_srgb = 0x2;
// WIC's PNG decoding and the pinned DirectXTK loader's sRGB interpretation, without a
// graphics API. No resize, premultiplication, orientation transform or generated mip chain.
// The caller retains encoded bytes until return. Output stays unchanged on error.
bool decode_png(const uint8_t*, size_t, PngPixels&, std::string& reason,
                std::shared_ptr<AllocationBudget> source_budget = {}, uint32_t loader_flags = 0);
// Any container the system's WIC decodes (PNG, JPEG, BMP, TIFF, GIF ...), as the original loads every file
// not named .dds (CustomResource::LoadFromFile, the pinned DirectXTK CreateWICTextureFromFileEx): its frame
// 0, the same format table, the loader's sRGB rule (PNG: the sRGB chunk; another container: its
// System.Image.ColorSpace 1). With `pixels` false only the metadata is made: whether and as what it loads.
bool decode_wic(const uint8_t*, size_t, PngPixels&, std::string& reason,
                std::shared_ptr<AllocationBudget> source_budget = {}, uint32_t loader_flags = 0, bool pixels = true);
uint32_t png_pixel_bytes(VkFormat);
}
