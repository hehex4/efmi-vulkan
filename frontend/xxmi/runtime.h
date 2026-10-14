#pragma once
#include "../assets/file_plan.h"
#include <array>
#include <map>

namespace efmi::frontend {
class RuntimeExtension;
struct RuntimeConfig {
    std::filesystem::path resource_root;
    std::vector<std::filesystem::path> inputs;
    // The caller supplies a project-owned directory. Never writes next to a mod.
    std::filesystem::path settings_directory;
    std::string settings_name;
    // Optional enabled Mods tree; the original Include implementation owns
    // recursion, ordering, namespaces, conditions and exclusions.
    std::filesystem::path mods_directory;
};
struct RuntimeInput {
    bool foreground = false;
    std::array<bool, 256> keys{};
    // Original frame_time and spatial-pool radius scale, supplied by the host.
    // Zero preserves the original startup value; do not infer a frame rate.
    float frame_seconds = 0;
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
    uint64_t draws = 0, presents = 0, key_events = 0, saves = 0;
    size_t bindings = 0;
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
