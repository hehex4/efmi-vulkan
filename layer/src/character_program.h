#pragma once
#include "character_package.h"
#include "../../frontend/xxmi/runtime.h"
#include "../../frontend/xxmi/owned_runtime.h"
#include <algorithm>
#include <functional>
#include <string>
#include <map>
#include <unordered_map>
#include <set>
#include <tuple>

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
        // The Mods folder, where the libraries the Mod calls by namespace are found (D-112); empty: none.
        std::filesystem::path mods;
        // Offline, without a GPU: a Mod with CustomShader sections loads with them declared, so its
        // sections and draw plans are known; the lists that run them refuse as outside the owned domain.
        bool declarations_only = false;
    };
    bool discover(const Source&, CharacterPackage&, std::string& reason, const GpuFactory& gpu = {});
    // The namespace an INI declares (`namespace = ...`), lower case; empty when it declares none.
    static std::string declared_namespace(const std::filesystem::path& ini);
    // The character Mods of an EFMI Mods folder: every enabled INI (no part of its path starts with
    // DISABLED, as the original skips them) that declares the EFMIv1 merged-skeleton initializer.
    static std::vector<std::filesystem::path> mods_characters(const std::filesystem::path& mods, std::string& reason);
    // The libraries a generation of `inputs` calls (D-112): every enabled INI of the Mods folder outside
    // every character Mod's folder that declares a namespace the inputs reference (`CommandList\NS\...`,
    // `$\NS\...`) and that neither an input nor `known` (the extension's own libraries) declares; then
    // those they call. `notes` names the ones left out and why.
    static std::vector<std::filesystem::path> libraries(const std::filesystem::path& mods, const std::vector<std::filesystem::path>& inputs,
                                                        const std::vector<std::string>& known, std::vector<std::string>& notes);
    // The libraries the generation loaded and those it left out (D-112), for the log.
    const std::vector<std::string>& library_notes() const { return library_notes_; }
    // The folder of the library of this namespace the generation loaded (D-112), empty without one.
    std::filesystem::path library_folder(const std::string& name) const
    {
        const auto found = libraries_.find(name);
        return found == libraries_.end() ? std::filesystem::path{} : found->second.parent_path();
    }
    // EFMITTL is loaded with the interface its bridge knows (D-113): the renderer supplies its hook
    // facts and records its captures.
    bool efmittl() const { return efmittl_; }
    // A shader hook event of the game's frame for the generation's libraries (D-113).
    void library_hook(efmi::frontend::LibraryHook hook) { if (runtime_) runtime_->library_hook(hook); }
    const std::shared_ptr<efmi::frontend::OwnedRuntime>& gpu() const { return gpu_; }
    const std::string& gpu_reason() const { return gpu_reason_; }
    // Mesh files the generation's INIs reference that are not in the package's stream profile.
    size_t outside_meshes() const { return outside_meshes_; }
    // Discovery: mesh sections of the mod the contract has no part for (they stay the game's).
    size_t left_to_game() const { return left_to_game_; }
    // The Mod's identification threshold (CharacterDiscovery::identify_components); 0 unknown.
    uint32_t identify_components() const { return identify_components_; }
    // `vertex`: the vertex input of the pipeline the draw uses, where the blend-index element is found
    // for a bone stream first seen; without it (offline) the packager's rule applies.
    // `textures`: when the plan leaves the application's draw (skip false) but binds textures for it in
    // its pre phase, and nothing else, `output` is a list of one texture-only draw with those pixels
    // and *textures is true (D-083). Without the pointer such a plan is just the application's draw.
    void note_timing(const char* name, uint64_t microseconds) { if (runtime_) runtime_->note_timing(name, microseconds); }
    bool plan(const efmi::frontend::RuntimeDraw&, CharacterPackage&, CharacterList&, bool& skip, bool& added, std::string& reason,
              const VertexInputRecipe* vertex = nullptr, bool* textures = nullptr);
    bool present(const efmi::frontend::RuntimeInput&, uint64_t frame, float seconds, std::string& reason, bool keep_replay_state = true);
    bool replayable() const { return runtime_ && runtime_->replayable(); }
    // The last Present again, for another presentation of the same frame (BUG-036).
    bool replay(const efmi::frontend::RuntimeInput&, uint64_t frame, float seconds, std::string& reason);
    bool save(std::string& reason) { return !runtime_ || runtime_->save(reason); }
    bool take_reload_request() { return runtime_ && runtime_->take_reload_request(); }
    void suspend_persistence() { if (runtime_) runtime_->suspend_persistence(); }
    void inherit_persistent(const CharacterProgram& previous) { runtime_->inherit_persistent(*previous.runtime_); }
    efmi::frontend::RuntimeStatus status() const { return runtime_ ? runtime_->status() : efmi::frontend::RuntimeStatus{}; }
private:
    bool load(const std::filesystem::path& ini, const std::filesystem::path& core, const std::filesystem::path& settings,
              const std::string& settings_name, std::shared_ptr<efmi::frontend::RuntimeExtension>, const GpuFactory&, std::string& reason,
              const std::filesystem::path& mods = {}, bool declarations_only = false);
    void catalogue(CharacterPackage&, const std::map<std::wstring, uint32_t>& remaps);
    std::shared_ptr<efmi::frontend::OwnedRuntime> gpu_;
    std::string gpu_reason_;
    std::vector<std::string> library_notes_;
    std::map<std::string, std::filesystem::path> libraries_; // loaded libraries by namespace
    bool efmittl_ = false;
    std::shared_ptr<efmi::frontend::IniRuntime> runtime_;
    std::map<std::wstring, uint32_t> streams_, images_;
    // The stream or image a plan item's resource names, by the resource object the compiler hands
    // out for a declaration (stable for the generation): the path key is made once per resource,
    // not per draw (it cost 10 us per matched draw).
    std::unordered_map<const void*, uint32_t> stream_cache_, image_cache_;
    efmi::assets::FilePlan file_plan_; // the last draw's, kept for its capacity (D-108)
    // A pixel slot's image (or kCharacterNone) while a plan is read: a few entries, kept sorted by
    // slot, no node per entry as a std::map has.
    struct PixelSlots {
        std::vector<std::pair<uint32_t, uint32_t>> values;
        uint32_t& operator[](uint32_t slot)
        {
            auto it = std::lower_bound(values.begin(), values.end(), slot, [](const auto& v, uint32_t s) { return v.first < s; });
            if (it == values.end() || it->first != slot) it = values.insert(it, {slot, 0});
            return it->second;
        }
        void erase(uint32_t slot)
        {
            auto it = std::lower_bound(values.begin(), values.end(), slot, [](const auto& v, uint32_t s) { return v.first < s; });
            if (it != values.end() && it->first == slot) values.erase(it);
        }
        bool empty() const { return values.empty(); }
        size_t size() const { return values.size(); }
        void clear() { values.clear(); }
        auto begin() const { return values.begin(); }
        auto end() const { return values.end(); }
    };
    // The containers a plan is read into, kept for their capacity (D-108).
    PixelSlots scratch_pixels_;
    std::vector<CharacterDraw> scratch_draws_;
    std::vector<std::vector<CharacterPixel>> scratch_material_;
    std::vector<uint8_t> scratch_signature_;
    std::string scratch_id_;
    uint32_t stream_index(const std::shared_ptr<const efmi::assets::FileResource>& resource);
    uint32_t image_index(const std::shared_ptr<const efmi::assets::FileResource>& resource);
    std::unordered_map<std::string, CharacterList> lists_;
    std::vector<std::pair<uint32_t, uint32_t>> index_bounds_;
    size_t outside_meshes_ = 0, left_to_game_ = 0;
    uint32_t identify_components_ = 0;
    std::map<std::tuple<uint32_t, std::string, uint32_t>, std::set<uint32_t>> element_offsets_;
};
}
