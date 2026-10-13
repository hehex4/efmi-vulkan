#include "character_backend.h"
#include "character_package.h"
#include "character_materials.h"
#include "merged_skeleton.h"
#include "state.h"
#include "json.h"
#include "../../frontend/assets/resource_cache.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <set>

namespace efvk {
namespace {
// Values of the core this follows (SpatialIdentification.ini): cell size of the position hash, base
// radius in cells per 1/120 s, and the number of distinct components that identifies the object.
constexpr float kCellSize = 0.125f;
constexpr float kBaseRadius = 3.0f;
constexpr uint32_t kIdentifyingComponents = 4;
constexpr uint64_t kExpiryFrames = 2;
constexpr size_t kIdentities = 16;
constexpr uint32_t kArenaEntries = 256, kArenaSlices = 64;
constexpr uint64_t kRehashFrames = 256;

enum Counter : uint32_t {
    C_MATCHED, C_REPLACED, C_ITEMS, C_HIDDEN, C_IDENTIFYING, C_INCOMPLETE, C_PENDING,
    C_STATE, C_INSTANCES, C_SET3, C_PIPELINE, C_VERTEX, C_VARIANT, C_BINDINGS,
    C_ENTRY, C_BONES, C_IMPORT, C_EXHAUSTED, C_IDENTITIES, C_SET3_RESTORED,
    C_MATERIAL, C_IMAGE, C_IMAGE_PENDING, C_MATERIAL_ITEMS, C_COMPAT_ITEMS, C_COUNT
};
const char* const kCounterNames[C_COUNT] = {
    "matched", "replaced", "items", "hidden", "identifying", "incomplete", "pending",
    "refused_state", "refused_instances", "refused_set3", "refused_pipeline", "refused_vertex", "refused_variant",
    "refused_bindings", "unreadable_entry", "unreadable_bones", "import_rejected", "exhausted", "identities_full",
    "restored_set3", "refused_material", "refused_image", "image_pending", "material_items", "dimension_zero_items"
};

struct GpuMesh {
    VkDevice device;
    SnapshotDispatch d;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    bool device_local = false;
    GpuMesh(VkDevice dev, const SnapshotDispatch& dispatch) : device(dev), d(dispatch) {}
    ~GpuMesh()
    {
        if (buffer) d.DestroyBuffer(device, buffer, nullptr);
        if (memory) d.FreeMemory(device, memory, nullptr);
    }
    bool create(const std::vector<uint8_t>& payload, const VkPhysicalDeviceMemoryProperties& properties, std::string& reason)
    {
        auto good = [&](VkResult result, const char* text) { if (result == VK_SUCCESS) return true; reason = text; return false; };
        VkBufferCreateInfo ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        // Room behind the last stream: an element may reach past its file's stride (kCharacterOvershoot).
        const size_t padding = 256;
        ci.size = payload.size() + padding; ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (!good(d.CreateBuffer(device, &ci, nullptr, &buffer), "create character mesh buffer")) return false;
        VkMemoryRequirements req; d.GetBufferMemoryRequirements(device, buffer, &req);
        if (req.size > (768ull << 20)) { reason = "character mesh allocation budget exceeded"; return false; }
        // A whole character is read by the device in every pass of every frame: it belongs in device
        // memory. Host memory works, but far slower; it is the fallback, and the status says which.
        uint32_t type = host_memory_type(properties, req.memoryTypeBits, true);
        device_local = type != UINT32_MAX;
        if (!device_local) type = host_memory_type(properties, req.memoryTypeBits, false);
        if (type == UINT32_MAX) { reason = "host-coherent character mesh memory unavailable"; return false; }
        VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size; ai.memoryTypeIndex = type;
        void* mapped = nullptr;
        if (!good(d.AllocateMemory(device, &ai, nullptr, &memory), "allocate character mesh memory") ||
            !good(d.BindBufferMemory(device, buffer, memory, 0), "bind character mesh memory") ||
            !good(d.MapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped), "map character mesh memory")) return false;
        std::memcpy(mapped, payload.data(), payload.size());
        std::memset(static_cast<uint8_t*>(mapped) + payload.size(), 0, padding);
        d.UnmapMemory(device, memory);
        return true;
    }
};

struct Identity {
    bool used = false, custom = false;
    int32_t cell[3] = {};
    uint64_t first_frame = 0, last_frame = 0, update_frame = 0;
    uint64_t id = 0, generation = 0; // generation changes whenever the master's bytes change
    uint32_t seen = 0;               // components met since this identity appeared
    MergedSkeleton skeleton;
    std::vector<uint8_t> coverage;   // per component: bit 0 current pose imported, bit 1 previous
};

struct IndexKey {
    VkBuffer buffer; uint64_t offset; uint32_t bytes;
    bool operator==(const IndexKey& o) const { return buffer == o.buffer && offset == o.offset && bytes == o.bytes; }
};
struct IndexKeyHash {
    size_t operator()(const IndexKey& k) const
    {
        return std::hash<uint64_t>()(reinterpret_cast<uint64_t>(k.buffer) ^ (k.offset * 0x9E3779B97F4A7C15ull) ^ (uint64_t(k.bytes) << 40));
    }
};
struct IndexHash { uint32_t buffer_id = 0; uint64_t gen = 0, next_full = 0; uint32_t hash = 0, quick = 0; };
struct BoneSite { bool ok = false; uint32_t set = 0, binding = 0; };
using StaticMaterial = CharacterMaterial;
struct ImageQueue {
    VkQueue queue = VK_NULL_HANDLE;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool waited = false;
};
} // namespace

class CharacterBackend {
public:
    Device* device;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
    VariantDispatch draw;
    SnapshotDispatch storage;
    CharacterPackage package;
    std::unordered_map<uint32_t, std::vector<uint32_t>> by_count; // immutable after init
    std::vector<BoneComponent> ranges;
    uint32_t bone_bytes = 0;
    std::unique_ptr<VariantCache> cache;
    efmi::assets::ResourceBudgets image_budgets;
    std::unique_ptr<efmi::assets::ResourceCache> images;
    std::vector<efmi::assets::ResourceHandle> image_handles;
    std::vector<std::vector<StaticMaterial>> materials; // profile x package draw, prepared at setup
    std::unordered_map<uint32_t, uint32_t> material_profiles; // pipeline generation -> exact profile
    std::set<uint32_t> image_counts{0};
    std::mutex queue_mx;
    bool queues_stopped = false;
    std::vector<ImageQueue> image_queues;
    std::vector<VkSemaphore> image_signals;
    VkQueue upload_queue = VK_NULL_HANDLE;
    VkFence publication_fence = VK_NULL_HANDLE;
    std::atomic<bool> images_published{false};
    std::atomic<uint32_t> image_queue_waits{0}, upload_queue_index{UINT32_MAX};
    PFN_vkCreateSemaphore CreateSemaphore = nullptr;
    PFN_vkDestroySemaphore DestroySemaphore = nullptr;
    PFN_vkCreateFence CreateFence = nullptr;
    PFN_vkDestroyFence DestroyFence = nullptr;
    PFN_vkWaitForFences WaitForFences = nullptr;
    std::atomic<bool> enabled{false}, ready{false};
    std::atomic<uint64_t> frame{1}, batches{0};
    std::atomic<uint64_t> counters[C_COUNT] = {};
    std::atomic<float> frame_seconds{1.0f / 60};
    uint64_t last_frame_us = 0;

    // Resources prepared off the draw path.
    std::mutex pool_mx;
    std::condition_variable wake;
    bool stopped = false;
    std::thread worker;
    std::shared_ptr<const GpuMesh> mesh;
    std::deque<std::shared_ptr<M2Command>> available;
    std::vector<std::weak_ptr<M2Command>> all;
    std::string last_reason;

    // The mod's state, under the mod lock: one matched draw at a time.
    std::mutex mod_mx;
    uint32_t lod = 0;
    std::array<Identity, kIdentities> identities;
    uint64_t next_identity = 1;
    std::unordered_map<IndexKey, IndexHash, IndexKeyHash> hashes;
    std::unordered_map<uint32_t, BoneSite> sites;
    std::vector<uint8_t> scratch, current, previous;

    CharacterBackend(Device* dev, const VkPhysicalDeviceProperties& p, const VkPhysicalDeviceMemoryProperties& m)
        : device(dev), properties(p), memory(m) {}
    ~CharacterBackend() { stop(); }

    void note(const std::string& reason) { std::lock_guard<std::mutex> lock(pool_mx); last_reason = reason; }
    bool refuse(Counter counter, const std::string& reason) { ++counters[counter]; note(reason); return false; }

    bool init()
    {
        if (!package.load(g_cfg.m2_package, last_reason)) return false;
#define X(n) draw.n = reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle, "vk" #n));
        EFVK_VARIANT_FUNCS(X)
#undef X
#define X(n) storage.n = reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle, "vk" #n)); \
        if (!storage.n) { last_reason = "missing vk" #n; return false; }
        EFVK_SNAPSHOT_FUNCS(X)
#undef X
        if (!draw.CreateGraphicsPipelines || !draw.CreateRenderPass || !draw.CmdDrawIndexed) { last_reason = "missing graphics entry"; return false; }
        for (uint32_t i = 0; i < package.sections.size(); ++i) by_count[package.sections[i].match_count].push_back(i);
        for (const auto& c : package.components) ranges.push_back({c.first, c.count});
        bone_bytes = (MergedSkeleton::kImplicitBones + 2 * package.bones) * static_cast<uint32_t>(MergedSkeleton::kBytesPerBone);
        if (package.materials && !init_materials()) return false;
        cache = std::make_unique<VariantCache>(device->handle, draw, properties.limits.maxBoundDescriptorSets, 256, properties.limits);
        enabled = g_cfg.m2_enabled;
        worker = std::thread([this] { prepare(); });
        return true;
    }

    void stop()
    {
        { std::lock_guard<std::mutex> lock(pool_mx); stopped = true; enabled = false; wake.notify_all(); }
        if (worker.joinable()) worker.join();
        if (cache) cache->stop();
        {
            std::lock_guard<std::mutex> lock(queue_mx);
            queues_stopped = true;
            if (images) images->stop();
            // Device shutdown only. Never wait for a queue or fence on the draw/submit path.
            if (images_published && publication_fence) WaitForFences(device->handle, 1, &publication_fence, VK_TRUE, UINT64_MAX);
            for (auto& q : image_queues) {
                if (q.waited) WaitForFences(device->handle, 1, &q.fence, VK_TRUE, UINT64_MAX);
                if (q.semaphore) DestroySemaphore(device->handle, q.semaphore, nullptr);
                if (q.fence) DestroyFence(device->handle, q.fence, nullptr);
            }
            image_queues.clear();
            if (publication_fence) DestroyFence(device->handle, publication_fence, nullptr);
            publication_fence = VK_NULL_HANDLE;
        }
        std::deque<std::shared_ptr<M2Command>> old;
        std::shared_ptr<const GpuMesh> old_mesh;
        { std::lock_guard<std::mutex> lock(pool_mx); old.swap(available); old_mesh.swap(mesh); ready = false; }
    }

    void prepare()
    {
        try {
            auto created = std::make_shared<GpuMesh>(device->handle, storage);
            std::string reason;
            if (!created->create(package.payload, memory, reason)) { note(reason); return; }
            logf("character mesh uploaded: %llu bytes in %s memory", static_cast<unsigned long long>(package.payload.size()),
                 created->device_local ? "device" : "host");
            { std::lock_guard<std::mutex> lock(pool_mx); mesh = created; }
            while (true) {
                {
                    std::unique_lock<std::mutex> lock(pool_mx);
                    all.erase(std::remove_if(all.begin(), all.end(), [](const auto& p) { return p.expired(); }), all.end());
                    if (stopped) return;
                    if (available.size() >= 2 || all.size() >= 8) { wake.wait_for(lock, std::chrono::milliseconds(100)); continue; }
                }
                auto batch = std::make_shared<M2Command>();
                batch->keep = created;
                uint64_t total_bytes = 0;
                for (uint32_t count : image_counts) {
                    auto snapshots = std::make_shared<DrawSnapshots>(device->handle, storage);
                    if (!snapshots->create_shared(memory, properties.limits, kArenaEntries, kArenaSlices, bone_bytes, 65536, count)) {
                        note(snapshots->reason); return;
                    }
                    total_bytes += snapshots->size();
                    if (total_bytes > (64ull << 20)) { note("character material arenas exceed the batch budget"); return; }
                    if (count) batch->material_snapshots.emplace(count, std::move(snapshots));
                    else batch->snapshots = std::move(snapshots);
                }
                {
                    std::lock_guard<std::mutex> lock(pool_mx);
                    if (stopped) return;
                    all.push_back(batch); available.push_back(std::move(batch)); ready = true;
                }
            }
        } catch (const std::exception& e) { note(e.what()); }
    }

    std::shared_ptr<M2Command> command()
    {
        std::lock_guard<std::mutex> lock(pool_mx);
        if (stopped || available.empty() || !mesh) return {};
        auto cmd = available.front(); available.pop_front();
        wake.notify_one();
        return cmd;
    }

    bool init_materials();
    bool material_draws(uint32_t pipeline_id, const GraphicsRecipe&, const PipelineLayoutInfo&, const CharacterList&,
                        std::vector<const StaticMaterial*>&, std::vector<std::vector<SnapshotImage>>&);

    // Caller holds res_mx. The index bytes of one draw, hashed once and then recognized by a short
    // fingerprint; the full hash is repeated now and then because mapped pools change unseen.
    bool region_hash(const DrawRec& r, uint64_t now, uint32_t& hash)
    {
        auto ib = device->bufs.find(r.ib);
        if (ib == device->bufs.end() || (r.index_size != 2 && r.index_size != 4)) return false;
        const uint64_t start = uint64_t(r.first) * r.index_size, bytes = uint64_t(r.count) * r.index_size;
        const BufInfo& info = ib->second;
        if (r.ib_off > info.size || start > info.size - r.ib_off || bytes > info.size - r.ib_off - start || bytes > (64u << 20)) return false;
        const uint64_t offset = r.ib_off + start;
        uint8_t edge[128];
        const size_t head = static_cast<size_t>(std::min<uint64_t>(bytes, 64)), tail = static_cast<size_t>(std::min<uint64_t>(bytes - head, 64));
        if (read_buffer_locked(device, info, offset, head, edge) != SRC_MAPPED ||
            (tail && read_buffer_locked(device, info, offset + bytes - tail, tail, edge + head) != SRC_MAPPED)) return false;
        const uint32_t quick = crc32c(0, edge, head + tail);
        const IndexKey key = {r.ib, offset, static_cast<uint32_t>(bytes)};
        auto found = hashes.find(key);
        if (found != hashes.end() && found->second.buffer_id == info.id && found->second.gen == info.gen &&
            found->second.quick == quick && now < found->second.next_full) {
            hash = found->second.hash;
            return true;
        }
        scratch.resize(static_cast<size_t>(bytes));
        if (read_buffer_locked(device, info, offset, bytes, scratch.data()) != SRC_MAPPED) return false;
        hash = crc32c(0, scratch.data(), scratch.size());
        if (hashes.size() >= 4096) hashes.clear();
        // Regions first seen in one frame are not all hashed again in one later frame.
        hashes[key] = {info.id, info.gen, now + kRehashFrames + (IndexKeyHash()(key) >> 7) % kRehashFrames, hash, quick};
        return true;
    }

    Identity* identify(const InstanceEntry& entry, uint64_t now)
    {
        float position[3];
        std::memcpy(position, entry.data() + 48, sizeof(position));
        int32_t cell[3];
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(position[i]) || std::fabs(position[i]) > 1.0e6f) return nullptr;
            cell[i] = static_cast<int32_t>(std::floor(position[i] / kCellSize));
        }
        const float scaled = std::ceil(kBaseRadius * (frame_seconds.load(std::memory_order_relaxed) * 120.0f));
        const int32_t radius = static_cast<int32_t>(std::min(std::max(scaled, kBaseRadius), 64.0f));
        Identity* best = nullptr;
        int32_t best_distance = INT32_MAX;
        Identity* vacant = nullptr;
        Identity* weakest = nullptr;
        uint32_t weakest_seen = UINT32_MAX;
        for (auto& identity : identities) {
            if (identity.used && now - identity.last_frame > kExpiryFrames) identity.used = false;
            if (!identity.used) { if (!vacant) vacant = &identity; continue; }
            int32_t distance = 0;
            for (int i = 0; i < 3; ++i) distance = std::max(distance, std::abs(cell[i] - identity.cell[i]));
            // Every component of one object has the same position within a frame: an identity
            // already met in this frame is only the same object at the very same place. Across
            // frames the object may have moved, by at most the radius.
            const int32_t allowed = identity.last_frame == now ? 0 : radius;
            if (distance <= allowed && distance < best_distance) { best = &identity; best_distance = distance; }
            uint32_t bits = 0;
            for (uint32_t value = identity.seen; value; value &= value - 1) ++bits;
            if (!identity.custom && bits < weakest_seen) { weakest = &identity; weakest_seen = bits; }
        }
        if (!best) {
            // Objects that merely share a mesh with the character must not keep it out: when every
            // slot is taken, the unidentified object seen with the fewest components gives way.
            if (!vacant) vacant = weakest;
            if (!vacant) return nullptr;
            std::string reason;
            if (!vacant->skeleton.configure(package.bones, 1, ranges, reason)) { note(reason); return nullptr; }
            vacant->used = true; vacant->custom = false; vacant->seen = 0;
            vacant->first_frame = now; vacant->update_frame = 0;
            vacant->id = next_identity++; vacant->generation = 0;
            vacant->coverage.assign(package.components.size(), 0);
            best = vacant;
        }
        std::memcpy(best->cell, cell, sizeof(cell));
        best->last_frame = now;
        return best;
    }

    // Caller holds res_mx. Where the vertex shader of a pipeline reads its bones, found once per pipeline.
    BoneSite site_of(const PipeInfo& pipe)
    {
        auto found = sites.find(pipe.id);
        if (found != sites.end()) return found->second;
        BoneSite site;
        if (pipe.recipe && pipe.layout) {
            for (const auto& stage : pipe.recipe->stages) {
                if (stage.stage != VK_SHADER_STAGE_VERTEX_BIT || !stage.shader) continue;
                VertexRepoint out;
                if (repoint_vertex(stage.shader->code, static_cast<uint32_t>(pipe.layout->sets.size()), out) && out.bone_set < kMaxSets)
                    site = {true, out.bone_set, out.bone_binding};
            }
        }
        if (sites.size() >= 4096) sites.clear();
        sites[pipe.id] = site;
        return site;
    }

    bool record(CmdBuf* cb, const DrawRec& r);
};

bool CharacterBackend::init_materials()
{
    using namespace efmi;
    if (device->material_queues.empty()) { last_reason = device->material_queue_reason; return false; }
    if (package.material_vendor && (package.material_vendor != properties.vendorID || package.material_device != properties.deviceID)) {
        last_reason = "material dimension compatibility belongs to a different device"; return false;
    }
    if (!prepare_character_materials(package, materials, image_counts, last_reason)) return false;
#define X(n) n = reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle, "vk" #n)); \
    if (!n) { last_reason = "missing material vk" #n; return false; }
    X(CreateSemaphore) X(DestroySemaphore) X(CreateFence) X(DestroyFence) X(WaitForFences)
#undef X
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    for (auto queue : device->material_queues) {
        image_queues.push_back({queue});
        auto& q = image_queues.back();
        if (CreateSemaphore(device->handle, &si, nullptr, &q.semaphore) != VK_SUCCESS ||
            CreateFence(device->handle, &fi, nullptr, &q.fence) != VK_SUCCESS) {
            last_reason = "cannot allocate material queue publication"; return false;
        }
        image_signals.push_back(q.semaphore);
    }
    if (CreateFence(device->handle, &fi, nullptr, &publication_fence) != VK_SUCCESS) {
        last_reason = "cannot allocate material publication fence"; return false;
    }
    assets::ResourceDevice resource;
    resource.device = device->handle; resource.physical = device->phys; resource.memory = memory;
    resource.graphics_family = device->material_family; resource.bc_enabled = device->material_bc_enabled;
    resource.format_properties = device->material_format_properties; resource.image_properties = device->material_image_properties;
    resource.dispatch.SetDeviceLoaderData = device->set_loader_data;
    resource.buffer_dispatch.SetDeviceLoaderData = device->set_loader_data;
#define X(n) resource.dispatch.n = reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle, "vk" #n)); \
    if (!resource.dispatch.n) { last_reason = "missing material vk" #n; return false; }
    EFMI_IMAGE_UPLOAD_FUNCS(X)
#undef X
#define X(n) resource.buffer_dispatch.n = reinterpret_cast<PFN_vk##n>(device->next_gdpa(device->handle, "vk" #n));
    EFMI_BUFFER_UPLOAD_FUNCS(X)
#undef X
    images = std::make_unique<assets::ResourceCache>(resource, image_budgets, 64);
    for (const auto& path : package.images) {
        auto handle = images->declare_image(std::filesystem::u8path(path), last_reason);
        if (!handle || !images->demand(handle)) return false;
        image_handles.push_back(std::move(handle));
    }
    return true;
}

bool CharacterBackend::material_draws(uint32_t pipeline_id, const GraphicsRecipe& recipe, const PipelineLayoutInfo& layout,
                                      const CharacterList& list,
                                      std::vector<const StaticMaterial*>& plans, std::vector<std::vector<SnapshotImage>>& views)
{
    static const StaticMaterial inherited{true, false, {}, {}, {}};
    // No command can retain a file image until its publication signal is enqueued.
    // Every consumer queue inserts its own wait before forwarding application work.
    if (package.materials && !images_published.load(std::memory_order_acquire)) {
        ++counters[C_IMAGE_PENDING]; return false;
    }
    uint32_t profile = kCharacterNone;
    bool no_fragment = true;
    if (package.materials) {
        const auto found = material_profiles.find(pipeline_id);
        if (found != material_profiles.end()) profile = found->second;
        else {
            for (const auto& stage : recipe.stages) if (stage.stage == VK_SHADER_STAGE_FRAGMENT_BIT && stage.shader) {
                no_fragment = false;
                for (uint32_t i = 0; i < package.profiles.size(); ++i)
                    if (package.profiles[i].code == stage.shader->code) { profile = i; break; }
            }
            if (no_fragment) profile = kCharacterNone - 1;
            if (profile == kCharacterNone && layout.complete) {
                // A fragment shader whose layout holds no image for it cannot read a texture: every
                // slot the mod binds is absent there, as for the pass that tests visibility with
                // depth only. Nothing to replace; the mesh is still the mod's.
                bool images = false, known = true;
                for (const auto& set : layout.sets) {
                    if (!set || !set->complete) { known = false; break; }
                    for (const auto& b : set->bindings) {
                        const uint32_t type = b.second.type;
                        if ((b.second.stages & VK_SHADER_STAGE_FRAGMENT_BIT) &&
                            (type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                             type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT ||
                             type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER ||
                             type > VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)) images = true;
                    }
                }
                if (known && !images) profile = kCharacterNone - 1;
            }
            if (material_profiles.size() >= 4096) material_profiles.clear();
            material_profiles[pipeline_id] = profile;
        }
    }
    // Decide the entire logical transaction before obtaining any views or issuing a command.
    for (uint32_t i = 0; i < list.count; ++i) {
        const uint32_t index = list.first + i;
        const StaticMaterial* plan = &inherited;
        if (package.materials && !package.pixels[index].empty() && profile != kCharacterNone - 1) {
            if (profile == kCharacterNone) return refuse(C_MATERIAL, "exact fragment has no static material profile");
            plan = &materials[profile][index];
            if (!plan->supported) return refuse(C_MATERIAL, plan->reason);
        }
        plans.push_back(plan);
    }
    bool pending = false;
    for (const auto* plan : plans) {
        std::vector<SnapshotImage> draw_views;
        for (uint32_t asset : plan->assets) {
            auto result = image_handles[asset]->snapshot();
            if (result.state == efmi::assets::ResourceState::Failed || result.state == efmi::assets::ResourceState::Stopped)
                return refuse(C_IMAGE, "character texture: " + result.reason);
            if (result.state != efmi::assets::ResourceState::Ready || !result.image || !result.image->view()) { pending = true; continue; }
            draw_views.push_back({result.image->view(), result.image});
        }
        views.push_back(std::move(draw_views));
    }
    if (pending) { ++counters[C_IMAGE_PENDING]; return false; }
    return true;
}

bool CharacterBackend::record(CmdBuf* cb, const DrawRec& r)
{
    if (!enabled.load(std::memory_order_relaxed) || !cache) return false;
    const auto candidates = by_count.find(r.count);
    if (candidates == by_count.end()) return false;
    bool possible = false;
    for (uint32_t index : candidates->second)
        possible |= package.sections[index].match_first == kCharacterNone || package.sections[index].match_first == r.first;
    if (!possible) return false;

    std::lock_guard<std::mutex> mod_lock(mod_mx);
    const uint64_t now = frame.load(std::memory_order_relaxed);
    InstanceEntry entry, patched;
    const CharacterList* list = nullptr;
    Identity* identity = nullptr;
    std::vector<std::shared_ptr<const PipelineVariant>> variants;
    std::vector<const StaticMaterial*> material_plans;
    std::vector<std::vector<SnapshotImage>> material_views;
    DrawGeometry original;
    // The application's own set 3, when it has one bound: the replacement uses that slot, so it is
    // bound again afterwards exactly as the application bound it.
    uint32_t restore_slot = 0;
    VkPipelineLayout restore_layout = VK_NULL_HANDLE;
    VkDescriptorSet restore_set = VK_NULL_HANDLE;
    std::vector<uint32_t> restore_offsets;
    {
        std::shared_lock<std::shared_mutex> lock(device->res_mx);
        uint32_t hash = 0;
        if (!region_hash(r, now, hash)) return false;
        const CharacterSection* section = nullptr;
        for (uint32_t index : candidates->second) {
            const auto& s = package.sections[index];
            if (s.hash == hash && (s.match_first == kCharacterNone || s.match_first == r.first)) { section = &s; break; }
        }
        if (!section) return false;
        ++counters[C_MATCHED];
        // The mod's LOD variable: a section that assigns it changes it for the sections that do not.
        if (section->lod != kCharacterNone) lod = section->lod;
        const CharacterComponent& component = package.components[section->component];
        if (cb->secondary || !cb->in_rp || cb->dyn_rp_count || cb->m2_extended_bind)
            return refuse(C_STATE, "character draw outside a primary classic render pass or after extended binds");
        if (r.instances != 1 || r.first_instance != 0) return refuse(C_INSTANCES, "character draw is instanced");
        auto pipe = device->pipes.find(r.pipeline);
        if (pipe == device->pipes.end() || !pipe->second.recipe || !pipe->second.layout)
            return refuse(C_PIPELINE, "character draw pipeline metadata missing");

        // The instance entry is at set 2 binding 0 in every pipeline of the game's characters.
        std::vector<BoundRange> ranges_bound;
        const auto& window = r.sets[2];
        if (window.dyn_count && uint64_t(window.dyn_first) + window.dyn_count > cb->dyn.size())
            return refuse(C_ENTRY, "character dynamic offsets incomplete");
        collect_buffer_ranges_locked(device, window, window.dyn_count ? cb->dyn.data() + window.dyn_first : nullptr, window.dyn_count, 2, ranges_bound);
        auto read_range = [&](const BoundRange* range, uint64_t offset, size_t size, uint8_t* output) {
            if (!range || offset > range->size || size > range->size - offset) return false;
            auto buffer = device->bufs.find(range->buffer);
            return buffer != device->bufs.end() && read_buffer_locked(device, buffer->second, range->off + offset, size, output) == SRC_MAPPED;
        };
        const BoundRange* instance = nullptr;
        for (const auto& range : ranges_bound)
            if (range.element == 0 && range.binding == 0 &&
                (range.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || range.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)) instance = &range;
        if (!read_range(instance, 0, entry.size(), entry.data())) return refuse(C_ENTRY, "character instance entry is not mapped");
        identity = identify(entry, now);
        if (!identity) return refuse(C_IDENTITIES, "character position invalid or all identities in use");
        identity->seen |= 1u << section->component;
        if (identity->update_frame != now) {
            std::string reason;
            if (!identity->skeleton.begin_update(now, reason)) return refuse(C_IMPORT, reason);
            identity->update_frame = now;
        }

        // Import this component's poses into the identity's master, whatever happens to the draw.
        uint32_t offsets[2];
        std::memcpy(offsets, entry.data() + 80, 8);
        const uint8_t dynamic = (offsets[0] >= MergedSkeleton::kStaticRows ? 1 : 0) | (offsets[1] >= MergedSkeleton::kStaticRows ? 2 : 0);
        const BoneSite site = site_of(pipe->second);
        if (site.ok && dynamic && identity->skeleton.needs_import(0, section->component, lod)) {
            const bool remapped = lod == 1 && component.has_remap;
            uint32_t last = component.count - 1;
            if (remapped) last = *std::max_element(component.remap_lod1.begin(), component.remap_lod1.end());
            const size_t needed = (size_t(last) + 2) * MergedSkeleton::kBytesPerBone;
            current.resize(needed); previous.resize(needed);
            std::vector<BoundRange> bone_ranges;
            const auto& bound = r.sets[site.set];
            bool readable = !(bound.dyn_count && uint64_t(bound.dyn_first) + bound.dyn_count > cb->dyn.size());
            if (readable) collect_buffer_ranges_locked(device, bound, bound.dyn_count ? cb->dyn.data() + bound.dyn_first : nullptr, bound.dyn_count, site.set, bone_ranges);
            const BoundRange* bones = nullptr;
            for (const auto& range : bone_ranges)
                if (range.element == 0 && range.binding == site.binding &&
                    (range.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || range.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)) bones = &range;
            readable = readable && (!(dynamic & 1) || read_range(bones, uint64_t(offsets[0]) * 16, needed, current.data())) &&
                       (!(dynamic & 2) || read_range(bones, uint64_t(offsets[1]) * 16, needed, previous.data()));
            if (!readable) {
                ++counters[C_BONES];
            } else {
                std::string reason;
                static const std::vector<uint16_t> identity_map;
                const auto result = identity->skeleton.import_component(0, section->component, lod, entry,
                    {current.data(), needed}, {previous.data(), needed}, remapped ? component.remap_lod1 : identity_map, 1.0f, reason);
                if (result == BoneImportResult::Imported) {
                    identity->coverage[section->component] |= dynamic;
                    ++identity->generation;
                } else if (result == BoneImportResult::Rejected) {
                    ++counters[C_IMPORT];
                    note("character bone import: " + reason);
                }
            }
        }

        // As in the core: the object is the mod's once enough of its components were seen at this
        // place and a frame has passed. Until then the game draws it.
        if (!identity->custom) {
            uint32_t bits = 0;
            for (uint32_t value = identity->seen; value; value &= value - 1) ++bits;
            if (bits >= std::min<uint32_t>(kIdentifyingComponents, static_cast<uint32_t>(package.components.size())) && now > identity->first_frame)
                identity->custom = true;
            else { ++counters[C_IDENTIFYING]; return false; }
        }
        list = &package.lists[component.lists[lod]];
        if (!list->count) { ++counters[C_HIDDEN]; return true; }
        for (uint32_t c = 0; c < package.components.size(); ++c)
            if ((list->required >> c & 1) && (identity->coverage[c] & dynamic) != dynamic) { ++counters[C_INCOMPLETE]; return false; }
        // The private set is bound right above the pipeline's own sets: slot 3 for most character
        // pipelines, slot 4 for the four-set ones (the game's visibility pass among them).
        const uint32_t own = static_cast<uint32_t>(pipe->second.layout->sets.size());
        if (own >= kMaxSets) {
            // Not a tracked slot: usable only if no pipeline layout of the application reaches it.
            if (device->max_layout_sets > own) return refuse(C_SET3, "character draw: the slot above the pipeline's sets is in use and not tracked");
        } else if (r.sets[own].pushed) {
            return refuse(C_SET3, "character draw with pushed descriptors at the private set's slot");
        } else if (r.sets[own].set) {
            // The tracker keeps a set only while later binds left it valid, so the layout it was
            // bound with agrees with the current one for the lower sets, and binding it again there
            // disturbs nothing. Vulkan keeps that layout alive while this recording lasts.
            const SetBind& app = r.sets[own];
            restore_slot = own;
            auto live = device->pipeline_layouts.find(app.layout_handle);
            if (!app.layout_handle || !app.layout || app.unsupported || live == device->pipeline_layouts.end() ||
                live->second != app.layout || (app.dyn_count && uint64_t(app.dyn_first) + app.dyn_count > cb->dyn.size()))
                return refuse(C_SET3, "character draw with an application set that cannot be bound again");
            restore_layout = app.layout_handle; restore_set = app.set;
            restore_offsets.assign(cb->dyn.begin() + app.dyn_first, cb->dyn.begin() + app.dyn_first + app.dyn_count);
        }

        // Restore exactly the vertex bindings the pipeline can read among the four tracked ones.
        uint32_t slots = 0;
        for (const auto& b : pipe->second.recipe->vertex.bindings) if (b.binding < kMaxVb) slots = std::max(slots, b.binding + 1);
        for (uint32_t slot = 0; slot < 4; ++slot)
            for (uint32_t i = 0; i < list->count; ++i)
                if (package.draws[list->first + i].vb[slot] != kCharacterNone) slots = std::max(slots, slot + 1);
        if (!slots || !r.ib) return refuse(C_BINDINGS, "character draw without tracked vertex or index bindings");
        for (uint32_t slot = 0; slot < slots; ++slot) {
            if (!r.vb[slot].buf) return refuse(C_BINDINGS, "character original vertex bindings incomplete");
            original.buffers.push_back(r.vb[slot].buf); original.offsets.push_back(r.vb[slot].off);
        }
        original.index = r.ib; original.index_offset = r.ib_off;
        original.index_type = r.index_size == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

        bool pending = false;
        if (!material_draws(pipe->second.id, *pipe->second.recipe, *pipe->second.layout, *list, material_plans, material_views)) return false;
        for (uint32_t i = 0; i < list->count; ++i) {
            VertexInputRecipe vertex;
            std::string reason;
            if (!package.vertex_input(package.draws[list->first + i], pipe->second.recipe->vertex, vertex, reason))
                return refuse(C_VERTEX, "character vertex declaration: " + reason);
            const auto result = cache->request(pipe->second.id, pipe->second.recipe, pipe->second.layout, vertex, material_plans[i]->patch);
            if (result.variant) { variants.push_back(result.variant); continue; }
            if (result.state == VariantState::Queued || result.state == VariantState::Building) { pending = true; continue; }
            return refuse(C_VARIANT, "character variant: " + result.reason);
        }
        if (pending) { ++counters[C_PENDING]; return false; }
        std::string reason;
        if (!identity->skeleton.patch_entry(0, entry, patched, reason)) return refuse(C_IMPORT, reason);
    }

    if (!cb->m2) cb->m2 = command();
    if (!cb->m2) return refuse(C_EXHAUSTED, "character snapshot pool is at capacity or refilling");
    const auto mesh_owner = std::static_pointer_cast<const GpuMesh>(cb->m2->keep);
    if (!mesh_owner) return refuse(C_EXHAUSTED, "character command owner has no mesh");
    // Everything is decided; from here nothing can fail after a command was emitted.
    std::vector<DrawGeometry> geometry(list->count);
    std::vector<VariantDraw> items(list->count);
    const std::vector<uint8_t>& master = identity->skeleton.bytes();
    const uint64_t key = identity->id << 32 ^ identity->generation;
    for (uint32_t i = 0; i < list->count; ++i) {
        const CharacterDraw& d = package.draws[list->first + i];
        SnapshotSlot slot;
        const uint32_t count = static_cast<uint32_t>(material_views[i].size());
        auto arena = count ? cb->m2->material_snapshots.at(count) : cb->m2->snapshots;
        if (!arena->append_shared(master.data(), master.size(), key, patched, material_views[i], slot))
            return refuse(C_EXHAUSTED, arena->reason);
        DrawGeometry& g = geometry[i];
        g = original;
        for (uint32_t s = 0; s < g.buffers.size(); ++s)
            if (d.vb[s] != kCharacterNone) { g.buffers[s] = mesh_owner->buffer; g.offsets[s] = package.streams[d.vb[s]].offset; }
        const auto& index = package.streams[d.index_stream];
        g.index = mesh_owner->buffer; g.index_offset = index.offset;
        g.index_type = index.stride == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        IndexedDraw args; args.count = d.count; args.first = d.first; args.vertex_offset = d.base;
        items[i] = {variants[i].get(), slot.set, slot.offset, &g, args};
    }
    cb->m2->variants.insert(cb->m2->variants.end(), variants.begin(), variants.end());
    if (!emit_variant_draws(draw, cb->handle, items.data(), items.size(), r.pipeline, original))
        return refuse(C_VARIANT, "character draw transaction refused");
    if (restore_layout) {
        draw.CmdBindDescriptorSets(cb->handle, VK_PIPELINE_BIND_POINT_GRAPHICS, restore_layout, restore_slot, 1, &restore_set,
                                   static_cast<uint32_t>(restore_offsets.size()), restore_offsets.empty() ? nullptr : restore_offsets.data());
        ++counters[C_SET3_RESTORED];
    }
    ++counters[C_REPLACED];
    counters[C_ITEMS] += list->count;
    for (const auto* plan : material_plans) {
        if (!plan->patch.images.empty() || !plan->patch.nulls.empty()) ++counters[C_MATERIAL_ITEMS];
        if (plan->compatibility) ++counters[C_COMPAT_ITEMS];
    }
    return true;
}

void character_device_setup(Device* device, const VkDeviceCreateInfo* info, VkInstance instance, PFN_vkGetInstanceProcAddr gipa)
{
    device->material_queue_reason = "material queue discovery unavailable";
    if (!gipa) return;
    const auto families = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    const auto get_queue = reinterpret_cast<PFN_vkGetDeviceQueue>(device->next_gdpa(device->handle, "vkGetDeviceQueue"));
    if (!families || !get_queue) return;
    uint32_t count = 0; families(device->phys, &count, nullptr);
    if (!count || count > 256) return;
    std::vector<VkQueueFamilyProperties> properties(count); families(device->phys, &count, properties.data());
    const VkDeviceQueueCreateInfo* chosen = nullptr;
    for (uint32_t i = 0; i < info->queueCreateInfoCount; ++i) {
        const auto& q = info->pQueueCreateInfos[i];
        if (q.queueFamilyIndex < properties.size() && (properties[q.queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            logf("character graphics queue request: family=%u count=%u flags=%u", q.queueFamilyIndex, q.queueCount, q.flags);
            if (chosen || q.flags || !q.queueCount || q.queueCount > 32) {
                device->material_queue_reason = "materials require one unprotected graphics queue family, at most 32 queues";
                return;
            }
            chosen = &q;
        }
    }
    if (!chosen) { device->material_queue_reason = "no enabled graphics queue family"; return; }
    device->material_family = chosen->queueFamilyIndex;
    device->material_bc_enabled = info->pEnabledFeatures && info->pEnabledFeatures->textureCompressionBC;
    for (auto p = static_cast<const VkBaseInStructure*>(info->pNext); p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
            device->material_bc_enabled = reinterpret_cast<const VkPhysicalDeviceFeatures2*>(p)->features.textureCompressionBC != 0;
    device->material_format_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(gipa(instance, "vkGetPhysicalDeviceFormatProperties"));
    device->material_image_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties>(gipa(instance, "vkGetPhysicalDeviceImageFormatProperties"));
    std::vector<VkQueue> queues;
    for (uint32_t i = 0; i < chosen->queueCount; ++i) {
        VkQueue queue = VK_NULL_HANDLE;
        get_queue(device->handle, device->material_family, i, &queue);
        if (!queue || (device->set_loader_data && device->set_loader_data(device->handle, queue) != VK_SUCCESS)) {
            device->material_queue_reason = "cannot initialize material queue dispatch"; return;
        }
        queues.push_back(queue);
    }
    device->material_queues = std::move(queues);
    device->material_queue_reason.clear();
}

VkResult character_submit(CharacterBackend& backend, VkQueue queue)
{
    if (!backend.images || !backend.cache) return VK_SUCCESS;
    std::lock_guard<std::mutex> lock(backend.queue_mx);
    if (backend.queues_stopped) return VK_SUCCESS;
    auto found = std::find_if(backend.image_queues.begin(), backend.image_queues.end(),
                              [&](const ImageQueue& q) { return q.queue == queue; });
    if (found == backend.image_queues.end()) return VK_SUCCESS;
    if (!backend.upload_queue) {
        backend.upload_queue = queue;
        backend.upload_queue_index = static_cast<uint32_t>(found - backend.image_queues.begin());
    }
    if (queue == backend.upload_queue) {
        backend.images->submit_prepared(queue);
        if (!backend.images_published && backend.images->stats().ready == backend.image_handles.size()) {
            VkSubmitInfo publish{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            publish.signalSemaphoreCount = static_cast<uint32_t>(backend.image_signals.size());
            publish.pSignalSemaphores = backend.image_signals.data();
            const auto result = backend.device->d.QueueSubmit(queue, 1, &publish, backend.publication_fence);
            if (result != VK_SUCCESS) { backend.note("material publication submission failed"); return result; }
            backend.images_published.store(true, std::memory_order_release);
        }
    }
    if (backend.images_published && !found->waited) {
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo wait{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        wait.waitSemaphoreCount = 1; wait.pWaitSemaphores = &found->semaphore; wait.pWaitDstStageMask = &stage;
        const auto result = backend.device->d.QueueSubmit(queue, 1, &wait, found->fence);
        if (result != VK_SUCCESS) { backend.note("material consumer queue wait failed"); return result; }
        found->waited = true;
        ++backend.image_queue_waits;
    }
    return VK_SUCCESS;
}

std::shared_ptr<CharacterBackend> character_create(Device* device, const VkPhysicalDeviceProperties& properties,
                                                   const VkPhysicalDeviceMemoryProperties& memory)
{
    auto backend = std::make_shared<CharacterBackend>(device, properties, memory);
    try {
        if (!backend->init()) { logf("character package refused: %s", backend->last_reason.c_str()); return backend; }
        logf("character package configured: %u sections, %u components, %u bones, %u draws, enabled=%u",
             static_cast<uint32_t>(backend->package.sections.size()), static_cast<uint32_t>(backend->package.components.size()),
             backend->package.bones, static_cast<uint32_t>(backend->package.draws.size()), backend->enabled.load() ? 1 : 0);
    } catch (const std::exception& e) {
        backend->enabled = false; backend->note(e.what());
        logf("character package exception: %s", e.what());
    }
    return backend;
}

void character_stop(CharacterBackend& backend) { backend.stop(); }

bool character_record(CharacterBackend& backend, CmdBuf* cb, const DrawRec& r)
{
    try { return backend.record(cb, r); }
    catch (const std::exception& e) { ++backend.counters[C_STATE]; backend.note(e.what()); return false; }
}

void character_retire(CharacterBackend& backend, uint32_t pipeline_id)
{
    if (backend.cache) backend.cache->retire(pipeline_id);
    std::lock_guard<std::mutex> lock(backend.mod_mx);
    backend.sites.erase(pipeline_id);
    backend.material_profiles.erase(pipeline_id);
}

void character_frame(CharacterBackend& backend, bool had_draws)
{
    if (!had_draws) return;
    const uint64_t now = host_time_us();
    if (backend.last_frame_us && now > backend.last_frame_us) {
        const float seconds = static_cast<float>(now - backend.last_frame_us) * 1.0e-6f;
        backend.frame_seconds.store(std::min(std::max(seconds, 0.001f), 0.25f), std::memory_order_relaxed);
    }
    backend.last_frame_us = now;
    backend.frame.fetch_add(1, std::memory_order_relaxed);
}

void character_sealed(CharacterBackend& backend) { ++backend.batches; }

void character_enable(CharacterBackend& backend, bool enabled) { backend.enabled = enabled && bool(backend.cache); }

void character_status(CharacterBackend& backend, JsonWriter& w)
{
    w.kv("kind", "character");
    w.kb("configured", bool(backend.cache));
    w.kb("enabled", backend.enabled.load());
    w.kb("ready", backend.ready.load());
    // The names the single-component backend reports, so that one reader serves both.
    uint64_t unsupported = 0, unreadable = backend.counters[C_ENTRY].load() + backend.counters[C_BONES].load();
    for (uint32_t c = C_STATE; c <= C_BINDINGS; ++c) unsupported += backend.counters[c].load();
    unsupported += backend.counters[C_MATERIAL].load() + backend.counters[C_IMAGE].load();
    w.kn("matched", backend.counters[C_MATCHED].load());
    w.kn("replaced", backend.counters[C_REPLACED].load());
    w.kn("pending", backend.counters[C_PENDING].load());
    w.kn("unsupported", unsupported);
    w.kn("unreadable", unreadable);
    w.kn("exhausted", backend.counters[C_EXHAUSTED].load());
    w.kn("sealed_batches", backend.batches.load());
    w.key("counters"); w.begin_object();
    for (uint32_t c = 0; c < C_COUNT; ++c) w.kn(kCounterNames[c], backend.counters[c].load());
    w.end_object();
    w.kn("frame", backend.frame.load());
    w.kn("sections", backend.package.sections.size());
    w.kn("package_bytes", backend.package.payload.size());
    w.kb("materials", backend.package.materials);
    w.kn("material_profiles", backend.package.profiles.size());
    w.kn("image_count", backend.image_handles.size());
    w.kn("image_resident_bytes", backend.image_budgets.resident->used());
    w.kb("dimension_zero_policy", backend.package.material_vendor != 0);
    w.kn("material_graphics_queues", backend.device->material_queues.size());
    w.kn("material_graphics_family", backend.device->material_family);
    w.kn("image_upload_queue_index", backend.upload_queue_index.load());
    w.kb("images_published", backend.images_published.load());
    w.kn("image_queue_waits", backend.image_queue_waits.load());
    if (backend.images) {
        const auto stats = backend.images->stats();
        w.kn("images_ready", stats.ready); w.kn("images_failed", stats.failed);
        w.kn("image_file_reads", stats.file_reads); w.kn("image_submissions", stats.submissions);
    }
    {
        std::lock_guard<std::mutex> lock(backend.pool_mx);
        w.kv("last_reason", backend.last_reason);
        w.kn("available_batches", backend.available.size());
        w.kb("mesh_in_device_memory", backend.mesh && backend.mesh->device_local);
    }
    {
        std::lock_guard<std::mutex> lock(backend.mod_mx);
        uint32_t used = 0, custom = 0;
        for (const auto& identity : backend.identities) { used += identity.used; custom += identity.used && identity.custom; }
        w.kn("identities", used);
        w.kn("identities_replaced", custom);
        w.kn("lod", backend.lod);
    }
}

} // namespace efvk
