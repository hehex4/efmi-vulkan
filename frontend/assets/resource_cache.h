#pragma once
#include "buffer_upload.h"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

namespace efmi::assets {
enum class ResourceState { Dormant, Queued, Loading, Prepared, Submitting, Submitted, Ready, Failed, Stopped };
struct ResourceSnapshot {
    ResourceState state=ResourceState::Dormant;
    std::shared_ptr<const ImageUpload> image;
    std::shared_ptr<const BufferUpload> buffer;
    std::string reason;
};
class ResourceRequest {
public:
    ResourceSnapshot snapshot() const;
private:
    friend class ResourceCache;
    std::filesystem::path path_;
    VkBufferUsageFlags buffer_usage_=0; // Zero selects the image decoder.
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
};
struct ResourceBudgets {
    std::shared_ptr<AllocationBudget> resident=std::make_shared<AllocationBudget>(512ull<<20);
    std::shared_ptr<AllocationBudget> staging=std::make_shared<AllocationBudget>(160ull<<20);
    std::shared_ptr<AllocationBudget> source=std::make_shared<AllocationBudget>(128ull<<20);
};
struct ResourceCacheStats { uint64_t file_reads=0,submissions=0,ready=0,failed=0; };

// One frontend generation, one device and one graphics queue. Workers read files,
// decode/allocate/record, poll fences and release staging. They never submit.
// Copy the same ResourceBudgets into overlapping generations to bound retained
// GPU allocations for both images and mesh buffers. Device and its dispatch
// must outlive all returned owners; one worker/one outstanding upload serves both.
class ResourceCache {
public:
    ResourceCache(const ResourceDevice&,ResourceBudgets={},size_t capacity=256);
    ~ResourceCache();
    ResourceCache(const ResourceCache&)=delete;
    ResourceCache& operator=(const ResourceCache&)=delete;
    // Call during frontend setup. Lexical normalization only: no file IO, no
    // eager load. Relative paths refuse. Equal normalized paths share a handle.
    ResourceHandle declare_image(const std::filesystem::path&,std::string& reason);
    // Equal path + usage share one immutable buffer. A different usage or an
    // image declaration is a distinct request, never an unsafe type alias.
    ResourceHandle declare_buffer(const std::filesystem::path&,VkBufferUsageFlags,std::string& reason);
    // Safe for an already resolved draw binding: enqueue once, never wait for
    // file IO/driver/fence. Failure is sticky until a new generation is made.
    bool demand(const ResourceHandle&);
    // Caller holds its ordinary queue synchronization and always uses the same
    // queue. At most one already-recorded image or buffer upload per call, with no file IO,
    // allocation, recording, fence polling or waiting. False also means no work.
    bool submit_prepared(VkQueue);
    ResourceCacheStats stats() const;
    // Off render path; prevents new demands, joins worker and drains its one
    // outstanding upload. Previously obtained Ready owners remain usable.
    void stop();
private:
    struct Upload;
    using Key=std::pair<std::filesystem::path,VkBufferUsageFlags>;
    ResourceHandle declare(const std::filesystem::path&,VkBufferUsageFlags,std::string&);
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
    std::thread worker_;
};
}
