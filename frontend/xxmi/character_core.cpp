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
    std::shared_ptr<CharacterDiscovery> discovery, facts;
    struct FoundSection { efvk::CharacterSection section; uint32_t original = efvk::kCharacterNone; };
    std::vector<FoundSection> found;
    // The constant a section's command list assigns to `name`, at its top level or inside its `if`
    // branches (EFMI exports guard the assignments with `if $mod_enabled && DRAW_TYPE == 4`): one
    // value when every assignment found gives the same constant, none when there is none, one that is
    // not constant, or two that differ. The live plan still runs the branches as the original does.
    static void assignments(const CommandList* list, const std::wstring& name, std::set<uint32_t>& values, bool& unknown, unsigned depth = 0)
    {
        if (!list || depth > 32) { unknown |= depth > 32; return; }
        for (const auto& held : list->commands) {
            // Adapted lists hold the original command inside the planner's bridge.
            const CommandListCommand* command = DrawPlanAdapter::original(*held);
            if (const auto* branch = dynamic_cast<const IfCommand*>(command)) {
                assignments(branch->true_commands_pre.get(), name, values, unknown, depth + 1);
                assignments(branch->false_commands_pre.get(), name, values, unknown, depth + 1);
                continue;
            }
            const auto* assignment = dynamic_cast<const VariableAssignment*>(command);
            if (!assignment || !assignment->var || _wcsicmp(assignment->var->name.c_str(), name.c_str())) continue;
            float value = 0;
            if (const_cast<CommandListExpression&>(assignment->expression).static_evaluate(&value) &&
                std::isfinite(value) && value >= 0 && value < 65536 && std::floor(value) == value)
                values.insert(uint32_t(value));
            else unknown = true;
        }
    }
    // Constant values assigned to `name` anywhere in the mod's command lists (a restore from a saved
    // variable is not a value of its own).
    uint32_t identification_threshold() const
    {
        const std::wstring name = L"$" + core_namespace + L"identification_min_components";
        const std::wstring prefix = L"commandlist" + mod;
        std::set<uint32_t> values;
        for (const auto& section : explicitCommandListSections) {
            if (_wcsnicmp(section.first.c_str(), prefix.c_str(), prefix.size())) continue;
            bool unknown = false;
            assignments(&section.second.command_list, name, values, unknown);
        }
        if (values.size() == 1 && *values.begin() > 0 && *values.begin() <= 16) return *values.begin();
        const auto core = command_list_globals.find(name);
        if (core != command_list_globals.end() && std::isfinite(core->second.fval) && core->second.fval >= 1 && core->second.fval <= 16)
            return uint32_t(core->second.fval);
        return 0;
    }
    // A `handling = skip` anywhere in the list (inside its branches too): the section hides a draw.
    static bool skips(const CommandList* list, unsigned depth = 0)
    {
        if (!list || depth > 32) return false;
        for (const auto& held : list->commands) {
            const CommandListCommand* command = DrawPlanAdapter::original(*held);
            if (dynamic_cast<const SkipCommand*>(command)) return true;
            if (const auto* branch = dynamic_cast<const IfCommand*>(command))
                if (skips(branch->true_commands_pre.get(), depth + 1) || skips(branch->false_commands_pre.get(), depth + 1)) return true;
        }
        return false;
    }
    // Every constant element-offset edit in a list (and its branches) into `out`.
    static void element_edits(const CommandList* list, std::map<std::tuple<uint32_t, std::string, uint32_t>, std::set<uint32_t>>& out,
                              unsigned depth = 0)
    {
        if (!list || depth > 32) return;
        for (const auto& held : list->commands) {
            const CommandListCommand* command = DrawPlanAdapter::original(*held);
            if (const auto* branch = dynamic_cast<const IfCommand*>(command)) {
                element_edits(branch->true_commands_pre.get(), out, depth + 1);
                element_edits(branch->false_commands_pre.get(), out, depth + 1);
                continue;
            }
            const auto* edit = dynamic_cast<const LayoutElementOperation*>(command);
            if (!edit || edit->override.replace.aligned_byte_offset == UINT32_MAX || !edit->dst.member_args[1].expression) continue;
            float index = 0;
            if (!edit->dst.member_args[1].expression->static_evaluate(&index) || !std::isfinite(index) || index < 0 || index > 255) continue;
            out[{edit->override.match.input_slot, edit->override.match.semantic_name, uint32_t(index)}].insert(edit->override.replace.aligned_byte_offset);
        }
    }
    std::map<std::tuple<uint32_t, std::string, uint32_t>, std::set<uint32_t>> element_offsets() const
    {
        std::map<std::tuple<uint32_t, std::string, uint32_t>, std::set<uint32_t>> out;
        const std::wstring lists = L"commandlist" + mod, overrides = L"textureoverride" + mod;
        for (const auto& section : explicitCommandListSections)
            if (!_wcsnicmp(section.first.c_str(), lists.c_str(), lists.size())) {
                element_edits(&section.second.command_list, out);
                element_edits(&section.second.post_command_list, out);
            }
        for (const auto& hashed : G->mTextureOverrideMap)
            for (const auto& entry : hashed.second)
                if (!_wcsnicmp(entry.ini_section.c_str(), overrides.c_str(), overrides.size())) {
                    element_edits(&entry.command_list, out);
                    element_edits(&entry.post_command_list, out);
                }
        return out;
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
    void set_facts(std::shared_ptr<CharacterDiscovery> out) { facts = std::move(out); }
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
        return {rabbit_textures ? rabbit_texture_declarations() : rabbitfx_library_declarations()};
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
        if (!rabbit_textures) {
            // RabbitFX (D-082): SetTextures binds the Mod's maps where this draw's fragment shader samples
            // the diffuse, lightmap and normal map; a role the draw has not, an unknown answer or a map
            // the Mod did not set binds nothing, and the game's texture stays.
            const std::wstring resolve = L"commandlist\\rabbitfx\\resolveroles";
            boundaries.push_back(resolve);
            a.native(explicitCommandListSections.at(resolve), resolve, [this](CommandListState&, unsigned phases, std::wstring&) {
                if (!(phases & 1)) return true;
                int32_t roles[3] = {-1, -1, -1};
                std::string reason;
                if (!current.texture_roles || !current.texture_roles(roles, reason)) roles[0] = roles[1] = roles[2] = -1;
                static const wchar_t* const maps[3] = {L"diffuse", L"lightmap", L"normalmap"};
                static const wchar_t* const slots[3] = {L"diffuse_slot", L"lightmap_slot", L"normal_slot"};
                for (int i = 0; i < 3; ++i) {
                    const auto declared = customResources.find(std::wstring(L"resource\\rabbitfx\\") + maps[i]);
                    const bool given = declared != customResources.end() && adapter->referenced_resource(&declared->second);
                    command_list_globals.at(std::wstring(L"$\\rabbitfx\\") + slots[i]).fval =
                        given && roles[i] >= 0 && roles[i] < int32_t(kRabbitRoleSlots) ? float(roles[i]) : -1.0f;
                }
                return true;
            });
        }
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
        auto matches = [](const FuzzyMatch& f, uint32_t value, bool) {
            // None: any value (an optional first index, a hash-only section's index count).
            if (value == efvk::kCharacterNone) return f.op == FuzzyMatchOp::ALWAYS;
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
                // A hash alone that skips the draw hides a game mesh by its index region's hash,
                // whatever its index count (as the original matches it); it is never a component.
                if (own && !mesh && entry.match_first_index.op == FuzzyMatchOp::ALWAYS && skips(&entry.command_list)) {
                    s.section.match_count = efvk::kCharacterNone; s.section.match_first = efvk::kCharacterNone;
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
        prepared = true;
        if (const auto out = discovery ? discovery : facts) {
            out->identify_components = identification_threshold();
            out->element_offsets = element_offsets();
        }
        return true;
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
std::shared_ptr<RuntimeExtension> character_core(const std::string& mod_namespace, const efvk::CharacterPackage& package,
                                               std::shared_ptr<CharacterDiscovery> facts)
{
    auto core = std::make_shared<CharacterCore>(mod_namespace, package, false);
    core->set_facts(std::move(facts));
    return core;
}
std::shared_ptr<RuntimeExtension> character_discovery(const std::string& mod_namespace, std::shared_ptr<CharacterDiscovery> output)
{
    if (!output) throw std::runtime_error("Character discovery needs its output");
    return std::make_shared<CharacterCore>(mod_namespace, std::move(output));
}
}
