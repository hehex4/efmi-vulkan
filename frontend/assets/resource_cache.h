#pragma once
#include "buffer_upload.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

namespace efmi::assets {
enum class ResourceState { Dormant, Queued, Loading, Prepared, Submitting, Submitted, Ready, Failed, Stopped };
struct ResourceSnapshot {
    ResourceState state=ResourceState::Dormant;
    std::shared_ptr<const ImageUpload> image;
    std::shared_ptr<const BufferUpload> buffer;
    std::string reason;
    uint64_t serial=0; // the timeline value the upload signals; 0 when it signals none
};
class ResourceRequest {
public:
    ResourceSnapshot snapshot() const;
private:
    friend class ResourceCache;
    std::filesystem::path path_;
    VkBufferUsageFlags buffer_usage_=0; // Zero selects the image decoder.
    uint32_t skip_levels_=0;            // Image levels left out of the current load.
    uint64_t busy_since_us_=0;          // When the load first waited for a budget other loads hold (D-103); 0 when not waiting.
    std::weak_ptr<const uint8_t> owner_;
    mutable std::mutex mutex_;
    ResourceSnapshot value_;
};
using ResourceHandle=std::shared_ptr<ResourceRequest>;
struct ResourceDevice {
    VkDevice device=VK_NULL_HANDLE;
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    uint32_t graphics_family=0;
    bool bc_enabled=false;
    VkPhysicalDeviceMemoryProperties memory{};
    UploadDispatch dispatch;
    BufferUploadDispatch buffer_dispatch;
    PFN_vkGetPhysicalDeviceFormatProperties format_properties=nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties image_properties=nullptr;
    float memory_priority=-1.0f; // VK_EXT_memory_priority of image memory; negative for none
    // The layer's upload queue and its mutex (D-103). Null: uploads wait for submit_prepared on an
    // application queue. Images and buffers are shared between the two families.
    VkQueue upload_queue=VK_NULL_HANDLE;
    uint32_t upload_family=UINT32_MAX;
    std::mutex* upload_mutex=nullptr;
    // Publication of the uploads the workers submit: the timeline signalled and the serial taken.
    VkSemaphore timeline=VK_NULL_HANDLE;
    std::atomic<uint64_t>* serial=nullptr;
};
struct ResourceBudgets {
    std::shared_ptr<AllocationBudget> resident=std::make_shared<AllocationBudget>(512ull<<20);
    std::shared_ptr<AllocationBudget> staging=std::make_shared<AllocationBudget>(160ull<<20);
    std::shared_ptr<AllocationBudget> source=std::make_shared<AllocationBudget>(128ull<<20);
};
struct ResourceCacheStats { uint64_t file_reads=0,submissions=0,ready=0,failed=0,releases=0; };

// One frontend generation, one device and one graphics queue. Workers read files,
// decode/allocate/record, poll fences and release staging. They never submit.
// Copy the same ResourceBudgets into overlapping generations to bound retained
// GPU allocations for both images and mesh buffers. Device and its dispatch
// must outlive all returned owners; one worker/one outstanding upload serves both.
class ResourceCache {
public:
    // `workers`: loaders running at once; one unless the device has an upload queue (D-103).
    ResourceCache(const ResourceDevice&,ResourceBudgets={},size_t capacity=256,unsigned workers=1);
    ~ResourceCache();
    ResourceCache(const ResourceCache&)=delete;
    ResourceCache& operator=(const ResourceCache&)=delete;
    // Call during frontend setup. Lexical normalization only: no file IO, no
    // eager load. Relative paths refuse. Equal normalized paths share a handle.
    // `tag` tells declarations of one path apart (a generation's own copy); equal path and tag share.
    ResourceHandle declare_image(const std::filesystem::path&,std::string& reason,uint64_t tag=0);
    // Equal path + usage share one immutable buffer. A different usage or an
    // image declaration is a distinct request, never an unsafe type alias.
    ResourceHandle declare_buffer(const std::filesystem::path&,VkBufferUsageFlags,std::string& reason);
    // Safe for an already resolved draw binding: enqueue once, never wait for
    // file IO/driver/fence. Failure is sticky until a new generation is made.
    bool demand(const ResourceHandle&,uint32_t skip_levels=0);
    // A Ready or Queued resource back to Dormant: its GPU object lives on in the owners that hold it
    // and is freed with the last of them; the next demand loads it again. False when it is loading.
    bool release(const ResourceHandle&);
    // A queued declaration a draw is waiting for goes to the front of the queue. Same guarantees
    // as demand; false when it is not queued (loading, done, failed or never demanded).
    bool prioritize(const ResourceHandle&);
    // Caller holds its ordinary queue synchronization and always uses the same
    // queue. At most one already-recorded image or buffer upload per call, with no file IO,
    // allocation, recording, fence polling or waiting. False also means no work.
    bool submit_prepared(VkQueue,VkSemaphore timeline=VK_NULL_HANDLE,uint64_t value=0);
    ResourceCacheStats stats() const;
    // Off render path; prevents new demands, joins worker and drains its one
    // outstanding upload. Previously obtained Ready owners remain usable.
    void stop();
private:
    struct Upload;
    using Key=std::tuple<std::filesystem::path,VkBufferUsageFlags,uint64_t>;
    ResourceHandle declare(const std::filesystem::path&,VkBufferUsageFlags,std::string&,uint64_t tag=0);
    void run();
    std::shared_ptr<Upload> prepare(const ResourceHandle&);
    void publish(const ResourceHandle&,ResourceState,std::shared_ptr<Upload> upload={},std::string reason={});
    ResourceDevice device_;
    ResourceBudgets budgets_;
    const size_t capacity_;
    std::shared_ptr<const uint8_t> identity_=std::make_shared<const uint8_t>(0);
    mutable std::mutex mutex_;
    std::mutex service_mutex_;
    std::condition_variable wake_;
    bool stopped_=false;
    VkQueue submission_queue_=VK_NULL_HANDLE;
    std::map<Key,ResourceHandle> entries_;
    std::deque<ResourceHandle> queue_;
    ResourceHandle pending_;
    std::shared_ptr<Upload> upload_;
    ResourceState phase_=ResourceState::Dormant;
    ResourceCacheStats stats_;
    std::vector<std::thread> workers_;
};
}
