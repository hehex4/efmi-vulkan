// Tracking of memory, buffers, pipelines and command buffers; frame assembly; probe requests.
#include "state.h"
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

Device* dev_create(VkDevice handle)
{
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
    delete dev;
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
}

void on_unmap_memory(Device* dev, VkDeviceMemory mem)
{
    if (dev->passive)
        return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    auto it = dev->mems.find(mem);
    if (it == dev->mems.end())
        return;
    MemInfo& m = it->second;
    if (m.mapped && g_cfg.unmap_snapshot_limit) {
        // A staging buffer may be copied from after its memory is unmapped. Keep what it holds now.
        for (VkBuffer h : m.buffers) {
            auto bit = dev->bufs.find(h);
            if (bit == dev->bufs.end())
                continue;
            BufInfo& b = bit->second;
            if (!(b.usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) || b.size == 0 || b.size > g_cfg.unmap_snapshot_limit)
                continue;
            if (b.mem_off < m.map_off || b.mem_off + b.size > m.map_off + m.map_size)
                continue;
            b.shadow.resize(static_cast<size_t>(b.size));
            memcpy(b.shadow.data(), m.mapped + (b.mem_off - m.map_off), static_cast<size_t>(b.size));
            b.known.v.clear();
            b.known.add(0, b.size);
            b.gen++;
        }
    }
    m.mapped = nullptr;
    m.map_off = 0;
    m.map_size = 0;
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

void on_create_graphics_pipeline(Device* dev, VkPipeline pipe, const VkGraphicsPipelineCreateInfo* info)
{
    if (dev->passive)
        return;
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    PipeInfo& p = dev->pipes[pipe];
    if (p.id)
        dev->pipe_by_id.erase(p.id);
    p = PipeInfo();
    p.id = dev->next_pipe_id++;
    auto layout = dev->pipeline_layouts.find(info->layout);
    if (layout != dev->pipeline_layouts.end()) p.layout = layout->second;
    track_stages_locked(dev, p, info->stageCount, info->pStages);
    for (uint32_t i = 0; i < info->stageCount; i++)
        p.stages |= info->pStages[i].stage;
    if (info->pInputAssemblyState)
        p.topology = info->pInputAssemblyState->topology;
    if (const VkPipelineVertexInputStateCreateInfo* vi = info->pVertexInputState) {
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
    p = PipeInfo();
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
    dev->pipes.erase(it);
    prune_shaders_locked(dev);
}

ReadSrc read_buffer_locked(Device* dev, const BufInfo& b, uint64_t off, uint64_t size, uint8_t* out)
{
    if (off > b.size || size > b.size - off)
        return SRC_NONE;
    if (!size)
        return SRC_SHADOW;
    if (b.mem) {
        auto it = dev->mems.find(b.mem);
        if (it != dev->mems.end() && it->second.mapped) {
            const MemInfo& m = it->second;
            uint64_t at = b.mem_off + off;
            if (at >= m.map_off && at + size <= m.map_off + m.map_size) {
                memcpy(out, m.mapped + (at - m.map_off), static_cast<size_t>(size));
                return SRC_MAPPED;
            }
        }
    }
    if (b.shadow.size() == b.size && b.known.covers(off, size)) {
        memcpy(out, b.shadow.data() + off, static_cast<size_t>(size));
        return SRC_SHADOW;
    }
    return SRC_NONE;
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
    if (sit != dev->bufs.end() && size <= (256ull << 20)) {
        w.data.resize(static_cast<size_t>(size));
        w.have_data = read_buffer_locked(dev, sit->second, src_off, size, w.data.data()) != SRC_NONE;
        if (!w.have_data)
            w.data.clear();
    }
    cb->writes.push_back(std::move(w));
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
        cb->dyn.insert(cb->dyn.end(), s->dyn.begin(), s->dyn.end());
        for (DrawRec r : s->draws) {
            for (SetBind& sb : r.sets)
                sb.dyn_first += dyn_base;
            if (rp)
                r.rp = rp;
            cb->draws.push_back(r);
        }
        cb->writes.insert(cb->writes.end(), s->writes.begin(), s->writes.end());
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
    std::unique_lock<std::shared_mutex> lock(dev->res_mx);
    std::vector<uint8_t> tmp;
    for (const PendingWrite& w : cb->writes) {
        auto dit = dev->bufs.find(w.dst);
        if (dit == dev->bufs.end())
            continue;
        BufInfo& d = dit->second;
        if (w.dst_off > d.size || w.size > d.size - w.dst_off)
            continue;
        const uint8_t* bytes = nullptr;
        if (w.src) {
            // What the staging buffer holds at submit time is what the GPU will copy.
            auto sit = dev->bufs.find(w.src);
            if (sit != dev->bufs.end()) {
                tmp.resize(static_cast<size_t>(w.size));
                if (read_buffer_locked(dev, sit->second, w.src_off, w.size, tmp.data()) != SRC_NONE)
                    bytes = tmp.data();
            }
        }
        if (!bytes && w.have_data)
            bytes = w.data.data();
        if (bytes) {
            if (d.shadow.size() != d.size)
                d.shadow.resize(static_cast<size_t>(d.size));
            memcpy(d.shadow.data() + w.dst_off, bytes, static_cast<size_t>(w.size));
            d.known.add(w.dst_off, w.size);
        } else {
            d.known.remove(w.dst_off, w.size);
            d.unknown_writes++;
            stats.unknown_writes++;
        }
        d.gen++;
    }
}

static void capture_draws(Device* dev, CmdBuf* cb, Capture& cap, uint32_t submit)
{
    std::shared_lock<std::shared_mutex> lock(dev->res_mx);
    std::vector<uint8_t> tmp;
    cap.draws.reserve(cap.draws.size() + cb->draws.size());
    for (const DrawRec& r : cb->draws) {
        CapDraw c;
        c.r = r;
        c.submit = submit;
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
            JsonWriter descriptors;
            write_descriptor_set_locked(descriptors, dev, s, c.dyn[i], r.pipeline, i);
            c.descriptors[i] = std::move(descriptors.out);
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
        cap.draws.push_back(std::move(c));
    }
}

void on_submit(Device* dev, uint32_t count, const VkCommandBuffer* cbs)
{
    if (dev->passive)
        return;
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

void on_present(Device* dev)
{
    if (dev->passive)
        return;
    std::lock_guard<std::mutex> lock(dev->frame_mx);
    dev->cur.frame = dev->frame_no;
    dev->total_draws += dev->cur.draws;
    dev->max_threads = std::max(dev->max_threads, static_cast<uint32_t>(dev->cur.threads.size()));
    dev->max_command_buffers = std::max(dev->max_command_buffers, dev->cur.command_buffers);
    dev->max_submits = std::max(dev->max_submits, dev->cur.submits);
    dev->max_secondaries = std::max(dev->max_secondaries, dev->cur.secondaries);
    dev->last = dev->cur;

    if (dev->cap_state == 2 && dev->cap_building) {
        dev->cap_building->stats = dev->cur;
        dev->cap_building->seq = ++dev->cap_seq;
        dev->cap_last = dev->cap_building;
        dev->cap_building.reset();
        dev->cap_state = 0;
        SetEvent(cap_event());
    } else if (dev->cap_state == 1) {
        dev->cap_state = 2;
        dev->cap_building = std::make_shared<Capture>();
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
    w.str("buffers");
    w.str("read_buffer");
    w.str("hash");
    w.str("pipeline");
    w.str("api_usage");
    w.str("descriptor_sets");
    w.str("descriptor_templates");
    w.str("push_descriptors");
    w.str("shader");
    w.end_array();

    DeviceRef ref;
    Device* dev = ref.dev;
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
    w.end_object();

    {
        std::lock_guard<std::mutex> lock(dev->frame_mx);
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
    w.kn("frame", cap.stats.frame);
    w.key("stats");
    write_stats(w, cap.stats);
    w.kn("draw_count", cap.draws.size());
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
            w.end_object();
        }
        w.end_array();
    }
    w.end_object();
}

std::string probe_capture(uint32_t timeout_ms, bool with_draws)
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
        }
    }
    if (!cap)
        return fail("timeout: no frame was presented");
    JsonWriter w;
    w.begin_object();
    w.kb("ok", true);
    w.key("result");
    write_capture(w, *cap, with_draws);
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
    uint32_t type_index = 0;
    if (b.mem) {
        auto it = dev->mems.find(b.mem);
        if (it != dev->mems.end()) {
            const MemInfo& m = it->second;
            type_index = m.type_index;
            mapped = m.mapped && b.mem_off >= m.map_off && b.mem_off + b.size <= m.map_off + m.map_size;
        }
    }
    w.kb("bound", b.mem != VK_NULL_HANDLE);
    w.kn("memory_offset", b.mem_off);
    w.kn("memory_type", type_index);
    w.kb("mapped", mapped);
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
