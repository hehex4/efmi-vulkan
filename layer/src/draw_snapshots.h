// Command-recording-owned, immutable-after-seal private VS buffer slices.
#pragma once
#include "pipeline_variant.h"
#include "merged_skeleton.h"

namespace efvk {

#define EFVK_SNAPSHOT_FUNCS(X) \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) \
    X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CreateDescriptorPool) X(DestroyDescriptorPool) \
    X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout)
struct SnapshotDispatch {
#define X(n) PFN_vk##n n = nullptr;
    EFVK_SNAPSHOT_FUNCS(X)
#undef X
};
struct SnapshotSlot { VkDescriptorSet set = VK_NULL_HANDLE; uint32_t offset = 0; };
// First memory type of `type_bits` the host can map coherently; with `device_local`, only one that
// also lives on the device. UINT32_MAX when there is none.
uint32_t host_memory_type(const VkPhysicalDeviceMemoryProperties&, uint32_t type_bits, bool device_local);
struct SnapshotImage {
    VkImageView view = VK_NULL_HANDLE;
    // Caller supplies a ready float-sampled 2D view in SHADER_READ_ONLY_OPTIMAL,
    // synchronized with this graphics queue, and its retained resource owner.
    std::shared_ptr<const void> owner;
};

class DrawSnapshots {
public:
    DrawSnapshots(VkDevice device, const SnapshotDispatch& dispatch) : device_(device), d_(dispatch) {}
    ~DrawSnapshots();
    DrawSnapshots(const DrawSnapshots&) = delete;
    DrawSnapshots& operator=(const DrawSnapshots&) = delete;
    // Allocate off the draw path. Uses host-coherent memory only, failing closed if unavailable.
    // Every entry has the full declared uniform range, including the last dynamic offset.
    bool create(const VkPhysicalDeviceMemoryProperties&, const VkPhysicalDeviceLimits&,
                uint32_t capacity, uint32_t bone_bytes, uint32_t uniform_range = 65536, uint32_t image_count = 0);
    // Storage in which consecutive draws that read the same bones share one copy of them: `slices`
    // bone copies for `entries` draws. Prefers device memory the host can write. No images.
    bool create_shared(const VkPhysicalDeviceMemoryProperties&, const VkPhysicalDeviceLimits&,
                       uint32_t entries, uint32_t slices, uint32_t bone_bytes, uint32_t uniform_range = 65536);
    // `key` names the bone bytes: an append with the key of the previous one reuses its copy.
    // The caller changes the key whenever the bytes change. Shared storage only.
    bool append_shared(const uint8_t* bones, size_t size, uint64_t key, const InstanceEntry&, SnapshotSlot&);
    uint32_t slices_used() const { return slices_used_; }
    bool append(const std::vector<uint8_t>& bones, const InstanceEntry&, SnapshotSlot&);
    bool append(const std::vector<uint8_t>& bones, const InstanceEntry&, const std::vector<SnapshotImage>&, SnapshotSlot&);
    void seal() { sealed_ = true; }
    bool sealed() const { return sealed_; }
    uint32_t used() const { return used_; }
    VkDeviceSize size() const { return bytes_; }
    std::string reason;
    // No reset/reuse method: recorded and resubmitted command buffers retain this object until
    // reset/free after GPU completion. The caller releases all owners before device destruction.
private:
    friend struct M2Command;
    // Only a command owner that has completed a successful legal reset/begin may recycle it.
    void recycle_completed() { for(auto& owner:image_owners_)owner.reset(); used_ = 0; sealed_ = false; slices_used_ = 0; have_key_ = false; }
    void clear();
    bool create_storage(const VkPhysicalDeviceMemoryProperties&, const VkPhysicalDeviceLimits&, uint32_t capacity,
                        uint32_t slices, uint32_t bone_bytes, uint32_t uniform_range, uint32_t image_count, bool shared);
    VkDevice device_;
    SnapshotDispatch d_;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    uint8_t* mapped_ = nullptr;
    std::vector<VkDescriptorSet> sets_;
    std::vector<std::shared_ptr<const void>> image_owners_;
    VkDeviceSize bytes_ = 0, bone_stride_ = 0, entry_base_ = 0, entry_stride_ = 0;
    uint32_t capacity_ = 0, bone_bytes_ = 0, used_ = 0, image_count_ = 0;
    uint32_t slice_capacity_ = 0, slices_used_ = 0, last_slice_ = 0;
    uint64_t last_key_ = 0;
    bool sealed_ = false, shared_ = false, have_key_ = false;
};

} // namespace efvk
