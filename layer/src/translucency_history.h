#pragma once
#include <array>
#include <cstdint>

namespace efvk {
// EFMITTL SelectLayer creates all targets only while FrontColor is null. A null
// source DSV leaves a partial layer until ReleaseLayers on a capture-size change.
// Keep this policy apart from the evictable Vulkan allocation cache.
class TranslucencyHistory {
public:
    void begin(uint32_t width, uint32_t height) {
        if (width != width_ || height != height_) {
            width_ = width; height_ = height; layers_.fill(Empty);
        }
        used_ = 0;
    }
    bool capture(bool depth) {
        auto& layer = layers_[used_ < 4 ? used_ : 3];
        if (layer == Empty) layer = depth ? Complete : ColorOnly;
        if (layer != Complete) return false;
        if (used_ < 4) ++used_;
        return true;
    }
private:
    enum Layer : uint8_t { Empty, ColorOnly, Complete };
    std::array<Layer, 4> layers_{};
    uint32_t width_ = 0, height_ = 0, used_ = 0;
};
}
