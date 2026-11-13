#pragma once
#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <tuple>

namespace efvk {
// Recording-local facts, never a prediction of global queue execution order.
class ImageLayouts {
public:
    static constexpr size_t limit = 16384;
    void reset() { cells_.clear(); exhausted_ = false; }
    void set(uint32_t id, uint32_t levels, uint32_t layers, VkImageSubresourceRange range, VkImageLayout layout) {
        if (exhausted_) return;
        if (!id || !normalize(levels, layers, range)) { overflow(); return; }
        visit(id, range, [&](const Key& key) {
            if (exhausted_) return;
            if (cells_.size() == limit && !cells_.count(key)) { overflow(); return; }
            cells_[key] = layout;
        });
    }
    void barrier(uint32_t id, uint32_t levels, uint32_t layers, VkImageSubresourceRange range,
                 VkImageLayout before, VkImageLayout after, uint32_t source_family, uint32_t destination_family,
                 bool synchronization2, bool unsupported = false) {
        if (unsupported || source_family != destination_family) {
            set(id, levels, layers, range, VK_IMAGE_LAYOUT_MAX_ENUM); return;
        }
        // This applies to legacy barriers too when the synchronization2 feature is enabled.
        if (synchronization2 && before == after) return;
        VkImageLayout known;
        if (before != VK_IMAGE_LAYOUT_UNDEFINED && get(id, levels, layers, range, known) && known != before)
            after = VK_IMAGE_LAYOUT_MAX_ENUM;
        set(id, levels, layers, range, after);
    }
    void merge(const ImageLayouts& source) {
        if (exhausted_) return;
        if (source.exhausted_) { overflow(); return; }
        for (const auto& cell : source.cells_) {
            if (cells_.size() == limit && !cells_.count(cell.first)) { overflow(); return; }
            cells_[cell.first] = cell.second;
        }
    }
    bool get(uint32_t id, uint32_t levels, uint32_t layers, VkImageSubresourceRange range, VkImageLayout& layout) const {
        if (exhausted_ || !id || !normalize(levels, layers, range)) return false;
        bool valid = true, first = true;
        visit(id, range, [&](const Key& key) {
            if (!valid) return;
            const auto cell = cells_.find(key);
            if (cell == cells_.end() || cell->second == VK_IMAGE_LAYOUT_MAX_ENUM) { valid = false; return; }
            if (first) { layout = cell->second; first = false; }
            else if (layout != cell->second) valid = false;
        });
        return valid && !first;
    }
    bool exhausted() const { return exhausted_; }
    void invalidate_all() { overflow(); }
private:
    using Key = std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>;
    std::map<Key, VkImageLayout> cells_;
    bool exhausted_ = false;
    void overflow() { cells_.clear(); exhausted_ = true; }
    static bool normalize(uint32_t levels, uint32_t layers, VkImageSubresourceRange& range) {
        if (!range.aspectMask || range.baseMipLevel >= levels || range.baseArrayLayer >= layers) return false;
        if (range.levelCount == VK_REMAINING_MIP_LEVELS) range.levelCount = levels - range.baseMipLevel;
        if (range.layerCount == VK_REMAINING_ARRAY_LAYERS) range.layerCount = layers - range.baseArrayLayer;
        if (!range.levelCount || range.levelCount > levels - range.baseMipLevel ||
            !range.layerCount || range.layerCount > layers - range.baseArrayLayer) return false;
        uint32_t aspects = 0;
        for (uint32_t bits = range.aspectMask; bits; bits &= bits - 1) ++aspects;
        return uint64_t(range.levelCount) * range.layerCount <= limit / aspects;
    }
    template<class F> static void visit(uint32_t id, const VkImageSubresourceRange& range, F visitor) {
        for (uint32_t bit = 0; bit < 32; ++bit) if (range.aspectMask & (1u << bit))
            for (uint32_t mip = 0; mip < range.levelCount; ++mip)
                for (uint32_t layer = 0; layer < range.layerCount; ++layer)
                    visitor(Key{id, 1u << bit, range.baseMipLevel + mip, range.baseArrayLayer + layer});
    }
};
struct Device;
struct CmdBuf;
void on_image_barriers(Device*, CmdBuf*, uint32_t, const VkImageMemoryBarrier*);
void on_image_barriers(Device*, CmdBuf*, const VkDependencyInfo*, bool event_source = false);
void on_image_pass(Device*, CmdBuf*, bool ending = false);
}
