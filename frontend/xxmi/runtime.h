#pragma once
#include "../assets/file_plan.h"
#include <array>
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
};
struct RuntimeStatus {
    uint64_t draws = 0, presents = 0, key_events = 0, saves = 0, shader_calls=0;
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
    bool present(const RuntimeInput&, uint64_t frame, float seconds, std::string& reason);
    bool save(std::string& reason);
    bool take_reload_request();
    void inherit_persistent(const IniRuntime& previous);
    // Retired workers must not overwrite a newer generation's settings file.
    void suspend_persistence();
    RuntimeStatus status() const;
    std::vector<std::shared_ptr<const assets::FileResource>> resources() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    IniRuntime();
};
}
