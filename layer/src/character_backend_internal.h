// Configuration execution and Vulkan replacement state shared by the module.
#pragma once
#include "character_backend.h"
#include "shader_fixes.h"
#include "native_execution.h"
#include "native_geometry.h"
#include "native_targets.h"
#include "../../frontend/gpu/dither_kills.h"
#include "character_module.h"
#include "command_owner.h"
#include "draw_snapshots.h"
#include "variant_cache.h"
#include "configuration_program.h"
#include "character_present.h"
#include "texture_residency.h"
#include "../../frontend/gpu/source_registers.h"
#include "../../frontend/gpu/texture_roles.h"
#include "texture_identity.h"
#include "translucency.h"
#include <functional>
#include "merged_skeleton.h"
#include "state.h"
#include "work.h"
#include "json.h"
#include "../../frontend/assets/resource_cache.h"

#include <algorithm>
#include <filesystem>
#include <array>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <tuple>
#include <set>
#include <future>
#include <fstream>

namespace efvk {
namespace character_internal {
constexpr uint32_t kHuntingPink = 1u << 31;
inline thread_local uint32_t t_hunting_marking = 0;
inline thread_local int t_shader_depth = -1;
inline thread_local bool t_hunting_skip = false;
inline void apply_hunting(FragmentPatch& patch) {
    patch.dynamic_depth = t_shader_depth;
    patch.original_stages = t_hunting_marking & ~kHuntingPink;
    patch.pink = (t_hunting_marking & kHuntingPink) != 0;
    if (patch.original_stages & VK_SHADER_STAGE_FRAGMENT_BIT) patch.no_dither = false;
}

std::filesystem::path module_folder();
std::filesystem::path absolute_folder(const std::string&);
std::filesystem::path configuration_root();
float ini_seconds();
extern std::atomic<uint64_t> g_hash_full, g_hash_bytes, g_hash_new, g_hash_gen, g_hash_quick, g_hash_due, g_quick_reads, g_quick_skipped;
extern std::atomic<uint64_t> g_quick_read_us;
extern std::atomic<bool> g_dither_off, g_dither_gated;
extern std::shared_mutex g_dither_mx;
extern std::unordered_map<uint32_t, bool> g_dither_pipelines;
bool pipeline_dither(uint32_t, const GraphicsRecipe&);
inline uint64_t twin_identity(uint32_t id) { return (uint64_t(0x40) << 40) | id; }
inline FragmentPatch twin_patch() { FragmentPatch patch; patch.keep_vertex = true; patch.no_dither = true; return patch; }
inline FragmentPatch other_state(FragmentPatch patch) { patch.no_dither = !patch.no_dither; return patch; }
}
using namespace character_internal;
struct ConfigurationGeneration {
    Device* device = nullptr;
    std::shared_ptr<const NativeShaderSet> shader_fixes;
    std::shared_ptr<OwnedPresenter> presenter;
    std::shared_ptr<NativeGeometryContext> geometry;
    std::shared_ptr<std::mutex> plan_mx = std::make_shared<std::mutex>();
    std::shared_ptr<ConfigurationProgram> generation;
    ConfigurationProgram* program = nullptr;
    std::atomic<uint64_t> frame{1}, presents{0};
    std::atomic<float> frame_seconds{1.0f / 60};
    uint64_t last_frame_us = 0, save_checked_us = host_time_us();
    std::vector<efmi::frontend::IniRuntime::DiscardGate> gates; // D-133, under plan_mx
    std::future<void> saving;
    uint32_t gaps_seen = 0; size_t gaps_shown = 0;                 // LayerGap texts logged and shown, on the presenting thread
    void announce() const;
    std::string reason, last_note;
    std::mutex note_mx;
    void note(const std::string& text);
    void save();
    // Resource owners retained by recorded work survive configuration exchange.
    std::once_flag stop_once;
    void stop() { std::call_once(stop_once, [this] { stop_now(); }); }
    void stop_now();
};

struct PipelineModules {
    uint32_t hashes[6] = {};
    uint32_t mask = 0, stages = 0, targets = 0;
    bool targets_known = false;
    uint64_t render_pass = 0;
};
bool pipeline_modules(Device* device, VkPipeline pipeline, PipelineModules& out);

// What hunting and frame analysis read of a draw (docs/vulkan-native-mods.md section 6): its modules, the hash of its
// index region (the TextureOverride hash), its vertex buffers' region hashes as the original's track_region_hashes
// takes them (ResourceHash.cpp GetVertexBufferRegionOffset and Size), and its colour targets' images.
struct DrawFacts {
    PipelineModules modules;
    uint32_t marking = 0; // original stage bits, plus kHuntingPink for a selected draw
    bool indexed = false, index_known = false;
    uint32_t index_hash = 0;
    uint32_t vertex_mask = 0;                 // bit i: binding i has a hash
    uint32_t vertex_hashes[kMaxVb] = {};
    uint32_t target_count = 0;
    uint32_t targets[8] = {};                 // image ids of the colour attachments the pipeline's subpass writes
    uint32_t target_format[8] = {}, target_width[8] = {}, target_height[8] = {};
};

// 3DMigoto's hunting (Hunting.cpp) over what the frame's draws use, for the process: the installation's [Hunting]
// keys reach it through the folder generation's input dispatcher. Kinds: vertex and pixel (fragment) shader modules,
// index buffer regions, vertex buffer regions, render targets.
class Hunting {
public:
    enum Kind { Vertex = 0, Pixel = 1, Index = 2, Buffer = 3, Target = 4, Compute = 5, Geometry = 6, Hull = 7, Domain = 8, Kinds = 9 };
    // At a folder generation's load and after every F10 exchange, as the original's reload reads [Hunting] again.
    void configure(const efmi::frontend::HuntingSettings& settings);
    // A hunting key, on the presenting thread. `device` finds a marked module's bytes for its dump; `folder` is where
    // dumps and frame analyses go (the layer's settings folder).
    void act(efmi::frontend::HuntingAction action, Device* device, const std::filesystem::path& folder);
    bool enabled() const { return mode_.load(std::memory_order_relaxed) == 1; }
    // The value the `hunting` operand reads: the original's mode, 2 (soft disabled) where no key was registered.
    int operand() const { const int mode = mode_.load(std::memory_order_relaxed); return mode ? mode : 2; }
    // While enabled, at a draw: what it uses visited; true when it is to be kept from the GPU (the selection, skip).
    bool visit(DrawFacts& facts);
    // Frame analysis (FrameAnalysis.cpp, the log): armed by its key, it records the draws between the next two
    // presentations, or until key release in hold mode. `analyse` adds the draw's lines.
    bool analysing() const { return analysing_.load(std::memory_order_relaxed); }
    void analyse(const DrawRec& r, const DrawFacts& facts);
    // Once per presentation: the original's TimeoutHuntingBuffers; a frame analysis begun or written.
    void frame(const std::filesystem::path& folder, Device* device = nullptr);
    void status(JsonWriter& w);
private:
    std::string line_locked(int kind) const;
    void select_locked(int kind, bool next);
    std::mutex mx_;
    std::atomic<int> mode_{0}; // the original's hunting mode: 0 no keys, 1 enabled, 2 soft disabled
    bool keys_ = false, told_mode_ = false;
    int marking_mode_ = 0;
    uint32_t actions_ = 3, analysis_options_ = 0;
    std::set<uint32_t> visited_[Kinds];  // in ascending order, as the original's std::set
    std::set<uint32_t> peers_[Kinds][2]; // the vertex and pixel modules drawn with the selected element, for the mark's log
    uint32_t selected_[Kinds] = {};
    bool has_selected_[Kinds] = {};
    int buffer_slot_ = -1;               // the original's gSelectedVertexBufferSlotId: -1 every slot
    std::map<uint32_t, std::string> target_text_; // a visited render target's description, for its mark
    uint64_t last_key_us_ = 0;
    uint64_t input_events_ = 0, idle_resets_ = 0;
    std::atomic<uint64_t> skipped_{0};
    uint64_t marks_ = 0, dumps_ = 0;
    std::string last_dump_;
    // Frame analysis.
    std::atomic<bool> analysing_{false};
    bool armed_ = false, finish_ = false;
    uint32_t session_options_ = 0;
    uint64_t analysis_frames_ = 0;
    std::string finish_reason_;
    uint64_t analysed_draws_ = 0, analyses_ = 0;
    std::filesystem::path analysis_path_;
    std::string analysis_;
};

class CharacterBackend {
public:
    // Route 2 (docs/vulkan-native-mods.md): the SPIR-V hashes the folder generation's ShaderOverride sections name,
    // with the generation they belong to (under index_mx, replaced on the presenting thread); hunting; what the
    // ShaderOverride lists did at draws no Mod section matched.
    struct ShaderHooks {
        std::shared_ptr<ConfigurationGeneration> folder;
        ConfigurationProgram* program = nullptr;
        std::vector<uint32_t> hashes; // ascending
    };
    std::shared_ptr<const ShaderHooks> shader_hooks;
    Hunting hunting;
    std::atomic<uint64_t> shader_draws_run{0}, shader_draws_skipped{0}, shader_draws_kept{0}, shader_draws_refused{0}, shader_draws_executed{0};
    std::atomic<uint64_t> shader_draws_pending{0};
    NativeCallCache native_calls;
    std::mutex shader_note_mx;
    std::string shader_note;
    bool shader_bindings_told = false;
    // Resources are cached by declaration/file identity, independently of namespaces.
    std::unique_ptr<TextureResidency> textures;
    // Variants are shared by effective pipeline and resource layout.
    std::shared_ptr<VariantCache> variants;
    std::string reason;
    // F10 exchanges one complete configuration; existing recordings retain their owners.
    std::shared_ptr<ConfigurationGeneration> folder, folder_staged;
    std::future<std::shared_ptr<ConfigurationGeneration>> folder_loading;
    std::vector<std::future<void>> folder_retiring;
    uint64_t folder_staged_at = 0, folder_reloads = 0, folder_rejected = 0;
    bool folder_reload_again = false; // an F10 pressed while the last one loaded (BUG-069)
    bool host_f10_down = false;       // F10 as the host sampled it while no generation is loaded (L-02)
    uint64_t folder_reload_began_us = 0; // the F10 reload that runs, for its notice (D-137)
    // D-133: the existing pipelines' twins, prepared when a discard gate appears (at the start, or RabbitFX
    // added by F10); presenting thread.
    bool twins_swept = false;
    std::future<void> twins_sweep;
    std::string folder_error;
    void retire_folder(std::shared_ptr<ConfigurationGeneration> old);
    Device* device = nullptr;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};
    // The variant pipeline cache every package's variants are created through (D-101), loaded from
    // the settings folder when the backend is made and written back when it stops: the variants a
    // character needed in one session compile from the driver's cached form in the next.
    VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
    PFN_vkDestroyPipelineCache destroy_pipeline_cache = nullptr;
    PFN_vkGetPipelineCacheData pipeline_cache_data = nullptr;
    std::filesystem::path pipeline_cache_path;
    uint64_t pipeline_cache_loaded = 0, pipeline_cache_saved = 0;
    void open_pipeline_cache();
    void close_pipeline_cache();
    std::atomic<bool> enabled{true};  // off through the probe: no draw is hashed or asked for
    std::atomic<uint64_t> frames{0}; // presentations with draws, for the hash tables' rehash schedule
    // The recording path's time at the device level (D-099): every indexed draw asked, and of that
    // the index region hashing; the packages' stages are in their own status.
    std::atomic<uint64_t> record_calls{0}, record_us{0}, hash_calls{0}, hash_us{0}, hash_misses{0};
    std::atomic<uint64_t> frame_us{0}, frame_calls{0}; // character_frame: every package's presentation work
    // D-133: the twins of the application's pipelines without the character dither, by pipeline id.
    std::shared_mutex twins_mx;
    using TwinKey = std::tuple<uint64_t, uint32_t, bool, uint32_t, int>;
    std::map<TwinKey, std::shared_ptr<const PipelineVariant>> twins;
    std::set<TwinKey> twins_failed;
    std::atomic<uint64_t> shader_fix_binds{0};
    std::atomic<uint64_t> twin_binds{0}, twins_prepared{0};
    std::shared_mutex index_mx;
    ~CharacterBackend();
};

std::shared_ptr<ConfigurationGeneration> folder_create(Device*, const VkPhysicalDeviceMemoryProperties&, std::shared_ptr<VariantCache>);
void folder_frame(ConfigurationGeneration&, bool, PresentWork*, const efmi::frontend::RuntimeInput&);
void folder_frame_and_exchange(CharacterBackend&, bool, PresentWork*, const efmi::frontend::RuntimeInput&);
bool ini_present(ConfigurationProgram&, OwnedPresenter*, uint64_t, float, bool, PresentWork*,
                 const efmi::frontend::RuntimeInput&, uint64_t, const std::function<void(const std::string&)>&);
bool index_region_hash(Device*, const DrawRec&, uint64_t, uint32_t&);
// character_backend_shaders.cpp (docs/vulkan-native-mods.md)
void shader_hooks_frame(CharacterBackend& b, bool presenting = true);
// What hunting and frame analysis read of a draw whose modules are known; `buffers`: its index and vertex buffers too.
void draw_facts(CharacterBackend& b, CmdBuf* cb, const DrawRec& r, bool buffers, DrawFacts& facts);
bool hunting_draw(CharacterBackend& b, CmdBuf* cb, const DrawRec& r, DrawFacts& facts);
bool shader_draw_unmatched(CharacterBackend& b, CmdBuf* cb, const DrawRec& r, const PipelineModules& modules, bool run_lists, bool marked);
void shader_hooks_status(CharacterBackend& b, JsonWriter& w);
// character_backend_twins.cpp
void prepare_twins(CharacterBackend& b);
VkPipeline character_bind(CharacterBackend& b, CmdBuf* cb, VkPipeline pipeline, uint32_t marking = 0);
void character_created(CharacterBackend& b, VkPipeline pipeline);

// Selection is one draw's state, including its Mod draws. The host restores the pipeline after forwarding.
class HuntingDrawScope {
public:
    HuntingDrawScope(CharacterBackend&, CmdBuf*, const DrawRec&, uint32_t marking,bool skip = false);
    ~HuntingDrawScope() { t_hunting_marking = previous_; t_shader_depth = previous_depth_; t_hunting_skip = previous_skip_; }
    const DrawRec& draw() const { return marked_ ? *marked_ : original_; }
private:
    const DrawRec& original_;
    std::unique_ptr<DrawRec> marked_;
    uint32_t previous_;
    int previous_depth_;
    bool previous_skip_;
};

} // namespace efvk
