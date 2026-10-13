#include "character_core.h"
#include "runtime_host.h"
#include "skeleton_plan.h"
#include "palette_files.h"
#include "globals.h"
#include <cmath>

void HostCompileGraph(const std::wstring&, const std::vector<std::wstring>&);

namespace efmi::frontend {
namespace {
const std::wstring core_namespace = L"\\efmiv1\\";
std::wstring command(const wchar_t* name) { return L"commandlist" + core_namespace + name; }
CommandListVariable& var(const wchar_t* name) { return command_list_globals.at(L"$" + core_namespace + name); }
ExplicitCommandListSection& list(const wchar_t* name) { return explicitCommandListSections.at(command(name)); }
uint32_t integer(float value)
{
    if (!std::isfinite(value) || value < 0 || value > 65536 || std::floor(value) != value) throw std::runtime_error("Invalid native Core integer");
    return uint32_t(value);
}
class CharacterCore : public RuntimeExtension {
    std::wstring mod;
    efvk::CharacterPackage expected;
    DrawPlanAdapter* adapter = nullptr;
    RuntimeDraw current;
    bool prepared = false, attached = false;
    CustomResource* skeleton = nullptr;
    std::vector<std::wstring> boundaries;
    bool fail(std::wstring& reason, const wchar_t* message) { reason = message; return false; }
    bool contract(std::wstring& reason)
    {
        return (current.identity && current.instances == 1 && !current.first_instance && current.component < expected.components.size() && current.lod < 2 &&
            integer(var(L"component_id").fval) == current.component && integer(var(L"lod_level").fval) == current.lod &&
            integer(var(L"bones_count").fval) == expected.bones && var(L"gpu_posed").fval == 1 && var(L"custom_mesh_scale").fval == 1) ||
            fail(reason, L"Original Core request differs from the resolved native identity, bone or LOD contract");
    }
public:
    CharacterCore(const std::string& name, const efvk::CharacterPackage& package) : expected(package)
    {
        if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
            throw std::runtime_error("Mod namespace must be a normalized ASCII identifier");
        mod = L"\\" + std::wstring(name.begin(), name.end()) + L"\\";
        // Only contracts are needed here; do not duplicate the mesh payload.
        expected.payload.clear(); expected.payload.shrink_to_fit();
    }
    std::wstring declarations() override
    {
        return LR"ini(namespace = EFMIv1
[Constants]
global $required_version = 0
global $object_guid = 0
global $mod_id = -1
[ResourceModName]
[ResourceModAuthor]
[ResourceModDesc]
[ResourceModLink]
[ResourceModLogo]
[CommandListRegisterMod]
)ini";
    }
    std::vector<std::wstring> native_shaders() override
    {
        std::vector<std::wstring> names;
        for (const auto* name : {L"mergedskeletoninitializer", L"_mergedskeleton_bonesimporter_rollingbuffer", L"_mergedskeleton_bonesimporter_constantbuffer",
                                L"_mergedskeleton_instanceconfigoverrider_rollingbuffer", L"_mergedskeleton_instanceconfigoverrider_constantbuffer"})
            names.push_back(L"customshader" + core_namespace + name);
        return names;
    }
    std::vector<std::wstring> install(DrawPlanAdapter& a) override
    {
        adapter = &a;
        auto native = [&](const wchar_t* name, DrawPlanAdapter::NativeCommand callback) {
            boundaries.push_back(command(name)); a.native(list(name), command(name), std::move(callback));
        };
        native(L"registermod", [this](CommandListState&, unsigned phases, std::wstring& why) {
            if (!(phases & 1)) return true;
            if (!std::isfinite(var(L"required_version").fval) || var(L"required_version").fval > 1.48f)
                return fail(why, L"Mod requires a newer EFMIv1 contract");
            var(L"mod_id").fval = 0; return true;
        });
        native(L"initializemergedskeleton", [this](CommandListState&, unsigned phases, std::wstring& why) {
            if (!(phases & 1)) return true;
            auto* target = adapter->referenced_resource(&customResources.at(L"resource" + core_namespace + L"outputmergedskeleton_template"));
            if (!target || (prepared && target != skeleton)) return fail(why, L"Unprepared native skeleton output");
            skeleton = target; return true;
        });
        native(L"component_readconfig", [this](CommandListState&, unsigned phases, std::wstring& why) { return !(phases & 1) || contract(why); });
        native(L"spatialidentity_identifycomponentinstances", [this](CommandListState&, unsigned phases, std::wstring& why) {
            // Identity resolution is the renderer's existing recording-clock
            // implementation, already supplied before this original callback.
            return !(phases & 1) || contract(why);
        });
        native(L"mergedskeleton_attachcomponent", [this](CommandListState&, unsigned phases, std::wstring& why) {
            if (!(phases & 1)) return true;
            if (!prepared || !contract(why)) return false;
            const auto* output = adapter->referenced_resource(&customResources.at(L"resource" + core_namespace + L"output_mergedskeleton"));
            if (output != skeleton) return fail(why, L"Native bone attachment references another skeleton");
            attached = true; return true;
        });
        native(L"component_drawinstances", [this](CommandListState& state, unsigned phases, std::wstring& why) {
            if (!(phases & 1)) return true;
            if (!prepared || !contract(why)) return false;
            attached = false;
            if (!adapter->invoke(list(L"callback_mergedskeleton_connectcomponent"), state, 1, why) || !attached)
                return fail(why, L"Mod did not attach its prepared merged skeleton");
            return adapter->invoke(list(L"callback_component_drawcustom"), state, 1, why);
        });
        native(L"overridetextures", [this](CommandListState&, unsigned phases, std::wstring& why) {
            // This generation registers mesh overrides only. Image-hash sections
            // are rejected during preparation, never assigned invented hashes.
            return !(phases & 1) || prepared || fail(why, L"Texture registry is not prepared");
        });
        // Preparation executes the original mod's bone declarations, including
        // its exact LOD1 file references. Runtime connection executes them too.
        HostCompileGraph(L"commandlist" + mod + L"initializemergedskeleton", boundaries);
        return boundaries;
    }
    bool validate_overrides(std::wstring& why) override
    {
        if (!G->mFuzzyTextureOverrides.empty()) return fail(why, L"Fuzzy/image overrides require a source-image identity provider");
        size_t count = 0;
        for (const auto& hashed : G->mTextureOverrideMap) {
            for (const auto& entry : hashed.second) {
                const std::wstring prefix = L"textureoverride" + mod + L"_entrypoint_";
                if (_wcsnicmp(entry.ini_section.c_str(), prefix.c_str(), prefix.size()) != 0 ||
                    std::none_of(expected.sections.begin(), expected.sections.end(), [&](const auto& s) { return s.hash == hashed.first; }))
                    return fail(why, L"Generation contains an unprofiled override; source-image identity is unavailable");
                ++count;
            }
        }
        return count == expected.sections.size() || fail(why, L"Original mesh override registry differs from its profile");
    }
    bool prepare(DrawPlanAdapter& a, CommandListState& state, std::wstring& why) override
    {
        auto& init = explicitCommandListSections.at(L"commandlist" + mod + L"initializemergedskeleton");
        a.adapt(init.command_list); a.adapt(init.post_command_list);
        DrawPlan plan;
        if (!a.execute_pair(init.command_list, init.post_command_list, state, plan, why) || !skeleton || !plan.items.empty()) return false;
        const auto pools = L"pool" + mod + L"_mergedskeleton_component_";
        SkeletonFilePlan metadata; FilePlanCompiler files(customResources);
        if (!read_skeleton_plan(integer(command_list_globals.at(L"$" + mod + L"bones_count").fval),
            integer(command_list_globals.at(L"$" + mod + L"max_instance_count").fval), integer(var(L"cfg_ms_max_lod_level_count").fval),
            customResourcePools.at(pools + L"vertexgroupoffsets"), customResourcePools.at(pools + L"vertexgroupcounts"),
            customResourcePools.at(pools + L"lodremaps"), a, files, metadata, why)) return false;
        core::PaletteDefinition palette; std::string error;
        if (!load_palette_files(metadata, palette, error)) { why.assign(error.begin(), error.end()); return false; }
        if (metadata.bones != expected.bones || metadata.components.size() < expected.components.size())
            return fail(why, L"Original skeleton dimensions changed");
        for (size_t i = 0; i < metadata.components.size(); ++i) {
            const auto& got = metadata.components[i];
            if (i >= expected.components.size()) { if (got.count) return fail(why, L"Unprofiled active component"); continue; }
            const auto& want = expected.components[i];
            const auto& maps = palette.components[i].lods;
            if (got.first != want.first || got.count != want.count ||
                !maps[0].available || !maps[0].indices.empty() || !maps[1].available ||
                maps[1].indices != want.remap_lod1)
                return fail(why, L"Original bone ranges or LOD1 remaps differ from the renderer profile");
        }
        prepared = true; return true;
    }
    bool before_draw(const RuntimeDraw& draw, CommandListState&, std::wstring&) override { current = draw; return true; }
};
}
std::shared_ptr<RuntimeExtension> character_core(const std::string& mod_namespace, const efvk::CharacterPackage& package)
{
    return std::make_shared<CharacterCore>(mod_namespace, package);
}
}
