// The merged skeleton's import on the GPU (D-117): the game's bone matrices of a drawn component
// are copied into the identity's merged skeleton by a compute dispatch before the frame's draws,
// as the Core's own importer does, instead of the CPU reading them from write-combined mapped
// memory (about 30 us per matched draw in the game) and copying the master into every recording.
//
// The dispatches belong to the recording whose draws read them: at the application's submission
// they go to the same queue, right before it. Nothing of this crosses the queues (D-091): no wait,
// no shared semaphore; each queue has a timeline of its own for the slots' reuse (BUG-050, BUG-052).
#pragma once

#include "state.h"
#include <memory>
#include <mutex>
#include <vector>

namespace efvk {

class BoneMerge;
struct BoneContext;

// A submission of ours as a ring slot or a command buffer remembers it: the queue it went to and
// the value it signals on that queue's timeline.
struct BoneSubmission { uint32_t queue = UINT32_MAX; uint64_t value = 0; };

// One identity's merged skeletons on the device: `slots` copies of the master layout (the implicit
// identity matrices, then two poses of `bones` bones), each written by one frame's dispatches and
// read by that frame's draws; a slot is reused once its recordings completed.
struct BoneRing {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint32_t bones = 0, slots = 0;
    VkDeviceSize slot_bytes = 0, slot_stride = 0;
    // A slot for a new frame: free of recordings, of jobs not yet submitted and of writes still
    // running on the device; UINT32_MAX when none is.
    uint32_t acquire();
    void hold(uint32_t slot);    // a recording bound the slot
    void release(uint32_t slot); // that recording completed
    void pend(uint32_t slot);    // a job for the slot was recorded
    void unpend(uint32_t slot);  // it was submitted or dropped
    ~BoneRing();
private:
    friend class BoneMerge;
    std::shared_ptr<BoneContext> ctx;
    std::mutex mx;
    std::vector<uint32_t> readers, pending;
    std::vector<std::vector<BoneSubmission>> written; // per slot, the last write on each queue
    bool initialized = false;                         // cleared and the implicit rows written, by the first submission
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

// One component's import: a dispatch in front of the recording that reads it.
struct BoneJob {
    std::shared_ptr<BoneRing> ring;
    uint32_t slot = 0;
    std::shared_ptr<BoneRemaps> remaps; // null without a remap
    uint32_t component = 0;
    VkBuffer source = VK_NULL_HANDLE;  // the game's bone buffer as bound at the draw
    uint32_t source_id = 0;            // the tracker's id of that buffer (a handle may be reused)
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
    // The jobs of the recordings an application submission on `queue` carries, dispatched there
    // first; each job's slot is unpended. The caller passes the queue's own submission thread.
    VkResult submit(VkQueue, std::vector<BoneJob>& jobs);
    // Components a submitted recording's draws read in a slot that none of its dispatches wrote (BUG-054).
    void note_missing(uint64_t components) { missing_.fetch_add(components, std::memory_order_relaxed); }
    void status(JsonWriter&);
private:
    struct Slot { VkCommandBuffer cb = VK_NULL_HANDLE; VkDescriptorPool pool = VK_NULL_HANDLE; BoneSubmission last; };
    bool make_pipeline();
    bool record(Slot&, const std::vector<BoneJob>& jobs, size_t begin, size_t end, std::string& why);
    void fail(const std::string& why);
    std::shared_ptr<BoneContext> ctx_;
    VkPhysicalDeviceMemoryProperties memory_;
    VkDeviceSize storage_alignment_ = 16;
    bool usable_ = false;
    std::string reason_;
    std::mutex mx_; // the submission path and the error text
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    std::vector<Slot> slots_;
    uint64_t submissions_total_ = 0;
    // Status.
    std::atomic<uint64_t> jobs_{0}, dispatches_{0}, submissions_{0}, rings_{0}, ring_bytes_{0}, slot_waits_{0}, failures_{0}, missing_{0};
    std::string error_;
};

} // namespace efvk
