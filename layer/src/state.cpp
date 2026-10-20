// Tracking of memory, buffers, pipelines and command buffers; frame assembly; probe requests.
#include "state.h"
#include <condition_variable>
#include <thread>
#include "m2_backend.h"
#include "pipeline_variant.h"
#include "json.h"

#include <algorithm>

namespace efvk {

// ---------------------------------------------------------------------------
// Call counters

std::atomic<uint64_t> g_api_calls[HI_COUNT];

const char* const g_api_names[HI_COUNT] = {
#define X(n) "vk" #n,
    EFVK_DEVICE_FUNCS(X)
#undef X
};

static std::mutex g_resolved_mx;
static std::vector<std::string> g_resolved;

void note_resolved(const char* name)
{
    std::lock_guard<std::mutex> lock(g_resolved_mx);
    if (g_resolved.size() >= 4096)
        return;
    for (const std::string& s : g_resolved)
        if (s == name)
            return;
    g_resolved.push_back(name);
}

// ---------------------------------------------------------------------------
// IntervalSet

void IntervalSet::add(uint64_t off, uint64_t size)
{
    if (!size)
        return;
    uint64_t end = off + size;
    std::vector<Range> out;
    out.reserve(v.size() + 1);
    bool placed = false;
    for (const Range& r : v) {
        if (r.end < off) {
            out.push_back(r);
        } else if (r.off > end) {
            if (!placed) {
                out.push_back({off, end});
                placed = true;
            }
            out.push_back(r);
        } else {
            off = std::min(off, r.off);
            end = std::max(end, r.end);
        }
    }
    if (!placed)
        out.push_back({off, end});
    v.swap(out);
}

void IntervalSet::remove(uint64_t off, uint64_t size)
{
    if (!size)
        return;
    uint64_t end = off + size;
    std::vector<Range> out;
    out.reserve(v.size() + 1);
    for (const Range& r : v) {
        if (r.end <= off || r.off >= end) {
            out.push_back(r);
            continue;
        }
        if (r.off < off)
            out.push_back({r.off, off});
        if (r.end > end)
            out.push_back({end, r.end});
    }
    v.swap(out);
}

bool IntervalSet::covers(uint64_t off, uint64_t size) const
{
    if (!size)
        return true;
    uint64_t end = off + size;
    for (const Range& r : v)
        if (r.off <= off && r.end >= end)
            return true;
    return false;
}

uint64_t IntervalSet::total() const
{
    uint64_t n = 0;
    for (const Range& r : v)
        n += r.end - r.off;
    return n;
}

// ---------------------------------------------------------------------------
// Device registry

static std::shared_mutex g_dev_mx;
static std::unordered_map<void*, Device*> g_devs;
static std::atomic<void*> g_fast_key{nullptr};
static std::atomic<Device*> g_fast_dev{nullptr};
static std::atomic<Device*> g_active{nullptr};
static std::atomic<Device*> g_passive{nullptr};
static HANDLE g_cap_event = nullptr;
static std::once_flag g_cap_event_once;

static HANDLE cap_event()
{
    std::call_once(g_cap_event_once, [] { g_cap_event = CreateEventW(nullptr, TRUE, FALSE, nullptr); });
    return g_cap_event;
}

// ---------------------------------------------------------------------------
// Stall watchdog: with no presentation for five seconds, the log names every thread inside a hook
// that can wait and every thread whose last hook is older than two seconds, every ten seconds while
// the stall lasts. Runs while a device exists, never in passthrough mode.

constexpr size_t kThreadTraces = 256;
static ThreadTrace g_thread_traces[kThreadTraces];
static std::atomic<size_t> g_thread_trace_count{0};
static std::atomic<uint64_t> g_presented_us{0};
static std::mutex g_watch_mx;
static std::condition_variable g_watch_cv;
static std::thread g_watch;
static bool g_watch_stop = false;

ThreadTrace* thread_trace()
{
    thread_local ThreadTrace* mine = nullptr;
    thread_local bool assigned = false;
    if (!assigned) {
        assigned = true;
        const size_t slot = g_thread_trace_count.fetch_add(1, std::memory_order_relaxed);
        if (slot < kThreadTraces) { mine = &g_thread_traces[slot]; mine->thread.store(GetCurrentThreadId(), std::memory_order_relaxed); }
    }
    return mine;
}

void note_presented() { g_presented_us.store(host_time_us(), std::memory_order_relaxed); }

static void watchdog()
{
    std::unique_lock<std::mutex> lock(g_watch_mx);
    uint64_t reported = 0;
    while (!g_watch_cv.wait_for(lock, std::chrono::seconds(1), [] { return g_watch_stop; })) {
        const uint64_t last = g_presented_us.load(std::memory_order_relaxed), now = host_time_us();
        if (!last || now < last || now - last < 5000000) { reported = 0; continue; }
        if (reported && now - reported < 10000000) continue;
        reported = now;
        logf("stall: no presentation returned for %llu ms", static_cast<unsigned long long>((now - last) / 1000));
        const size_t count = std::min(g_thread_trace_count.load(std::memory_order_relaxed), kThreadTraces);
        for (size_t i = 0; i < count; ++i) {
            const ThreadTrace& t = g_thread_traces[i];
            const int hook = t.hook.load(std::memory_order_relaxed);
            const uint64_t since = t.since_us.load(std::memory_order_relaxed);
            const int inside = t.inside.load(std::memory_order_relaxed);
            if (hook < 0 || hook >= HI_COUNT || !since || now < since) continue;
            const uint64_t age = (now - since) / 1000;
            if (!inside && age < 2000) continue;
            logf("stall: thread %lu %s vk%s for %llu ms", static_cast<unsigned long>(t.thread.load(std::memory_order_relaxed)),
                 inside ? "inside" : "last entered", g_api_names[hook], static_cast<unsigned long long>(age));
        }
    }
}

static void watchdog_start()
{
    if (g_cfg.mode == Mode::Passthrough) return;
    std::lock_guard<std::mutex> lock(g_watch_mx);
    if (g_watch.joinable()) return;
    g_watch_stop = false;
    g_watch = std::thread(watchdog);
}

static void watchdog_stop()
{
    std::thread done;
    {
        std::lock_guard<std::mutex> lock(g_watch_mx);
        g_watch_stop = true;
        done.swap(g_watch);
    }
    g_watch_cv.notify_all();
    if (done.joinable()) done.join();
}

Device* dev_create(VkDevice handle)
{
    watchdog_start();
    Device* dev = new Device();
    dev->handle = handle;
    void* key = *reinterpret_cast<void**>(handle);
    std::unique_lock<std::shared_mutex> lock(g_dev_mx);
    g_devs[key] = dev;
    g_fast_key.store(key, std::memory_order_release);
    g_fast_dev.store(dev, std::memory_order_release);
    if (!g_active.load())
        g_active.store(dev);
    cap_event();
    return dev;
}

void dev_destroy(Device* dev)
{
    std::unique_lock<std::shared_mutex> lock(g_dev_mx);
    for (auto it = g_devs.begin(); it != g_devs.end(); ++it) {
        if (it->second == dev) {
            if (g_fast_dev.load() == dev) {
                g_fast_key.store(nullptr, std::memory_order_release);
                g_fast_dev.store(nullptr, std::memory_order_release);
            }
            g_devs.erase(it);
            break;
        }
    }
    if (g_active.load() == dev)
        g_active.store(g_devs.empty() ? nullptr : g_devs.begin()->second);
    const bool last = g_devs.empty();
    lock.unlock();
    delete dev;
    if (last) watchdog_stop();
}

Device* dev_of_key(void* key)
{
    if (key == g_fast_key.load(std::memory_order_acquire)) {
        Device* d = g_fast_dev.load(std::memory_order_acquire);
        if (d)
            return d;
    }
    {
        std::shared_lock<std::shared_mutex> lock(g_dev_mx);
        auto it = g_devs.find(key);
        if (it != g_devs.end())
            return it->second;
    }
    Device* passive = g_passive.load(std::memory_order_acquire);
    if (passive)
        g_attach.passive_calls.fetch_add(1, std::memory_order_relaxed);
    return passive;
}

void dev_set_passive(Device* dev)
{
    dev->passive = true;
    g_passive.store(dev, std::memory_order_release);
}

DeviceRef::DeviceRef() : lock(g_dev_mx)
{
    dev = g_active.load();
}

// ---------------------------------------------------------------------------
// Command buffer lookup

void CmdBuf::reset_recording()
{
    if (m2) m2->reset_after_completion();
    character_frame = 0;
    m2_extended_bind = false;
    gfx_pipeline = VK_NULL_HANDLE;
    comp_pipeline = VK_NULL_HANDLE;
    ib = VK_NULL_HANDLE;
    ib_off = 0;
    index_size = 0;
    for (VbBind& b : vb)
        b = VbBind();
    for (SetBind& s : sets)
        s = SetBind();
    for (SetBind& s : comp_sets)
        s = SetBind();
    in_rp = false;
    draws.clear();
    dyn.clear();
    writes.clear();
    rec_snaps.clear();
    image_writes.clear();
    threads.clear();
    rp_count = 0;
    dyn_rp_count = 0;
    secondaries = 0;
    copies = 0;
}

CmdBuf* cb_of(Device* dev, VkCommandBuffer cb)
{
    static thread_local Device* t_dev = nullptr;
    static thread_local VkCommandBuffer t_handle = VK_NULL_HANDLE;
    static thread_local CmdBuf* t_cb = nullptr;
    static thread_local uint64_t t_epoch = 0;

    if (dev->passive)
        return nullptr;
    uint64_t epoch = dev->cb_epoch.load(std::memory_order_acquire);
    if (t_dev == dev && t_handle == cb && t_epoch == epoch)
        return t_cb;

    std::shared_lock<std::shared_mutex> lock(dev->cb_mx);
    auto it = dev->cbs.find(cb);
    CmdBuf* found = it == dev->cbs.end() ? nullptr : it->second.get();
    t_dev = dev;
    t_handle = cb;
    t_cb = found;
    t_epoch = epoch;
    return found;
}

static void add_thread(std::vector<uint32_t>& list, uint32_t tid)
{
    if (std::find(list.begin(), list.end(), tid) == list.end())
        list.push_back(tid);
}

// ---------------------------------------------------------------------------
// Resources

void on_alloc_memory(Device* dev, VkDeviceMemory mem, const VkMemoryAllocateInfo* info)
{
    if (dev->passive)
        return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    MemInfo& m = dev->mems[mem];
    m = MemInfo();
    m.size = info->allocationSize;
    m.type_index = info->memoryTypeIndex;
}

void on_free_memory(Device* dev, VkDeviceMemory mem)
{
    if (dev->passive)
        return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    auto it = dev->mems.find(mem);
    if (it == dev->mems.end())
        return;
    for (VkBuffer b : it->second.buffers) {
        auto bit = dev->bufs.find(b);
        if (bit != dev->bufs.end() && bit->second.mem == mem)
            bit->second.mem = VK_NULL_HANDLE;
    }
    dev->mems.erase(it);
}

void on_map_memory(Device* dev, VkDeviceMemory mem, uint64_t off, uint64_t size, void* data)
{
    if (dev->passive)
        return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    auto it = dev->mems.find(mem);
    if (it == dev->mems.end())
        return;
    MemInfo& m = it->second;
    m.mapped = static_cast<uint8_t*>(data);
    m.map_off = off;
    m.map_size = size == VK_WHOLE_SIZE ? (m.size > off ? m.size - off : 0) : size;
    m.write_combined = mapped_range_is_write_combined(data, static_cast<size_t>(m.map_size));
}

void on_unmap_memory(Device* dev, VkDeviceMemory mem)
{
    if (dev->passive)
        return;
    WorkScope work(dev->work, WK_UNMAP);
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    auto it = dev->mems.find(mem);
    if (it == dev->mems.end())
        return;
    MemInfo& m = it->second;
    if (m.mapped) {
        const uint32_t transfer = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        for (VkBuffer h : m.buffers) {
            auto bit = dev->bufs.find(h);
            if (bit == dev->bufs.end())
                continue;
            BufInfo& b = bit->second;
            if (b.size == 0 || b.mem_off >= m.map_off + m.map_size || b.mem_off + b.size <= m.map_off)
                continue; // outside the mapped range: the application could not write it
            const bool inside = b.mem_off >= m.map_off && b.size <= m.map_size - (b.mem_off - m.map_off);
            const bool source = (b.usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0;
            const bool staging = source && (b.usage & ~transfer) == 0;
            const bool small = g_cfg.unmap_snapshot_limit && b.size <= g_cfg.unmap_snapshot_limit;
            if (inside && small && (staging || (source && g_cfg.unmap_snapshot_all))) {
                // A staging buffer may be copied from after its memory is unmapped. Keep what it holds now.
                b.shadow.resize(static_cast<size_t>(b.size));
                copy_buffer_bytes(b.shadow.data(), m.mapped + (b.mem_off - m.map_off),
                                  static_cast<size_t>(b.size), m.write_combined);
                work.bytes += b.size;
                b.known.v.clear();
                b.known.add(0, b.size);
                b.gen++;
                dev->unmap_taken.fetch_add(1, std::memory_order_relaxed);
                dev->unmap_taken_bytes.fetch_add(b.size, std::memory_order_relaxed);
                continue;
            }
            // Not copied. Whatever the shadow knows from earlier buffer copies may have been
            // overwritten through the mapping without the layer seeing it, so it is no longer served.
            if (!b.known.v.empty()) {
                b.known.v.clear();
                b.gen++;
                dev->unmap_invalidated.fetch_add(1, std::memory_order_relaxed);
            }
            if (inside && small && source) {
                dev->unmap_passed.fetch_add(1, std::memory_order_relaxed);
                dev->unmap_passed_bytes.fetch_add(b.size, std::memory_order_relaxed);
            }
        }
    }
    m.mapped = nullptr;
    m.map_off = 0;
    m.map_size = 0;
    m.write_combined = false;
}

void on_create_buffer(Device* dev, VkBuffer buf, const VkBufferCreateInfo* info)
{
    if (dev->passive)
        return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    BufInfo& b = dev->bufs[buf];
    if (b.id)
        dev->buf_by_id.erase(b.id);
    b = BufInfo();
    b.id = dev->next_buf_id++;
    b.size = info->size;
    b.usage = info->usage;
    dev->buf_by_id[b.id] = buf;
}

void on_destroy_buffer(Device* dev, VkBuffer buf)
{
    if (dev->passive)
        return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    auto it = dev->bufs.find(buf);
    if (it == dev->bufs.end())
        return;
    if (it->second.mem) {
        auto mit = dev->mems.find(it->second.mem);
        if (mit != dev->mems.end()) {
            auto& list = mit->second.buffers;
            list.erase(std::remove(list.begin(), list.end(), buf), list.end());
        }
    }
    dev->buf_by_id.erase(it->second.id);
    dev->bufs.erase(it);
}

void on_bind_buffer(Device* dev, VkBuffer buf, VkDeviceMemory mem, uint64_t off)
{
    if (dev->passive)
        return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    auto it = dev->bufs.find(buf);
    if (it == dev->bufs.end())
        return;
    it->second.mem = mem;
    it->second.mem_off = off;
    auto mit = dev->mems.find(mem);
    if (mit != dev->mems.end())
        mit->second.buffers.push_back(buf);
}

static void note_use(Device* dev, const char* what, uint64_t value)
{
    char key[64];
    snprintf(key, sizeof(key), "%s:%llu", what, static_cast<unsigned long long>(value));
    dev->pipeline_uses[key]++;
}

static void note_chain(Device* dev, const char* what, const void* next)
{
    int guard = 0;
    for (auto* s = static_cast<const VkBaseInStructure*>(next); s && guard < 16; s = s->pNext, guard++)
        note_use(dev, what, static_cast<uint32_t>(s->sType));
}

// Counts what a graphics pipeline creation uses, for the day the layer has to create a pipeline
// like it. Only state the specification requires to be valid is read; state that may be ignored
// (depth-stencil and colour blend without such attachments) is counted by presence alone.
static void note_pipeline_creation_locked(Device* dev, PipeInfo& p, const VkGraphicsPipelineCreateInfo* info)
{
    dev->graphics_pipelines_created++;
    p.create_flags = info->flags;
    p.subpass = info->subpass;
    note_use(dev, "flags", info->flags);
    note_use(dev, "stages", info->stageCount);
    note_use(dev, "subpass", info->subpass);
    note_chain(dev, "next", info->pNext);
    if (info->basePipelineHandle)
        dev->pipeline_uses["base_pipeline"]++;
    if (!info->renderPass)
        dev->pipeline_uses["no_render_pass"]++;
    for (uint32_t i = 0; i < info->stageCount; i++) {
        const VkPipelineShaderStageCreateInfo& st = info->pStages[i];
        note_chain(dev, "stage_next", st.pNext);
        if (st.flags)
            note_use(dev, "stage_flags", st.flags);
        if (!st.module)
            dev->pipeline_uses["stage_without_module"]++;
        if (st.pSpecializationInfo && st.pSpecializationInfo->mapEntryCount) {
            p.specialized = true;
            dev->pipeline_uses["specialization"]++;
        }
        if (st.pName && strcmp(st.pName, "main"))
            dev->pipeline_uses["entry_point_not_main"]++;
    }
    if (const VkPipelineDynamicStateCreateInfo* dyn = info->pDynamicState) {
        note_chain(dev, "dynamic_next", dyn->pNext);
        for (uint32_t i = 0; i < dyn->dynamicStateCount; i++) {
            p.dynamic_states.push_back(static_cast<uint32_t>(dyn->pDynamicStates[i]));
            note_use(dev, "dynamic", static_cast<uint32_t>(dyn->pDynamicStates[i]));
        }
    } else {
        dev->pipeline_uses["no_dynamic_state"]++;
    }
    // A pipeline library may leave every other state out.
    if (info->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR)
        return;
    const bool mesh = (p.stages & (VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT)) != 0;
    if (!mesh && info->pVertexInputState) {
        note_chain(dev, "vertex_input_next", info->pVertexInputState->pNext);
        note_use(dev, "vertex_bindings", info->pVertexInputState->vertexBindingDescriptionCount);
    }
    if (!mesh && info->pInputAssemblyState) {
        note_use(dev, "topology", info->pInputAssemblyState->topology);
        if (info->pInputAssemblyState->primitiveRestartEnable)
            dev->pipeline_uses["primitive_restart"]++;
    }
    if (info->pTessellationState && (p.stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT))
        dev->pipeline_uses["tessellation"]++;
    bool discard = false;
    if (const VkPipelineRasterizationStateCreateInfo* rs = info->pRasterizationState) {
        note_chain(dev, "raster_next", rs->pNext);
        discard = rs->rasterizerDiscardEnable != VK_FALSE;
        if (discard)
            dev->pipeline_uses["rasterizer_discard"]++;
        if (rs->polygonMode != VK_POLYGON_MODE_FILL)
            note_use(dev, "polygon_mode", rs->polygonMode);
        if (rs->depthBiasEnable)
            dev->pipeline_uses["depth_bias"]++;
        if (rs->depthClampEnable)
            dev->pipeline_uses["depth_clamp"]++;
        note_use(dev, "cull", rs->cullMode);
    } else {
        dev->pipeline_uses["no_rasterization_state"]++;
    }
    if (!discard && info->pMultisampleState) {
        const VkPipelineMultisampleStateCreateInfo* ms = info->pMultisampleState;
        note_chain(dev, "multisample_next", ms->pNext);
        note_use(dev, "samples", ms->rasterizationSamples);
        if (ms->pSampleMask)
            dev->pipeline_uses["sample_mask"]++;
        if (ms->sampleShadingEnable)
            dev->pipeline_uses["sample_shading"]++;
        if (ms->alphaToCoverageEnable)
            dev->pipeline_uses["alpha_to_coverage"]++;
    }
    if (!discard && info->pViewportState) {
        note_chain(dev, "viewport_next", info->pViewportState->pNext);
        note_use(dev, "viewports", info->pViewportState->viewportCount);
    }
    dev->pipeline_uses[info->pDepthStencilState ? "depth_stencil_given" : "depth_stencil_null"]++;
    dev->pipeline_uses[info->pColorBlendState ? "color_blend_given" : "color_blend_null"]++;
}

template <typename Info> static void keep_render_pass(Device* dev, VkRenderPass pass, const Info* info)
{
    if (dev->passive || !g_cfg.track_resources) return;
    auto recipe = std::make_shared<RenderPassRecipe>(RenderPassRecipe::capture(*info));
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    recipe->id = dev->next_resource_id++;
    dev->render_passes[pass] = std::move(recipe);
}

void on_create_render_pass(Device* dev, VkRenderPass pass, const VkRenderPassCreateInfo* info)
{
    keep_render_pass(dev, pass, info);
}

void on_create_render_pass2(Device* dev, VkRenderPass pass, const VkRenderPassCreateInfo2* info)
{
    keep_render_pass(dev, pass, info);
}

void on_destroy_render_pass(Device* dev, VkRenderPass pass)
{
    if (dev->passive || !g_cfg.track_resources) return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    dev->render_passes.erase(pass);
}

void on_create_graphics_pipeline(Device* dev, VkPipeline pipe, const VkGraphicsPipelineCreateInfo* info)
{
    if (dev->passive)
        return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    PipeInfo& p = dev->pipes[pipe];
    if (p.id)
        dev->pipe_by_id.erase(p.id);
    std::vector<uint32_t> old_shaders = shader_ids_of(p);
    p = PipeInfo();
    forget_shaders_locked(dev, old_shaders);
    p.id = dev->next_pipe_id++;
    auto layout = dev->pipeline_layouts.find(info->layout);
    if (layout != dev->pipeline_layouts.end()) p.layout = layout->second;
    track_stages_locked(dev, p, info->stageCount, info->pStages);
    for (uint32_t i = 0; i < info->stageCount; i++)
        p.stages |= info->pStages[i].stage;
    note_pipeline_creation_locked(dev, p, info);
    if (g_cfg.track_resources) {
        auto rp = dev->render_passes.find(info->renderPass);
        p.recipe = std::make_shared<GraphicsRecipe>(GraphicsRecipe::capture(*info, p.shaders,
            rp == dev->render_passes.end() ? nullptr : rp->second));
        dev->pipeline_uses[p.recipe->complete ? "recipe_complete" : "recipe_unsupported:" + p.recipe->reason]++;
    }
    // A pipeline with a mesh or task stage ignores vertex input and input assembly state; the
    // pointers may then be anything.
    const bool mesh = (p.stages & (VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT)) != 0;
    if (!mesh && info->pInputAssemblyState)
        p.topology = info->pInputAssemblyState->topology;
    if (const VkPipelineVertexInputStateCreateInfo* vi = mesh ? nullptr : info->pVertexInputState) {
        for (uint32_t i = 0; i < vi->vertexBindingDescriptionCount; i++) {
            const VkVertexInputBindingDescription& b = vi->pVertexBindingDescriptions[i];
            p.bindings.push_back({b.binding, b.stride, b.inputRate == VK_VERTEX_INPUT_RATE_INSTANCE ? 1u : 0u});
        }
        for (uint32_t i = 0; i < vi->vertexAttributeDescriptionCount; i++) {
            const VkVertexInputAttributeDescription& a = vi->pVertexAttributeDescriptions[i];
            p.attributes.push_back({a.location, a.binding, static_cast<uint32_t>(a.format), a.offset});
        }
    }
    dev->pipe_by_id[p.id] = pipe;
}

void on_create_compute_pipeline(Device* dev, VkPipeline pipe, const VkComputePipelineCreateInfo* info)
{
    if (dev->passive)
        return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    PipeInfo& p = dev->pipes[pipe];
    if (p.id)
        dev->pipe_by_id.erase(p.id);
    std::vector<uint32_t> old_shaders = shader_ids_of(p);
    p = PipeInfo();
    forget_shaders_locked(dev, old_shaders);
    p.id = dev->next_pipe_id++;
    p.compute = true;
    p.stages = VK_SHADER_STAGE_COMPUTE_BIT;
    auto layout = dev->pipeline_layouts.find(info->layout);
    if (layout != dev->pipeline_layouts.end()) p.layout = layout->second;
    track_stages_locked(dev, p, 1, &info->stage);
    dev->pipe_by_id[p.id] = pipe;
}

void on_destroy_pipeline(Device* dev, VkPipeline pipe)
{
    if (dev->passive)
        return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    auto it = dev->pipes.find(pipe);
    if (it == dev->pipes.end())
        return;
    dev->pipe_by_id.erase(it->second.id);
    std::vector<uint32_t> shaders = shader_ids_of(it->second);
    dev->pipes.erase(it);
    forget_shaders_locked(dev, shaders);
}

// Borrow only under res_mx. No allocation/read for inaccessible memory. The caller must not
// change the source buffer's shadow allocation while using its pointer.
static const uint8_t* buffer_data_locked(Device* dev, const BufInfo& b, uint64_t off, uint64_t size,
                                         ReadSrc& source, bool& write_combined)
{
    source = SRC_NONE;
    write_combined = false;
    if (off > b.size || size > b.size - off)
        return nullptr;
    if (b.mem) {
        auto it = dev->mems.find(b.mem);
        if (it != dev->mems.end() && it->second.mapped) {
            const MemInfo& m = it->second;
            if (off <= UINT64_MAX - b.mem_off) {
                const uint64_t at = b.mem_off + off;
                if (at >= m.map_off && at - m.map_off <= m.map_size && size <= m.map_size - (at - m.map_off)) {
                    source = SRC_MAPPED;
                    write_combined = m.write_combined;
                    return m.mapped + (at - m.map_off);
                }
            }
        }
    }
    if (b.shadow.size() == b.size && b.known.covers(off, size)) {
        source = SRC_SHADOW;
        return b.shadow.data() + off;
    }
    return nullptr;
}

ReadSrc read_buffer_locked(Device* dev, const BufInfo& b, uint64_t off, uint64_t size, uint8_t* out)
{
    if (off > b.size || size > b.size - off) return SRC_NONE;
    if (!size) return SRC_SHADOW;
    ReadSrc source;
    bool write_combined;
    const auto* bytes = buffer_data_locked(dev, b, off, size, source, write_combined);
    if (bytes) copy_buffer_bytes(out, bytes, static_cast<size_t>(size), write_combined);
    return source;
}

// ---------------------------------------------------------------------------
// Command buffers

void on_alloc_cbs(Device* dev, const VkCommandBufferAllocateInfo* info, const VkCommandBuffer* cbs)
{
    if (dev->passive)
        return;
    std::unique_lock<std::shared_mutex> lock(dev->cb_mx);
    for (uint32_t i = 0; i < info->commandBufferCount; i++) {
        std::unique_ptr<CmdBuf> cb(new CmdBuf());
        cb->handle = cbs[i];
        cb->pool = info->commandPool;
        cb->id = dev->next_cb_id++;
        cb->secondary = info->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        dev->cbs[cbs[i]] = std::move(cb);
    }
    dev->cb_epoch.fetch_add(1, std::memory_order_release);
}

void on_free_cbs(Device* dev, uint32_t count, const VkCommandBuffer* cbs)
{
    if (dev->passive)
        return;
    if (!cbs)
        return;
    std::unique_lock<std::shared_mutex> lock(dev->cb_mx);
    for (uint32_t i = 0; i < count; i++)
        if (cbs[i])
            dev->cbs.erase(cbs[i]);
    dev->cb_epoch.fetch_add(1, std::memory_order_release);
}

void on_destroy_pool(Device* dev, VkCommandPool pool)
{
    if (dev->passive)
        return;
    std::unique_lock<std::shared_mutex> lock(dev->cb_mx);
    for (auto it = dev->cbs.begin(); it != dev->cbs.end();) {
        if (it->second->pool == pool)
            it = dev->cbs.erase(it);
        else
            ++it;
    }
    dev->cb_epoch.fetch_add(1, std::memory_order_release);
}

void on_reset_pool(Device* dev, VkCommandPool pool)
{
    if (dev->passive)
        return;
    WorkScope work(dev->work, WK_RESET);
    std::shared_lock<std::shared_mutex> lock(dev->cb_mx);
    for (auto& kv : dev->cbs)
        if (kv.second->pool == pool)
            kv.second->reset_recording();
}

static bool wants_shadow(const BufInfo& b)
{
    return (b.usage & g_cfg.shadow_usage) != 0;
}

void on_copy_buffer(Device* dev, CmdBuf* cb, VkBuffer src, VkBuffer dst, uint64_t src_off, uint64_t dst_off, uint64_t size)
{
    WorkScope work(dev->work, WK_COPY_RECORD);
    cb->copies++;
    std::shared_lock<std::shared_mutex> lock(dev->res_mx);
    auto dit = dev->bufs.find(dst);
    if (dit == dev->bufs.end() || !wants_shadow(dit->second))
        return;
    if (size == VK_WHOLE_SIZE) {
        auto sit = dev->bufs.find(src);
        if (sit == dev->bufs.end() || sit->second.size < src_off)
            return;
        size = sit->second.size - src_off;
    }
    PendingWrite w;
    w.dst = dst;
    w.dst_off = dst_off;
    w.size = size;
    w.src = src;
    w.src_off = src_off;
    auto sit = dev->bufs.find(src);
    if (sit != dev->bufs.end() && size && size <= (256ull << 20)) {
        ReadSrc source;
        bool write_combined;
        if (const auto* bytes = buffer_data_locked(dev, sit->second, src_off, size, source, write_combined)) {
            w.data.resize(static_cast<size_t>(size));
            copy_buffer_bytes(w.data.data(), bytes, static_cast<size_t>(size), write_combined);
            w.have_data = true;
            work.bytes += size;
        }
    }
    cb->writes.push_back(std::move(w));
}

void on_copy_buffer_to_image(Device* dev, CmdBuf* cb, VkBuffer src, VkImage dst, uint64_t src_off, uint32_t row_length,
                             uint32_t image_height, const VkImageSubresourceLayers& layers, VkOffset3D offset,
                             VkExtent3D extent)
{
    // Only the first level of the first layer identifies a texture here; the DX11 side hashes the same.
    if (!g_cfg.image_hash || !g_cfg.track_resources || dev->passive || layers.mipLevel != 0 || layers.baseArrayLayer != 0)
        return;
    PendingImageWrite w;
    w.dst = dst;
    w.src = src;
    w.src_off = src_off;
    w.row_length = row_length;
    w.image_height = image_height;
    w.ox = offset.x;
    w.oy = offset.y;
    w.oz = offset.z;
    w.w = extent.width;
    w.h = extent.height;
    w.d = extent.depth;
    cb->image_writes.push_back(w);
}

void on_update_buffer(Device* dev, CmdBuf* cb, VkBuffer dst, uint64_t off, uint64_t size, const void* data)
{
    cb->copies++;
    std::shared_lock<std::shared_mutex> lock(dev->res_mx);
    auto dit = dev->bufs.find(dst);
    if (dit == dev->bufs.end() || !wants_shadow(dit->second))
        return;
    PendingWrite w;
    w.dst = dst;
    w.dst_off = off;
    w.size = size;
    w.have_data = true;
    w.data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    cb->writes.push_back(std::move(w));
}

void on_fill_buffer(Device* dev, CmdBuf* cb, VkBuffer dst, uint64_t off, uint64_t size, uint32_t value)
{
    cb->copies++;
    std::shared_lock<std::shared_mutex> lock(dev->res_mx);
    auto dit = dev->bufs.find(dst);
    if (dit == dev->bufs.end() || !wants_shadow(dit->second) || off > dit->second.size)
        return;
    if (size == VK_WHOLE_SIZE)
        size = (dit->second.size - off) & ~3ull;
    PendingWrite w;
    w.dst = dst;
    w.dst_off = off;
    w.size = size;
    w.have_data = true;
    w.data.resize(static_cast<size_t>(size));
    for (uint64_t i = 0; i + 4 <= size; i += 4)
        memcpy(w.data.data() + i, &value, 4);
    cb->writes.push_back(std::move(w));
}

void on_execute_commands(Device* dev, CmdBuf* cb, uint32_t count, const VkCommandBuffer* cbs)
{
    uint32_t rp = cb->in_rp ? cb->rp_count + cb->dyn_rp_count : 0;
    for (uint32_t i = 0; i < count; i++) {
        CmdBuf* s;
        {
            std::shared_lock<std::shared_mutex> lock(dev->cb_mx);
            auto it = dev->cbs.find(cbs[i]);
            s = it == dev->cbs.end() ? nullptr : it->second.get();
        }
        if (!s)
            continue;
        uint32_t dyn_base = static_cast<uint32_t>(cb->dyn.size());
        uint32_t draw_base = static_cast<uint32_t>(cb->draws.size());
        cb->dyn.insert(cb->dyn.end(), s->dyn.begin(), s->dyn.end());
        for (RecSnap rs : s->rec_snaps) {
            rs.draw += draw_base;
            cb->rec_snaps.push_back(std::move(rs));
        }
        for (DrawRec r : s->draws) {
            for (SetBind& sb : r.sets)
                sb.dyn_first += dyn_base;
            if (rp)
                r.rp = rp;
            cb->draws.push_back(r);
        }
        cb->writes.insert(cb->writes.end(), s->writes.begin(), s->writes.end());
        cb->image_writes.insert(cb->image_writes.end(), s->image_writes.begin(), s->image_writes.end());
        cb->copies += s->copies;
        cb->rp_count += s->rp_count;
        cb->dyn_rp_count += s->dyn_rp_count;
        cb->secondaries += 1 + s->secondaries;
        for (uint32_t t : s->threads)
            add_thread(cb->threads, t);
    }
    // Descriptor state in the primary is undefined after executing secondaries.
    // Recorded draws retain their push snapshots independently of these bindings.
    on_unsupported_descriptor_bind(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, "secondary_execution");
    on_unsupported_descriptor_bind(cb, VK_PIPELINE_BIND_POINT_COMPUTE, "secondary_execution");
}

// ---------------------------------------------------------------------------
// Frame assembly

static void apply_writes(Device* dev, CmdBuf* cb, FrameStats& stats)
{
    if (cb->writes.empty())
        return;
    WorkScope work(dev->work, WK_BUFFER_SUBMIT);
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    for (const PendingWrite& w : cb->writes) {
        auto dit = dev->bufs.find(w.dst);
        if (dit == dev->bufs.end())
            continue;
        BufInfo& d = dit->second;
        if (!w.size || w.dst_off > d.size || w.size > d.size - w.dst_off)
            continue;
        const uint8_t* bytes = nullptr;
        bool write_combined = false;
        if (w.src) {
            // What the staging buffer holds at submit time is what the GPU will copy.
            auto sit = dev->bufs.find(w.src);
            if (sit != dev->bufs.end()) {
                ReadSrc source;
                bytes = buffer_data_locked(dev, sit->second, w.src_off, w.size, source, write_combined);
            }
        }
        if (!bytes && w.have_data)
            bytes = w.data.data();
        if (bytes) {
            if (d.shadow.size() != d.size)
                d.shadow.resize(static_cast<size_t>(d.size));
            // A self-copy from a shadow already has full storage, so resize cannot invalidate
            // its borrowed pointer. memmove also handles aliasing without a temporary copy.
            copy_buffer_bytes(d.shadow.data() + w.dst_off, bytes, static_cast<size_t>(w.size), write_combined);
            work.bytes += w.size;
            d.known.add(w.dst_off, w.size);
        } else {
            d.known.remove(w.dst_off, w.size);
            d.unknown_writes++;
            stats.unknown_writes++;
        }
        d.gen++;
    }
}

// ---------------------------------------------------------------------------
// Buffer contents of selected draws, read when the draw is recorded and again at submit

// One range a snapshot of a draw consists of, with the bytes as they are now.
struct SnapItem {
    uint8_t kind = SNAP_UNIFORM;
    uint8_t set = 0;
    uint8_t src = SRC_NONE;
    uint32_t binding = 0, element = 0, word = 0, value = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    uint32_t buf_id = 0;
    uint64_t off = 0, size = 0;
    uint32_t crc = 0;
    const char* skipped = nullptr;
    std::vector<uint8_t> data; // empty unless the caller asked for the bytes
};

static bool snap_selects(const SnapRequest& req, const DrawRec& r, bool hash_valid, uint32_t hash)
{
    if (r.kind != DK_INDEXED)
        return false;
    return req.all || (hash_valid && std::binary_search(req.hashes.begin(), req.hashes.end(), hash));
}

static void read_item_locked(Device* dev, SnapItem& item, bool keep, std::vector<uint8_t>& tmp)
{
    auto it = dev->bufs.find(item.buffer);
    if (it == dev->bufs.end())
        return;
    tmp.resize(static_cast<size_t>(item.size));
    item.src = read_buffer_locked(dev, it->second, item.off, item.size, tmp.data());
    if (item.src == SRC_NONE)
        return;
    item.crc = crc32c(0, tmp.data(), tmp.size());
    if (keep)
        item.data = tmp;
}

// Uniform ranges first, in set and binding order, then the follow windows. `dyn` are the dynamic
// offsets of the command buffer the draw's set binds index into. The caller holds res_mx.
static void read_snapshot_locked(Device* dev, const SnapRequest& req, const DrawRec& r, const std::vector<uint32_t>& dyn,
                                 bool keep, std::vector<SnapItem>& out)
{
    std::vector<BoundRange> ranges;
    for (uint32_t i = 0; i < kMaxSets; i++) {
        const SetBind& s = r.sets[i];
        bool have = s.dyn_count && static_cast<size_t>(s.dyn_first) + s.dyn_count <= dyn.size();
        collect_buffer_ranges_locked(dev, s, have ? dyn.data() + s.dyn_first : nullptr, have ? s.dyn_count : 0, i, ranges);
    }
    std::vector<uint8_t> tmp;
    std::vector<uint32_t> values;
    for (const BoundRange& br : ranges) {
        if (br.type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && br.type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
            continue;
        SnapItem item;
        item.set = br.set;
        item.binding = br.binding;
        item.element = br.element;
        item.buffer = br.buffer;
        item.buf_id = br.buffer_id;
        item.off = br.off;
        item.size = br.size;
        bool source = req.follow && br.set == req.follow_set && br.binding == req.follow_binding && br.element == 0;
        if (br.size > req.max_range)
            item.skipped = "larger_than_max_range";
        else
            read_item_locked(dev, item, keep, tmp);
        out.push_back(std::move(item));
        uint64_t words = 4ull * req.follow_count;
        if (source && words && req.follow_offset <= br.size && words <= br.size - req.follow_offset) {
            // The values are read on their own, so they do not depend on the range being kept.
            auto bit = dev->bufs.find(br.buffer);
            values.resize(req.follow_count);
            if (bit == dev->bufs.end() ||
                read_buffer_locked(dev, bit->second, br.off + req.follow_offset, words, reinterpret_cast<uint8_t*>(values.data())) == SRC_NONE)
                values.clear();
        }
        if (source && req.follow_entry && req.follow_entry <= br.size) {
            SnapItem entry;
            entry.kind = SNAP_ENTRY;
            entry.set = br.set;
            entry.binding = br.binding;
            entry.element = br.element;
            entry.buffer = br.buffer;
            entry.buf_id = br.buffer_id;
            entry.off = br.off;
            entry.size = req.follow_entry;
            read_item_locked(dev, entry, keep, tmp);
            out.push_back(std::move(entry));
        }
    }
    for (uint32_t w = 0; w < values.size(); w++) {
        uint64_t start = static_cast<uint64_t>(values[w]) * req.follow_stride;
        bool fits = false;
        for (const BoundRange& br : ranges) {
            if (br.type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && br.type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
                continue;
            if (start > br.size || req.follow_bytes > br.size - start)
                continue;
            fits = true;
            SnapItem item;
            item.kind = SNAP_FOLLOW;
            item.set = br.set;
            item.binding = br.binding;
            item.element = br.element;
            item.word = w;
            item.value = values[w];
            item.buffer = br.buffer;
            item.buf_id = br.buffer_id;
            item.off = br.off + start;
            item.size = req.follow_bytes;
            read_item_locked(dev, item, keep, tmp);
            out.push_back(std::move(item));
        }
        if (!fits) {
            // Keep the value visible although no storage range of the draw can hold the window.
            SnapItem item;
            item.kind = SNAP_FOLLOW;
            item.word = w;
            item.value = values[w];
            item.size = req.follow_bytes;
            item.skipped = "no_storage_range_fits";
            out.push_back(std::move(item));
        }
    }
}

void on_record_indexed(Device* dev, CmdBuf* cb)
{
    if (!dev->snap_recording.load(std::memory_order_relaxed))
        return;
    std::shared_ptr<const SnapRequest> req = std::atomic_load(&dev->snap_live);
    if (!req || cb->draws.empty())
        return;
    const DrawRec& r = cb->draws.back();
    std::shared_lock<std::shared_mutex> lock(dev->res_mx);
    if (!req->all) {
        // Selection is by region hash, so the index bytes have to be readable already.
        auto bit = dev->bufs.find(r.ib);
        if (bit == dev->bufs.end() || !r.index_size || !r.count)
            return;
        uint64_t size = static_cast<uint64_t>(r.index_size) * r.count;
        if (size > (256ull << 20))
            return;
        std::vector<uint8_t> tmp(static_cast<size_t>(size));
        if (read_buffer_locked(dev, bit->second, r.ib_off + static_cast<uint64_t>(r.index_size) * r.first, size, tmp.data()) == SRC_NONE)
            return;
        if (!snap_selects(*req, r, true, crc32c(0, tmp.data(), tmp.size())))
            return;
    }
    std::vector<SnapItem> items;
    read_snapshot_locked(dev, *req, r, cb->dyn, false, items);
    RecSnap rs;
    rs.draw = static_cast<uint32_t>(cb->draws.size() - 1);
    rs.ranges.reserve(items.size());
    for (const SnapItem& item : items) {
        RecRange rr;
        rr.kind = item.kind;
        rr.set = item.set;
        rr.src = item.src;
        rr.binding = item.binding;
        rr.element = item.element;
        rr.word = item.word;
        rr.buffer = item.buffer;
        rr.off = item.off;
        rr.size = item.size;
        rr.crc = item.crc;
        rs.ranges.push_back(rr);
    }
    cb->rec_snaps.push_back(std::move(rs));
}

// Keeps what the selected draw reads as it is now, at submit, and compares it with the record-time read.
static void snapshot_draw_locked(Device* dev, Capture& cap, CapDraw& c, const std::vector<uint32_t>& dyn, const RecSnap* rec)
{
    const SnapRequest& req = *cap.snap;
    std::vector<SnapItem> items;
    read_snapshot_locked(dev, req, c.r, dyn, true, items);
    c.snap_selected = true;
    cap.snap_draws++;
    c.snaps.reserve(items.size());
    for (SnapItem& item : items) {
        DrawSnap s;
        s.kind = item.kind;
        s.set = item.set;
        s.src = item.src;
        s.binding = item.binding;
        s.element = item.element;
        s.word = item.word;
        s.value = item.value;
        s.buf_id = item.buf_id;
        s.off = item.off;
        s.size = item.size;
        s.crc = item.crc;
        s.skipped = item.skipped;
        if (item.src != SRC_NONE) {
            auto key = std::make_tuple(item.buf_id, item.off, item.size);
            auto known = cap.blob_index.find(key);
            if (known != cap.blob_index.end()) {
                s.blob = known->second;
            } else if (cap.snap_bytes + item.size <= req.budget) {
                SnapBlob b;
                b.buf_id = item.buf_id;
                b.off = item.off;
                b.size = item.size;
                b.src = item.src;
                b.crc = item.crc;
                b.data = std::move(item.data);
                s.blob = static_cast<int32_t>(cap.blobs.size());
                cap.snap_bytes += b.size;
                cap.blobs.push_back(std::move(b));
                cap.blob_index[key] = s.blob;
            } else {
                cap.snap_omitted++;
            }
        }
        if (rec && !item.skipped) {
            for (const RecRange& rr : rec->ranges) {
                if (rr.kind != item.kind || rr.set != item.set || rr.binding != item.binding || rr.element != item.element ||
                    rr.word != item.word)
                    continue;
                s.rec_src = rr.src;
                s.rec_crc = rr.crc;
                if (rr.buffer != item.buffer || rr.off != item.off || rr.size != item.size)
                    s.rec = REC_OTHER;
                else if (rr.src == SRC_NONE || item.src == SRC_NONE)
                    s.rec = REC_UNREADABLE;
                else
                    s.rec = rr.crc == item.crc ? REC_EQUAL : REC_DIFFERENT;
                break;
            }
        }
        if (!item.skipped)
            cap.rec_counts[s.rec]++;
        c.snaps.push_back(s);
    }
}

static void capture_draws(Device* dev, CmdBuf* cb, Capture& cap, uint32_t submit)
{
    std::shared_lock<std::shared_mutex> lock(dev->res_mx);
    std::vector<uint8_t> tmp;
    cap.draws.reserve(cap.draws.size() + cb->draws.size());
    size_t rec_at = 0;
    uint32_t position = 0;
    for (const DrawRec& r : cb->draws) {
        CapDraw c;
        c.r = r;
        c.submit = submit;
        const uint32_t at = position++;
        if (r.pipeline) {
            auto pit = dev->pipes.find(r.pipeline);
            if (pit != dev->pipes.end())
                c.pipe_id = pit->second.id;
        }
        if (r.indirect) {
            auto iit = dev->bufs.find(r.indirect);
            if (iit != dev->bufs.end())
                c.indirect_id = iit->second.id;
        }
        for (uint32_t i = 0; i < kMaxVb; i++) {
            if (!r.vb[i].buf)
                continue;
            auto vit = dev->bufs.find(r.vb[i].buf);
            if (vit != dev->bufs.end())
                c.vb_id[i] = vit->second.id;
        }
        for (uint32_t i = 0; i < kMaxSets; i++) {
            const SetBind& s = r.sets[i];
            if (s.dyn_count && static_cast<size_t>(s.dyn_first) + s.dyn_count <= cb->dyn.size())
                c.dyn[i].assign(cb->dyn.begin() + s.dyn_first, cb->dyn.begin() + s.dyn_first + s.dyn_count);
            if (cap.descriptor_bytes < cap.descriptor_budget) {
                JsonWriter descriptors;
                write_descriptor_set_locked(descriptors, dev, s, c.dyn[i], r.pipeline, i);
                cap.descriptor_bytes += descriptors.out.size();
                c.descriptors[i] = std::move(descriptors.out);
            } else {
                c.descriptors[i] = "{\"tracked\":false,\"reason\":\"capture_descriptor_budget\"}";
                cap.descriptors_omitted++;
            }
        }
        if (r.ib) {
            auto bit = dev->bufs.find(r.ib);
            if (bit != dev->bufs.end()) {
                const BufInfo& b = bit->second;
                c.ib_id = b.id;
                if (r.kind == DK_INDEXED && r.index_size && r.count) {
                    uint64_t off = r.ib_off + static_cast<uint64_t>(r.index_size) * r.first;
                    uint64_t size = static_cast<uint64_t>(r.index_size) * r.count;
                    if (size <= (256ull << 20)) {
                        tmp.resize(static_cast<size_t>(size));
                        c.hash_src = read_buffer_locked(dev, b, off, size, tmp.data());
                        if (c.hash_src != SRC_NONE) {
                            c.hash = crc32c(0, tmp.data(), static_cast<size_t>(size));
                            c.hash_valid = true;
                        }
                    }
                }
            }
        }
        if (cap.snap) {
            while (rec_at < cb->rec_snaps.size() && cb->rec_snaps[rec_at].draw < at)
                rec_at++;
            const RecSnap* rec = rec_at < cb->rec_snaps.size() && cb->rec_snaps[rec_at].draw == at ? &cb->rec_snaps[rec_at] : nullptr;
            if (snap_selects(*cap.snap, r, c.hash_valid, c.hash))
                snapshot_draw_locked(dev, cap, c, cb->dyn, rec);
        }
        cap.draws.push_back(std::move(c));
    }
}

void on_submit(Device* dev, uint32_t count, const VkCommandBuffer* cbs)
{
    if (dev->passive)
        return;
    WorkScope work(dev->work, WK_SUBMIT);
    std::lock_guard<std::mutex> lock(dev->frame_mx);
    FrameStats& st = dev->cur;
    uint32_t submit = st.submits++;
    for (uint32_t i = 0; i < count; i++) {
        CmdBuf* cb;
        {
            std::shared_lock<std::shared_mutex> cl(dev->cb_mx);
            auto it = dev->cbs.find(cbs[i]);
            cb = it == dev->cbs.end() ? nullptr : it->second.get();
        }
        if (!cb)
            continue;
        apply_writes(dev, cb, st);
        apply_image_writes(dev, cb);
        st.command_buffers++;
        st.secondaries += cb->secondaries;
        st.render_passes += cb->rp_count;
        st.dynamic_render_passes += cb->dyn_rp_count;
        st.buffer_copies += cb->copies;
        uint32_t n = 0;
        for (const DrawRec& r : cb->draws) {
            switch (r.kind) {
            case DK_DRAW: st.draws++; n++; break;
            case DK_INDEXED: st.draws++; st.indexed++; n++; break;
            case DK_INDIRECT: st.draws++; st.indirect++; n++; break;
            case DK_INDEXED_INDIRECT: st.draws++; st.indexed++; st.indirect++; n++; break;
            default: st.dispatches++; break;
            }
        }
        st.max_draws_per_cb = std::max(st.max_draws_per_cb, n);
        for (uint32_t t : cb->threads)
            add_thread(st.threads, t);
        if (dev->cap_state == 2 && dev->cap_building)
            capture_draws(dev, cb, *dev->cap_building, submit);
    }
}

void on_present(Device* dev, PresentWork* present)
{
    if (dev->passive)
        return;
    std::lock_guard<std::mutex> lock(dev->frame_mx);
    // Timestamp consecutive host present boundaries, including any previous driver present wait.
    // Sample inside the lock to preserve order if several queues present on different threads.
    const uint64_t us = host_time_us();
    dev->pacing.observe(us, dev->frame_no, dev->image_hash_us.load(std::memory_order_relaxed), dev->cap_state == 2);
    dev->cur.frame = dev->frame_no;
    m2_frame(dev, dev->cur.draws != 0, present);
    dev->total_draws += dev->cur.draws;
    dev->max_threads = std::max(dev->max_threads, static_cast<uint32_t>(dev->cur.threads.size()));
    dev->max_command_buffers = std::max(dev->max_command_buffers, dev->cur.command_buffers);
    dev->max_submits = std::max(dev->max_submits, dev->cur.submits);
    dev->max_secondaries = std::max(dev->max_secondaries, dev->cur.secondaries);
    dev->last = dev->cur;

    if (dev->cap_state == 2 && dev->cap_building && dev->cap_wait_draws && !dev->cur.draws) {
        // Keep record-time reads armed while waiting for a graphics submission. Other queues
        // may present during command recording. Retain only the next interval's contents and
        // budget, not a concatenation of upload/compute intervals or their resource snapshots.
        const uint64_t skipped = dev->cap_building->skipped_empty_intervals + 1;
        dev->cap_building = std::make_shared<Capture>();
        dev->cap_building->wait_draws = true;
        dev->cap_building->skipped_empty_intervals = skipped;
        dev->cap_building->descriptor_budget = dev->cap_descriptor_budget;
        dev->cap_building->snap = dev->cap_snap;
    } else if (dev->cap_state == 2 && dev->cap_building) {
        dev->cap_building->stats = dev->cur;
        dev->cap_building->seq = ++dev->cap_seq;
        dev->cap_last = dev->cap_building;
        dev->cap_building.reset();
        dev->cap_state = 0;
        dev->snap_recording.store(false, std::memory_order_relaxed);
        std::atomic_store(&dev->snap_live, std::shared_ptr<const SnapRequest>());
        SetEvent(cap_event());
    } else if (dev->cap_state == 1) {
        dev->cap_state = 2;
        dev->cap_building = std::make_shared<Capture>();
        dev->cap_building->wait_draws = dev->cap_wait_draws;
        dev->cap_building->descriptor_budget = dev->cap_descriptor_budget;
        dev->cap_building->snap = dev->cap_snap;
        // Draws recorded from here on are read at record time as well; the pointer goes first.
        std::atomic_store(&dev->snap_live, dev->cap_snap);
        dev->snap_recording.store(dev->cap_snap != nullptr, std::memory_order_relaxed);
    }

    dev->cur = FrameStats();
    dev->frame_no++;
    g_active.store(dev, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Probe requests

static std::string fail(const char* msg)
{
    JsonWriter w;
    w.begin_object();
    w.kb("ok", false);
    w.kv("error", msg);
    w.end_object();
    return w.out;
}

static void write_stats(JsonWriter& w, const FrameStats& s)
{
    w.begin_object();
    w.kn("frame", s.frame);
    w.kn("submits", s.submits);
    w.kn("command_buffers", s.command_buffers);
    w.kn("secondary_command_buffers", s.secondaries);
    w.kn("render_passes", s.render_passes);
    w.kn("dynamic_render_passes", s.dynamic_render_passes);
    w.kn("draws", s.draws);
    w.kn("indexed", s.indexed);
    w.kn("indirect", s.indirect);
    w.kn("dispatches", s.dispatches);
    w.kn("buffer_copies", s.buffer_copies);
    w.kn("unknown_writes", s.unknown_writes);
    w.kn("max_draws_per_command_buffer", s.max_draws_per_cb);
    w.key("threads");
    w.begin_array();
    for (uint32_t t : s.threads)
        w.num(t);
    w.end_array();
    w.end_object();
}

static const char* src_name(uint8_t s)
{
    switch (s) {
    case SRC_MAPPED: return "mapped";
    case SRC_SHADOW: return "shadow";
    default: return "none";
    }
}

static const char* kind_name(uint8_t k)
{
    switch (k) {
    case DK_DRAW: return "draw";
    case DK_INDEXED: return "indexed";
    case DK_INDIRECT: return "indirect";
    case DK_INDEXED_INDIRECT: return "indexed_indirect";
    case DK_DISPATCH: return "dispatch";
    default: return "dispatch_indirect";
    }
}

static const char* snap_name(uint8_t k)
{
    switch (k) {
    case SNAP_FOLLOW: return "follow";
    case SNAP_ENTRY: return "entry";
    default: return "uniform";
    }
}

static const char* rec_name(uint8_t r)
{
    switch (r) {
    case REC_EQUAL: return "equal";
    case REC_DIFFERENT: return "different";
    case REC_OTHER: return "other_range";
    case REC_UNREADABLE: return "unreadable";
    default: return "not_recorded";
    }
}

static void write_pacing(JsonWriter& w, const PresentPacing& p)
{
    auto sample = [&w](const PresentSample& s) {
        w.begin_object(); w.kn("frame", s.frame); w.kn("microseconds", s.us);
        w.kn("image_hash_microseconds", s.image_hash_us); w.kb("capture", s.capture);
        w.kn("host_end_us", s.end_us); w.end_object();
    };
    w.begin_object();
    w.kv("measurement", "host_present_interval");
    w.kn("window_capacity", PresentPacing::capacity);
    w.kn("window_intervals", p.size);
    w.kn("total_intervals", p.intervals);
    w.kn("total_over_16ms", p.over_16ms); w.kn("total_over_33ms", p.over_33ms); w.kn("total_over_50ms", p.over_50ms);
    w.kn("total_capture_intervals", p.captures);
    w.key("lifetime_peak");
    if (p.intervals) sample(p.peak); else w.null();
    w.key("recent");
    if (!p.size) {
        w.null();
    } else {
        std::vector<PresentSample> sorted(p.samples.begin(), p.samples.begin() + p.size);
        std::sort(sorted.begin(), sorted.end(), [](const PresentSample& a, const PresentSample& b) { return a.us < b.us; });
        uint64_t sum = 0, over16 = 0, over33 = 0, over50 = 0, captures = 0;
        for (const auto& s : sorted) {
            sum += s.us; over16 += s.us > 16000; over33 += s.us > 33000; over50 += s.us > 50000; captures += s.capture;
        }
        const auto& last = p.samples[(p.next + PresentPacing::capacity - 1) % PresentPacing::capacity];
        const auto& first = p.samples[p.size == PresentPacing::capacity ? p.next : 0];
        w.begin_object();
        w.kn("first_frame", first.frame); w.kn("last_frame", last.frame);
        w.kn("mean_us", sum / p.size); w.kn("min_us", sorted.front().us);
        w.kn("p50_us", sorted[(p.size * 50 + 99) / 100 - 1].us);
        w.kn("p95_us", sorted[(p.size * 95 + 99) / 100 - 1].us);
        w.kn("p99_us", sorted[(p.size * 99 + 99) / 100 - 1].us);
        w.kn("max_us", sorted.back().us);
        w.kn("over_16ms", over16); w.kn("over_33ms", over33); w.kn("over_50ms", over50);
        w.kn("capture_intervals", captures);
        w.key("worst"); w.begin_array();
        for (size_t i = 0; i < std::min<size_t>(8, p.size); ++i) sample(sorted[p.size - 1 - i]);
        w.end_array(); w.end_object();
    }
    w.end_object();
}

static void write_tracker_work(JsonWriter& w, TrackerWork& work)
{
    const auto s = work.snapshot();
    w.begin_object();
    w.kb("enabled", g_cfg.profile_tracker);
    w.kv("clock", "qpc_microseconds");
    w.kv("measurement", "tracker_wall_time_including_lock_wait_excluding_driver");
    w.kn("slow_threshold_us", TrackerWork::slow_us);
    w.kn("slow_capacity", WorkSnapshot::capacity);
    w.kn("slow_total", s.slow_count);
    w.key("categories"); w.begin_object();
    for (size_t i = 0; i < WK_COUNT; ++i) {
        w.key(work_name(static_cast<WorkKind>(i))); w.begin_object();
        const auto& t = s.totals[i];
        w.kn("calls", t.calls); w.kn("microseconds", t.us); w.kn("max_us", t.max_us);
        w.kn("copied_bytes", t.bytes); w.end_object();
    }
    w.end_object();
    w.key("recent_slow"); w.begin_array();
    for (size_t i = 0; i < s.size; ++i) {
        const auto& e = s.events[i];
        w.begin_object(); w.kv("category", work_name(e.kind)); w.kn("host_start_us", e.start_us);
        w.kn("microseconds", e.us); w.kn("copied_bytes", e.bytes); w.end_object();
    }
    w.end_array(); w.end_object();
}

std::string probe_status()
{
    JsonWriter w;
    w.begin_object();
    w.kb("ok", true);
    w.key("result");
    w.begin_object();
    w.kv("version", EFVK_VERSION);
    w.kn("protocol", EFVK_PROTOCOL);
    w.kn("pid", GetCurrentProcessId());
    w.kv("exe", exe_name());
    w.kv("mode", g_cfg.mode == Mode::Passthrough ? "passthrough" : "probe");
    w.kv("crc", crc32c_uses_hardware() ? "sse4.2" : "table");
    w.kv("entry", g_cfg.entry == Entry::Inject ? "inject" : "layer");
    w.kb("resources", g_cfg.track_resources);
    w.kn("shader_budget", g_cfg.shader_budget);
    w.kn("unmap_snapshot_limit", g_cfg.unmap_snapshot_limit);
    w.kb("unmap_snapshot_all", g_cfg.unmap_snapshot_all);
    w.kn("shadow_usage", g_cfg.shadow_usage);
    w.kv("mapped_read", streaming_reads_available() ? "wc_sse4.1" : "memmove");
    w.key("attach");
    w.begin_object();
    w.kb("loader_found", g_attach.loader_found.load() != 0);
    w.kb("loader_was_loaded", g_attach.loader_was_loaded.load() != 0);
    w.kn("exports_redirected", g_attach.exports_redirected.load());
    w.kn("instances_created", g_attach.instances_created.load());
    w.kn("devices_created", g_attach.devices_created.load());
    w.kn("passive_calls", g_attach.passive_calls.load());
    w.end_object();
    w.key("features");
    w.begin_array();
    w.str("capture");
    w.str("capture_wait_draws");
    w.str("buffers");
    w.str("read_buffer");
    w.str("hash");
    w.str("pipeline");
    w.str("api_usage");
    w.str("descriptor_sets");
    w.str("descriptor_templates");
    w.str("push_descriptors");
    w.str("shader");
    w.str("snapshot");
    w.str("set");
    w.str("host_pacing");
    w.str("tracker_work");
    w.str("m2_component");
    w.end_array();

    DeviceRef ref;
    Device* dev = ref.dev;
    m2_status(dev, w);
    if (!dev) {
        w.key("device");
        w.null();
        w.end_object();
        w.end_object();
        return w.out;
    }

    w.key("device");
    w.begin_object();
    w.kv("name", dev->gpu_name);
    char api[32];
    snprintf(api, sizeof(api), "%u.%u.%u", VK_API_VERSION_MAJOR(dev->api_version), VK_API_VERSION_MINOR(dev->api_version),
             VK_API_VERSION_PATCH(dev->api_version));
    w.kv("api_version", api);
    w.kn("vendor_id", dev->vendor_id);
    w.kn("device_id", dev->device_id);
    w.kn("driver_version", dev->driver_version);
    w.kn("application_api", dev->app_api_version);
    w.kb("null_descriptors", dev->null_descriptors);
    w.kb("null_descriptors_added", dev->null_descriptors_added);
    w.kb("draw_parameters", dev->draw_parameters);
    w.kb("draw_parameters_added", dev->draw_parameters_added);
    w.key("swapchains");
    w.begin_array();
    {
        std::lock_guard<std::mutex> lock(dev->swap_mx);
        for (const auto& [handle, s] : dev->swapchains) {
            w.begin_object();
            w.kn("format", static_cast<uint64_t>(s.format));
            w.kn("color_space", static_cast<uint64_t>(s.color_space));
            w.kn("width", s.extent.width);
            w.kn("height", s.extent.height);
            w.kn("usage", s.usage);
            w.kn("flags", s.flags);
            w.kn("images", s.images.size());
            w.kb("window", s.window != nullptr);
            w.kb("usage_added", s.usage_added);
            w.end_object();
        }
    }
    w.end_array();
    w.end_object();

    PresentPacing pacing;
    {
        std::lock_guard<std::mutex> lock(dev->frame_mx);
        pacing = dev->pacing;
        w.kn("frame", dev->frame_no);
        w.kn("total_draws", dev->total_draws);
        w.key("last_frame");
        write_stats(w, dev->last);
        w.key("maxima");
        w.begin_object();
        w.kn("threads", dev->max_threads);
        w.kn("command_buffers", dev->max_command_buffers);
        w.kn("submits", dev->max_submits);
        w.kn("secondary_command_buffers", dev->max_secondaries);
        w.end_object();
        w.kn("capture_state", static_cast<uint64_t>(dev->cap_state));
        w.kn("capture_seq", dev->cap_seq);
    }
    // Sort/format the bounded copy outside the frame lock, on the probe thread.
    w.key("host_pacing");
    write_pacing(w, pacing);
    w.key("tracker_work");
    write_tracker_work(w, dev->work);
    {
        std::shared_lock<std::shared_mutex> lock(dev->res_mx);
        uint64_t shadow = 0, known = 0, mapped = 0;
        for (const auto& kv : dev->bufs) {
            shadow += kv.second.shadow.size();
            known += kv.second.known.total();
        }
        for (const auto& kv : dev->mems)
            if (kv.second.mapped)
                mapped++;
        w.key("counts");
        w.begin_object();
        w.kn("buffers", dev->bufs.size());
        w.kn("memories", dev->mems.size());
        w.kn("mapped_memories", mapped);
        w.kn("pipelines", dev->pipes.size());
        w.kn("shader_modules", dev->shaders.size());
        w.kn("retained_shaders", dev->shader_by_id.size());
        w.kn("retained_shader_bytes", dev->shader_bytes->load(std::memory_order_relaxed));
        w.kn("shaders_not_retained", dev->shaders_not_retained);
        w.key("image_content");
        w.begin_object();
        w.kb("enabled", g_cfg.image_hash && g_cfg.track_resources);
        for (uint8_t i = IMG_HASHED; i < IMG_STATES; i++)
            w.kn(image_content_name(i), dev->image_uploads[i].load(std::memory_order_relaxed));
        w.kn("bytes", dev->image_hash_bytes.load(std::memory_order_relaxed));
        w.kn("microseconds", dev->image_hash_us.load(std::memory_order_relaxed));
        w.end_object();
        w.key("unmap");
        w.begin_object();
        w.kn("copied", dev->unmap_taken.load(std::memory_order_relaxed));
        w.kn("copied_bytes", dev->unmap_taken_bytes.load(std::memory_order_relaxed));
        w.kn("passed_over", dev->unmap_passed.load(std::memory_order_relaxed));
        w.kn("passed_over_bytes", dev->unmap_passed_bytes.load(std::memory_order_relaxed));
        w.kn("shadows_dropped", dev->unmap_invalidated.load(std::memory_order_relaxed));
        w.end_object();
        w.kn("images", dev->images.size());
        w.kn("image_views", dev->image_views.size());
        w.kn("buffer_views", dev->buffer_views.size());
        w.kn("samplers", dev->samplers.size());
        w.kn("descriptor_sets", dev->descriptor_sets.size());
        w.kn("descriptor_set_layouts", dev->set_layouts.size());
        w.kn("descriptor_templates", dev->descriptor_templates.size());
        w.kn("pipeline_layouts", dev->pipeline_layouts.size());
        w.kn("shadow_bytes", shadow);
        w.kn("shadow_known_bytes", known);
        w.end_object();
    }
    {
        std::shared_lock<std::shared_mutex> lock(dev->cb_mx);
        w.kn("command_buffers_alive", dev->cbs.size());
    }
    w.end_object();
    w.end_object();
    return w.out;
}

static void write_capture(JsonWriter& w, const Capture& cap, bool with_draws)
{
    w.begin_object();
    w.kn("seq", cap.seq);
    w.kb("wait_draws", cap.wait_draws);
    w.kn("skipped_empty_intervals", cap.skipped_empty_intervals);
    w.kn("frame", cap.stats.frame);
    w.key("stats");
    write_stats(w, cap.stats);
    w.kn("draw_count", cap.draws.size());
    w.kn("descriptor_budget", cap.descriptor_budget);
    w.kn("descriptor_bytes", cap.descriptor_bytes);
    w.kn("descriptors_omitted", cap.descriptors_omitted);
    w.key("snapshot");
    if (cap.snap) {
        const SnapRequest& q = *cap.snap;
        w.begin_object();
        w.kb("all", q.all);
        w.kn("hashes", q.hashes.size());
        w.kn("max_range", q.max_range);
        w.kn("budget", q.budget);
        w.kn("draws", cap.snap_draws);
        w.kn("blobs", cap.blobs.size());
        w.kn("bytes", cap.snap_bytes);
        w.kn("omitted", cap.snap_omitted);
        w.key("record");
        w.begin_object();
        for (uint32_t i = 0; i < 5; i++)
            w.kn(rec_name(static_cast<uint8_t>(i)), cap.rec_counts[i]);
        w.end_object();
        w.end_object();
    } else {
        w.null();
    }
    if (with_draws) {
        w.key("draws");
        w.begin_array();
        uint32_t n = 0;
        for (const CapDraw& c : cap.draws) {
            const DrawRec& r = c.r;
            w.begin_object();
            w.kn("n", n++);
            w.kv("kind", kind_name(r.kind));
            w.kn("submit", c.submit);
            w.kn("cb", r.cb_id);
            w.kn("tid", r.tid);
            w.kn("rp", r.rp);
            w.kn("pipeline", c.pipe_id);
            w.kn("count", r.count);
            w.kn("instances", r.instances);
            w.kn("first", r.first);
            w.ki("vertex_offset", r.vertex_offset);
            w.kn("first_instance", r.first_instance);
            if (r.kind == DK_INDEXED || r.kind == DK_INDEXED_INDIRECT) {
                w.key("ib");
                w.begin_object();
                w.kn("buf", c.ib_id);
                w.kn("offset", r.ib_off);
                w.kn("index_size", r.index_size);
                if (c.hash_valid) {
                    w.key("hash");
                    w.hex32(c.hash);
                } else {
                    w.key("hash");
                    w.null();
                }
                w.kv("src", src_name(c.hash_src));
                w.end_object();
            }
            if (r.indirect) {
                w.key("indirect");
                w.begin_object();
                w.kn("buf", c.indirect_id);
                w.kn("offset", r.indirect_off);
                w.end_object();
            }
            w.key("vb");
            w.begin_array();
            for (uint32_t i = 0; i < kMaxVb; i++) {
                w.begin_array();
                w.num(c.vb_id[i]);
                w.num(r.vb[i].off);
                w.end_array();
            }
            w.end_array();
            w.key("sets");
            w.begin_array();
            for (uint32_t i = 0; i < kMaxSets; i++) {
                w.begin_object();
                char h[24];
                snprintf(h, sizeof(h), "0x%llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(r.sets[i].set)));
                w.kv("set", h);
                w.key("dyn");
                w.begin_array();
                for (uint32_t d : c.dyn[i])
                    w.num(d);
                w.end_array();
                w.key("contents");
                w.raw(c.descriptors[i]);
                w.end_object();
            }
            w.end_array();
            if (c.snap_selected) {
                w.key("snapshot");
                w.begin_array();
                for (const DrawSnap& s : c.snaps) {
                    w.begin_object();
                    w.kv("kind", snap_name(s.kind));
                    if (s.kind == SNAP_FOLLOW) {
                        w.kn("word", s.word);
                        w.kn("value", s.value);
                    }
                    w.kn("set", s.set);
                    w.kn("binding", s.binding);
                    w.kn("element", s.element);
                    w.kn("buf", s.buf_id);
                    w.kn("offset", s.off);
                    w.kn("size", s.size);
                    w.kv("src", src_name(s.src));
                    if (s.skipped) {
                        w.kv("skipped", s.skipped);
                    } else {
                        w.key("crc");
                        if (s.src != SRC_NONE)
                            w.hex32(s.crc);
                        else
                            w.null();
                        w.key("id");
                        if (s.blob >= 0)
                            w.num(static_cast<uint64_t>(s.blob));
                        else
                            w.null();
                        w.key("record");
                        w.begin_object();
                        w.kv("state", rec_name(s.rec));
                        if (s.rec != REC_NONE) {
                            w.kv("src", src_name(s.rec_src));
                            w.key("crc");
                            if (s.rec_src != SRC_NONE)
                                w.hex32(s.rec_crc);
                            else
                                w.null();
                        }
                        w.end_object();
                    }
                    w.end_object();
                }
                w.end_array();
            }
            w.end_object();
        }
        w.end_array();
    }
    w.end_object();
}

std::string probe_capture(uint32_t timeout_ms, bool with_draws, uint64_t descriptor_budget,
                          std::shared_ptr<const SnapRequest> snap, bool wait_draws)
{
    uint64_t before;
    {
        DeviceRef ref;
        if (!ref.dev)
            return fail("no device");
        std::lock_guard<std::mutex> lock(ref.dev->frame_mx);
        before = ref.dev->cap_seq;
        if (ref.dev->cap_state == 0) {
            ResetEvent(cap_event());
            ref.dev->cap_descriptor_budget = descriptor_budget;
            ref.dev->cap_wait_draws = wait_draws;
            ref.dev->cap_snap = std::move(snap);
            ref.dev->cap_state = 1;
        }
    }
    // The registry lock is not held while waiting, so a device can still be destroyed.
    WaitForSingleObject(cap_event(), timeout_ms);

    DeviceRef ref;
    if (!ref.dev)
        return fail("device destroyed while capturing");
    std::shared_ptr<Capture> cap;
    {
        std::lock_guard<std::mutex> lock(ref.dev->frame_mx);
        if (ref.dev->cap_seq > before)
            cap = ref.dev->cap_last;
        else {
            ref.dev->cap_state = 0;
            ref.dev->cap_building.reset();
            ref.dev->snap_recording.store(false, std::memory_order_relaxed);
            std::atomic_store(&ref.dev->snap_live, std::shared_ptr<const SnapRequest>());
        }
    }
    if (!cap)
        return fail(wait_draws ? "timeout: no interval containing draws was captured" : "timeout: no frame was presented");
    JsonWriter w;
    w.begin_object();
    w.kb("ok", true);
    w.key("result");
    write_capture(w, *cap, with_draws);
    w.end_object();
    return w.out;
}

std::string probe_set(int unmap_snapshot_all, int64_t unmap_snapshot_limit)
{
    {
        // The unmap path reads these settings with the resource lock held, so they change under it.
        DeviceRef ref;
        std::unique_lock<std::shared_mutex> lock;
        if (ref.dev)
            lock = std::unique_lock<std::shared_mutex>(ref.dev->res_mx);
        if (unmap_snapshot_all >= 0)
            g_cfg.unmap_snapshot_all = unmap_snapshot_all != 0;
        if (unmap_snapshot_limit >= 0)
            g_cfg.unmap_snapshot_limit = static_cast<uint64_t>(unmap_snapshot_limit);
    }
    logf("settings changed by the probe client: unmap_snapshot=%s up to %llu",
         g_cfg.unmap_snapshot_all ? "all sources" : "staging only", static_cast<unsigned long long>(g_cfg.unmap_snapshot_limit));
    JsonWriter w;
    w.begin_object();
    w.kb("ok", true);
    w.key("result");
    w.begin_object();
    w.kb("unmap_snapshot_all", g_cfg.unmap_snapshot_all);
    w.kn("unmap_snapshot_limit", g_cfg.unmap_snapshot_limit);
    w.end_object();
    w.end_object();
    return w.out;
}

std::string probe_snapshot(uint64_t seq, uint32_t id, std::vector<uint8_t>& payload)
{
    DeviceRef ref;
    if (!ref.dev)
        return fail("no device");
    std::shared_ptr<Capture> cap;
    {
        std::lock_guard<std::mutex> lock(ref.dev->frame_mx);
        cap = ref.dev->cap_last;
    }
    if (!cap || cap->seq != seq)
        return fail("that capture is not the latest one; its contents are gone");
    if (id >= cap->blobs.size())
        return fail("no such snapshot id in this capture");
    const SnapBlob& b = cap->blobs[id];
    payload = b.data;
    JsonWriter w;
    w.begin_object();
    w.kb("ok", true);
    w.key("result");
    w.begin_object();
    w.kn("seq", cap->seq);
    w.kn("id", id);
    w.kn("buf", b.buf_id);
    w.kn("offset", b.off);
    w.kn("size", b.size);
    w.kv("src", src_name(b.src));
    w.key("crc");
    w.hex32(b.crc);
    w.kv("when", "submit");
    w.end_object();
    w.end_object();
    return w.out;
}

static void write_buffer(JsonWriter& w, Device* dev, const BufInfo& b)
{
    w.begin_object();
    w.kn("id", b.id);
    w.kn("size", b.size);
    w.kn("usage", b.usage);
    bool mapped = false;
    bool write_combined = false;
    uint32_t type_index = 0;
    if (b.mem) {
        auto it = dev->mems.find(b.mem);
        if (it != dev->mems.end()) {
            const MemInfo& m = it->second;
            type_index = m.type_index;
            mapped = m.mapped && b.mem_off >= m.map_off && b.mem_off + b.size <= m.map_off + m.map_size;
            write_combined = mapped && m.write_combined;
        }
    }
    w.kb("bound", b.mem != VK_NULL_HANDLE);
    w.kn("memory_offset", b.mem_off);
    w.kn("memory_type", type_index);
    w.kb("mapped", mapped);
    w.kb("mapped_write_combined", write_combined);
    w.kn("shadow_known_bytes", b.known.total());
    w.kn("unknown_writes", b.unknown_writes);
    w.kn("generation", b.gen);
    w.end_object();
}

std::string probe_buffers(uint32_t usage_mask, uint64_t min_size, uint32_t limit)
{
    DeviceRef ref;
    if (!ref.dev)
        return fail("no device");
    Device* dev = ref.dev;
    std::shared_lock<std::shared_mutex> lock(dev->res_mx);
    std::vector<const BufInfo*> list;
    for (const auto& kv : dev->bufs) {
        const BufInfo& b = kv.second;
        if (usage_mask && !(b.usage & usage_mask))
            continue;
        if (b.size < min_size)
            continue;
        list.push_back(&b);
    }
    std::sort(list.begin(), list.end(), [](const BufInfo* a, const BufInfo* b) { return a->id < b->id; });
    JsonWriter w;
    w.begin_object();
    w.kb("ok", true);
    w.key("result");
    w.begin_object();
    w.kn("total", list.size());
    w.key("buffers");
    w.begin_array();
    uint32_t n = 0;
    for (const BufInfo* b : list) {
        if (limit && n++ >= limit)
            break;
        write_buffer(w, dev, *b);
    }
    w.end_array();
    w.end_object();
    w.end_object();
    return w.out;
}

std::string probe_read_buffer(uint32_t id, uint64_t off, uint64_t size, std::vector<uint8_t>& payload)
{
    DeviceRef ref;
    if (!ref.dev)
        return fail("no device");
    Device* dev = ref.dev;
    std::shared_lock<std::shared_mutex> lock(dev->res_mx);
    auto hit = dev->buf_by_id.find(id);
    if (hit == dev->buf_by_id.end())
        return fail("unknown buffer id");
    auto bit = dev->bufs.find(hit->second);
    if (bit == dev->bufs.end())
        return fail("unknown buffer id");
    const BufInfo& b = bit->second;
    if (off > b.size)
        return fail("offset beyond the end of the buffer");
    if (size == 0 || size > b.size - off)
        size = b.size - off;
    if (size > (64ull << 20))
        return fail("more than 64 MiB requested");
    payload.resize(static_cast<size_t>(size));
    ReadSrc src = read_buffer_locked(dev, b, off, size, payload.data());
    if (src == SRC_NONE) {
        payload.clear();
        return fail("the bytes are not available on the CPU (not mapped and no shadow)");
    }
    JsonWriter w;
    w.begin_object();
    w.kb("ok", true);
    w.key("result");
    w.begin_object();
    w.kn("id", id);
    w.kn("offset", off);
    w.kn("size", size);
    w.kv("src", src_name(src));
    w.kn("generation", b.gen);
    w.end_object();
    w.end_object();
    return w.out;
}

std::string probe_hash(uint32_t id, uint64_t off, uint64_t size)
{
    std::vector<uint8_t> bytes;
    std::string head = probe_read_buffer(id, off, size, bytes);
    if (bytes.empty())
        return head;
    JsonWriter w;
    w.begin_object();
    w.kb("ok", true);
    w.key("result");
    w.begin_object();
    w.kn("id", id);
    w.kn("offset", off);
    w.kn("size", bytes.size());
    w.key("hash");
    w.hex32(crc32c(0, bytes.data(), bytes.size()));
    w.end_object();
    w.end_object();
    return w.out;
}

std::string probe_api_usage()
{
    JsonWriter w;
    w.begin_object();
    w.kb("ok", true);
    w.key("result");
    w.begin_object();
    w.kn("hooked_functions", HI_COUNT);
    w.key("calls");
    w.begin_object();
    for (int i = 0; i < HI_COUNT; i++) {
        uint64_t n = g_api_calls[i].load(std::memory_order_relaxed);
        if (n)
            w.kn(g_api_names[i], n);
    }
    w.end_object();
    {
        std::lock_guard<std::mutex> lock(g_resolved_mx);
        std::vector<std::string> names = g_resolved;
        std::sort(names.begin(), names.end());
        w.key("resolved");
        w.begin_array();
        for (const std::string& n : names)
            w.str(n);
        w.end_array();
    }
    DeviceRef ref;
    w.key("extensions");
    w.begin_array();
    if (ref.dev)
        for (const std::string& e : ref.dev->extensions)
            w.str(e);
    w.end_array();
    w.key("pipeline_creation");
    w.begin_object();
    if (ref.dev) {
        std::shared_lock<std::shared_mutex> lock(ref.dev->res_mx);
        w.kn("graphics_pipelines", ref.dev->graphics_pipelines_created);
        w.key("uses");
        w.begin_object();
        for (const auto& kv : ref.dev->pipeline_uses)
            w.kn(kv.first.c_str(), kv.second);
        w.end_object();
    }
    w.end_object();
    w.end_object();
    w.end_object();
    return w.out;
}

std::string probe_pipeline(uint32_t id)
{
    DeviceRef ref;
    if (!ref.dev)
        return fail("no device");
    Device* dev = ref.dev;
    std::shared_lock<std::shared_mutex> lock(dev->res_mx);
    auto hit = dev->pipe_by_id.find(id);
    if (hit == dev->pipe_by_id.end())
        return fail("unknown pipeline id");
    auto pit = dev->pipes.find(hit->second);
    if (pit == dev->pipes.end())
        return fail("unknown pipeline id");
    const PipeInfo& p = pit->second;
    JsonWriter w;
    w.begin_object();
    w.kb("ok", true);
    w.key("result");
    w.begin_object();
    w.kn("id", p.id);
    w.kb("compute", p.compute);
    w.kn("stages", p.stages);
    w.kn("topology", p.topology);
    write_stages(w, p);
    w.kn("create_flags", p.create_flags);
    w.kn("subpass", p.subpass);
    w.kb("specialized", p.specialized);
    w.key("recipe");
    if (p.recipe) p.recipe->write(w); else w.null();
    w.key("variant_preflight");
    w.begin_object();
    VertexRepoint rewrite;
    std::string reason = !p.recipe ? "graphics_recipe_unavailable" : !p.layout ? "pipeline_layout_unavailable" : "";
    const bool supported = p.recipe && p.layout &&
        prepare_vertex_variant(*p.recipe, *p.layout, dev->max_bound_descriptor_sets, rewrite, reason);
    w.kb("supported", supported); w.kv("reason", reason);
    w.kn("max_bound_descriptor_sets", dev->max_bound_descriptor_sets);
    if (supported) {
        w.kn("own_set", rewrite.own_set); w.kn("bone_set", rewrite.bone_set); w.kn("bone_binding", rewrite.bone_binding);
    }
    w.end_object();
    w.key("layout_recipe");
    if (!p.layout) w.null();
    else {
        w.begin_object(); w.kb("complete", p.layout->complete); w.kn("flags", p.layout->flags);
        w.key("sets"); w.begin_array();
        for (const auto& set : p.layout->sets) {
            if (!set) { w.null(); continue; }
            w.begin_object(); w.kn("id", set->id); w.kb("complete", set->complete); w.kn("flags", set->flags);
            w.key("bindings"); w.begin_array();
            for (const auto& item : set->bindings) {
                const auto& b = item.second;
                w.begin_object(); w.kn("binding", item.first); w.kn("type", b.type); w.kn("count", b.count);
                w.kn("stages", b.stages); w.kn("flags", b.flags); w.kn("immutable_samplers", b.immutable_samplers.size()); w.end_object();
            }
            w.end_array(); w.end_object();
        }
        w.end_array(); w.end_object();
    }
    w.key("dynamic_states");
    w.begin_array();
    for (uint32_t d : p.dynamic_states)
        w.num(d);
    w.end_array();
    w.key("layout_sets");
    w.begin_array();
    if (p.layout)
        for (const auto& set : p.layout->sets)
            w.num(set ? set->id : 0);
    w.end_array();
    w.key("push_constants");
    w.begin_array();
    if (p.layout)
        for (const VkPushConstantRange& r : p.layout->pushes) {
            w.begin_object();
            w.kn("stages", r.stageFlags);
            w.kn("offset", r.offset);
            w.kn("size", r.size);
            w.end_object();
        }
    w.end_array();
    w.key("bindings");
    w.begin_array();
    for (const auto& b : p.bindings) {
        w.begin_object();
        w.kn("binding", b.binding);
        w.kn("stride", b.stride);
        w.kb("per_instance", b.per_instance != 0);
        w.end_object();
    }
    w.end_array();
    w.key("attributes");
    w.begin_array();
    for (const auto& a : p.attributes) {
        w.begin_object();
        w.kn("location", a.location);
        w.kn("binding", a.binding);
        w.kn("format", a.format);
        w.kn("offset", a.offset);
        w.end_object();
    }
    w.end_array();
    w.end_object();
    w.end_object();
    return w.out;
}

} // namespace efvk
