#include "../frontend/xxmi/runtime.h"
#include "../frontend/xxmi/character_core.h"
#include "../layer/src/character_program.h"
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <thread>
using namespace efmi::frontend;
// Independent slow CRC for fixture files, not the production hot path.
namespace efvk { uint32_t crc32c(uint32_t seed, const void* data, size_t size) {
    uint32_t hash = ~seed; const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) { hash ^= p[i]; for (int b = 0; b < 8; ++b) hash = (hash >> 1) ^ (0x82f63b78u & (0u - (hash & 1))); }
    return ~hash;
} }
static int checks = 0;
static void check(bool result, const char* text)
{
    if (!result) throw std::runtime_error(text);
    ++checks;
}
static void write(const std::filesystem::path& path, const char* text)
{
    std::ofstream out(path, std::ios::binary); out << text;
    if (!out) throw std::runtime_error("Cannot write fixture");
}
static const char* fixture = R"ini(namespace = runtime_test
[Constants]
global persist $dress = 0
global persist $other = 0
global $active = 0
global $holding = 0
[KeyDress]
condition = $active == 1
key = ctrl 5
type = cycle
$dress = 0,1,2,
[KeyOther]
condition = $active == 1
key = ctrl 5
type = cycle
$other = 0,1,
[KeyHold]
key = alt 6
type = hold
$holding = 1
[Present]
post $active = 0
[TextureOverrideObject]
hash = 12345678
match_index_count = 30
$active = 1
handling = skip
if $dress == 0
 drawindexed = 3,0,0
elif $dress == 1
 drawindexed = 6,3,0
else
 drawindexed = 9,9,0
endif
)ini";
static int inspect_character(int argc, wchar_t** argv)
{
    check(argc == 7, "Character inspection expects package, INI, Core, namespace and library mode");
    const auto profile = std::filesystem::absolute(argv[2]);
    const auto ini = std::filesystem::absolute(argv[3]);
    const auto core = std::filesystem::absolute(argv[4]);
    const std::wstring wide_name = argv[5];
    if (wide_name.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789_") != std::wstring::npos)
        throw std::runtime_error("Invalid fixture namespace");
    const std::string name(wide_name.begin(), wide_name.end());
    const bool rabbit = !wcscmp(argv[6], L"texture-roles");
    if (!rabbit && wcscmp(argv[6], L"none")) throw std::runtime_error("Unknown fixture library mode");
    std::string reason; efvk::CharacterPackage package;
    if (!package.load(profile.u8string(), reason)) throw std::runtime_error(reason);
    RuntimeConfig config{ini.parent_path(), {core / "API.ini", core / "MergedSkeleton.ini",
        core / "SpatialIdentification.ini", core / "Helpers.ini", ini},
        profile.parent_path() / "inspection-settings", "isolated"};
    auto runtime = IniRuntime::load(config, character_core(name, package, rabbit), reason);
    if (!runtime) throw std::runtime_error("Original character runtime: " + reason);
    RuntimeInput input; efmi::assets::FilePlan plan;
    uint32_t lod = 0; size_t draws = 0, hides = 0;
    for (unsigned round = 0; round < 2; ++round) {
        for (const auto& section : package.sections) {
            if (section.lod != efvk::kCharacterNone) lod = section.lod;
            RuntimeDraw request; request.hash = section.hash; request.count = section.match_count;
            request.first = section.match_first == efvk::kCharacterNone ? 0 : section.match_first;
            request.component = section.component; request.lod = lod; request.frame = round + 1;
            if (section.component != efvk::kCharacterNone) {
                request.identity = 1;
                // Synthetic source-API facts, not measurements of this character.
                request.rolling_bone_source = true; request.source_vs_config_slot = 2;
            }
            if (!runtime->draw(request, plan, reason)) throw std::runtime_error("Original character entry: " + reason);
            if (round) {
                size_t geometry = 0;
                for (const auto& item : plan.items)
                    geometry += item.operation == efmi::assets::FileOperation::DrawIndexed;
                draws += geometry; hides += plan.skip_original && !geometry;
                if (section.component == efvk::kCharacterNone)
                    check(plan.skip_original && !geometry, "Independent original hide is executed without an identity");
            }
        }
        if (!runtime->present(input, round + 1, float(round), reason)) throw std::runtime_error(reason);
    }
    const auto state = runtime->status();
    for (const auto& section : package.sections) if (section.component != efvk::kCharacterNone) {
        RuntimeDraw probe; probe.hash = section.hash; probe.count = section.match_count;
        probe.first = section.match_first == efvk::kCharacterNone ? 0 : section.match_first;
        probe.component = section.component; probe.lod = section.lod == efvk::kCharacterNone ? lod : section.lod;
        probe.identity = 1; probe.source_vs_config_slot = 7;
        check(!runtime->draw(probe, plan, reason), "Invalid source-API slot is refused before execution");
        probe.source_vs_config_slot = -1;
        check(runtime->draw(probe, plan, reason), "Unknown bone-source facts retain original fallback behavior");
        const auto unknown = runtime->status().variables;
        check(unknown.at("$\\efmiv1\\instance_config_vs_cb_slot") == -1 &&
              unknown.at("$\\efmiv1\\bones_data_source") == 0,
              "Absent source-API facts do not become an invented slot or bone source");
        probe.source_vs_config_slot = 2; probe.rolling_bone_source = true;
        check(runtime->draw(probe, plan, reason), "Resolved input recovers after unknown and invalid contracts");
        break;
    }
    printf("{\"scope\":\"Original main INI plans with synthetic native Core inputs; no GPU/shader-profile/game result\","
           "\"sections\":%zu,\"bones\":%u,\"components\":%zu,\"keys_with_f10\":%zu,\"draws\":%zu,\"hidden\":%zu,\"library_texture_roles\":%s}\n",
           package.sections.size(), package.bones, package.components.size(), state.bindings, draws, hides, rabbit ? "true" : "false");
    return 0;
}
int wmain(int argc, wchar_t** argv)
{
    try {
        if (argc > 1 && !wcscmp(argv[1], L"--inspect-character")) return inspect_character(argc, argv);
        check(argc == 2, "Fixture directory argument");
        const std::filesystem::path folder = std::filesystem::path(argv[1]) / "fixture";
        std::filesystem::create_directories(folder);
        const auto ini = folder / "mod.ini"; write(ini, fixture);
        const auto settings = folder / "settings";
        std::filesystem::remove(settings / "runtime.ini");
        RuntimeConfig config{folder, {ini}, settings, "runtime"}; std::string reason;
        auto first = IniRuntime::load(config, {}, reason);
        if (!first) throw std::runtime_error(reason);
        auto value = [&](const std::shared_ptr<IniRuntime>& r, const char* key) { return r->status().variables.at(std::string("$\\runtime_test\\") + key); };
        RuntimeInput input; input.foreground = true;
        auto present = [&](std::shared_ptr<IniRuntime> r) { if (!r->present(input, 10, 1.f, reason)) throw std::runtime_error(reason); };
        RuntimeDraw draw; draw.hash = 0x12345678; draw.count = 30; draw.frame = 1;
        efmi::assets::FilePlan plan;
        auto render = [&](std::shared_ptr<IniRuntime> r) { if (!r->draw(draw, plan, reason)) throw std::runtime_error(reason); };
        check(first->status().bindings == 4, "Three original keys plus F10");
        input.keys[VK_CONTROL] = true; input.keys['5'] = true; present(first);
        check(value(first, "dress") == 0 && value(first, "other") == 0, "Inactive condition blocks both listeners");
        input.keys.fill(false); present(first); render(first);
        check(plan.skip_original && plan.items.back().index_count == 3, "Original matching and default branch");
        input.keys[VK_CONTROL] = true; input.keys['5'] = true; present(first);
        check(value(first, "dress") == 1 && value(first, "other") == 1, "Both duplicate key listeners activate");
        present(first);
        check(value(first, "dress") == 1, "Held cycle key does not repeat");
        render(first); check(plan.items.back().index_count == 6 && plan.items.back().first_index == 3, "Original condition changes draws");
        check(first->status().dirty, "Original persistent assignment marks settings dirty");
        check(first->save(reason), "Save settings");
        check(!first->status().dirty && first->status().saves == 1, "Atomic settings published");
        auto second = IniRuntime::load(config, {}, reason);
        if (!second) throw std::runtime_error(reason);
        check(value(second, "dress") == 1 && value(second, "other") == 1, "Original constants reload saved values");
        input.keys.fill(false); present(first); render(first);
        input.keys[VK_CONTROL] = true; input.keys['5'] = true; present(first);
        check(value(first, "dress") == 2 && value(first, "other") == 0 && value(second, "dress") == 1, "Generations keep independent variables and key state");
        input.keys.fill(false); present(first);
        input.keys[VK_MENU] = true; input.keys['6'] = true; present(first);
        check(value(first, "holding") == 1, "Original hold key activates");
        input.foreground = false; present(first);
        check(value(first, "holding") == 0, "Focus loss releases original hold");
        input.foreground = true; input.keys.fill(false); input.keys[VK_F10] = true; present(first);
        check(first->status().reload_requested && !second->status().reload_requested, "F10 requests reload only for the active generation");
        check(first->take_reload_request() && !first->take_reload_request(), "F10 consumed once");
        second->inherit_persistent(*first);
        check(value(second, "dress") == 2 && value(second, "other") == 0 && second->status().dirty, "Reload carries latest persistent values, not stale parse snapshot");
        present(second);
        check(!second->take_reload_request(), "F10 held through generation exchange does not repeat");
        input.keys.fill(false); present(second); input.keys[VK_F10] = true; present(second);
        check(second->take_reload_request(), "Releasing and pressing F10 works in the new generation");
        first->suspend_persistence();
        check(first->save(reason) && first->status().dirty, "Retired writer cannot clear or overwrite pending values");
        write(ini, "namespace = runtime_test\n[Constants]\nglobal $x = 0\n[Present]\n$x = (\n");
        auto failed = IniRuntime::load(config, {}, reason);
        check(!failed, "Malformed reload candidate refused");
        render(first); check(plan.items.back().index_count == 9, "Failed parse leaves live generation usable");
        first.reset(); render(second);
        check(plan.items.back().index_count == 9, "Retiring one generation preserves another");
        write(ini, "namespace = hide_fixture\n[TextureOverrideHide]\nhash = 12345678\nmatch_index_count = 30\nhandling = skip\nthis = null\n");
        auto hide_config = config; hide_config.settings_name = "hide_fixture";
        auto hidden = IniRuntime::load(hide_config, {}, reason);
        if (!hidden) throw std::runtime_error("Independent IB suppression INI: " + reason);
        check(bool(hidden), "Independent IB suppression INI loads without a character skeleton");
        render(hidden);
        check(plan.skip_original && plan.items.size() == 1 && !plan.items[0].resource &&
              plan.items[0].operation == efmi::assets::FileOperation::BindIndex,
              "IB override this=null uses the actual triggering index binding");
        render(second); check(plan.items.back().index_count == 9, "Pure hide does not change another generation's draw");
        write(ini, fixture);
        // Read-only acceptance of the actual main INI. The panel is a separate
        // input with a CustomShader and remains explicitly outside this fixture.
        const auto project = folder.parent_path().parent_path().parent_path();
        const auto package_path = project / "build/character/arcane-materials.bin";
        if (std::filesystem::exists(package_path)) {
            efvk::CharacterPackage package;
            check(package.load(package_path.u8string(), reason), "Load actual material profile");
            const std::filesystem::path mod = L"D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Mods/arcane fes 1.2";
            const std::filesystem::path core = L"D:/XXMI-Launcher-Portable-v2.1.6/EFMI/Core/EFMI";
            RuntimeConfig actual{mod, {core / "API.ini", core / "MergedSkeleton.ini", core / "SpatialIdentification.ini", core / "Helpers.ini", mod / "arcane.ini"}, settings, "actual"};
            std::filesystem::remove(settings / "actual.ini");
            auto live = IniRuntime::load(actual, character_core("arcane_fes", package), reason);
            if (!live) throw std::runtime_error("Actual INI: " + reason);
            input = {}; input.foreground = true;
            auto render_section = [&](size_t i) {
                const auto& section = package.sections[i]; RuntimeDraw request;
                request.hash = section.hash; request.count = section.match_count;
                request.first = section.match_first == efvk::kCharacterNone ? 0 : section.match_first;
                request.component = section.component; request.lod = section.lod == efvk::kCharacterNone ? 0 : section.lod;
                request.identity = 1; request.frame = 20;
                if (!live->draw(request, plan, reason)) throw std::runtime_error("Actual original entry: " + reason);
            };
            render_section(0); present(live);
            check(live->status().variables.at("$\\arcane_fes\\mod_enabled") == 1, "Original Present registers this mod through Core-VK");
            for (size_t i = 0; i < package.sections.size(); ++i) render_section(i);
            check(live->status().draws == package.sections.size() + 1, "All actual LOD0/LOD1 original entry sections execute");
            render_section(0); input.keys[VK_CONTROL] = true; input.keys['5'] = true; present(live);
            check(live->status().variables.at("$\\arcane_fes\\swapkey0") == 1, "Actual original Ctrl+5 changes outfit variable");
            check(live->save(reason), "Actual original persistent settings save");
            live.reset();
            efvk::CharacterProgram program;
            std::filesystem::remove(settings / "arcane_fes.ini");
            if (!program.init((folder / "actual-live.bin").u8string(), package, reason))
                throw std::runtime_error("Actual renderer catalogue: " + reason);
            check(package.images.size() == 10, "Ten reachable DDS images including alternate; dormant missing declarations excluded");
            auto program_draw = [&](size_t section_index) {
                const auto& section = package.sections[section_index]; RuntimeDraw request;
                request.hash = section.hash; request.count = section.match_count;
                request.first = section.match_first == efvk::kCharacterNone ? 0 : section.match_first;
                request.component = section.component; request.lod = section.lod == efvk::kCharacterNone ? 0 : section.lod;
                request.identity = 1; request.frame = 100;
                efvk::CharacterList list; bool skip = false, added = false;
                if (!program.plan(request, package, list, skip, added, reason)) throw std::runtime_error("Actual renderer draw: " + reason);
                return list;
            };
            input = {}; input.foreground = true;
            auto program_present = [&] { if (!program.present(input, 100, 1.f, reason)) throw std::runtime_error(reason); };
            program_draw(0); program_present();
            for (size_t i = 0; i < package.sections.size(); ++i) program_draw(i);
            check(program.status().draws == package.sections.size() + 1, "Actual commands translate into validated renderer draws for every entry");
            const auto default_outfit = program_draw(1);
            check(default_outfit.count == 9, "Default outfit draws nine independent mesh ranges");
            struct Key { int modifier, code; const char* variable; };
            const Key keys[] = {{VK_CONTROL,'5',"swapkey0"},{VK_CONTROL,'6',"swapkey1"},{VK_CONTROL,'7',"swapkey2"},
                {VK_CONTROL,'8',"swapkey3"},{VK_MENU,'5',"swapkey4"},{VK_MENU,'6',"swapkey5"},{VK_MENU,'7',"swapkey6"},
                {VK_MENU,'8',"swapkey11"},{VK_MENU,'9',"swapkey9"},{VK_CONTROL,'L',"stocking"}};
            for (const auto& binding : keys) {
                input.keys.fill(false); program_present(); program_draw(0);
                input.keys[binding.modifier] = input.keys[binding.code] = true; program_present();
                check(program.status().variables.at(std::string("$\\arcane_fes\\") + binding.variable) == 1, "Actual original binding changes persistent variable");
                for (size_t i = 0; i < package.sections.size(); ++i) program_draw(i);
            }
            const auto state = program.status();
            check(state.bindings == 14 && state.variables.at("$\\arcane_fes\\swapkey7") == 1 &&
                  state.variables.at("$\\arcane_fes\\swapkey8") == 1 && state.variables.at("$\\arcane_fes\\swapkey12") == 1,
                  "All thirteen original listeners run, including three duplicate key pairs");
            check(program_draw(1).count == 2, "Combined outfit conditions leave two mesh ranges");
            const auto stocking = program_draw(2);
            bool alternate = false;
            for (const auto& pixel : package.pixels.at(stocking.first)) if (pixel.asset != efvk::kCharacterNone)
                alternate |= package.images.at(pixel.asset).find("DiffuseMap1.dds") != std::string::npos;
            check(alternate, "Original stocking branch resolves the alternate DDS into the actual draw");
            check(program.save(reason), "Renderer-owned runtime saves settings outside the original mod");
        }
        printf("Original INI runtime: %d checks passed\n", checks);
        return 0;
    } catch (const std::exception& e) { fprintf(stderr, "Runtime fixture failed: %s\n", e.what()); return 1; }
}
