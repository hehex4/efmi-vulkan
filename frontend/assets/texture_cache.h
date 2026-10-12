#pragma once
#include "image_upload.h"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

namespace efmi::assets {
enum class TextureState { Dormant, Queued, Loading, Prepared, Submitting, Submitted, Ready, Failed, Stopped };
struct TextureSnapshot {
    TextureState state=TextureState::Dormant;
    std::shared_ptr<const ImageUpload> image;
    std::string reason;
};
class TextureRequest {
public:
    TextureSnapshot snapshot() const;
private:
    friend class TextureCache;
    std::filesystem::path path_;
    std::weak_ptr<const uint8_t> owner_;
    mutable std::mutex mutex_;
    TextureSnapshot value_;
};
using TextureHandle=std::shared_ptr<TextureRequest>;
struct TextureDevice {
    VkDevice device=VK_NULL_HANDLE;
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    uint32_t graphics_family=0;
    bool bc_enabled=false;
    VkPhysicalDeviceMemoryProperties memory{};
    UploadDispatch dispatch;
    PFN_vkGetPhysicalDeviceFormatProperties format_properties=nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties image_properties=nullptr;
};
struct TextureBudgets {
    std::shared_ptr<AllocationBudget> resident=std::make_shared<AllocationBudget>(512ull<<20);
    std::shared_ptr<AllocationBudget> staging=std::make_shared<AllocationBudget>(160ull<<20);
    std::shared_ptr<AllocationBudget> source=std::make_shared<AllocationBudget>(128ull<<20);
};
struct TextureCacheStats { uint64_t file_reads=0,submissions=0,ready=0,failed=0; };

// One frontend generation, one device and one graphics queue. Workers read files,
// decode/allocate/record, poll fences and release staging. They never submit.
// Copy the same TextureBudgets into overlapping generations to bound retained
// GPU allocations. Device and its dispatch must outlive all returned owners.
class TextureCache {
public:
    TextureCache(const TextureDevice&,TextureBudgets={},size_t capacity=256);
    ~TextureCache();
    TextureCache(const TextureCache&)=delete;
    TextureCache& operator=(const TextureCache&)=delete;
    // Call during frontend setup. Lexical normalization only: no file IO, no
    // eager load. Relative paths refuse. Equal normalized paths share a handle.
    TextureHandle declare_image(const std::filesystem::path&,std::string& reason);
    // Safe for an already resolved draw binding: enqueue once, never wait for
    // file IO/driver/fence. Failure is sticky until a new generation is made.
    bool demand(const TextureHandle&);
    // Caller holds its ordinary queue synchronization and always uses the same
    // queue. At most one already-recorded upload per call, with no file IO,
    // allocation, recording, fence polling or waiting. False also means no work.
    bool submit_prepared(VkQueue);
    TextureCacheStats stats() const;
    // Off render path; prevents new demands, joins worker and drains its one
    // outstanding upload. Previously obtained Ready owners remain usable.
    void stop();
private:
    void run();
    std::shared_ptr<ImageUpload> prepare(const TextureHandle&);
    void publish(const TextureHandle&,TextureState,std::shared_ptr<const ImageUpload> image={},std::string reason={});
    TextureDevice device_;
    TextureBudgets budgets_;
    const size_t capacity_;
    std::shared_ptr<const uint8_t> identity_=std::make_shared<const uint8_t>(0);
    mutable std::mutex mutex_;
    std::mutex service_mutex_;
    std::condition_variable wake_;
    bool stopped_=false;
    VkQueue submission_queue_=VK_NULL_HANDLE;
    std::map<std::filesystem::path,TextureHandle> entries_;
    std::deque<TextureHandle> queue_;
    TextureHandle pending_;
    std::shared_ptr<ImageUpload> upload_;
    TextureState phase_=TextureState::Dormant;
    TextureCacheStats stats_;
    std::thread worker_;
};
}
