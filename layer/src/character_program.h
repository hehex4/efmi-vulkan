#pragma once
#include "character_package.h"
#include "../../frontend/xxmi/runtime.h"
#include "../../frontend/xxmi/owned_runtime.h"
#include <functional>
#include <map>

namespace efvk {
// A parsed original INI generation and its file-to-renderer resource catalogue.
// A missing sidecar keeps the existing static/sandbox package behavior.
class CharacterProgram {
public:
    static bool configured(const std::string& package_path);
    // `gpu` makes the owned runtime for this generation (null keeps a CPU generation). With one,
    // the Mod folder's other enabled INIs join the generation, and the GPU commands of loading
    // stay recorded until the first Present (D-072).
    using GpuFactory = std::function<std::shared_ptr<efmi::frontend::OwnedRuntime>(const std::filesystem::path& working, std::string& why)>;
    bool init(const std::string& package_path, CharacterPackage&, std::string& reason, const GpuFactory& gpu = {});
    // Directory generation (D-078): no package file. The Mod's INI is loaded as for a package and the
    // package is made from what the original engine declares: the contract from the generation
    // (frontend CharacterDiscovery), every mesh file of its resources as a stream, its DDS files as
    // the image catalogue. Bone streams are found by the live plans. `core` is Core\EFMI.
    struct Source {
        std::filesystem::path ini, core, settings;
        std::string mod_namespace;
    };
    bool discover(const Source&, CharacterPackage&, std::string& reason, const GpuFactory& gpu = {});
    // The namespace an INI declares (`namespace = ...`), lower case; empty when it declares none.
    static std::string declared_namespace(const std::filesystem::path& ini);
    // The character Mods of an EFMI Mods folder: every enabled INI (no part of its path starts with
    // DISABLED, as the original skips them) that declares the EFMIv1 merged-skeleton initializer.
    static std::vector<std::filesystem::path> mods_characters(const std::filesystem::path& mods, std::string& reason);
    const std::shared_ptr<efmi::frontend::OwnedRuntime>& gpu() const { return gpu_; }
    const std::string& gpu_reason() const { return gpu_reason_; }
    // Mesh files the generation's INIs reference that are not in the package's stream profile.
    size_t outside_meshes() const { return outside_meshes_; }
    // Discovery: mesh sections of the mod the contract has no part for (they stay the game's).
    size_t left_to_game() const { return left_to_game_; }
    // `vertex`: the vertex input of the pipeline the draw uses, where the blend-index element is found
    // for a bone stream first seen; without it (offline) the packager's rule applies.
    bool plan(const efmi::frontend::RuntimeDraw&, CharacterPackage&, CharacterList&, bool& skip, bool& added, std::string& reason,
              const VertexInputRecipe* vertex = nullptr);
    bool present(const efmi::frontend::RuntimeInput&, uint64_t frame, float seconds, std::string& reason);
    bool save(std::string& reason) { return !runtime_ || runtime_->save(reason); }
    bool take_reload_request() { return runtime_ && runtime_->take_reload_request(); }
    void suspend_persistence() { if (runtime_) runtime_->suspend_persistence(); }
    void inherit_persistent(const CharacterProgram& previous) { runtime_->inherit_persistent(*previous.runtime_); }
    efmi::frontend::RuntimeStatus status() const { return runtime_ ? runtime_->status() : efmi::frontend::RuntimeStatus{}; }
private:
    bool load(const std::filesystem::path& ini, const std::filesystem::path& core, const std::filesystem::path& settings,
              const std::string& settings_name, std::shared_ptr<efmi::frontend::RuntimeExtension>, const GpuFactory&, std::string& reason);
    void catalogue(CharacterPackage&, const std::map<std::wstring, uint32_t>& remaps);
    std::shared_ptr<efmi::frontend::OwnedRuntime> gpu_;
    std::string gpu_reason_;
    std::shared_ptr<efmi::frontend::IniRuntime> runtime_;
    std::map<std::wstring, uint32_t> streams_, images_;
    std::map<std::string, CharacterList> lists_;
    std::vector<std::pair<uint32_t, uint32_t>> index_bounds_;
    size_t outside_meshes_ = 0, left_to_game_ = 0;
};
}
