#include "bone_merge.h"
#include "merged_skeleton.h"
#include "draw_snapshots.h"
#include "json.h"
#include <algorithm>
#include <cstring>
#include <set>

namespace efvk {
extern const uint32_t bone_merge_spirv[];
extern const size_t bone_merge_spirv_words;

#define EFVK_BONE_FUNCS(X) \
    X(CreateShaderModule) X(DestroyShaderModule) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
    X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateComputePipelines) X(DestroyPipeline) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(ResetDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) X(ResetCommandBuffer) \
    X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch) X(CmdPipelineBarrier) X(CmdUpdateBuffer) X(CmdFillBuffer) \
    X(QueueSubmit) X(CreateSemaphore) X(DestroySemaphore) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) \
    X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory)

// What the rings and remaps outlive the merge with: the device entry points and the timeline that
// says which submissions completed.
struct BoneContext {
#define X(n) PFN_vk##n n = nullptr;
    EFVK_BONE_FUNCS(X)
#undef X
    PFN_vkGetSemaphoreCounterValue GetSemaphoreCounterValue = nullptr;
    PFN_vkWaitSemaphores WaitSemaphores = nullptr;
    VkDevice device = VK_NULL_HANDLE;
    VkSemaphore timeline = VK_NULL_HANDLE;
    std::atomic<uint64_t> submitted{0}; // the value the last submission signals
    uint64_t completed() const
    {
        uint64_t value = 0;
        return timeline && GetSemaphoreCounterValue(device, timeline, &value) == VK_SUCCESS ? value : 0;
    }
    ~BoneContext() { if (timeline) DestroySemaphore(device, timeline, nullptr); }
};

namespace {
constexpr uint32_t kSlots = 8;           // frames a ring holds apart: the frames in flight and a margin
constexpr uint32_t kCommandBuffers = 8;  // submissions in flight
constexpr uint32_t kJobsPerSubmission = 1024;
constexpr uint32_t kImplicitBytes = MergedSkeleton::kImplicitBones * MergedSkeleton::kBytesPerBone; // 24576, within vkCmdUpdateBuffer's limit
struct Push { uint32_t source_rows0, source_rows1, destination, pose_rows, first, count, flags, remap_offset; float scale; };

VkDeviceSize aligned(VkDeviceSize value, VkDeviceSize alignment) { alignment = std::max<VkDeviceSize>(alignment, 1); return (value + alignment - 1) / alignment * alignment; }
uint32_t device_local_type(const VkPhysicalDeviceMemoryProperties& p, uint32_t bits)
{
    for (uint32_t i = 0; i < p.memoryTypeCount; ++i)
        if ((bits >> i & 1) && (p.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return i;
    for (uint32_t i = 0; i < p.memoryTypeCount; ++i) if (bits >> i & 1) return i;
    return UINT32_MAX;
}
const std::vector<float>& identity_rows()
{
    static const std::vector<float> rows = [] {
        std::vector<float> out(MergedSkeleton::kImplicitBones * 12, 0.0f);
        for (uint32_t b = 0; b < MergedSkeleton::kImplicitBones; ++b) { out[b * 12 + 0] = 1; out[b * 12 + 5] = 1; out[b * 12 + 10] = 1; }
        return out;
    }();
    return rows;
}
} // namespace

uint32_t BoneRing::acquire()
{
    std::lock_guard<std::mutex> lock(mx);
    const uint64_t done = ctx->completed();
    for (uint32_t s = 0; s < slots; ++s)
        if (!readers[s] && !pending[s] && written[s] <= done) return s;
    return UINT32_MAX;
}
void BoneRing::hold(uint32_t slot) { std::lock_guard<std::mutex> lock(mx); if (slot < slots) ++readers[slot]; }
void BoneRing::release(uint32_t slot) { std::lock_guard<std::mutex> lock(mx); if (slot < slots && readers[slot]) --readers[slot]; }
BoneRing::~BoneRing()
{
    if (!ctx) return;
    // A dispatch of a frame whose draws were not recorded (the identity was still identifying, or
    // its generation went) may still run: the ring waits for its last write before it goes.
    uint64_t last = 0;
    for (uint64_t value : written) last = std::max(last, value);
    if (last && last > ctx->completed() && ctx->WaitSemaphores) {
        VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wi.semaphoreCount = 1; wi.pSemaphores = &ctx->timeline; wi.pValues = &last;
        ctx->WaitSemaphores(ctx->device, &wi, 1000ull * 1000 * 1000);
    }
    if (buffer) ctx->DestroyBuffer(ctx->device, buffer, nullptr);
    if (memory) ctx->FreeMemory(ctx->device, memory, nullptr);
}
BoneRemaps::~BoneRemaps()
{
    if (!ctx) return;
    if (buffer) ctx->DestroyBuffer(ctx->device, buffer, nullptr);
    if (memory) ctx->FreeMemory(ctx->device, memory, nullptr);
}

BoneMerge::BoneMerge(Device* device, const VkPhysicalDeviceProperties& properties, const VkPhysicalDeviceMemoryProperties& memory)
    : ctx_(std::make_shared<BoneContext>()), memory_(memory)
{
    ctx_->device = device->handle;
    storage_alignment_ = std::max<VkDeviceSize>(properties.limits.minStorageBufferOffsetAlignment, 16);
    bool missing = false;
#define X(n) ctx_->n = reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle, "vk" #n)); missing |= !ctx_->n;
    EFVK_BONE_FUNCS(X)
#undef X
    if (missing) { reason_ = "missing a device entry point"; return; }
    if (!device->timeline_semaphores) { reason_ = "the device has no timeline semaphores"; return; }
    ctx_->GetSemaphoreCounterValue = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(device->next_gdpa(device->handle, "vkGetSemaphoreCounterValue"));
    if (!ctx_->GetSemaphoreCounterValue)
        ctx_->GetSemaphoreCounterValue = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(device->next_gdpa(device->handle, "vkGetSemaphoreCounterValueKHR"));
    ctx_->WaitSemaphores = reinterpret_cast<PFN_vkWaitSemaphores>(device->next_gdpa(device->handle, "vkWaitSemaphores"));
    if (!ctx_->WaitSemaphores) ctx_->WaitSemaphores = reinterpret_cast<PFN_vkWaitSemaphores>(device->next_gdpa(device->handle, "vkWaitSemaphoresKHR"));
    if (!ctx_->GetSemaphoreCounterValue || !ctx_->WaitSemaphores) { reason_ = "missing timeline semaphore entry points"; return; }
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; si.pNext = &type;
    if (ctx_->CreateSemaphore(ctx_->device, &si, nullptr, &ctx_->timeline) != VK_SUCCESS) { reason_ = "cannot create the bone timeline"; return; }
    if (!make_pipeline()) return;
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pi.queueFamilyIndex = device->material_family;
    if (ctx_->CreateCommandPool(ctx_->device, &pi, nullptr, &pool_) != VK_SUCCESS) { reason_ = "cannot create the bone command pool"; return; }
    std::vector<VkCommandBuffer> cbs(kCommandBuffers);
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool_; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = kCommandBuffers;
    if (ctx_->AllocateCommandBuffers(ctx_->device, &ai, cbs.data()) != VK_SUCCESS) { reason_ = "cannot allocate the bone command buffers"; return; }
    // Dispatchable objects a layer makes get the loader's dispatch pointer from the layer (the layers
    // below read it; without it the validation layer faulted in the layer entry).
    if (device->set_loader_data) for (auto cb : cbs) device->set_loader_data(device->handle, cb);
    slots_.resize(kCommandBuffers);
    for (uint32_t i = 0; i < kCommandBuffers; ++i) {
        slots_[i].cb = cbs[i];
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kJobsPerSubmission * 3};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dp.maxSets = kJobsPerSubmission; dp.poolSizeCount = 1; dp.pPoolSizes = &size;
        if (ctx_->CreateDescriptorPool(ctx_->device, &dp, nullptr, &slots_[i].pool) != VK_SUCCESS) { reason_ = "cannot create a bone descriptor pool"; return; }
    }
    usable_ = true;
}

BoneMerge::~BoneMerge()
{
    VkDevice dev = ctx_->device;
    for (auto& s : slots_) if (s.pool) ctx_->DestroyDescriptorPool(dev, s.pool, nullptr);
    if (pool_) ctx_->DestroyCommandPool(dev, pool_, nullptr);
    if (pipeline_) ctx_->DestroyPipeline(dev, pipeline_, nullptr);
    if (layout_) ctx_->DestroyPipelineLayout(dev, layout_, nullptr);
    if (set_layout_) ctx_->DestroyDescriptorSetLayout(dev, set_layout_, nullptr);
}

bool BoneMerge::make_pipeline()
{
    VkDevice dev = ctx_->device;
    VkDescriptorSetLayoutBinding bindings[3] = {};
    for (uint32_t i = 0; i < 3; ++i) {
        bindings[i].binding = i; bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1; bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 3; li.pBindings = bindings;
    if (ctx_->CreateDescriptorSetLayout(dev, &li, nullptr, &set_layout_) != VK_SUCCESS) { reason_ = "bone set layout"; return false; }
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &set_layout_; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &range;
    if (ctx_->CreatePipelineLayout(dev, &pl, nullptr, &layout_) != VK_SUCCESS) { reason_ = "bone pipeline layout"; return false; }
    VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mi.codeSize = bone_merge_spirv_words * 4; mi.pCode = bone_merge_spirv;
    VkShaderModule module = VK_NULL_HANDLE;
    if (ctx_->CreateShaderModule(dev, &mi, nullptr, &module) != VK_SUCCESS) { reason_ = "bone shader module"; return false; }
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module = module; ci.stage.pName = "main";
    ci.layout = layout_;
    const VkResult result = ctx_->CreateComputePipelines(dev, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline_);
    ctx_->DestroyShaderModule(dev, module, nullptr);
    if (result != VK_SUCCESS) { reason_ = "bone compute pipeline"; return false; }
    return true;
}

std::shared_ptr<BoneRing> BoneMerge::ring(uint32_t bones, std::string& why)
{
    if (!usable_) { why = reason_; return nullptr; }
    if (!bones || bones > 65536) { why = "bone ring: invalid skeleton"; return nullptr; }
    auto r = std::make_shared<BoneRing>();
    r->ctx = ctx_; r->bones = bones; r->slots = kSlots;
    r->slot_bytes = VkDeviceSize(MergedSkeleton::kImplicitBones + 2ull * bones) * MergedSkeleton::kBytesPerBone;
    r->slot_stride = aligned(r->slot_bytes, storage_alignment_);
    r->readers.assign(kSlots, 0); r->pending.assign(kSlots, 0); r->written.assign(kSlots, 0);
    VkDevice dev = ctx_->device;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = r->slot_stride * kSlots; bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (ctx_->CreateBuffer(dev, &bi, nullptr, &r->buffer) != VK_SUCCESS) { why = "bone ring: create buffer"; return nullptr; }
    VkMemoryRequirements req; ctx_->GetBufferMemoryRequirements(dev, r->buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size;
    ai.memoryTypeIndex = device_local_type(memory_, req.memoryTypeBits);
    if (ai.memoryTypeIndex == UINT32_MAX || ctx_->AllocateMemory(dev, &ai, nullptr, &r->memory) != VK_SUCCESS) { why = "bone ring: allocate memory"; return nullptr; }
    if (ctx_->BindBufferMemory(dev, r->buffer, r->memory, 0) != VK_SUCCESS) { why = "bone ring: bind memory"; return nullptr; }
    rings_.fetch_add(1, std::memory_order_relaxed); ring_bytes_.fetch_add(req.size, std::memory_order_relaxed);
    return r;
}

std::shared_ptr<BoneRemaps> BoneMerge::remaps(const std::vector<std::vector<uint16_t>>& tables, std::string& why)
{
    if (!usable_) { why = reason_; return nullptr; }
    auto r = std::make_shared<BoneRemaps>();
    r->ctx = ctx_;
    std::vector<uint32_t> words;
    for (const auto& table : tables) {
        r->offsets.push_back(table.empty() ? UINT32_MAX : uint32_t(words.size()));
        for (uint16_t source : table) words.push_back(source);
    }
    if (words.empty()) words.resize(4, 0); // a valid binding for the jobs that use no table
    VkDevice dev = ctx_->device;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = words.size() * 4; bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (ctx_->CreateBuffer(dev, &bi, nullptr, &r->buffer) != VK_SUCCESS) { why = "bone remaps: create buffer"; return nullptr; }
    VkMemoryRequirements req; ctx_->GetBufferMemoryRequirements(dev, r->buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size;
    ai.memoryTypeIndex = host_memory_type(memory_, req.memoryTypeBits, false);
    if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = host_memory_type(memory_, req.memoryTypeBits, true);
    if (ai.memoryTypeIndex == UINT32_MAX || ctx_->AllocateMemory(dev, &ai, nullptr, &r->memory) != VK_SUCCESS) { why = "bone remaps: allocate memory"; return nullptr; }
    if (ctx_->BindBufferMemory(dev, r->buffer, r->memory, 0) != VK_SUCCESS) { why = "bone remaps: bind memory"; return nullptr; }
    void* mapped = nullptr;
    if (ctx_->MapMemory(dev, r->memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) { why = "bone remaps: map memory"; return nullptr; }
    std::memcpy(mapped, words.data(), words.size() * 4);
    ctx_->UnmapMemory(dev, r->memory);
    return r;
}

void BoneMerge::queue(BoneJob&& job)
{
    std::lock_guard<std::mutex> lock(mx_);
    // A later import of the same component in the same frame replaces the earlier one (a lower LOD
    // replacing a higher), as the CPU import does; two dispatches on one range would race.
    for (auto it = jobs_.rbegin(); it != jobs_.rend(); ++it)
        if (it->ring == job.ring && it->slot == job.slot && it->component == job.component) { *it = std::move(job); return; }
    { std::lock_guard<std::mutex> ring_lock(job.ring->mx); ++job.ring->pending[job.slot]; }
    jobs_.push_back(std::move(job));
    queued_.store(uint32_t(jobs_.size()), std::memory_order_relaxed);
    jobs_total_.fetch_add(1, std::memory_order_relaxed);
}

void BoneMerge::fail(const std::string& why)
{
    failures_.fetch_add(1, std::memory_order_relaxed);
    if (why != error_) logf("bones: %s", why.c_str());
    error_ = why;
}

bool BoneMerge::record(Slot& slot, std::vector<BoneJob>& jobs, size_t begin, size_t end, std::string& why)
{
    VkCommandBuffer cmd = slot.cb;
    if (ctx_->ResetCommandBuffer(cmd, 0) != VK_SUCCESS || ctx_->ResetDescriptorPool(ctx_->device, slot.pool, 0) != VK_SUCCESS) { why = "bone command buffer reset"; return false; }
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (ctx_->BeginCommandBuffer(cmd, &bi) != VK_SUCCESS) { why = "bone command buffer begin"; return false; }
    // A ring's first use: every slot zero (bones no component imports stay zero, as in the CPU master)
    // and the implicit identity matrices written.
    bool initialized_any = false;
    std::set<BoneRing*> seen;
    for (size_t i = begin; i < end; ++i) {
        BoneRing& ring = *jobs[i].ring;
        if (!seen.insert(&ring).second) continue;
        bool fresh;
        { std::lock_guard<std::mutex> lock(ring.mx); fresh = !ring.initialized; ring.initialized = true; }
        if (!fresh) continue;
        ctx_->CmdFillBuffer(cmd, ring.buffer, 0, VK_WHOLE_SIZE, 0);
        VkMemoryBarrier fill{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; fill.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; fill.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ctx_->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &fill, 0, nullptr, 0, nullptr);
        for (uint32_t s = 0; s < ring.slots; ++s) ctx_->CmdUpdateBuffer(cmd, ring.buffer, ring.slot_stride * s, kImplicitBytes, identity_rows().data());
        initialized_any = true;
    }
    if (initialized_any) {
        VkMemoryBarrier m{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        m.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; m.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        ctx_->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                                 0, 1, &m, 0, nullptr, 0, nullptr);
    }
    ctx_->CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    for (size_t i = begin; i < end; ++i) {
        const BoneJob& job = jobs[i];
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = slot.pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &set_layout_;
        if (ctx_->AllocateDescriptorSets(ctx_->device, &ai, &set) != VK_SUCCESS) { why = "bone descriptor set"; return false; }
        const VkDescriptorBufferInfo infos[3] = {
            {job.source, job.source_offset, job.source_size},
            {job.remaps ? job.remaps->buffer : job.ring->buffer, 0, VK_WHOLE_SIZE},
            {job.ring->buffer, job.ring->slot_stride * job.slot, job.ring->slot_bytes}};
        VkWriteDescriptorSet writes[3] = {};
        for (uint32_t b = 0; b < 3; ++b) {
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[b].dstSet = set; writes[b].dstBinding = b;
            writes[b].descriptorCount = 1; writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[b].pBufferInfo = &infos[b];
        }
        ctx_->UpdateDescriptorSets(ctx_->device, 3, writes, 0, nullptr);
        Push push{};
        push.source_rows0 = job.rows[0]; push.source_rows1 = job.rows[1];
        push.destination = MergedSkeleton::kStaticRows; push.pose_rows = job.ring->bones * MergedSkeleton::kRowsPerBone;
        push.first = job.first; push.count = job.count;
        push.flags = (job.poses & 3) | (job.remap && job.remaps && job.component < job.remaps->offsets.size() &&
                                        job.remaps->offsets[job.component] != UINT32_MAX ? 4u : 0u);
        push.remap_offset = push.flags & 4 ? job.remaps->offsets[job.component] : 0;
        push.scale = 1.0f;
        ctx_->CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &set, 0, nullptr);
        ctx_->CmdPushConstants(cmd, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        ctx_->CmdDispatch(cmd, (job.count + 63) / 64, 1, 1);
        dispatches_.fetch_add(1, std::memory_order_relaxed);
    }
    // The frame's draws read the rings in the application's submissions after this one.
    VkMemoryBarrier m{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    m.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; m.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ctx_->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &m, 0, nullptr, 0, nullptr);
    if (ctx_->EndCommandBuffer(cmd) != VK_SUCCESS) { why = "bone command buffer end"; return false; }
    return true;
}

VkResult BoneMerge::submit(VkQueue queue)
{
    if (!usable_) return VK_SUCCESS;
    std::vector<BoneJob> jobs;
    {
        std::lock_guard<std::mutex> lock(mx_);
        jobs.swap(jobs_); queued_.store(0, std::memory_order_relaxed);
    }
    // Draws submitted on another queue than the dispatches they read wait for those dispatches.
    if (last_queue_ && last_queue_ != queue && last_value_ > ctx_->completed()) {
        VkTimelineSemaphoreSubmitInfo values{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
        values.waitSemaphoreValueCount = 1; values.pWaitSemaphoreValues = &last_value_;
        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        VkSubmitInfo wait{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        wait.pNext = &values; wait.waitSemaphoreCount = 1; wait.pWaitSemaphores = &ctx_->timeline; wait.pWaitDstStageMask = &stage;
        const VkResult result = ctx_->QueueSubmit(queue, 1, &wait, VK_NULL_HANDLE);
        if (result != VK_SUCCESS) return result;
        queue_waits_.fetch_add(1, std::memory_order_relaxed);
        last_queue_ = queue;
    }
    if (jobs.empty()) return VK_SUCCESS;
    auto drop = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) { std::lock_guard<std::mutex> lock(jobs[i].ring->mx); if (jobs[i].ring->pending[jobs[i].slot]) --jobs[i].ring->pending[jobs[i].slot]; }
    };
    for (size_t begin = 0; begin < jobs.size(); begin += kJobsPerSubmission) {
        const size_t end = std::min(jobs.size(), begin + kJobsPerSubmission);
        const uint64_t value = ctx_->submitted.load() + 1;
        Slot& slot = slots_[value % kCommandBuffers];
        if (slot.value > ctx_->completed()) {
            // Eight submissions behind the device: wait for the oldest, briefly.
            VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
            wi.semaphoreCount = 1; wi.pSemaphores = &ctx_->timeline; wi.pValues = &slot.value;
            slot_waits_.fetch_add(1, std::memory_order_relaxed);
            if (ctx_->WaitSemaphores(ctx_->device, &wi, 500ull * 1000 * 1000) != VK_SUCCESS) { fail("bone submission ring stalled"); drop(begin, jobs.size()); return VK_SUCCESS; }
        }
        std::string why;
        if (!record(slot, jobs, begin, end, why)) { fail(why); drop(begin, jobs.size()); return VK_SUCCESS; }
        VkTimelineSemaphoreSubmitInfo values{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
        values.signalSemaphoreValueCount = 1; values.pSignalSemaphoreValues = &value;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.pNext = &values; si.commandBufferCount = 1; si.pCommandBuffers = &slot.cb;
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = &ctx_->timeline;
        const VkResult result = ctx_->QueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
        if (result != VK_SUCCESS) { fail("bone submission failed"); drop(begin, jobs.size()); return result; }
        slot.value = value; ctx_->submitted.store(value);
        for (size_t i = begin; i < end; ++i) {
            std::lock_guard<std::mutex> lock(jobs[i].ring->mx);
            jobs[i].ring->written[jobs[i].slot] = value;
            if (jobs[i].ring->pending[jobs[i].slot]) --jobs[i].ring->pending[jobs[i].slot];
        }
        last_queue_ = queue; last_value_ = value;
        submissions_.fetch_add(1, std::memory_order_relaxed);
    }
    return VK_SUCCESS;
}

void BoneMerge::status(JsonWriter& w)
{
    w.key("bones"); w.begin_object();
    w.kb("gpu", g_cfg.m2_gpu_bones); w.kb("usable", usable_); w.kv("reason", reason_);
    w.kn("jobs", jobs_total_.load()); w.kn("dispatches", dispatches_.load()); w.kn("submissions", submissions_.load());
    w.kn("rings", rings_.load()); w.kn("ring_bytes", ring_bytes_.load());
    w.kn("queue_waits", queue_waits_.load()); w.kn("slot_waits", slot_waits_.load()); w.kn("failures", failures_.load());
    w.kn("completed", ctx_->completed()); w.kn("submitted", ctx_->submitted.load());
    { std::lock_guard<std::mutex> lock(mx_); w.kv("last_error", error_); }
    w.end_object();
}

} // namespace efvk
