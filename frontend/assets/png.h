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
// WIC's PNG decoding and default sRGB interpretation, without a graphics API.
// No resize, premultiplication, orientation transform or generated mip chain.
// The caller retains encoded bytes until return. Output stays unchanged on error.
bool decode_png(const uint8_t*, size_t, PngPixels&, std::string& reason,
                std::shared_ptr<AllocationBudget> source_budget = {});
uint32_t png_pixel_bytes(VkFormat);
}
