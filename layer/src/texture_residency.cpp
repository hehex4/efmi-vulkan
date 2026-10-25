#include "texture_residency.h"
#include "json.h"
#include <algorithm>
#include <filesystem>
#include <thread>
#include <chrono>

namespace efvk {
namespace assets = efmi::assets;
namespace {
constexpr uint32_t kBudgetSampleFrames = 30; // the device's free memory is read about twice a second
constexpr uint64_t kIdleFrames = 600;        // an image not bound for this long goes first when the set is over the target
constexpr uint64_t kProtectedFrames = 8;     // an image bound this recently is never evicted
constexpr uint64_t kDemoteFrames = 30;       // at most one demotion this often while the working set is over the target
}

TextureResidency::TextureResidency(Device* device, const VkPhysicalDeviceMemoryProperties& memory)
    : device_(device), memory_(memory)
{
    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i)
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) heap_bytes_ = std::max<uint64_t>(heap_bytes_, memory.memoryHeaps[i].size);
    cap_bytes_ = g_cfg.m2_texture_budget_mb ? g_cfg.m2_texture_budget_mb << 20 : std::clamp<uint64_t>(heap_bytes_ / 2, 256ull << 20, 4096ull << 20);
    reserve_bytes_ = g_cfg.m2_reserve_mb ? g_cfg.m2_reserve_mb << 20 : std::max<uint64_t>(512ull << 20, heap_bytes_ / 16);
    target_bytes_ = cap_bytes_;
    floor_bytes_ = device->pageable_memory ? std::min(reserve_bytes_, cap_bytes_) : 64ull << 20;
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
    // The layer's upload queue (D-103): the loaders submit there themselves and publish through the timeline.
    resource.upload_queue = device->upload_queue; resource.upload_family = device->upload_family; resource.upload_mutex = &device->upload_mx;
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
        budgets.staging = std::make_shared<assets::AllocationBudget>(192ull << 20);
        budgets.source = std::make_shared<assets::AllocationBudget>(192ull << 20);
        resource.timeline = timeline_; resource.serial = &signalled_;
        // Three loaders with a queue of the layer's own (a character's textures load at once), one otherwise.
        cache_ = std::make_unique<assets::ResourceCache>(resource, budgets, 4096, device->upload_queue ? 3u : 1u);
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
    // A queue may still hold a wait on it (D-098): destroyed on the presenting thread a few presentations later.
    if (timeline_ && DestroySemaphore_) retire_semaphore(device_, timeline_, DestroySemaphore_);
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
    released_since_sample_ += image.charged;
    image.charged = 0;
}

bool TextureResidency::loadable(uint64_t bytes) const
{
    if (resident_bytes_ + bytes > cap_bytes_) return false;
    if (device_->pageable_memory && resident_bytes_ + bytes <= floor_bytes_) return true;
    return !budget_known_ || free_now() >= reserve_bytes_ / 2 + bytes;
}

void TextureResidency::evict_for(uint64_t bytes)
{
    while (!loadable(bytes)) {
        Image* victim = nullptr;
        for (auto& image : images_) {
            if (!image.resident || image.last_bound + kProtectedFrames > frame_) continue;
            if (image.handle->snapshot().state != assets::ResourceState::Ready) continue;
            if (!victim || image.last_bound < victim->last_bound) victim = &image;
        }
        if (!victim) return;
        release(*victim);
        ++evictions_;
    }
}

void TextureResidency::evict_idle(uint64_t bytes, uint64_t idle_frames, uint64_t limit)
{
    // Least recently bound first, among the Ready images no frame bound for `idle_frames`.
    while (resident_bytes_ + bytes > limit) {
        Image* victim = nullptr;
        for (auto& image : images_) {
            if (!image.resident || image.last_bound + idle_frames > frame_) continue;
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
    // The working set does not fit the target: the largest resident full-size texture loads again
    // without its top level on its next bind, a quarter of its memory. Paced, so the set converges
    // without the frames losing several characters at once.
    if (frame_ < demoted_frame_ + kDemoteFrames) return false;
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
    // Memory tight, the set over the target, or demoted: a large texture loads without its top level,
    // a quarter of its memory (a mip chain's levels below the top hold a third of the top's bytes).
    const bool tight = budget_known_ && free_now() < reserve_bytes_ * 2;
    image.skip_levels = large(image) && (image.reduce_next || pressure_ || tight) ? 1 : 0;
    auto charge = [&] { return image.file_bytes >> (2 * image.skip_levels); };
    // Over the target the idle images go first. What the frames keep binding stays: evicting it
    // would load it again a frame later (D-097); the set shrinks through paced demotions instead.
    if (!fits(charge())) evict_idle(charge(), kIdleFrames, target_bytes_);
    if (!fits(charge())) { pressure_ = true; if (large(image) && demote_one()) image.skip_levels = 1; }
    // The cap and half the reserve are the limits; a load within them goes ahead beyond the target.
    // Short of free memory, what no recent frame bound gives way (it was evicted only past the cap).
    if (!loadable(charge())) evict_for(charge());
    if (!loadable(charge())) { ++budget_waits_; return; }
    if (!cache_->demand(image.handle, image.skip_levels)) return;
    image.resident = true;
    image.reduce_next = false;
    image.charged = charge();
    resident_bytes_ += image.charged;
    loaded_since_sample_ += image.charged;
    ++loads_;
    if (image.skip_levels) ++reduced_;
}

void TextureResidency::bind(const std::vector<ImageId>& ids, std::vector<Binding>& bindings)
{
    bindings.clear(); bindings.resize(ids.size());
    if (ids.empty()) return;
    std::unique_lock<std::mutex> lock(mutex_);
    if (!usable_ || stopped_) {
        for (auto& out : bindings) { out.failed = true; out.reason = stopped_ ? "texture residency stopped" : reason_; }
        return;
    }
    struct Waiting { size_t slot; assets::ResourceHandle handle; };
    std::vector<Waiting> waiting; // No allocation for an already resident draw.
    auto accept = [&](size_t slot, const assets::ResourceSnapshot& snapshot) {
        auto& out = bindings[slot];
        if (stopped_) { out.failed = true; out.reason = "texture residency stopped"; return true; }
        // Only indices and shared request handles cross the unlocked wait. declare() may grow
        // images_ during an F10 reload, invalidating every Image reference into that vector.
        auto& image = images_[ids[slot]];
        if (snapshot.state == assets::ResourceState::Failed || snapshot.state == assets::ResourceState::Stopped) {
            if (image.resident) {
                image.resident = false; resident_bytes_ -= std::min(resident_bytes_, image.charged); image.charged = 0;
            }
            if (!image.failure_logged) { image.failure_logged = true; logf("texture failed: %s: %s", image.path.c_str(), snapshot.reason.c_str()); }
            out.failed = true; out.reason = snapshot.reason; return true;
        }
        if (snapshot.state != assets::ResourceState::Ready || !snapshot.serial || !snapshot.image || !snapshot.image->view()) return false;
        if (snapshot.serial > published_) {
            uint64_t value = 0;
            if (GetSemaphoreCounterValue_(device_->handle, timeline_, &value) == VK_SUCCESS && value > published_) published_ = value;
        }
        if (snapshot.serial > published_) return false;
        out.view = snapshot.image->view(); out.owner = snapshot.image; return true;
    };
    // Queue the whole selected transaction before waiting: all existing loaders can work at once.
    // demand() still enforces the same residency, staging and source budgets. No unused variant loads.
    for (size_t slot = 0; slot < ids.size(); ++slot) {
        if (ids[slot] >= images_.size()) { bindings[slot].failed = true; bindings[slot].reason = "unknown texture"; continue; }
        auto& image = images_[ids[slot]];
        image.last_bound = frame_;
        const auto snapshot = image.handle->snapshot();
        if (accept(slot, snapshot)) continue;
        if (snapshot.state == assets::ResourceState::Dormant) demand(image);
        else if (snapshot.state == assets::ResourceState::Queued) cache_->prioritize(image.handle);
        if (device_->upload_queue && image.resident) waiting.push_back({slot, image.handle});
        else ++pending_;
    }
    if (waiting.empty()) return;
    const uint64_t began = host_time_us();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    lock.unlock();
    for (const auto& request : waiting) request.handle->wait_until(deadline);
    lock.lock();
    // Re-read under the residency lock: a concurrent release/reload may have changed the request.
    // A fence can precede the host counter observation by a moment; one bounded second look for
    // the whole batch preserves completed-only publication (D-091), without N separate deadlines.
    for (unsigned retry = 0; ; ++retry) {
        bool unpublished = false;
        for (const auto& request : waiting) {
            auto& out = bindings[request.slot];
            if (out.view || out.failed) continue;
            const auto snapshot = request.handle->snapshot();
            if (!accept(request.slot, snapshot) && snapshot.state == assets::ResourceState::Ready) unpublished = true;
        }
        if (!unpublished || retry == 50) break;
        lock.unlock(); std::this_thread::sleep_for(std::chrono::microseconds(100)); lock.lock();
    }
    const uint64_t waited = host_time_us() - began;
    ++waits_sync_; wait_images_ += waiting.size(); wait_sync_us_ += waited;
    wait_sync_max_us_ = std::max(wait_sync_max_us_, waited);
    bool timed_out = false;
    for (const auto& request : waiting)
        if (!bindings[request.slot].view && !bindings[request.slot].failed) { ++pending_; timed_out = true; }
    if (timed_out) ++waits_timed_out_;
}

bool TextureResidency::ensure(const std::vector<ImageId>& ids)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!usable_) return ids.empty();
    bool all = true;
    for (ImageId id : ids) {
        if (id >= images_.size()) { all = false; continue; }
        Image& image = images_[id];
        image.last_bound = frame_;
        const auto snapshot = image.handle->snapshot();
        if (snapshot.state == assets::ResourceState::Ready && snapshot.serial && snapshot.serial <= published_) continue;
        if (snapshot.state == assets::ResourceState::Dormant) demand(image);
        else if (snapshot.state == assets::ResourceState::Queued) cache_->prioritize(image.handle);
        all = false;
    }
    return all;
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
    uint32_t demanded = 0, resident_live = 0;
    for (ImageId other : live) if (other < images_.size() && images_[other].resident) ++resident_live;
    for (ImageId id : ids) {
        if (id >= images_.size()) continue;
        Image& image = images_[id];
        if (image.handle->snapshot().state != assets::ResourceState::Dormant) continue;
        bool shown = false;
        for (ImageId other : live)
            if (other < images_.size() && images_[other].resident && images_[other].path == image.path) { shown = true; break; }
        if (shown) { demand(image); ++demanded; }
    }
    logf("textures: reload candidate demands %u of %zu images (%u of %zu live ones resident)", demanded, ids.size(), resident_live, live.size());
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
    // Without a queue of the layer's own: one prepared upload, signalling the timeline with its serial.
    if (!device_->upload_queue && cache_->submit_prepared(queue, timeline_, signalled_.load() + 1)) { ++signalled_; ++uploads_; }
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
    loaded_since_sample_ = released_since_sample_ = 0;
    // What the device can hold of ours: what it holds now plus what is free beyond the reserve.
    const uint64_t headroom = free_bytes_ > reserve_bytes_ ? free_bytes_ - reserve_bytes_ : 0;
    const uint64_t allowed = free_bytes_ >= reserve_bytes_ ? resident_bytes_ + headroom
                                                          : resident_bytes_ - std::min(resident_bytes_, reserve_bytes_ - free_bytes_);
    target_bytes_ = std::clamp<uint64_t>(allowed, floor_bytes_, cap_bytes_);
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
    // Over the target: the idle images go, then one demotion at a time shrinks what the frames bind.
    if (resident_bytes_ > target_bytes_) {
        evict_idle(0, kIdleFrames, target_bytes_);
        if (resident_bytes_ > target_bytes_) { pressure_ = true; demote_one(); }
    }
    if (resident_bytes_ > cap_bytes_) evict_idle(0, kProtectedFrames, cap_bytes_);
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
    w.kn("reserve_bytes", reserve_bytes_); w.kn("floor_bytes", floor_bytes_); w.kn("device_free_bytes", free_bytes_); w.kn("free_estimate_bytes", free_now());
    w.kb("budget_known", budget_known_); w.kb("pressure", pressure_);
    w.kn("idle_frames", kIdleFrames); w.kn("protected_frames", kProtectedFrames);
    w.kn("loads", loads_); w.kn("reduced", reduced_); w.kn("evictions", evictions_); w.kn("demotions", demotions_);
    w.kn("budget_waits", budget_waits_); w.kn("pending", pending_); w.kn("uploads", uploads_); w.kn("published", published_); w.kn("waits", waits_);
    w.kb("upload_queue", device_->upload_queue != VK_NULL_HANDLE);
    w.kn("draw_waits", waits_sync_); w.kn("draw_wait_images", wait_images_); w.kn("draw_wait_microseconds", wait_sync_us_); w.kn("draw_wait_max_microseconds", wait_sync_max_us_);
    w.kn("draw_waits_timed_out", waits_timed_out_);
    w.kn("activating_bytes", activating_bytes.load());
    if (cache_) {
        const auto stats = cache_->stats();
        w.kn("file_reads", stats.file_reads); w.kn("submissions", stats.submissions);
        w.kn("ready", stats.ready); w.kn("load_failures", stats.failed); w.kn("releases", stats.releases);
    }
    w.end_object();
}

} // namespace efvk
