#pragma once
#include "../assets/file_plan.h"
#include <tuple>
#include <array>
#include <functional>
#include <map>

namespace efmi::frontend {
class RuntimeExtension;
struct SourceShaderState;
struct RuntimeShaderCall;
struct RuntimeConfig {
    std::filesystem::path resource_root;
    std::vector<std::filesystem::path> inputs;
    // The caller supplies a project-owned directory. Never writes next to a mod.
    std::filesystem::path settings_directory;
    std::string settings_name;
    // Optional enabled Mods tree; the original Include implementation owns
    // recursion, ordering, namespaces, conditions and exclusions.
    std::filesystem::path mods_directory;
    // Opt in only when the host supplies original tracked shader facts and an
    // executable pre/original/post call boundary. Existing character plans do not.
    bool shader_hooks=false;
    std::shared_ptr<const SourceShaderState> initial_shaders;
    // The host answers texture queries of draws (RuntimeDraw::pixel_texture): texture-hash
    // overrides, CheckTextureOverride and Core's OverrideTextures run against its answers (D-077).
    bool texture_identity=false;
    // Libraries the inputs call by namespace, from outside the resource root (D-112). Parsed after
    // the inputs, each under its path relative to the root (`..\EFMITTL\translucency.ini`), so the
    // files it names resolve against its own folder as the original resolves them.
    std::vector<std::filesystem::path> libraries;
};
struct RuntimeInput {
    bool foreground = false;
    std::array<bool, 256> keys{};
    // Original frame_time and spatial-pool radius scale, supplied by the host.
    // Zero preserves the original startup value; do not infer a frame rate.
    float frame_seconds = 0;
    // The presentation window and cursor, sampled with the keys. The original reads them with
    // GetClientRect, GetCursorInfo and ScreenToClient on its hWnd; a generation answers those
    // calls from this sample. Without a window the original has no hWnd: window_width falls
    // back to the resolution and then to 1920x1080, and cursor_window_* are screen positions.
    struct Window {
        bool present = false;
        int32_t client_left = 0, client_top = 0; // Client origin in screen coordinates.
        int32_t client_width = 0, client_height = 0;
    } window;
    struct Cursor {
        bool showing = false;
        int32_t screen_x = 0, screen_y = 0;
        int32_t hotspot_x = 0, hotspot_y = 0; // GetIconInfo on the cursor; zero without one.
    } cursor;
    // The swap chain size, which the original keeps in mResolutionInfo (res_width/res_height).
    // Zero keeps the previous value; a generation starts with the original's unset -1.
    uint32_t resolution_width = 0, resolution_height = 0;
    // GetDpiForMonitor's effective DPI for the window's monitor (the primary one without a
    // window), read per-monitor aware as the original does. The original reports at least 96;
    // zero, without a sample, reports that.
    uint32_t effective_dpi = 0;
    // The host's steady clock at this presentation in microseconds (the original's
    // GetSystemTicks), which advances the original FPS counter. Zero leaves it unchanged.
    uint64_t ticks_us = 0;
};
struct RuntimeDraw {
    uint32_t hash = 0, count = 0, first = 0, instances = 1, first_instance = 0;
    int32_t base = 0;
    uint64_t frame = 0;
    float seconds = 0;
    // Supplied only after the renderer has resolved this single native identity
    // and imported the component's source bones. No original-resource queries.
    uint32_t component = UINT32_MAX, lod = UINT32_MAX;
    uint64_t identity = 0;
    // Optional source-API facts from a verified shader contract. A Vulkan
    // binding number alone does not establish the original DX11 constant slot.
    bool rolling_bone_source = false;
    int32_t source_vs_config_slot = -1;
    float frame_seconds = 0;
    // With RuntimeConfig::texture_identity: the application's texture at a DX11 pixel slot of this
    // draw, as the original's resource hash (`bound` false for an empty slot). False refuses.
    std::function<bool(unsigned slot, bool& bound, uint32_t& hash, std::string& reason)> pixel_texture;
    // RabbitFX's texture roles in this draw (D-082): the DX11 t registers of the diffuse map, the
    // lightmap and the normal map its fragment shader samples, -1 for a role it has not. False:
    // not known, and the RabbitFX library binds nothing.
    std::function<bool(int32_t roles[3], std::string& reason)> texture_roles;
    // The renderer's instance entry of this single-instance draw (the 64 floats per instance the
    // original Core reads from `Original_VS_CB`, its translation at floats 12 to 14), valid during the
    // call; a Mod that runs the Core's spatial identification itself reads it (D-086). Null: none.
    const uint8_t* instance_entry = nullptr;
    size_t instance_entry_size = 0;
    // The draw's render targets as its pipeline's subpass names them (D-087): bit i for a color
    // attachment at location i, which the fragment shader's output i (DX11 oN) writes, bit 8 for a
    // depth attachment. `render_pass` tells the attachments of different render passes apart.
    bool targets_known = false;
    uint32_t render_targets = 0;
    uint64_t render_pass = 0;
    // The shader stages of the draw's pipeline, as Vulkan stage bits (vertex 1, geometry 8,
    // fragment 16), known with the render targets (D-089).
    uint32_t shader_stages = 0;
    // What a library's shader hooks see of this draw under DX11, from the host's Vulkan pipeline
    // (D-113): its vertex shader has the output interface of the game's lit character vertex shader.
    bool lit_vertex = false;
    // The size of the draw's colour target 0 as the host knows it (the original's rt_width and
    // rt_height, read off the bound render target), -1 when the host does not know it.
    float target_width = -1, target_height = -1;
};
// The events of the game's frame a library's shader hooks see under DX11 and the host reports
// (D-113): the game's copy shader drew with a single colour target.
enum class LibraryHook : uint32_t { CopyShader = 1 };
struct RuntimeStatus {
    uint64_t draws = 0, presents = 0, replays = 0, key_events = 0, saves = 0, shader_calls=0;
    // Microseconds the matched command lists took to run and their plans to compile, over all draws,
    // and the calls and microseconds per command class.
    uint64_t draw_execute_us = 0, draw_compile_us = 0;
    std::vector<std::tuple<std::string, uint64_t, uint64_t>> commands;
    size_t bindings = 0;
    // Original warnings while this generation was parsed. The original engine reports them and
    // continues with what it could parse; so does this runtime. At most the latest 64 texts.
    size_t diagnostics = 0;
    std::vector<std::string> diagnostic_messages;
    bool dirty = false, reload_requested = false;
    std::map<std::string, float> variables;
};

// Owns an isolated generation of the original XXMI frontend. Calls are serialized
// across generations because the original engine addresses its globals directly.
// Files/resources in emitted plans are owned, not pointers into this generation.
class IniRuntime {
public:
    static std::shared_ptr<IniRuntime> load(const RuntimeConfig&, std::shared_ptr<RuntimeExtension>, std::string& reason);
    ~IniRuntime();
    IniRuntime(const IniRuntime&) = delete;
    IniRuntime& operator=(const IniRuntime&) = delete;
    bool draw(const RuntimeDraw&, assets::FilePlan&, std::string& reason);
    bool bind_source_shaders(const SourceShaderState&,std::string& reason);
    bool intercept(const RuntimeShaderCall&,std::string& reason);
    // Input is supplied by the host. Tests never send input to other processes.
    // `keep_replay_state`: keep what replay() needs (every variable's value before the lists, D-104);
    // the host asks for it only where it will replay, the copy of every variable costing per package.
    bool present(const RuntimeInput&, uint64_t frame, float seconds, std::string& reason, bool keep_replay_state = true);
    bool replayable() const;
    // The last present() again for another presentation of the same frame (frame generation,
    // a frame presented again): the Present lists run from the variable state present() started
    // with, recording their GPU work again, and every variable and parameter is restored after.
    // No key is dispatched and no transition advances. False with the reason, or without a
    // presentation to replay (BUG-036).
    bool replay(const RuntimeInput&, uint64_t frame, float seconds, std::string& reason);
    bool save(std::string& reason);
    bool take_reload_request();
    // A shader hook event of the game's frame (D-113), for the libraries the generation loaded.
    void library_hook(LibraryHook);
    void inherit_persistent(const IniRuntime& previous);
    // Retired workers must not overwrite a newer generation's settings file.
    void suspend_persistence();
    RuntimeStatus status() const;
    void note_timing(const char* name, uint64_t microseconds);
    std::vector<std::shared_ptr<const assets::FileResource>> resources() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    IniRuntime();
};
}
