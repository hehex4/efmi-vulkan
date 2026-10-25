// The merged skeleton's import on the GPU (D-117): the game's bone matrices of a drawn component
// are copied into the identity's merged skeleton by a compute dispatch before the frame's draws,
// as the Core's own importer does, instead of the CPU reading them from write-combined mapped
// memory (about 30 us per matched draw in the game) and copying the master into every recording.
#pragma once

#include "state.h"
#include <memory>
#include <mutex>
#include <vector>

namespace efvk {

class BoneMerge;
struct BoneContext;

// One identity's merged skeletons on the device: `slots` copies of the master layout (the implicit
// identity matrices, then two poses of `bones` bones), each written by one frame's dispatches and
// read by that frame's draws; a slot is reused once its recordings completed.
struct BoneRing {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint32_t bones = 0, slots = 0;
    VkDeviceSize slot_bytes = 0, slot_stride = 0;
    // A slot for a new frame: free of recordings, of queued writes and of writes still running on
    // the device; UINT32_MAX when none is.
    uint32_t acquire();
    void hold(uint32_t slot);    // a recording bound the slot
    void release(uint32_t slot); // that recording completed
    ~BoneRing();
private:
    friend class BoneMerge;
    std::shared_ptr<BoneContext> ctx;
    std::mutex mx;
    std::vector<uint32_t> readers, pending; // recordings bound; jobs queued and not yet submitted
    std::vector<uint64_t> written;          // the submission that last wrote the slot (0: none yet)
    bool initialized = false;               // cleared and the implicit rows written, by the first submission
};

// A package's LOD remap tables as the dispatches read them: every component's table in one buffer.
struct BoneRemaps {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    std::vector<uint32_t> offsets; // per component, in uints; UINT32_MAX for a component without one
    ~BoneRemaps();
private:
    friend class BoneMerge;
    std::shared_ptr<BoneContext> ctx;
};

// One component's import: the dispatch of a frame.
struct BoneJob {
    std::shared_ptr<BoneRing> ring;
    uint32_t slot = 0;
    std::shared_ptr<BoneRemaps> remaps; // null without a remap
    uint32_t component = 0;
    VkBuffer source = VK_NULL_HANDLE;  // the game's bone buffer as bound at the draw
    VkDeviceSize source_offset = 0, source_size = 0;
    uint32_t rows[2] = {0, 0};        // the entry's offsets, rows into the source range
    uint32_t first = 0, count = 0;    // the component's merged range
    uint32_t poses = 0;               // bit 0 current, bit 1 previous
    bool remap = false;
};

class BoneMerge {
public:
    BoneMerge(Device*, const VkPhysicalDeviceProperties&, const VkPhysicalDeviceMemoryProperties&);
    ~BoneMerge();
    bool usable() const { return usable_; }
    const std::string& reason() const { return reason_; }
    // A source range's offset must be a multiple of this to be bound to the dispatch.
    VkDeviceSize alignment() const { return storage_alignment_; }
    std::shared_ptr<BoneRing> ring(uint32_t bones, std::string& why);
    std::shared_ptr<BoneRemaps> remaps(const std::vector<std::vector<uint16_t>>& tables, std::string& why);
    void queue(BoneJob&&);
    bool queued() const { return queued_.load(std::memory_order_relaxed) != 0; }
    // Before an application submission on a graphics queue: every queued job dispatched on it, after
    // a wait for an earlier submission of ours on another queue that may still run (D-091: our
    // submissions wait on nothing else, so the wait cannot hold the queues against each other).
    VkResult submit(VkQueue);
    void status(JsonWriter&);
private:
    struct Slot { VkCommandBuffer cb = VK_NULL_HANDLE; VkDescriptorPool pool = VK_NULL_HANDLE; uint64_t value = 0; };
    bool make_pipeline();
    bool record(Slot&, std::vector<BoneJob>&, size_t begin, size_t end, std::string& why);
    void fail(const std::string& why);
    std::shared_ptr<BoneContext> ctx_;
    VkPhysicalDeviceMemoryProperties memory_;
    VkDeviceSize storage_alignment_ = 16;
    bool usable_ = false;
    std::string reason_;
    std::mutex mx_;
    std::vector<BoneJob> jobs_;
    std::atomic<uint32_t> queued_{0};
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    std::vector<Slot> slots_;
    VkQueue last_queue_ = VK_NULL_HANDLE;
    uint64_t last_value_ = 0;
    // Status.
    std::atomic<uint64_t> jobs_total_{0}, dispatches_{0}, submissions_{0}, rings_{0}, ring_bytes_{0}, queue_waits_{0}, slot_waits_{0}, failures_{0};
    std::string error_;
};

} // namespace efvk
