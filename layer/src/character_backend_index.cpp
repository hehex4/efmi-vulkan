// Index-region identity supplied to the original configuration matcher.
#include "character_backend_internal.h"

namespace efvk {
namespace {
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
struct IndexHash { uint32_t buffer_id = 0; uint64_t gen = 0, next_full = 0; uint32_t hash = 0, quick = 0; uint64_t seen = 0, next_quick = 0; };
// Caller holds res_mx. The index bytes of one draw, hashed once for every package of the device
// (D-097) and then recognized by a short fingerprint; the full hash is repeated now and then because
// mapped pools change unseen. Each recording thread keeps its own table, so no draw waits for another.
// The table holds every region a scene draws (the open world draws thousands per frame, D-099); when
// it is full, the entries not seen for a while go, never the whole table: emptying it made every draw
// hash its whole region again (20 ms per frame in the 1.6.8 session).
constexpr size_t kHashTableEntries = 65536;
constexpr uint64_t kHashTableIdleFrames = 8;
constexpr uint64_t kRehashFrames = 1024;
struct IndexHashTable {
    std::unordered_map<IndexKey, IndexHash, IndexKeyHash> hashes;
    std::vector<uint8_t> scratch;
    uint64_t full_hashes = 0, trims = 0;
    void trim(uint64_t now)
    {
        for (auto it = hashes.begin(); it != hashes.end();) {
            if (it->second.seen + kHashTableIdleFrames < now) it = hashes.erase(it);
            else ++it;
        }
        // Still full (one frame draws more regions than the table holds): start over.
        if (hashes.size() >= kHashTableEntries) hashes.clear();
        ++trims;
    }
};
} // namespace

namespace character_internal {
// Full hashes by cause, device-wide (D-099): a region seen first, the buffer's generation changed
// (a copy into the buffer, anywhere in it), the edge fingerprint changed, or the periodic repeat.
std::atomic<uint64_t> g_hash_full{0}, g_hash_bytes{0}, g_hash_new{0}, g_hash_gen{0}, g_hash_quick{0}, g_hash_due{0}, g_quick_reads{0}, g_quick_skipped{0};
std::atomic<uint64_t> g_quick_read_us{0}; // microseconds of the edge fingerprint reads (write-combined memory)
} // namespace character_internal

// Caller holds res_mx. The index bytes of one draw, hashed once for every package of the device
// (D-097) and then recognized by a short fingerprint; the full hash is repeated now and then because
// mapped pools change unseen. Each recording thread keeps its own table (above).
// The fingerprint is two uncached lines read over the bus (most of a draw's cost in the 1.6.8 session,
// D-099): it is read every draw only for a region whose hash a package acts on, where a stale hash
// would replace the wrong object; for the others every kQuickFrames, a stale hash there delays a
// match by that much at most.
constexpr uint64_t kQuickFrames = 16;
bool index_region_hash(Device* device, const DrawRec& r, uint64_t now, uint32_t& hash)
{
    thread_local IndexHashTable table;
    auto ib = device->bufs.find(r.ib);
    if (ib == device->bufs.end() || (r.index_size != 2 && r.index_size != 4)) return false;
    const uint64_t start = uint64_t(r.first) * r.index_size, bytes = uint64_t(r.count) * r.index_size;
    const BufInfo& info = ib->second;
    if (r.ib_off > info.size || start > info.size - r.ib_off || bytes > info.size - r.ib_off - start || bytes > (64u << 20)) return false;
    const uint64_t offset = r.ib_off + start;
    const IndexKey key = {r.ib, offset, static_cast<uint32_t>(bytes)};
    // The region's generation: the newest tracked write overlapping it (D-099), not the buffer's.
    const uint64_t region_gen = info.region_gen(offset, bytes);
    auto found = table.hashes.find(key);
    const bool current = found != table.hashes.end() && found->second.buffer_id == info.id && found->second.gen == region_gen &&
                         now < found->second.next_full;
    // Stable bytes are not proof that a coherent mapped pool cannot be rewritten (BUG-040).
    // Positive matches must still inspect the fingerprint on every draw.
    uint8_t edge[128];
    const size_t head = static_cast<size_t>(std::min<uint64_t>(bytes, 64)), tail = static_cast<size_t>(std::min<uint64_t>(bytes - head, 64));
    const uint64_t read_began = host_time_us();
    if (read_buffer_locked(device, info, offset, head, edge) != SRC_MAPPED ||
        (tail && read_buffer_locked(device, info, offset + bytes - tail, tail, edge + head) != SRC_MAPPED)) return false;
    g_quick_read_us += host_time_us() - read_began;
    const uint32_t quick = crc32c(0, edge, head + tail);
    ++g_quick_reads;
    if (current && found->second.quick == quick) {
        found->second.seen = now; found->second.next_quick = now + kQuickFrames; hash = found->second.hash;
        return true;
    }
    if (found == table.hashes.end()) ++g_hash_new;
    else if (found->second.buffer_id != info.id || found->second.gen != region_gen) ++g_hash_gen;
    else if (found->second.quick != quick) ++g_hash_quick;
    else ++g_hash_due;
    table.scratch.resize(static_cast<size_t>(bytes));
    if (read_buffer_locked(device, info, offset, bytes, table.scratch.data()) != SRC_MAPPED) return false;
    hash = crc32c(0, table.scratch.data(), table.scratch.size());
    ++table.full_hashes; ++g_hash_full; g_hash_bytes += bytes;
    if (found == table.hashes.end() && table.hashes.size() >= kHashTableEntries) table.trim(now);
    // Regions first seen in one frame are not all hashed again in one later frame.
    table.hashes[key] = {info.id, region_gen, now + kRehashFrames + (IndexKeyHash()(key) >> 7) % kRehashFrames, hash, quick, now, now + kQuickFrames};
    return true;
}

bool character_record(CharacterBackend& b, CmdBuf* cb, const DrawRec& r)
{
    if (!b.enabled.load(std::memory_order_relaxed)) return false;
    DrawFacts facts;
    pipeline_modules(b.device, r.pipeline, facts.modules);
    const bool marked = hunting_draw(b, cb, r, facts);
    HuntingDrawScope marking(b, cb, r, facts.marking, marked);
    if (cb->native_target_blocked) return true;
    return shader_draw_unmatched(b, cb, marking.draw(), facts.modules, true, marked);
}
} // namespace efvk
