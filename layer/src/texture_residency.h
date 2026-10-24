// Device-wide residency of the character Mods' textures (D-094): an image is loaded on its first
// bind, as 3DMigoto substantiates a resource on first use; when the images hold more than the
// device's free memory allows, the ones no frame has bound for a while are evicted, least recently
// bound first, and loaded again on their next bind, as the DX11 runtime pages a game's textures;
// what the frames keep binding stays, and a working set the target does not hold shrinks by large
// textures loading without their top mip level (D-097). One timeline semaphore publishes every
// upload to every graphics queue (D-095). One object per device; every character generation declares its images into it
// under a tag of its own, so a reload reads its files again while the live generation keeps its own.
#pragma once

#include "state.h"
#include "../../frontend/assets/image_upload.h"
#include "../../frontend/assets/resource_cache.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace efvk {
class JsonWriter;

class TextureResidency {
public:
    using ImageId = uint32_t;
    static constexpr ImageId kNone = UINT32_MAX;
    TextureResidency(Device*, const VkPhysicalDeviceMemoryProperties&);
    ~TextureResidency();
    TextureResidency(const TextureResidency&) = delete;
    TextureResidency& operator=(const TextureResidency&) = delete;
    bool usable() const { return usable_; }
    const std::string& reason() const { return reason_; }
    // Declaration only, no file IO; equal paths under one tag share an image. kNone with the reason.
    ImageId declare(const std::string& utf8_path, uint64_t tag, std::string& reason);
    struct Binding {
        VkImageView view = VK_NULL_HANDLE;                      // null: not resident yet, or failed
        std::shared_ptr<const efmi::assets::ImageUpload> owner; // keeps the image alive while a recording lives
        bool failed = false;
        std::string reason;
    };
    // Queue all selected images before waiting on the owned queue. One bounded deadline per
    // transaction; only Ready images whose timeline publication completed can be returned.
    // Without an independent upload queue, unresolved bindings stay null for later submissions.
    void bind(const std::vector<ImageId>&, std::vector<Binding>&);
    // The images a package's frame needs, counted as bound now: what is not resident is demanded,
    // and true only when every one is published (D-097).
    bool ensure(const std::vector<ImageId>&);
    // Every image of a package demanded at once (m2_texture_preload), within the budget.
    void preload(const std::vector<ImageId>&);
    // The images of `ids` whose path a resident image of `live` has: a reload candidate brings up
    // what the live generation shows, so the exchange leaves no draw to the game.
    void preload_matching(const std::vector<ImageId>& ids, const std::vector<ImageId>& live);
    // A generation done with its images: their GPU objects live on in the recordings that hold them.
    void forget(const std::vector<ImageId>&);
    bool resident(ImageId) const;
    bool failed(ImageId) const;
    uint64_t file_bytes(ImageId) const;
    // Of these images: how many are demanded and not Ready yet, and whether any failed.
    uint64_t loading(const std::vector<ImageId>&) const;
    bool any_failed(const std::vector<ImageId>&) const;
    // Before an application submission on a graphics queue of the material family: one prepared
    // upload goes to that queue, and the queue waits for what is published (an already satisfied wait).
    VkResult before_submit(VkQueue);
    // Once per frame: the device's free memory sampled, images over the target evicted.
    void frame();
    // Device-local bytes packages are bringing up right now (their meshes), for the activation gate.
    std::atomic<uint64_t> activating_bytes{0};
    void stop();
    void status(JsonWriter&) const;

private:
    struct Image {
        efmi::assets::ResourceHandle handle;
        std::string path;         // normalized, for matching across generations
        uint64_t tag = 0;
        uint64_t file_bytes = 0;
        uint64_t charged = 0;     // bytes counted while resident: the file's, a quarter per level left out
        uint64_t last_bound = 0;  // frame of the last bind
        bool resident = false;    // demanded and not released: loading or Ready
        bool reduce_next = false; // the next load leaves the top level out (demoted under pressure)
        bool failure_logged = false;
        uint32_t skip_levels = 0; // mip levels the current load leaves out
    };
    bool fits(uint64_t bytes) const { return resident_bytes_ + bytes <= target_bytes_; }
    // The cap and half the reserve are the limits a load never crosses; the target is what the set
    // comes back under through idle eviction and demotion.
    bool loadable(uint64_t bytes) const;
    // The device's free memory as sampled, less what was loaded since.
    uint64_t free_now() const { return free_bytes_ > loaded_since_sample_ ? free_bytes_ - loaded_since_sample_ : 0; }
    bool large(const Image&) const;
    void demand(Image&);
    // Releases Ready images not bound for `idle_frames`, least recently bound first, until the
    // resident bytes plus `bytes` are within `limit` or none is left.
    void evict_idle(uint64_t bytes, uint64_t idle_frames, uint64_t limit);
    bool demote_one();
    void release(Image&);
    void sample_budget();
    bool queue_known(VkQueue) const;

    Device* device_;
    VkPhysicalDeviceMemoryProperties memory_;
    std::unique_ptr<efmi::assets::ResourceCache> cache_;
    std::map<const efmi::assets::ResourceRequest*, ImageId> by_handle_;
    std::vector<Image> images_;
    mutable std::mutex mutex_;
    bool usable_ = false, stopped_ = false;
    std::string reason_;
    // Publication: the timeline semaphore every upload signals with its serial; the counter value
    // read at each submission is what is published; per queue the value it waited for.
    VkSemaphore timeline_ = VK_NULL_HANDLE;
    PFN_vkDestroySemaphore DestroySemaphore_ = nullptr;
    PFN_vkGetSemaphoreCounterValue GetSemaphoreCounterValue_ = nullptr;
    std::atomic<uint64_t> signalled_{0}; // the serial the last upload took (the loaders take the next, D-103)
    uint64_t published_ = 0;
    std::map<VkQueue, uint64_t> waited_;
    // Budget: the cap (m2_texture_budget_mb or half the heap), the reserve kept free for the game
    // (m2_reserve_mb), and the target the device's free memory allows under them.
    uint64_t heap_bytes_ = 0, cap_bytes_ = 0, reserve_bytes_ = 0, target_bytes_ = 0, free_bytes_ = 0;
    uint64_t resident_bytes_ = 0; // charged bytes of the images demanded and not released
    uint64_t loaded_since_sample_ = 0;
    uint64_t frame_ = 0, next_sample_ = 0, demoted_frame_ = 0;
    bool budget_known_ = false, pressure_ = false;
    uint64_t loads_ = 0, reduced_ = 0, evictions_ = 0, demotions_ = 0, budget_waits_ = 0, pending_ = 0, uploads_ = 0, waits_ = 0;
    uint64_t waits_sync_ = 0, wait_sync_us_ = 0, wait_sync_max_us_ = 0, waits_timed_out_ = 0; // draws that waited for a texture (D-103)
    uint64_t wait_images_ = 0; // images across the waiting draw batches (duplicates included)
};

} // namespace efvk
