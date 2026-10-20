#include "character_core.h"
#include "runtime_host.h"
#include "skeleton_plan.h"
#include "palette_files.h"
#include "rabbit_textures.h"
#include "globals.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

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
    bool rabbit_textures = false;
    bool textures = false;                 // the host answers texture queries (D-077)
    size_t outside_package = 0, texture_sections = 0;
    // Discovery (D-078): the mod's mesh sections as the registry holds them, with the original
    // component id and LOD they assign; the contract is completed once the skeleton is known.
    std::shared_ptr<CharacterDiscovery> discovery;
    struct FoundSection { efvk::CharacterSection section; uint32_t original = efvk::kCharacterNone; };
    std::vector<FoundSection> found;
    // The constant a section's command list assigns to `name`, at its top level or inside its `if`
    // branches (EFMI exports guard the assignments with `if $mod_enabled && DRAW_TYPE == 4`): one
    // value when every assignment found gives the same constant, none when there is none, one that is
    // not constant, or two that differ. The live plan still runs the branches as the original does.
    static void assignments(const CommandList* list, const std::wstring& name, std::set<uint32_t>& values, bool& unknown, unsigned depth = 0)
    {
        if (!list || depth > 32) { unknown |= depth > 32; return; }
        for (const auto& command : list->commands) {
            if (const auto* branch = dynamic_cast<const IfCommand*>(command.get())) {
                assignments(branch->true_commands_pre.get(), name, values, unknown, depth + 1);
                assignments(branch->false_commands_pre.get(), name, values, unknown, depth + 1);
                continue;
            }
            const auto* assignment = dynamic_cast<const VariableAssignment*>(command.get());
            if (!assignment || !assignment->var || _wcsicmp(assignment->var->name.c_str(), name.c_str())) continue;
            float value = 0;
            if (const_cast<CommandListExpression&>(assignment->expression).static_evaluate(&value) &&
                std::isfinite(value) && value >= 0 && value < 65536 && std::floor(value) == value)
                values.insert(uint32_t(value));
            else unknown = true;
        }
    }
    static uint32_t assigned(const CommandList& list, const std::wstring& name)
    {
        std::set<uint32_t> values;
        bool unknown = false;
        assignments(&list, name, values, unknown);
        return !unknown && values.size() == 1 ? *values.begin() : efvk::kCharacterNone;
    }
    static bool constant(const FuzzyMatch& f, uint32_t& value)
    {
        if (f.op != FuzzyMatchOp::EQUAL || f.rhs_type1 != FuzzyMatchOperandType::VALUE || f.rhs_type2 != FuzzyMatchOperandType::VALUE ||
            f.mask != UINT32_MAX || f.numerator != 1 || f.denominator != 1) return false;
        value = f.val; return true;
    }
    std::vector<uint32_t> original_components;
    CustomResource* skeleton = nullptr;
    std::vector<std::wstring> boundaries;
    bool fail(std::wstring& reason, const wchar_t* message) { reason = message; return false; }
    bool contract(std::wstring& reason)
    {
        return (current.identity && current.instances == 1 && !current.first_instance && current.component < expected.components.size() && current.lod < 2 &&
            original_components.size() == expected.components.size() &&
            integer(var(L"component_id").fval) == original_components[current.component] && integer(var(L"lod_level").fval) == current.lod &&
            integer(var(L"bones_count").fval) == expected.bones && var(L"gpu_posed").fval == 1 && var(L"custom_mesh_scale").fval == 1) ||
            fail(reason, L"Original Core request differs from the resolved native identity, bone or LOD contract");
    }
public:
    CharacterCore(const std::string& name, std::shared_ptr<CharacterDiscovery> output)
        : CharacterCore(name, efvk::CharacterPackage{}, false) { discovery = std::move(output); }
    CharacterCore(const std::string& name, const efvk::CharacterPackage& package, bool rabbit) : expected(package), rabbit_textures(rabbit)
    {
        // A declared namespace, or the name the runtime parses an INI without one under (its path
        // relative to the Mod folder, as the original names such a file by its path): printable
        // ASCII in lower case, without the separators of section names.
        if (name.empty() || name.size() > 260 || std::any_of(name.begin(), name.end(), [](char c) {
                return c < 0x20 || c > 0x7e || c == '\\' || c == '[' || c == ']' || (c >= 'A' && c <= 'Z'); }))
            throw std::runtime_error("Mod namespace must be lower-case printable ASCII without separators");
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
    std::vector<std::wstring> declaration_units() override
    {
        return rabbit_textures ? std::vector<std::wstring>{rabbit_texture_declarations()} : std::vector<std::wstring>{};
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
        native(L"component_readconfig", [this](CommandListState&, unsigned phases, std::wstring& why) {
            if (!(phases & 1)) return true;
            if (!contract(why)) return false;
            var(L"instance_config_vs_cb_slot").fval = float(current.source_vs_config_slot);
            return true;
        });
        native(L"mergedskeleton_detectbonedatasource", [this](CommandListState&, unsigned phases, std::wstring& why) {
            if (!(phases & 1)) return true;
            if (!prepared || !contract(why)) return false;
            // Never invent a source slot from Vulkan descriptor coordinates.
            // Unknown input preserves the original Core's no-replacement path.
            var(L"bones_data_source").fval = current.rolling_bone_source && current.source_vs_config_slot != -1
                ? var(L"enum_bones_source_vs_t0").fval : 0;
            return true;
        });
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
        // With the host's texture identities (D-077) the original OverrideTextures runs: its
        // CheckTextureOverride on ps-t0 to ps-t22 finds texture-hash sections by the hash of what is
        // bound. Without them the generation registers mesh overrides only and refuses image-hash
        // sections during preparation, never assigning invented hashes.
        textures = a.has_texture_queries();
        if (!textures)
            native(L"overridetextures", [this](CommandListState&, unsigned phases, std::wstring& why) {
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
        if (discovery) return discover_sections(why);
        std::vector<bool> seen(expected.sections.size(), false);
        auto matches = [](const FuzzyMatch& f, uint32_t value, bool optional) {
            if (optional && value == efvk::kCharacterNone) return f.op == FuzzyMatchOp::ALWAYS;
            return f.op == FuzzyMatchOp::EQUAL && f.rhs_type1 == FuzzyMatchOperandType::VALUE &&
                f.rhs_type2 == FuzzyMatchOperandType::VALUE && f.val == value && f.mask == UINT32_MAX &&
                f.numerator == 1 && f.denominator == 1;
        };
        for (const auto& hashed : G->mTextureOverrideMap) {
            for (const auto& entry : hashed.second) {
                const std::wstring prefix = L"textureoverride" + mod;
                size_t index = 0;
                for (; index < expected.sections.size(); ++index) {
                    const auto& s = expected.sections[index];
                    if (s.hash == hashed.first && matches(entry.match_index_count, s.match_count, false) &&
                        matches(entry.match_first_index, s.match_first, true)) break;
                }
                const bool mesh = entry.match_index_count.op != FuzzyMatchOp::ALWAYS;
                if (index < expected.sections.size() && !seen[index] && !_wcsnicmp(entry.ini_section.c_str(), prefix.c_str(), prefix.size())) {
                    seen[index] = true; continue;
                }
                // A mesh section the renderer has no part for (a LOD beyond its scope) stays the
                // game's; an image-hash section runs against the host's texture identities.
                if (mesh) { ++outside_package; continue; }
                if (!textures) return fail(why, L"Generation contains an unprofiled override; source-image identity is unavailable");
                ++texture_sections;
            }
        }
        return std::all_of(seen.begin(), seen.end(), [](bool v) { return v; }) || fail(why, L"Original mesh override registry differs from its profile");
    }
    // Discovery: a mesh section of this mod (a constant index count, an optional constant first
    // index) is part of the contract; the component it assigns decides what it draws, none makes it
    // a candidate to hide the game's mesh, which the live plan decides. Everything else stays the
    // game's or, with texture identities, runs against them, as for a package.
    bool discover_sections(std::wstring& why)
    {
        const std::wstring prefix = L"textureoverride" + mod;
        const std::wstring component_id = L"$" + core_namespace + L"component_id", lod_level = L"$" + mod + L"lod_level";
        for (const auto& hashed : G->mTextureOverrideMap) {
            for (const auto& entry : hashed.second) {
                const bool mesh = entry.match_index_count.op != FuzzyMatchOp::ALWAYS;
                FoundSection s;
                s.section.hash = hashed.first;
                const bool own = !_wcsnicmp(entry.ini_section.c_str(), prefix.c_str(), prefix.size());
                const bool first_ok = entry.match_first_index.op == FuzzyMatchOp::ALWAYS || constant(entry.match_first_index, s.section.match_first);
                if (own && mesh && constant(entry.match_index_count, s.section.match_count) && s.section.match_count && first_ok) {
                    s.original = assigned(entry.command_list, component_id);
                    s.section.lod = assigned(entry.command_list, lod_level);
                    found.push_back(s);
                    continue;
                }
                if (mesh) { ++outside_package; continue; }
                if (!textures) return fail(why, L"Generation contains an unprofiled override; source-image identity is unavailable");
                ++texture_sections;
            }
        }
        if (found.size() > 256) return fail(why, L"More mesh sections than one character contract holds");
        return true;
    }
    // Discovery: the active components in bone order, their LOD remaps, and the sections mapped to
    // them. A section on an inactive component or a LOD beyond 1 stays the game's (the renderer's
    // scope is LOD0 and LOD1, as for a package).
    bool discover_contract(const SkeletonFilePlan& metadata, const core::PaletteDefinition& palette, std::wstring& why)
    {
        std::vector<uint32_t> order;
        for (uint32_t i = 0; i < metadata.components.size(); ++i) if (metadata.components[i].count) order.push_back(i);
        std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return metadata.components[a].first < metadata.components[b].first; });
        if (order.empty() || order.size() > 32) return fail(why, L"Original skeleton has no or too many active components");
        efvk::CharacterPackage contract;
        contract.bones = metadata.bones;
        std::vector<std::filesystem::path> remaps;
        std::map<uint32_t, uint32_t> dense;
        uint32_t cursor = 0;
        for (uint32_t original : order) {
            const auto& got = metadata.components[original];
            const auto& maps = palette.components[original].lods;
            if (got.first != cursor) return fail(why, L"Original components do not partition the skeleton");
            cursor += got.count;
            if (!maps[0].available || !maps[0].indices.empty() || !maps[1].available)
                return fail(why, L"Original component has a LOD0 remap or no LOD1 assignment");
            efvk::CharacterComponent c;
            c.first = got.first; c.count = got.count;
            c.has_remap = !maps[1].indices.empty(); c.remap_lod1 = maps[1].indices;
            const auto& lod1 = got.lods[1];
            if (c.has_remap && (lod1.source != RemapSource::File || lod1.path.empty())) return fail(why, L"Original LOD1 remap without its file");
            remaps.push_back(c.has_remap ? lod1.path : std::filesystem::path{});
            dense.emplace(original, uint32_t(contract.components.size()));
            contract.components.push_back(std::move(c));
        }
        if (cursor != metadata.bones) return fail(why, L"Original components do not cover the skeleton");
        size_t left = outside_package;
        for (const auto& s : found) {
            auto section = s.section;
            if (section.lod != efvk::kCharacterNone && section.lod > 1) { ++left; continue; }
            if (s.original == efvk::kCharacterNone) {
                section.component = efvk::kCharacterNone; section.lod = efvk::kCharacterNone;
            } else {
                const auto it = dense.find(s.original);
                if (it == dense.end()) { ++left; continue; }
                section.component = it->second;
            }
            contract.sections.push_back(section);
        }
        if (contract.sections.empty()) return fail(why, L"The mod declares no mesh section the renderer can take");
        expected = contract;
        discovery->contract = std::move(contract);
        discovery->remap_files = std::move(remaps);
        discovery->left_to_game = left;
        return true;
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
        if (discovery && !discover_contract(metadata, palette, why)) return false;
        if (metadata.bones != expected.bones || metadata.components.size() < expected.components.size())
            return fail(why, L"Original skeleton dimensions changed");
        original_components.assign(expected.components.size(), UINT32_MAX);
        for (size_t i = 0; i < metadata.components.size(); ++i) {
            const auto& got = metadata.components[i];
            if (!got.count) continue;
            size_t dense = 0;
            while (dense < expected.components.size() && expected.components[dense].first != got.first) ++dense;
            if (dense == expected.components.size() || original_components[dense] != UINT32_MAX)
                return fail(why, L"Unprofiled or overlapping active component");
            const auto& want = expected.components[dense];
            const auto& maps = palette.components[i].lods;
            if (got.first != want.first || got.count != want.count ||
                !maps[0].available || !maps[0].indices.empty() || !maps[1].available ||
                maps[1].indices != want.remap_lod1)
                return fail(why, L"Original bone ranges or LOD1 remaps differ from the renderer profile");
            original_components[dense] = uint32_t(i);
        }
        if (std::find(original_components.begin(), original_components.end(), UINT32_MAX) != original_components.end())
            return fail(why, L"Profiled component is absent from original skeleton");
        prepared = true; return true;
    }
    bool before_draw(const RuntimeDraw& draw, CommandListState&, std::wstring& why) override
    {
        if (draw.source_vs_config_slot != -1 && (draw.source_vs_config_slot < 1 || draw.source_vs_config_slot > 3))
            return fail(why, L"Unsupported original Core instance-config constant slot");
        current = draw; return true;
    }
};
}
std::shared_ptr<RuntimeExtension> character_core(const std::string& mod_namespace, const efvk::CharacterPackage& package, bool rabbit)
{
    return std::make_shared<CharacterCore>(mod_namespace, package, rabbit);
}
std::shared_ptr<RuntimeExtension> character_discovery(const std::string& mod_namespace, std::shared_ptr<CharacterDiscovery> output)
{
    if (!output) throw std::runtime_error("Character discovery needs its output");
    return std::make_shared<CharacterCore>(mod_namespace, std::move(output));
}
}
