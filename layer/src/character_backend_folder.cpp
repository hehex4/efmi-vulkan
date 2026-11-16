// One original configuration registry, its Present and atomic F10 exchange.
#include "character_backend_internal.h"

namespace efvk {
// Loading failures are visible even if no command lists are available.
void ConfigurationGeneration::announce() const
{
    if (!program) notice(NoticeLevel::Dire, "configuration", ("EFMI Vulkan configuration: " + reason).c_str());
}

void ConfigurationGeneration::note(const std::string& text)
{
    std::lock_guard<std::mutex> lock(note_mx);
    if (text == last_note) return;
    last_note = text; logf("folder generation: %s", text.c_str());
}

void ConfigurationGeneration::save()
{
    if (saving.valid() && saving.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    if (saving.valid()) saving.get();
    auto keep = generation;
    saving = std::async(std::launch::async, [keep] {
        std::string error;
        if (keep && !keep->save(error)) logf("folder generation settings: %s", error.c_str());
    });
}

void ConfigurationGeneration::stop_now()
{
    if (saving.valid()) saving.get();
    if (presenter) presenter->stop();
    if (program) { std::string error; if (!program->save(error)) logf("folder generation shutdown settings: %s", error.c_str()); }
}

std::shared_ptr<ConfigurationGeneration> folder_create(Device* device, const VkPhysicalDeviceMemoryProperties& memory,
                                                               std::shared_ptr<VariantCache> variants)
{
    auto folder = std::make_shared<ConfigurationGeneration>();
    folder->device = device;
    const auto root = configuration_root();
    const auto settings = g_cfg.m2_settings.empty() ? module_folder() / "settings" : absolute_folder(g_cfg.m2_settings);
    efmi::frontend::RuntimeConfig source{root, {}, settings, "configuration"};
    source.installation = root;
    std::string gpu_reason;
    auto gpu = OwnedPresenter::runtime(device, memory, root, gpu_reason, variants ? variants->pipeline_cache() : VK_NULL_HANDLE);
    if (!gpu_reason.empty()) logf("configuration GPU: %s", gpu_reason.c_str());
    auto program = std::make_shared<ConfigurationProgram>();
    if (program->load(std::move(source), gpu, folder->reason)) {
        folder->generation = std::move(program);
        folder->program = folder->generation.get();
        if (gpu) folder->presenter = std::make_shared<OwnedPresenter>(device, gpu);
        folder->gates = folder->program->discard_gates();
        const auto loaded = folder->program->status();
        logf("configuration loaded: %zu variables, %zu file resources, %zu diagnostics", loaded.variables.size(),
             folder->program->runtime()->resources().size(), loaded.diagnostics);
        for (const auto& warning : loaded.diagnostic_messages) logf("INI warning: %s", warning.c_str());
    } else logf("configuration refused: %s", folder->reason.c_str());
    folder->shader_fixes = load_shader_fixes(device, variants ? variants->native_shaders() : nullptr,
        folder->program ? folder->program->runtime()->shader_depth_filters() : std::map<uint32_t, int>{});
    return folder;
}

// The folder generation's part of a presentation: its INI Present once, under the lock its packages'
// plans take, and its saves (D-131).
void folder_frame(ConfigurationGeneration& folder, bool had_draws, PresentWork* present, const efmi::frontend::RuntimeInput& base)
{
    const uint64_t now = host_time_us();
    if (had_draws) {
        if (folder.last_frame_us && now > folder.last_frame_us) {
            const float seconds = static_cast<float>(now - folder.last_frame_us) * 1.0e-6f;
            folder.frame_seconds.store(std::min(std::max(seconds, 0.001f), 0.25f), std::memory_order_relaxed);
        }
        folder.last_frame_us = now;
        folder.frame.fetch_add(1, std::memory_order_relaxed);
    }
    folder.presents.fetch_add(1, std::memory_order_relaxed);
    if (!folder.program) return;
    ThreadTrace* const trace = folder.device->trace ? folder.device->trace() : nullptr;
    StageMark locking(trace, "folder Present (generation lock)");
    std::lock_guard<std::mutex> lock(*folder.plan_mx);
    // What the layer did not do as the original does since the last presentation (LayerGap, D-137): each
    // in the log, the first three of a generation on the screen, then one line for the rest.
    if (const uint32_t count = folder.program->runtime_gap_count(); count != folder.gaps_seen) {
        for (const auto& text : folder.program->runtime_gaps_since(folder.gaps_seen)) {
            logf("folder generation: not as the original: %s", text.c_str());
            if (folder.gaps_shown < 3) notice(NoticeLevel::Warning, text.c_str(), ("EFMI Vulkan: " + text).c_str());
            else if (folder.gaps_shown == 3) notice(NoticeLevel::Notice, "", "EFMI Vulkan: more that the layer does not do as the original is in efmi_vk_layer.log");
            ++folder.gaps_shown;
        }
        folder.gaps_seen = count;
    }
    StageMark stage(trace, "folder Present (owned scope, INI Present, owned submission)");
    if (!ini_present(*folder.program, folder.presenter.get(), folder.frame.load(), folder.frame_seconds.load(std::memory_order_relaxed),
                     had_draws, present, base, now, [&folder](const std::string& text) { folder.note(text); }))
        return;
    // The discard gates the presentation's assignments left (D-133): any one set turns the dither off.
    folder.gates = folder.program->discard_gates();
    g_dither_off.store(std::any_of(folder.gates.begin(), folder.gates.end(), [](const auto& g) { return g.value != 0; }), std::memory_order_relaxed);
    g_dither_gated.store(!folder.gates.empty(), std::memory_order_relaxed);
    if (now - folder.save_checked_us >= g_cfg.settings_save_seconds * 1000000) {
        folder.save_checked_us = now;
        if (folder.program->settings_dirty()) folder.save();
    }
}

void CharacterBackend::retire_folder(std::shared_ptr<ConfigurationGeneration> old)
{
    if (old) folder_retiring.push_back(std::async(std::launch::async, [old = std::move(old)] {
        try { old->stop(); }
        catch (const std::exception& e) { logf("folder generation retirement: %s", e.what()); }
    }));
}

// D-131: the folder generation's presentation work and its F10 (presenting thread). The successor loads in
// the background; packages the frames draw (activated ones) bring their successors up, with the images
// they show; once they are ready every package is exchanged at this one point, a Mod added becoming a
// package and one gone stopping, and the old generation stops off this thread.
void folder_frame_and_exchange(CharacterBackend& b, bool had_draws, PresentWork* present, const efmi::frontend::RuntimeInput& input)
{
    if (b.folder) folder_frame(*b.folder, had_draws, present, input);
    // A discard gate there now and not before (the start, RabbitFX added by F10, a module exchange): the
    // twins of the pipelines the game already has are prepared off this thread (D-133).
    const bool gated = g_dither_gated.load(std::memory_order_relaxed);
    if (!gated) b.twins_swept = false;
    else if (!b.twins_swept && !(b.twins_sweep.valid() && b.twins_sweep.wait_for(std::chrono::seconds(0)) != std::future_status::ready)) {
        if (b.twins_sweep.valid()) b.twins_sweep.get();
        b.twins_swept = true;
        b.twins_sweep = std::async(std::launch::async, [&b] {
            try { prepare_twins(b); } catch (const std::exception& e) { logf("character dither: twins not prepared: %s", e.what()); }
        });
    }
    for (auto it = b.folder_retiring.begin(); it != b.folder_retiring.end();) {
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) { it->get(); it = b.folder_retiring.erase(it); }
        else ++it;
    }
    if (b.folder_loading.valid() && b.folder_loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto candidate = b.folder_loading.get();
        if (!candidate || !candidate->program) {
            ++b.folder_rejected;
            b.folder_error = candidate ? candidate->reason : "configuration load failed";
            notice(NoticeLevel::Warning, "configuration", ("Reload failed; current configuration retained: " + b.folder_error).c_str());
            b.retire_folder(std::move(candidate));
        } else {
            if (b.folder && b.folder->program) {
                std::lock_guard<std::mutex> lock(*b.folder->plan_mx);
                b.folder_reload_again |= b.folder->program->take_reload_request();
                candidate->program->inherit_persistent(*b.folder->program);
                b.folder->program->suspend_persistence();
            }
            auto old = std::move(b.folder);
            b.folder = std::move(candidate);
            ++b.folder_reloads; b.folder_error.clear();
            logf("configuration generation %llu: F10 exchange complete", static_cast<unsigned long long>(b.folder_reloads));
            b.folder->announce();
            b.retire_folder(std::move(old));
        }
        b.folder_reload_began_us = 0;
    }
    // Without a loaded generation (the folder refused at the start, or every character Mod was gone at the
    // last reload) there is no reload_config binding to take the key: it is the host's then, as the
    // original reloads whatever it holds. F10 did nothing in that state before, and the game had to be
    // started again after a Mod was repaired (docs/architecture-review-2.1.md, L-02).
    bool host_reload = false;
    if (b.folder && !b.folder->program) {
        const bool down = input.foreground && input.keys[VK_F10] && !input.keys[VK_CONTROL] && !input.keys[VK_SHIFT] && !input.keys[VK_MENU];
        host_reload = down && !b.host_f10_down;
        b.host_f10_down = down;
    } else b.host_f10_down = false;
    // A request is taken only when no reload runs: one pressed meanwhile waits for it (BUG-069).
    if (b.folder && !b.folder_loading.valid() && !b.folder_staged &&
        (b.folder->program ? (std::exchange(b.folder_reload_again, false) || b.folder->program->take_reload_request()) : host_reload)) {
        logf("folder generation: F10 reload starts");
        if (!b.folder_reload_began_us) b.folder_reload_began_us = host_time_us();
        Device* const d = b.device; const auto p = b.properties; const auto m = b.memory;
        auto variants = b.variants;
        auto current = b.folder;
        b.folder_loading = std::async(std::launch::async, [d, m, variants, current] {
            // The settings the successor reads are those of now.
            std::string reason;
            if (current->program && !current->program->save(reason)) logf("folder generation settings before reload: %s", reason.c_str());
            return folder_create(d, m, variants);
        });
    }
}

} // namespace efvk
