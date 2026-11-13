#include "gpu_analysis.h"
#include "state.h"
#include "command_owner.h"
#include "character_module.h"
#include "json.h"
#include "../../frontend/gpu/native_shader.h"
#include <set>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <thread>

namespace efvk {
namespace {
const char capture_owner_key = 0;
constexpr uint64_t kJobBytes = 8ull << 20, kFrameBytes = 64ull << 20, kResidentBytes = 128ull << 20;
struct Session {
    std::filesystem::path directory;
    uint64_t bytes = 0, next = 0, events = 0;
};
struct Readback {
    Device* device = nullptr;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    uint64_t bytes = 0, charged = 0, serial = 0;
    std::shared_ptr<std::atomic<uint64_t>> budget;
    std::shared_ptr<void> source;
    std::shared_ptr<Session> session;
    std::string file, metadata;
    ~Readback() {
        if (mapped) device->d.UnmapMemory(device->handle, memory);
        if (buffer) device->d.DestroyBuffer(device->handle, buffer, nullptr);
        if (memory) device->d.FreeMemory(device->handle, memory, nullptr);
        if (charged) budget->fetch_sub(charged);
    }
};
struct CaptureOwner : CommandOwner {
    std::vector<std::shared_ptr<Readback>> jobs;
    void reset_after_completion() override { jobs.clear(); used = false; }
    bool seal() override { return false; }
};
struct Ticket {
    std::shared_ptr<GpuAnalysis> engine;
    std::vector<std::shared_ptr<Readback>> jobs;
};
struct ImageRequest {
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t slot = 0, binding = 0, element = 0;
};
struct BufferRequest {
    VkBuffer buffer = VK_NULL_HANDLE;
    uint32_t id = 0, set = 0, binding = 0, element = 0;
    uint64_t offset = 0, bytes = 0;
    std::shared_ptr<void> owner;
    std::string reason;
};
}

class GpuAnalysis {
public:
    explicit GpuAnalysis(Device* d) : device(d) {
#define GET(name) name = reinterpret_cast<PFN_vk##name>(d->next_gdpa(d->handle, "vk" #name));
        GET(CreateSemaphore) GET(DestroySemaphore) GET(GetSemaphoreCounterValue) GET(CmdPipelineBarrier) GET(GetBufferMemoryRequirements)
#undef GET
        if(!GetSemaphoreCounterValue)GetSemaphoreCounterValue=reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(d->next_gdpa(d->handle,"vkGetSemaphoreCounterValueKHR"));
    }
    ~GpuAnalysis() { stop(); if (timeline) DestroySemaphore(device->handle, timeline, nullptr); }
    void frame(const char* directory) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!directory) { active.reset(); return; }
        if (active && active->directory == std::filesystem::u8path(directory)) return;
        active = std::make_shared<Session>(); active->directory = std::filesystem::u8path(directory);
        if (!worker.joinable()) worker = std::thread([this] { run(); });
        if (device->timeline_semaphores && !timeline && CreateSemaphore && DestroySemaphore && GetSemaphoreCounterValue && CmdPipelineBarrier && GetBufferMemoryRequirements) {
            VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO}; type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; info.pNext = &type;
            if (CreateSemaphore(device->handle, &info, nullptr, &timeline) != VK_SUCCESS) timeline = VK_NULL_HANDLE;
        }
        JsonWriter w; w.begin_object(); w.kv("state", "scope");
        w.kv("scope", "New one-time recordings on one application queue; post-dispatch and end-of-pass GPU copies; no CPU shadows"); w.end_object();
        events.emplace_back(active, w.out); wake.notify_one();
    }
    void refuse(const std::shared_ptr<Session>& session, const char* point, const std::string& reason) {
        if (session->events++ >= 256) return;
        JsonWriter w; w.begin_object(); w.kv("state", "refused"); w.kv("point", point); w.kv("reason", reason); w.end_object();
        events.emplace_back(session, w.out); wake.notify_one();
    }
    std::shared_ptr<Session> begin(CmdBuf* cb, const char* point) {
        if (!active) return {};
        std::string reason;
        if (device->application_queue_count != 1 || device->material_queues.size()!=1) reason = "Cross-queue application resource hazards are not tracked";
        else if (device->synchronization2_enabled) reason = "Synchronization2 signal scopes require a capture synchronization adapter";
        else if (!(cb->begin_flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) || (cb->begin_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT))
            reason = "Readback requires one-time non-simultaneous command recordings";
        else if (!timeline) reason = "GPU completion timeline is unavailable";
        if (!reason.empty()) { refuse(active, point, reason); return {}; }
        return active;
    }
    std::shared_ptr<Readback> allocate(const std::shared_ptr<Session>& session, uint64_t bytes, std::string& reason) {
        if (!bytes || bytes > kJobBytes || session->bytes + bytes > kFrameBytes || session->next >= 256) { reason = "GPU capture byte or job budget exceeded"; return {}; }
        auto job = std::make_shared<Readback>(); job->device = device; job->session = session; job->bytes = bytes; job->budget = budget;
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; buffer.size = bytes; buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (device->d.CreateBuffer(device->handle, &buffer, nullptr, &job->buffer) != VK_SUCCESS) { reason = "Readback buffer allocation failed"; return {}; }
        VkMemoryRequirements requirements{}; GetBufferMemoryRequirements(device->handle, job->buffer, &requirements);
        uint32_t index = UINT32_MAX;
        for (uint32_t i = 0; i < device->memory_properties.memoryTypeCount; ++i)
            if ((requirements.memoryTypeBits & (1u << i)) && (device->memory_properties.memoryTypes[i].propertyFlags &
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { index = i; break; }
        if (index == UINT32_MAX || budget->load() + requirements.size > kResidentBytes) { reason = "Coherent staging memory or resident budget unavailable"; return {}; }
        VkMemoryAllocateInfo memory{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; memory.allocationSize = requirements.size; memory.memoryTypeIndex = index;
        if (device->d.AllocateMemory(device->handle, &memory, nullptr, &job->memory) != VK_SUCCESS ||
            device->d.BindBufferMemory(device->handle, job->buffer, job->memory, 0) != VK_SUCCESS ||
            device->d.MapMemory(device->handle, job->memory, 0, bytes, 0, &job->mapped) != VK_SUCCESS) { reason = "Coherent readback mapping failed"; return {}; }
        job->charged = requirements.size; budget->fetch_add(job->charged); session->bytes += bytes;
        return job;
    }
    void buffers(CmdBuf* cb, bool compute) {
        const char* point = compute ? "post-dispatch" : "end-of-pass";
        std::lock_guard<std::mutex> lock(mutex);
        const auto session = begin(cb, point); if (!session) return;
        std::vector<BufferRequest> requests;
        {
            std::shared_lock<std::shared_mutex> resources(device->res_mx);
            const auto* sets = compute ? cb->comp_sets : cb->sets;
            for (uint32_t slot = 0; slot < kMaxSets; ++slot) {
                const auto& bound = sets[slot]; const DescriptorSetInfo* contents = bound.pushed.get();
                const auto found = device->descriptor_sets.find(bound.set);
                if (!contents && found != device->descriptor_sets.end() && found->second.id == bound.id) contents = &found->second;
                if (!contents || contents->unsupported || bound.unsupported || !contents->layout) continue;
                if(contents->layout->flags & ~VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR){refuse(session,point,"Descriptor layout flags require a capture adapter");continue;}
                size_t dynamic = 0;
                for (const auto& binding : contents->layout->bindings) {
                    if(binding.second.flags){refuse(session,point,"Descriptor binding flags require a capture adapter");continue;}
                    const auto type = binding.second.type;
                    const bool adjusted = type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
                    for (uint32_t element = 0; element < binding.second.count; ++element) {
                        const auto value = contents->values.find({binding.first, element});
                        if (value == contents->values.end() || !value->second.known || !value->second.buffer) continue;
                        BufferRequest r; r.buffer = value->second.buffer; r.set = slot; r.binding = binding.first; r.element = element; r.offset = value->second.offset;
                        const auto buffer = device->bufs.find(r.buffer);
                        if (buffer == device->bufs.end() || buffer->second.id != value->second.buffer_id) r.reason = "Buffer identity or upload owner is unavailable";
                        else {
                            const auto& b = buffer->second; r.id = b.id; r.owner = b.lifetime;
                            if (adjusted) {
                                if (dynamic + element >= bound.dyn_count || uint64_t(bound.dyn_first) + dynamic + element >= cb->dyn.size()) r.reason = "Dynamic buffer offset unavailable";
                                else if(r.offset>UINT64_MAX-cb->dyn[bound.dyn_first+dynamic+element])r.reason="Dynamic buffer offset overflow";
                                else r.offset += cb->dyn[bound.dyn_first + dynamic + element];
                            }
                            if (r.offset <= b.size) r.bytes = value->second.range == VK_WHOLE_SIZE ? b.size - r.offset : value->second.range;
                            if(!b.capture_safe)r.reason="Buffer extensions or flags require a capture adapter";
                            else if (!(b.usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) r.reason = "Buffer was not created with TRANSFER_SRC usage";
                            else if (!b.lifetime || b.lifetime->dependencies.empty()) r.reason = "Buffer allocation lifetime cannot be retained";
                            else if (!r.bytes || r.offset > b.size || r.bytes > b.size - r.offset || r.offset % 4 || r.bytes % 4) r.reason = "Buffer copy range is invalid or unaligned";
                        }
                        requests.push_back(std::move(r));
                    }
                    if (adjusted) dynamic += binding.second.count;
                }
            }
        }
        auto owner = std::static_pointer_cast<CaptureOwner>(owner_for(cb->owners, &capture_owner_key, [] {
            auto p = std::make_shared<CaptureOwner>(); p->backend = &capture_owner_key; return p;
        }));
        for (auto& r : requests) {
            if (!r.reason.empty()) { refuse(session, point, r.reason); continue; }
            std::string reason; auto job = allocate(session, r.bytes, reason);
            if (!job) { refuse(session, point, reason); continue; }
            job->source = std::move(r.owner); job->file = "buffer-" + std::to_string(++session->next) + ".buf";
            JsonWriter w; w.begin_object(); w.kv("point", point); w.kn("command_buffer", cb->id); w.kn("call", cb->draws.size());
            w.kn("resource", r.id); w.kn("set", r.set); w.kn("binding", r.binding); w.kn("element", r.element);
            w.kn("offset", r.offset); w.kn("bytes", r.bytes); w.kv("file", job->file); w.end_object(); job->metadata = w.out;
            VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; before.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            CmdPipelineBarrier(cb->handle, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
            VkBufferCopy copy{r.offset, 0, r.bytes}; device->d.CmdCopyBuffer(cb->handle, r.buffer, job->buffer, 1, &copy);
            VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; after.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            after.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
            CmdPipelineBarrier(cb->handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &after, 0, nullptr, 0, nullptr);
            owner->jobs.push_back(std::move(job)); owner->used = true;
        }
    }
    void images(CmdBuf* cb, bool compute) {
        const char* point=compute?"post-dispatch":"end-of-pass";
        std::lock_guard<std::mutex> lock(mutex);const auto session=begin(cb,point);if(!session)return;
        std::vector<ImageRequest> requests;
        std::set<std::pair<uint32_t,uint32_t>> accessed;
        std::set<VkImage> attachments;
        {
            std::shared_lock<std::shared_mutex> resources(device->res_mx);
            const auto pipeline=device->pipes.find(compute?cb->comp_pipeline:cb->gfx_pipeline);
            if(pipeline==device->pipes.end()){refuse(session,point,"Image consumer pipeline is unavailable");return;}
            for(const auto& stage:pipeline->second.shaders){
                std::vector<std::pair<uint32_t,uint32_t>> bindings;std::string reason;
                if(!stage.shader || !efmi::gpu::native_shader_image_bindings(stage.shader->code,stage.entry,bindings,reason)){
                    refuse(session,point,"Image consumer reflection is unavailable");return;
                }
                accessed.insert(bindings.begin(),bindings.end());
            }
            if(!compute && cb->pass.info)for(const auto& view:cb->pass.info->views)if(view.known)attachments.insert(view.view.image);
            const auto* sets=compute?cb->comp_sets:cb->sets;
            for(uint32_t slot=0;slot<kMaxSets;++slot){
                const auto& bound=sets[slot];const DescriptorSetInfo* contents=bound.pushed.get();
                const auto found=device->descriptor_sets.find(bound.set);
                if(!contents && found!=device->descriptor_sets.end() && found->second.id==bound.id)contents=&found->second;
                if(!contents || contents->unsupported || bound.unsupported || !contents->layout ||
                   (contents->layout->flags & ~VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR))continue;
                for(const auto& value:contents->values){
                    if(!accessed.count({slot,value.first.first}))continue;
                    const auto binding=contents->layout->bindings.find(value.first.first);
                    if(binding==contents->layout->bindings.end() || binding->second.flags || !value.second.known || !value.second.image_view)continue;
                    if(binding->second.count!=1){refuse(session,point,"Image descriptor arrays require element-use tracking");continue;}
                    // Only actual descriptors whose source is tracked. File uploads do not request transfer usage.
                    if(value.second.native){refuse(session,point,"Uploaded native images have no capture transfer usage");continue;}
                    const auto view=device->image_views.find(value.second.image_view);
                    if(view==device->image_views.end() || view->second.id!=value.second.image_view_id)continue;
                    // Attachments may have changed layout at pass end; export them from their final layout below.
                    if(attachments.count(view->second.image))continue;
                    requests.push_back({value.second.image_view,VkImageLayout(value.second.image_layout),slot,value.first.first,value.first.second});
                }
            }
            if(!compute && cb->pass.info && cb->pass.recipe && cb->pass.recipe->complete && !cb->pass.info->imageless){
                const auto& attachments=cb->pass.recipe->attachments;
                for(size_t i=0;i<cb->pass.info->views.size() && i<attachments.size();++i){
                    const auto& view=cb->pass.info->views[i];
                    if(view.known && attachments[i].storeOp==VK_ATTACHMENT_STORE_OP_STORE)
                        requests.push_back({view.handle,attachments[i].finalLayout,UINT32_MAX,uint32_t(i),0});
                }
            }
        }
        auto owner=std::static_pointer_cast<CaptureOwner>(owner_for(cb->owners,&capture_owner_key,[]{auto p=std::make_shared<CaptureOwner>();p->backend=&capture_owner_key;return p;}));
        for(const auto& r:requests){
            ImageInfo image;ImageViewInfo view;std::string reason;uint32_t bw=0,bh=0,block=0;
            {
                std::shared_lock<std::shared_mutex> resources(device->res_mx);
                const auto v=device->image_views.find(r.view);
                if(v==device->image_views.end()){refuse(session,point,"Image view unavailable");continue;}
                view=v->second;const auto i=device->images.find(view.image);
                if(i==device->images.end() || i->second.id!=view.image_id){refuse(session,point,"Image identity unavailable");continue;}
                image=i->second;
            }
            const auto range=view.range;
            if(!image.capture_safe)reason="Image extensions or flags require a capture adapter";
            else if(!view.lifetime || view.lifetime->dependencies.empty() || !image.lifetime || image.lifetime->dependencies.empty())reason="Image allocation lifetime cannot be retained (including swapchain images)";
            else if(!(image.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT))reason="Image was not created with TRANSFER_SRC usage";
            else if(image.type!=VK_IMAGE_TYPE_2D || image.samples!=VK_SAMPLE_COUNT_1_BIT || range.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT ||
                    (range.layerCount!=1 && !(range.layerCount==VK_REMAINING_ARRAY_LAYERS && image.layers-range.baseArrayLayer==1)) ||
                    (range.levelCount!=1 && !(range.levelCount==VK_REMAINING_MIP_LEVELS && image.levels-range.baseMipLevel==1)) ||
                    range.baseMipLevel>=image.levels || range.baseArrayLayer>=image.layers || image.flags ||
                    !format_block(image.format,bw,bh,block))reason="Image capture needs an owned single-sample 2D color subresource with a known format";
            else if(r.layout!=VK_IMAGE_LAYOUT_GENERAL && r.layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                    r.layout!=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && r.layout!=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)reason="Image layout cannot be restored by this capture adapter";
            if(!reason.empty()){refuse(session,point,reason);continue;}
            const uint32_t width=std::max(1u,image.extent.width>>std::min(range.baseMipLevel,31u)),height=std::max(1u,image.extent.height>>std::min(range.baseMipLevel,31u));
            const uint64_t bytes=uint64_t((width+bw-1)/bw)*((height+bh-1)/bh)*block;
            auto job=allocate(session,bytes,reason);if(!job){refuse(session,point,reason);continue;}
            job->source=view.lifetime;job->file="image-"+std::to_string(++session->next)+".bin";
            JsonWriter w;w.begin_object();w.kv("point",point);w.kn("command_buffer",cb->id);w.kn("call",cb->draws.size());w.kn("resource",image.id);
            w.kn("set",r.slot);w.kn("binding",r.binding);w.kn("element",r.element);w.kn("format",image.format);w.kn("width",width);w.kn("height",height);
            w.kn("mip",range.baseMipLevel);w.kn("layer",range.baseArrayLayer);w.kn("bytes",bytes);w.kv("file",job->file);w.end_object();job->metadata=w.out;
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            barrier.oldLayout=r.layout;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            barrier.image=view.image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,range.baseMipLevel,1,range.baseArrayLayer,1};
            CmdPipelineBarrier(cb->handle,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,range.baseMipLevel,range.baseArrayLayer,1};copy.imageExtent={width,height,1};
            device->d.CmdCopyImageToBuffer(cb->handle,view.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,job->buffer,1,&copy);
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.newLayout=r.layout;
            VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            CmdPipelineBarrier(cb->handle,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,1,&barrier);
            owner->jobs.push_back(std::move(job));owner->used=true;
        }
    }
    void stop() {
        { std::lock_guard<std::mutex> lock(mutex); stopping = true; active.reset(); wake.notify_one(); }
        if (worker.joinable()) worker.join();
    }
    void run() {
        for (;;) {
            std::vector<std::shared_ptr<Readback>> ready;
            std::vector<std::pair<std::shared_ptr<Session>, std::string>> messages;
            bool done = false;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if(pending.empty() && events.empty() && !stopping)wake.wait(lock,[&]{return stopping || !events.empty() || !pending.empty();});
                else wake.wait_for(lock, std::chrono::milliseconds(5), [&] { return stopping || !events.empty(); });
                uint64_t completed = 0;
                if (timeline && GetSemaphoreCounterValue(device->handle, timeline, &completed) != VK_SUCCESS) completed = 0;
                for (auto it = pending.begin(); it != pending.end();) {
                    if ((*it)->serial <= completed) { ready.push_back(*it); it = pending.erase(it); }
                    else if (stopping) { refuse((*it)->session, "completion", "GPU completion unavailable at device shutdown"); it = pending.erase(it); }
                    else ++it;
                }
                messages.swap(events); done = stopping;
            }
            for (const auto& job : ready) {
                const auto path = job->session->directory / job->file;
                std::ofstream out(path, std::ios::binary); out.write(static_cast<const char*>(job->mapped), std::streamsize(job->bytes)); out.close();
                auto record = job->metadata; record.pop_back(); record += out ? ",\"state\":\"complete\"}" : ",\"state\":\"write_failed\"}";
                messages.emplace_back(job->session, std::move(record));
            }
            for (const auto& event : messages) { std::ofstream out(event.first->directory / "resources.jsonl", std::ios::binary | std::ios::app); out << event.second << '\n'; }
            if (done) return;
        }
    }
    Device* device;
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    bool stopping = false;
    VkSemaphore timeline = VK_NULL_HANDLE;
    uint64_t next_serial = 0;
    std::shared_ptr<std::atomic<uint64_t>> budget = std::make_shared<std::atomic<uint64_t>>(0);
    std::shared_ptr<Session> active;
    std::vector<std::shared_ptr<Readback>> pending;
    std::vector<std::pair<std::shared_ptr<Session>, std::string>> events;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements = nullptr;
    PFN_vkCreateSemaphore CreateSemaphore = nullptr;
    PFN_vkDestroySemaphore DestroySemaphore = nullptr;
    PFN_vkGetSemaphoreCounterValue GetSemaphoreCounterValue = nullptr;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier = nullptr;
};

void gpu_analysis_frame(Device* d, const char* directory) {
    auto engine = std::atomic_load(&d->gpu_analysis);
    if (!engine && directory) { engine = std::make_shared<GpuAnalysis>(d); std::atomic_store(&d->gpu_analysis, engine); }
    if (engine) engine->frame(directory);
}
void gpu_analysis_dispatch(Device* d, CmdBuf* cb) { if (const auto engine = std::atomic_load(&d->gpu_analysis)) {engine->buffers(cb, true);engine->images(cb, true);} }
void gpu_analysis_pass(Device* d, CmdBuf* cb) { if (const auto engine = std::atomic_load(&d->gpu_analysis)) {engine->buffers(cb, false);engine->images(cb, false);} }
void gpu_analysis_prepare(Device* d, SubmitWork& work) {
    const auto engine = std::atomic_load(&d->gpu_analysis); if (!engine) return;
    auto ticket = std::make_shared<Ticket>(); ticket->engine = engine;
    std::lock_guard<std::mutex> lock(engine->mutex);
    for (const auto& batch : work.batches) for (auto* cb : batch.cbs) if (cb)
        for (const auto* owners : {&cb->owners, &cb->executed_owners}) for (const auto& owner : *owners)
            if (owner && owner->used && owner->backend == &capture_owner_key)
                for (const auto& job : std::static_pointer_cast<CaptureOwner>(owner)->jobs)
                    if (!job->serial && std::find(ticket->jobs.begin(), ticket->jobs.end(), job) == ticket->jobs.end()) ticket->jobs.push_back(job);
    if (ticket->jobs.empty()) return;
    const auto serial = ++engine->next_serial;
    for (const auto& job : ticket->jobs) job->serial = serial;
    work.signals.push_back({engine->timeline, serial}); work.analysis_ticket = std::move(ticket);
}
void gpu_analysis_submitted(SubmitWork& work, VkResult result) {
    auto ticket = std::static_pointer_cast<Ticket>(work.analysis_ticket); work.analysis_ticket.reset(); if (!ticket) return;
    auto& engine = *ticket->engine; std::lock_guard<std::mutex> lock(engine.mutex);
    for (const auto& job : ticket->jobs) {
        if (result == VK_SUCCESS) engine.pending.push_back(job);
        else engine.refuse(job->session, "submission", "Application submission failed; no completed GPU bytes exported");
    }
    engine.wake.notify_one();
}
void gpu_analysis_stop(Device* d) {
    auto engine = std::atomic_exchange(&d->gpu_analysis, std::shared_ptr<GpuAnalysis>{}); if (engine) engine->stop();
}
}
