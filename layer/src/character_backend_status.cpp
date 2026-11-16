#include "character_backend_internal.h"
namespace efvk {
void character_status(CharacterBackend& b, JsonWriter& w)
{
    w.kv("kind", "configuration"); w.kv("reason", b.reason); w.kb("ready", character_ready(b));
    w.key("record"); w.begin_object();
    w.kn("calls", b.record_calls.load()); w.kn("microseconds", b.record_us.load());
    w.kn("frame_calls", b.frame_calls.load()); w.kn("frame_microseconds", b.frame_us.load());
    w.kn("hash_calls", b.hash_calls.load()); w.kn("hash_microseconds", b.hash_us.load()); w.kn("hash_misses", b.hash_misses.load());
    w.kn("full_hashes", g_hash_full.load()); w.kn("full_hash_bytes", g_hash_bytes.load());
    w.kn("full_new", g_hash_new.load()); w.kn("full_generation", g_hash_gen.load()); w.kn("full_fingerprint", g_hash_quick.load()); w.kn("full_due", g_hash_due.load());
    w.kn("fingerprint_reads", g_quick_reads.load()); w.kn("fingerprint_skipped", g_quick_skipped.load());
    w.kn("fingerprint_read_microseconds", g_quick_read_us.load());
    w.kb("pipeline_cache", b.pipeline_cache != VK_NULL_HANDLE); w.kn("pipeline_cache_loaded_bytes", b.pipeline_cache_loaded);
    if (b.variants) { w.kn("variant_waits", b.variants->waits()); w.kn("variant_wait_microseconds", b.variants->wait_microseconds()); }
    w.kb("upload_queue", b.device && b.device->upload_queue != VK_NULL_HANDLE);
    w.kn("snapshot_copy_us", g_snapshot_timing.copy_us.load()); w.kn("snapshot_descriptor_us", g_snapshot_timing.descriptor_us.load());
    w.kn("snapshot_images_us", g_snapshot_timing.images_us.load()); w.kn("snapshot_entry_us", g_snapshot_timing.entry_us.load());
    w.end_object();
    if (b.textures) b.textures->status(w);
    if (b.folder) {
        // D-131: the folder generation, its reloads and the Mods it left out.
        w.key("configuration_generation"); w.begin_object();
        w.kn("presents", b.folder->presents.load());
        w.kn("reloads", b.folder_reloads); w.kn("rejected", b.folder_rejected); w.kv("reload_error", b.folder_error);
        w.kb("loading", b.folder_loading.valid()); w.kb("staged", b.folder_staged != nullptr);
        if (b.folder->presenter) b.folder->presenter->status(w);
        if (b.folder->program) {
            const auto ini = b.folder->program->status();
            w.key("ini"); w.begin_object(); w.kn("presents", ini.presents); w.kn("draws", ini.draws); w.kn("diagnostics", ini.diagnostics);
            // The folder's variables, also where no package has the generation (an INI without mesh sections).
            w.key("variables"); w.begin_object();
            for (const auto& value : ini.variables) { w.key(value.first.c_str()); if (std::isfinite(value.second)) w.raw(std::to_string(value.second)); else w.null(); }
            w.end_object(); w.end_object();
        }
        {
            std::lock_guard<std::mutex> lock(*b.folder->plan_mx);
            w.key("discard_gates"); w.begin_array();
            for (const auto& g : b.folder->gates) {
                w.begin_object(); w.kn("index", g.index); w.kn("component", g.component); w.key("value"); w.raw(std::to_string(g.value)); w.end_object();
            }
            w.end_array();
        }
        w.kb("dither_off", g_dither_off.load());
        {
            std::shared_lock<std::shared_mutex> lock(b.twins_mx);
            w.kn("dither_twins", b.twins.size()); w.kn("dither_twins_failed", b.twins_failed.size());
        }
        w.kn("dither_twin_binds", b.twin_binds.load());
        w.kn("dither_twins_prepared", b.twins_prepared.load());
        w.end_object();
    }
    shader_hooks_status(b, w);
    {
        const auto engine = efmi::frontend::engine_lock_stats();
        w.key("engine_lock"); w.begin_object();
        w.kn("entries", engine.entries); w.kn("waits", engine.waits); w.kn("wait_us", engine.wait_us);
        w.end_object();
    }
}
}
