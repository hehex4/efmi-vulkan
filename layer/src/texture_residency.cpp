#include "texture_residency.h"
#include "json.h"
#include <algorithm>
#include <filesystem>

namespace efvk {
namespace assets = efmi::assets;
namespace {
constexpr uint32_t kBudgetSampleFrames = 60; // the device's free memory is read about once a second
constexpr uint64_t kRecentFrames = 2;        // an image bound this recently is not evicted
}

TextureResidency::TextureResidency(Device* device, const VkPhysicalDeviceMemoryProperties& memory)
    : device_(device), memory_(memory)
{
    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i)
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) heap_bytes_ = std::max<uint64_t>(heap_bytes_, memory.memoryHeaps[i].size);
    cap_bytes_ = g_cfg.m2_texture_budget_mb ? g_cfg.m2_texture_budget_mb << 20 : std::clamp<uint64_t>(heap_bytes_ / 2, 256ull << 20, 4096ull << 20);
    reserve_bytes_ = g_cfg.m2_reserve_mb ? g_cfg.m2_reserve_mb << 20 : std::max<uint64_t>(1024ull << 20, heap_bytes_ / 8);
    target_bytes_ = cap_bytes_;
    if (!device->timeline_semaphores) { reason_ = "textures need timeline semaphores, which the device does not enable"; return; }
    if (device->material_queues.empty() || !device->material_format_properties || !device->material_image_properties) {
        reason_ = device->material_queue_reason.empty() ? "textures need a graphics queue family" : device->material_queue_reason; return;
    }
    assets::ResourceDevice resource;
    resource.device = device->handle; resource.physical = device->phys; resource.memory = memory;
    resource.graphics_family = device->material_family; resource.bc_enabled = device->material_bc_enabled;
    resource.format_properties = device->material_format_properties; resource.image_properties = device->material_image_properties;
    resource.dispatch.SetDeviceLoaderData = device->set_loader_data;
    resource.buffer_dispatch.SetDeviceLoaderData = device->set_loader_data;
    // The layer's textures give way first under memory pressure (D-095): a low priority.
    resource.memory_priority = device->memory_priority ? 0.25f : -1.0f;
#define X(n) resource.dispatch.n = reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle, "vk" #n)); \
    if (!resource.dispatch.n) { reason_ = "missing texture vk" #n; return; }
    EFMI_IMAGE_UPLOAD_FUNCS(X)
#undef X
#define X(n) resource.buffer_dispatch.n = reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle, "vk" #n));
    EFMI_BUFFER_UPLOAD_FUNCS(X)
#undef X
    const auto CreateSemaphore = reinterpret_cast<PFN_vkCreateSemaphore>(device->next_gdpa(device->handle, "vkCreateSemaphore"));
    DestroySemaphore_ = reinterpret_cast<PFN_vkDestroySemaphore>(device->next_gdpa(device->handle, "vkDestroySemaphore"));
    GetSemaphoreCounterValue_ = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(device->next_gdpa(device->handle, "vkGetSemaphoreCounterValue"));
    if (!GetSemaphoreCounterValue_)
        GetSemaphoreCounterValue_ = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(device->next_gdpa(device->handle, "vkGetSemaphoreCounterValueKHR"));
    if (!CreateSemaphore || !DestroySemaphore_ || !GetSemaphoreCounterValue_) { reason_ = "missing timeline semaphore entry points"; return; }
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE; type.initialValue = 0;
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    info.pNext = &type;
    if (CreateSemaphore(device->handle, &info, nullptr, &timeline_) != VK_SUCCESS) { reason_ = "cannot create the texture timeline semaphore"; return; }
    try {
        // The ledger never refuses on its own: the target below keeps the residency within the budget.
        assets::ResourceBudgets budgets;
        budgets.resident = std::make_shared<assets::AllocationBudget>(std::max<uint64_t>(heap_bytes_, 1ull << 30));
        budgets.staging = std::make_shared<assets::AllocationBudget>(160ull << 20);
        budgets.source = std::make_shared<assets::AllocationBudget>(128ull << 20);
        cache_ = std::make_unique<assets::ResourceCache>(resource, budgets, 4096);
    } catch (const std::exception& e) { reason_ = std::string("texture cache: ") + e.what(); return; }
    usable_ = true;
    logf("textures: timeline publication, budget cap %llu MiB, reserve %llu MiB, memory priority %s, pageable %s",
         static_cast<unsigned long long>(cap_bytes_ >> 20), static_cast<unsigned long long>(reserve_bytes_ >> 20),
         device->memory_priority ? "on" : "off", device->pageable_memory ? "on" : "off");
}

TextureResidency::~TextureResidency() { stop(); }

void TextureResidency::stop()
{
    std::unique_ptr<assets::ResourceCache> cache;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) return;
        stopped_ = true; usable_ = false;
        cache.swap(cache_);
    }
    // Joins the worker and drains the one outstanding upload: no signal of the timeline is pending after it.
    if (cache) cache->stop();
    cache.reset();
    if (timeline_ && DestroySemaphore_) DestroySemaphore_(device_->handle, timeline_, nullptr);
    timeline_ = VK_NULL_HANDLE;
}

TextureResidency::ImageId TextureResidency::declare(const std::string& utf8_path, uint64_t tag, std::string& reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usable_) { reason = reason_; return kNone; }
    const auto path = std::filesystem::u8path(utf8_path);
    auto handle = cache_->declare_image(path, reason, tag);
    if (!handle) return kNone;
    const auto found = by_handle_.find(handle.get());
    if (found != by_handle_.end()) return found->second;
    if (images_.size() >= 4096) { reason = "texture declaration capacity exceeded"; return kNone; }
    Image image;
    image.handle = handle; image.tag = tag;
    std::error_code error;
    image.path = path.lexically_normal().u8string();
    const auto size = std::filesystem::file_size(path, error);
    image.file_bytes = error ? 0 : static_cast<uint64_t>(size);
    const ImageId id = static_cast<ImageId>(images_.size());
    images_.push_back(std::move(image));
    by_handle_.emplace(handle.get(), id);
    return id;
}

bool TextureResidency::large(const Image& image) const
{
    const uint64_t threshold = g_cfg.m2_texture_reduce_mb << 20;
    return threshold && image.file_bytes >= threshold;
}

void TextureResidency::release(Image& image)
{
    if (!image.resident) return;
    cache_->release(image.handle);
    image.resident = false;
    resident_bytes_ -= std::min(resident_bytes_, image.charged);
    image.charged = 0;
}

void TextureResidency::evict_for(uint64_t bytes)
{
    // Least recently bound first, among the images not bound in the last frames and not loading.
    while (!fits(bytes)) {
        Image* victim = nullptr;
        for (auto& image : images_) {
            if (!image.resident || image.last_bound + kRecentFrames >= frame_) continue;
            if (image.handle->snapshot().state != assets::ResourceState::Ready) continue;
            if (!victim || image.last_bound < victim->last_bound) victim = &image;
        }
        if (!victim) return;
        release(*victim);
        ++evictions_;
    }
}

bool TextureResidency::demote_one()
{
    // The working set does not fit: the largest resident full-size texture loads again without its
    // top level on its next bind, a quarter of its memory. One per frame, so the set converges.
    if (demoted_frame_ == frame_) return false;
    Image* victim = nullptr;
    for (auto& image : images_) {
        if (!image.resident || image.skip_levels || !large(image)) continue;
        if (image.handle->snapshot().state != assets::ResourceState::Ready) continue;
        if (!victim || image.file_bytes > victim->file_bytes) victim = &image;
    }
    if (!victim) return false;
    release(*victim);
    victim->reduce_next = true;
    demoted_frame_ = frame_;
    ++demotions_;
    return true;
}

void TextureResidency::demand(Image& image)
{
    // Memory tight, the budget under pressure, or demoted: a large texture loads without its top level,
    // a quarter of its memory (a mip chain's levels below the top hold a third of the top's bytes).
    const bool tight = budget_known_ && free_bytes_ < reserve_bytes_ * 2;
    image.skip_levels = large(image) && (image.reduce_next || pressure_ || tight) ? 1 : 0;
    auto charge = [&] { return image.file_bytes >> (2 * image.skip_levels); };
    if (!fits(charge())) evict_for(charge());
    if (!fits(charge())) { pressure_ = true; if (demote_one() && large(image)) image.skip_levels = 1; }
    if (!fits(charge())) { ++budget_waits_; return; }
    if (!cache_->demand(image.handle, image.skip_levels)) return;
    image.resident = true;
    image.reduce_next = false;
    image.charged = charge();
    resident_bytes_ += image.charged;
    ++loads_;
    if (image.skip_levels) ++reduced_;
}

TextureResidency::Binding TextureResidency::bind(ImageId id)
{
    Binding out;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usable_ || id >= images_.size()) { out.failed = true; out.reason = usable_ ? "unknown texture" : reason_; return out; }
    Image& image = images_[id];
    image.last_bound = frame_;
    const auto snapshot = image.handle->snapshot();
    switch (snapshot.state) {
    case assets::ResourceState::Ready:
        if (snapshot.serial && snapshot.serial <= published_ && snapshot.image && snapshot.image->view()) {
            out.view = snapshot.image->view(); out.owner = snapshot.image; return out;
        }
        ++pending_; return out; // published at the next submission
    case assets::ResourceState::Failed:
    case assets::ResourceState::Stopped:
        if (image.resident) { image.resident = false; resident_bytes_ -= std::min(resident_bytes_, image.charged); image.charged = 0; }
        out.failed = true; out.reason = snapshot.reason; return out;
    case assets::ResourceState::Dormant:
        demand(image);
        ++pending_; return out;
    case assets::ResourceState::Queued:
        cache_->prioritize(image.handle);
        ++pending_; return out;
    default:
        ++pending_; return out;
    }
}

void TextureResidency::preload(const std::vector<ImageId>& ids)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usable_) return;
    for (ImageId id : ids) {
        if (id >= images_.size()) continue;
        Image& image = images_[id];
        if (image.handle->snapshot().state == assets::ResourceState::Dormant) demand(image);
    }
}

void TextureResidency::preload_matching(const std::vector<ImageId>& ids, const std::vector<ImageId>& live)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usable_) return;
    for (ImageId id : ids) {
        if (id >= images_.size()) continue;
        Image& image = images_[id];
        if (image.handle->snapshot().state != assets::ResourceState::Dormant) continue;
        bool shown = false;
        for (ImageId other : live)
            if (other < images_.size() && images_[other].resident && images_[other].path == image.path) { shown = true; break; }
        if (shown) demand(image);
    }
}

void TextureResidency::forget(const std::vector<ImageId>& ids)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usable_) return;
    for (ImageId id : ids)
        if (id < images_.size()) release(images_[id]);
}

bool TextureResidency::resident(ImageId id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usable_ || id >= images_.size()) return false;
    const auto snapshot = images_[id].handle->snapshot();
    return snapshot.state == assets::ResourceState::Ready && snapshot.serial && snapshot.serial <= published_;
}

bool TextureResidency::failed(ImageId id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usable_ || id >= images_.size()) return true;
    const auto state = images_[id].handle->snapshot().state;
    return state == assets::ResourceState::Failed || state == assets::ResourceState::Stopped;
}

uint64_t TextureResidency::file_bytes(ImageId id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return id < images_.size() ? images_[id].file_bytes : 0;
}

uint64_t TextureResidency::loading(const std::vector<ImageId>& ids) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t count = 0;
    if (!usable_) return 0;
    for (ImageId id : ids) {
        if (id >= images_.size() || !images_[id].resident) continue;
        const auto snapshot = images_[id].handle->snapshot();
        const bool published = snapshot.state == assets::ResourceState::Ready && snapshot.serial && snapshot.serial <= published_;
        const bool failed = snapshot.state == assets::ResourceState::Failed || snapshot.state == assets::ResourceState::Stopped;
        if (!published && !failed) ++count;
    }
    return count;
}

bool TextureResidency::any_failed(const std::vector<ImageId>& ids) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usable_) return false;
    for (ImageId id : ids) {
        if (id >= images_.size()) continue;
        const auto state = images_[id].handle->snapshot().state;
        if (state == assets::ResourceState::Failed || state == assets::ResourceState::Stopped) return true;
    }
    return false;
}

bool TextureResidency::queue_known(VkQueue queue) const
{
    return std::find(device_->material_queues.begin(), device_->material_queues.end(), queue) != device_->material_queues.end();
}

VkResult TextureResidency::before_submit(VkQueue queue)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usable_ || !queue_known(queue)) return VK_SUCCESS;
    // One prepared upload, signalling the timeline with its serial once it completed.
    if (cache_->submit_prepared(queue, timeline_, signalled_ + 1)) { ++signalled_; ++uploads_; }
    uint64_t value = 0;
    if (GetSemaphoreCounterValue_(device_->handle, timeline_, &value) == VK_SUCCESS && value > published_) published_ = value;
    auto& waited = waited_[queue];
    if (waited >= published_) return VK_SUCCESS;
    // The wait is satisfied already (the counter reached the value), so it can never hold this queue
    // behind another (D-091); it gives the queue's later commands the visibility of the uploads.
    VkTimelineSemaphoreSubmitInfo values{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    values.waitSemaphoreValueCount = 1; values.pWaitSemaphoreValues = &published_;
    const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo wait{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    wait.pNext = &values; wait.waitSemaphoreCount = 1; wait.pWaitSemaphores = &timeline_; wait.pWaitDstStageMask = &stage;
    const auto result = device_->d.QueueSubmit(queue, 1, &wait, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) return result;
    waited = published_;
    ++waits_;
    return VK_SUCCESS;
}

void TextureResidency::sample_budget()
{
    if (!device_->memory_budget || !device_->memory_properties2) return;
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
    properties.pNext = &budget;
    device_->memory_properties2(device_->phys, &properties);
    uint64_t heap = 0; bool found = false;
    for (uint32_t i = 0; i < properties.memoryProperties.memoryHeapCount; ++i) {
        const auto& h = properties.memoryProperties.memoryHeaps[i];
        if (!(h.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) || (found && h.size <= heap)) continue;
        found = true; heap = h.size;
        free_bytes_ = budget.heapBudget[i] > budget.heapUsage[i] ? budget.heapBudget[i] - budget.heapUsage[i] : 0;
    }
    if (!found) return;
    budget_known_ = true;
    // What the device can hold of ours: what it holds now plus what is free beyond the reserve.
    const uint64_t headroom = free_bytes_ > reserve_bytes_ ? free_bytes_ - reserve_bytes_ : 0;
    const uint64_t allowed = free_bytes_ >= reserve_bytes_ ? resident_bytes_ + headroom
                                                          : resident_bytes_ - std::min(resident_bytes_, reserve_bytes_ - free_bytes_);
    target_bytes_ = std::clamp<uint64_t>(allowed, 64ull << 20, cap_bytes_);
}

void TextureResidency::frame()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usable_) return;
    ++frame_;
    pressure_ = false;
    if (frame_ >= next_sample_) { next_sample_ = frame_ + kBudgetSampleFrames; sample_budget(); }
    // Failed loads hold nothing.
    for (auto& image : images_)
        if (image.resident) {
            const auto state = image.handle->snapshot().state;
            if (state == assets::ResourceState::Failed || state == assets::ResourceState::Stopped) {
                image.resident = false; resident_bytes_ -= std::min(resident_bytes_, image.charged); image.charged = 0;
            }
        }
    if (resident_bytes_ > target_bytes_) evict_for(0);
}

void TextureResidency::status(JsonWriter& w) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    w.key("textures"); w.begin_object();
    w.kb("usable", usable_); w.kv("reason", reason_);
    uint64_t resident = 0, loading = 0, failed = 0, reduced = 0;
    for (const auto& image : images_) {
        if (!image.resident) continue;
        const auto state = image.handle->snapshot().state;
        if (state == assets::ResourceState::Ready) { ++resident; reduced += image.skip_levels ? 1 : 0; }
        else if (state == assets::ResourceState::Failed || state == assets::ResourceState::Stopped) ++failed;
        else ++loading;
    }
    w.kn("declared", images_.size()); w.kn("resident", resident); w.kn("resident_reduced", reduced); w.kn("loading", loading); w.kn("failed", failed);
    w.kn("resident_bytes", resident_bytes_); w.kn("target_bytes", target_bytes_); w.kn("cap_bytes", cap_bytes_);
    w.kn("reserve_bytes", reserve_bytes_); w.kn("device_free_bytes", free_bytes_); w.kb("budget_known", budget_known_);
    w.kn("loads", loads_); w.kn("reduced", reduced_); w.kn("evictions", evictions_); w.kn("demotions", demotions_);
    w.kn("budget_waits", budget_waits_); w.kn("pending", pending_); w.kn("uploads", uploads_); w.kn("published", published_); w.kn("waits", waits_);
    w.kn("activating_bytes", activating_bytes.load());
    if (cache_) {
        const auto stats = cache_->stats();
        w.kn("file_reads", stats.file_reads); w.kn("submissions", stats.submissions);
        w.kn("ready", stats.ready); w.kn("load_failures", stats.failed); w.kn("releases", stats.releases);
    }
    w.end_object();
}

} // namespace efvk
