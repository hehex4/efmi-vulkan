#include "character_program.h"
#include "../../frontend/xxmi/character_core.h"
#include "json.h"
#include <chrono>
#include <fstream>
#include <optional>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <string_view>

namespace efvk {
namespace {
// The folder of the module this code runs in (the layer DLL; a test program offline). Its configuration,
// settings, caches and log are never Mod content, also when a player unpacked the release into the Mods
// folder (EFMI\Mods\EFMI-Vulkan): its settings file is an INI the scans would otherwise load as a Mod's.
const std::filesystem::path& layer_folder()
{
    static const std::filesystem::path folder = [] {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&layer_folder), &module))
            return std::filesystem::path{};
        std::wstring path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) return std::filesystem::path{};
        path.resize(length);
        return std::filesystem::path(path).parent_path();
    }();
    return folder;
}
bool layer_folder_entry(const std::filesystem::directory_entry& entry)
{
    std::error_code error;
    const auto& own = layer_folder();
    return !own.empty() && !_wcsicmp(entry.path().filename().c_str(), own.filename().c_str()) && entry.is_directory(error) &&
           std::filesystem::equivalent(entry.path(), own, error);
}
std::wstring key(const std::filesystem::path& path)
{
    auto result = path.lexically_normal().wstring();
    std::transform(result.begin(), result.end(), result.begin(), ::towlower); return result;
}
std::vector<uint8_t> read(const std::filesystem::path& path, uint64_t limit)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() < 0 || uint64_t(in.tellg()) > limit) throw std::runtime_error("Runtime resource missing or over budget: " + path.u8string());
    std::vector<uint8_t> bytes(size_t(in.tellg())); in.seekg(0); in.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!in) throw std::runtime_error("Runtime resource read failed");
    return bytes;
}
// An INI's lines without comments, trimmed and in lower case (D-112).
std::string ini_text(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::string line, text;
    while (std::getline(in, line)) {
        const auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == ';') continue;
        text.append(line, start, std::string::npos); text += '\n';
    }
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(tolower(c)); });
    return text;
}
// Whether the text has a section of this name (lower case), as the original names a section: the
// line up to its first `]`, without the spaces around.
bool has_section(const std::string& text, const char* wanted, bool prefix = false)
{
    const size_t length = strlen(wanted);
    for (size_t at = 0; at < text.size();) {
        const auto end = text.find('\n', at);
        const auto line = std::string_view(text).substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? text.size() : end + 1;
        if (line.empty() || line[0] != '[') continue;
        auto name = line.substr(1, line.find(']') == std::string_view::npos ? std::string_view::npos : line.find(']') - 1);
        while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.remove_prefix(1);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t' || name.back() == '\r')) name.remove_suffix(1);
        if (prefix ? name.substr(0, length) == wanted : name == wanted) return true;
    }
    return false;
}
template<class T> void put(std::vector<uint8_t>& out, T value)
{
    const auto* p = reinterpret_cast<const uint8_t*>(&value); out.insert(out.end(), p, p + sizeof(T));
}
// Route refreshed source bytes back through the existing complete package
// validator. It checks bone indices, weights, remaps, bounds and partitions.
bool validate_refreshed(CharacterPackage& package, std::string& reason)
{
    std::vector<uint8_t> bytes{'E','F','M','I','C','H','0','1'};
    for (auto value : {package.bones, uint32_t(package.components.size()), uint32_t(package.streams.size()), uint32_t(package.sections.size()),
                      uint32_t(package.lists.size()), uint32_t(package.draws.size()), uint32_t(package.edits.size()), 0u}) put(bytes, value);
    put(bytes, uint64_t(package.payload.size()));
    for (const auto& c : package.components)
        for (auto value : {c.first, c.count, c.remap_stream, c.lists[0], c.lists[1], 0u}) put(bytes, value);
    for (const auto& s : package.streams) {
        put(bytes, s.kind); put(bytes, s.stride); put(bytes, s.offset); put(bytes, s.size); put(bytes, s.bone_index_offset); put(bytes, s.bone_weight_offset);
    }
    for (const auto& s : package.sections)
        for (auto value : {s.hash, s.match_count, s.match_first, s.component, s.lod}) put(bytes, value);
    for (const auto& l : package.lists) { put(bytes, l.first); put(bytes, l.count); }
    for (const auto& d : package.draws) {
        put(bytes, d.index_stream); put(bytes, d.count); put(bytes, d.first); put(bytes, d.base);
        for (auto value : d.vb) put(bytes, value);
    }
    for (const auto& e : package.edits) { put(bytes, e.binding); put(bytes, uint32_t(e.source)); put(bytes, uint32_t(e.format)); }
    bytes.insert(bytes.end(), package.payload.begin(), package.payload.end());
    const auto checksum = crc32c(0, bytes.data() + 48, bytes.size() - 48); std::memcpy(bytes.data() + 36, &checksum, 4);
    CharacterPackage validated;
    if (!validated.decode(bytes, reason)) return false;
    for (size_t i = 0; i < validated.sections.size(); ++i) {
        validated.sections[i].stride_slot = package.sections[i].stride_slot; validated.sections[i].stride = package.sections[i].stride;
    }
    validated.materials = package.materials; validated.material_vendor = package.material_vendor; validated.material_device = package.material_device;
    validated.images = std::move(package.images); validated.profiles = std::move(package.profiles); validated.pixels = std::move(package.pixels);
    package = std::move(validated); return true;
}
}
bool CharacterProgram::configured(const std::string& path)
{
    return std::filesystem::exists(std::filesystem::u8path(path + ".runtime.json"));
}
bool CharacterProgram::init(const std::string& path, CharacterPackage& package, std::string& reason, const GpuFactory& factory)
{
    try {
        const auto manifest = std::filesystem::u8path(path + ".runtime.json");
        const auto contents = read(manifest, 1 << 20);
        JsonRequest config;
        if (!config.parse(std::string(contents.begin(), contents.end())) || config.get("schema") != "1") throw std::runtime_error("Invalid runtime sidecar schema");
        const auto ini = std::filesystem::u8path(config.get("ini"));
        const auto core = std::filesystem::u8path(config.get("core"));
        if (!ini.is_absolute() || !core.is_absolute()) throw std::runtime_error("Runtime INI/Core paths must be absolute");
        std::vector<uint8_t> payload;
        std::vector<CharacterSource> files;
        std::map<std::wstring, uint32_t> remaps;
        for (uint32_t i = 0; i < package.streams.size(); ++i) {
            auto& s = package.streams[i];
            const auto source = std::filesystem::u8path(config.get(("stream_" + std::to_string(i)).c_str()));
            files.push_back({source.u8string(), 0, 0});
            if (!source.is_absolute()) throw std::runtime_error("Runtime stream catalogue is incomplete");
            auto data = read(source, 128ull << 20);
            if (data.empty() || data.size() % s.stride || payload.size() + data.size() > (512ull << 20)) throw std::runtime_error("Runtime mesh size or stride invalid");
            while (payload.size() % 16) payload.push_back(0);
            s.offset = payload.size(); s.size = data.size(); payload.insert(payload.end(), data.begin(), data.end());
            if (s.kind != CharacterStream::Remap && !streams_.emplace(key(source), i).second) throw std::runtime_error("Duplicate runtime stream catalogue entry");
            if (s.kind == CharacterStream::Remap) remaps.emplace(key(source), i);
        }
        package.payload = std::move(payload);
        if (!validate_refreshed(package, reason)) return false;
        if (!config.get("rabbit_textures").empty())
            throw std::runtime_error("RabbitFX texture-role inspection is not a complete runtime shader contract");
        auto facts = std::make_shared<efmi::frontend::CharacterDiscovery>();
        if (!load(ini, core, manifest.parent_path() / "settings", config.get("settings", "mod"),
                  efmi::frontend::character_core(config.get("namespace"), package, facts), factory, reason)) return false;
        identify_components_ = facts->identify_components;
        element_offsets_ = facts->element_offsets;
        efmittl_ = facts->efmittl;
        catalogue(package, remaps);
        // Validated and catalogued: the bytes are read again from the files when needed (ISSUE-011).
        return package.release_payload(std::move(files), reason);
    } catch (const std::exception& e) { reason = e.what(); return false; }
}
bool CharacterProgram::load(const std::filesystem::path& ini, const std::filesystem::path& core, const std::filesystem::path& settings_folder,
                            const std::string& settings_name, std::shared_ptr<efmi::frontend::RuntimeExtension> extension,
                            const GpuFactory& factory, std::string& reason, const std::filesystem::path& mods, bool declarations_only)
{
    efmi::frontend::RuntimeConfig options{ini.parent_path(), {core / "API.ini", core / "MergedSkeleton.ini", core / "SpatialIdentification.ini", core / "Helpers.ini", ini},
                                         settings_folder, settings_name};
    if (!mods.empty()) options.native_library_files = native_library_files(mods);
    if (factory) gpu_ = factory(core.parent_path().parent_path(), gpu_reason_);
    if (gpu_) {
        // The original loads every enabled INI of the Mod folder (DISABLED names excluded).
        const auto folder = ini.parent_path();
        std::error_code error;
        for (auto it = std::filesystem::recursive_directory_iterator(folder, error); !error && it != std::filesystem::recursive_directory_iterator();
             it.increment(error)) {
            const auto name = it->path().filename().wstring();
            // The generation's own settings are never a Mod input, wherever they are kept.
            const bool settings = it->is_directory() && std::filesystem::equivalent(it->path(), options.settings_directory, error);
            if (settings || !_wcsnicmp(name.c_str(), L"DISABLED", 8) || layer_folder_entry(*it)) { if (it->is_directory()) it.disable_recursion_pending(); continue; }
            if (!it->is_regular_file() || _wcsicmp(it->path().extension().c_str(), L".ini") || std::filesystem::equivalent(it->path(), ini, error)) continue;
            if (options.inputs.size() >= 64) throw std::runtime_error("The Mod folder has too many INI files for one generation");
            options.inputs.push_back(it->path());
        }
        // The libraries the Mod calls by namespace from elsewhere in the Mods folder (D-112), as the
        // original, which loads the whole folder into one space, finds them.
        library_notes_.clear(); libraries_.clear();
        if (!mods.empty()) options.libraries = libraries(mods, options.inputs, efmi::frontend::character_core_libraries(), library_notes_);
        for (const auto& library : options.libraries) libraries_[declared_namespace(library)] = library;
    }
    return start(options, std::move(extension), declarations_only, reason);
}
bool CharacterProgram::start(efmi::frontend::RuntimeConfig& options, std::shared_ptr<efmi::frontend::RuntimeExtension> extension,
                             bool declarations_only, std::string& reason)
{
    // The layer answers texture queries of replaced draws (D-077).
    options.texture_identity = true;
    if (gpu_) {
        extension = gpu_->combine(std::move(extension));
        gpu_->begin_deferred();
    } else if (declarations_only) {
        extension = efmi::frontend::declarations_only(std::move(extension));
    }
    runtime_ = efmi::frontend::IniRuntime::load(options, extension, reason);
    if (gpu_) {
        if (!runtime_ && gpu_->scope_active()) gpu_->discard();
        // Recorded work stays pending; replaced draws keep the plan until the first Present.
        if (runtime_ && gpu_->scope_active()) gpu_->suspend();
    }
    return bool(runtime_);
}
bool CharacterProgram::owns(const std::filesystem::path& file) const
{
    if (folders_.empty()) return true;
    const auto name = key(file);
    return std::any_of(folders_.begin(), folders_.end(), [&](const std::wstring& folder) { return !name.compare(0, folder.size(), folder); });
}
void CharacterProgram::catalogue(CharacterPackage& package, const std::map<std::wstring, uint32_t>& remaps)
{
    {
        for (uint32_t i = 0; i < package.images.size(); ++i) images_.emplace(key(std::filesystem::u8path(package.images[i])), i);
        for (const auto& file : runtime_->resources()) {
            if (!owns(file->path)) continue; // another Mod's of a folder generation
            if (file->usage) {
                const auto found = streams_.find(key(file->path));
                if (found == streams_.end()) {
                    if (remaps.count(key(file->path))) continue; // Validated by the native Core palette reader.
                    // A mesh of sections the renderer has no part for (another INI of the Mod folder,
                    // a LOD beyond the packager's scope) stays with the game's draws, which are not
                    // planned; a replaced draw that binds it refuses in plan().
                    ++outside_meshes_; continue;
                }
                const auto& stream = package.streams[found->second];
                if (file->stride != stream.stride || (file->usage == VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != (stream.kind == CharacterStream::Index))
                    throw std::runtime_error("Original file declaration differs from the renderer stream profile");
            } else if (_wcsicmp(file->path.extension().c_str(), L".dds")) {
                continue; // Only owned GPU work reads other image files; a replaced draw binding one refuses.
            } else if (std::error_code error; !std::filesystem::exists(file->path, error)) {
                continue; // The original's load of it fails and it binds nothing (image_index, BUG-076).
            } else if (!images_.count(key(file->path))) {
                if (package.images.size() >= kCharacterImages) throw std::runtime_error("Runtime image catalogue exceeds budget");
                images_.emplace(key(file->path), uint32_t(package.images.size())); package.images.push_back(file->path.u8string());
            }
        }
        index_bounds_.resize(package.streams.size());
        for (size_t s = 0; s < package.streams.size(); ++s) {
            const auto& stream = package.streams[s]; if (stream.kind != CharacterStream::Index) continue;
            uint32_t low = UINT32_MAX, high = 0;
            for (uint64_t at = 0; at < stream.size; at += stream.stride) {
                uint32_t v = 0; std::memcpy(&v, package.payload.data() + stream.offset + at, stream.stride);
                low = std::min(low, v); high = std::max(high, v);
            }
            index_bounds_[s] = {low, high};
        }
    }
}
std::string CharacterProgram::settings_name(const std::filesystem::path& ini, const std::string& declared)
{
    // The settings file is the namespace's and the Mod folder's: two Mods of one character declare
    // the same namespace (`namespace = yvonne`), and the toggles persisted by the one must not come
    // up in the other when it takes its place (BUG-039). The original keeps one d3dx_user.ini by
    // namespace alone and inherits them.
    std::string name = declared;
    if (name.empty()) {
        const auto file = ini.filename().wstring();
        for (wchar_t c : file) name += c < 0x80 ? static_cast<char>(towlower(c)) : '_';
    }
    std::string tag;
    for (wchar_t c : ini.parent_path().filename().wstring())
        tag += c < 0x80 && (isalnum(static_cast<int>(c)) || c == L'_' || c == L'-') ? static_cast<char>(c) : '_';
    if (!tag.empty()) name += "-" + tag;
    for (char& c : name) if (!isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
    if (name.size() > 80) name.resize(80);
    return name;
}
std::string CharacterProgram::declared_namespace(const std::filesystem::path& ini)
{
    const auto bytes = read(ini, 32u << 20);
    std::istringstream in(std::string(bytes.begin(), bytes.end()));
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) line.erase(0, 3);
        const auto start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos || line[start] == ';') continue;
        if (line[start] == '[') return {}; // the namespace line precedes every section
        const auto equals = line.find('=', start);
        if (equals == std::string::npos) continue;
        std::string name = line.substr(start, equals - start);
        name.erase(name.find_last_not_of(" \t") + 1);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name != "namespace") continue;
        std::string value = line.substr(equals + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t\r") + 1);
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return value;
    }
    return {};
}
std::vector<std::filesystem::path> CharacterProgram::mods_characters(const std::filesystem::path& mods, std::string& reason)
{
    std::vector<std::filesystem::path> found;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(mods, error); !error && it != std::filesystem::recursive_directory_iterator();
         it.increment(error)) {
        const auto name = it->path().filename().wstring();
        if (!_wcsnicmp(name.c_str(), L"DISABLED", 8) || layer_folder_entry(*it)) { if (it->is_directory(error)) it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(error) || _wcsicmp(it->path().extension().c_str(), L".ini")) continue;
        if (has_section(ini_text(it->path()), "commandlistinitializemergedskeleton")) found.push_back(it->path());
    }
    if (error) reason = "cannot read the Mods folder " + mods.u8string() + ": " + error.message();
    std::sort(found.begin(), found.end());
    return found;
}
// The installed files of the libraries the runtime provides natively (RabbitFX, D-082, D-133): enabled
// INIs of the Mods folder that declare one of their namespaces.
std::vector<std::filesystem::path> CharacterProgram::native_library_files(const std::filesystem::path& mods)
{
    const auto native = efmi::frontend::character_core_libraries();
    std::vector<std::filesystem::path> found;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(mods, error); !error && it != std::filesystem::recursive_directory_iterator();
         it.increment(error)) {
        if (!_wcsnicmp(it->path().filename().c_str(), L"DISABLED", 8) || layer_folder_entry(*it)) { if (it->is_directory(error)) it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(error) || _wcsicmp(it->path().extension().c_str(), L".ini")) continue;
        const auto name = declared_namespace(it->path());
        if (!name.empty() && std::find(native.begin(), native.end(), name) != native.end()) found.push_back(it->path());
    }
    std::sort(found.begin(), found.end());
    return found;
}
std::vector<std::filesystem::path> CharacterProgram::libraries(const std::filesystem::path& mods, const std::vector<std::filesystem::path>& inputs,
                                                              const std::vector<std::string>& known, std::vector<std::string>& notes)
{
    std::set<std::string> declared(known.begin(), known.end());
    std::string calls;
    for (const auto& input : inputs) {
        calls += ini_text(input);
        const auto name = declared_namespace(input);
        if (!name.empty()) declared.insert(name);
    }
    // Most generations call nothing beyond their own INIs and the Core: the Mods folder is read only
    // when a reference names a namespace no input declares (by its first part; `CommandList\NS\Name`).
    std::set<std::string> heads;
    for (const auto& name : declared) heads.insert(name.substr(0, name.find('\\')));
    bool unknown = false;
    for (const char* prefix : {"commandlist\\", "customshader\\", "resource\\", "$\\"}) {
        const std::string p = prefix;
        for (size_t at = calls.find(p); !unknown && at != std::string::npos; at = calls.find(p, at + 1)) {
            const auto from = at + p.size(), end = calls.find_first_of("\\\n", from);
            if (end != std::string::npos && calls[end] == '\\' && !heads.count(calls.substr(from, end - from))) unknown = true;
        }
    }
    if (!unknown) return {};
    // Every enabled INI of the folder: those of character Mods mark their folders, whose INIs belong to
    // that Mod's generation; the others are libraries by their declared namespace.
    struct Candidate { std::filesystem::path path; std::string name, text; bool done = false; };
    std::vector<Candidate> candidates;
    std::vector<std::filesystem::path> characters;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(mods, error); !error && it != std::filesystem::recursive_directory_iterator();
         it.increment(error)) {
        if (!_wcsnicmp(it->path().filename().c_str(), L"DISABLED", 8) || layer_folder_entry(*it)) { if (it->is_directory(error)) it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(error) || _wcsicmp(it->path().extension().c_str(), L".ini")) continue;
        auto text = ini_text(it->path());
        if (has_section(text, "commandlistinitializemergedskeleton")) { characters.push_back(it->path().parent_path()); continue; }
        auto name = declared_namespace(it->path());
        if (!name.empty() && !declared.count(name)) candidates.push_back({it->path(), std::move(name), std::move(text)});
    }
    if (error) { notes.push_back("libraries: cannot read the Mods folder " + mods.u8string() + ": " + error.message()); return {}; }
    const auto inside = [](const std::filesystem::path& path, const std::filesystem::path& folder) {
        const auto relative = path.lexically_relative(folder);
        return !relative.empty() && *relative.begin() != "..";
    };
    std::vector<std::filesystem::path> taken;
    for (bool grew = true; grew;) {
        grew = false;
        for (auto& c : candidates) {
            if (c.done || calls.find("\\" + c.name + "\\") == std::string::npos) continue;
            c.done = true;
            if (std::any_of(characters.begin(), characters.end(), [&](const auto& folder) { return inside(c.path, folder); })) {
                notes.push_back("library not loaded: " + c.path.u8string() + " (namespace " + c.name + ") belongs to another character Mod");
                continue;
            }
            // A section acting on the game's resources by hash would act once for every generation that
            // loads the library, where the original acts once.
            if (has_section(c.text, "textureoverride", true)) {
                notes.push_back("library not loaded: " + c.path.u8string() + " (namespace " + c.name + ") acts on game resources by hash");
                continue;
            }
            if (taken.size() >= 16) { notes.push_back("library not loaded: " + c.path.u8string() + ": more than 16 libraries"); continue; }
            notes.push_back("library loaded: " + c.path.u8string() + " (namespace " + c.name + ")");
            taken.push_back(c.path);
            calls += c.text;
            grew = true;
        }
    }
    return taken;
}
bool CharacterProgram::discover(const Source& source, CharacterPackage& package, std::string& reason, const GpuFactory& factory)
{
    try {
        if (!source.ini.is_absolute() || !source.core.is_absolute() || !source.settings.is_absolute())
            throw std::runtime_error("Directory generation paths must be absolute");
        // Without a namespace line the runtime parses the INI under its file name (its path relative
        // to the Mod folder), which names its sections and variables as the original's implicit
        // namespace does. The settings file gets a name of plain characters.
        std::string mod_namespace = source.mod_namespace;
        if (mod_namespace.empty()) {
            const auto name = source.ini.filename().wstring();
            if (std::any_of(name.begin(), name.end(), [](wchar_t c) { return c < 0x20 || c > 0x7e; }))
                throw std::runtime_error("The INI declares no namespace and its file name is not ASCII");
            mod_namespace.assign(name.begin(), name.end());
            std::transform(mod_namespace.begin(), mod_namespace.end(), mod_namespace.begin(), ::tolower);
        }
        const std::string settings_name = CharacterProgram::settings_name(source.ini, mod_namespace);
        auto found = std::make_shared<efmi::frontend::CharacterDiscovery>();
        if (!load(source.ini, source.core, source.settings, settings_name,
                  efmi::frontend::character_discovery(mod_namespace, found), factory, reason, source.mods, source.declarations_only)) {
            // Offline the sections are known before a declaration that needs the GPU refuses: kept for
            // the dry run's listing (the capture replay builds its draws from them).
            if (source.declarations_only) { package = {}; package.sections = found->declared; }
            return false;
        }
        return adopt(*found, package, reason);
    } catch (const std::exception& e) { reason = e.what(); return false; }
}
bool CharacterProgram::adopt(efmi::frontend::CharacterDiscovery& found, CharacterPackage& package, std::string& reason)
{
    try {
        CharacterPackage made = found.contract;
        made.lists.clear();
        for (auto& c : made.components) {
            c.lists[0] = uint32_t(made.lists.size()); made.lists.push_back({});
            c.lists[1] = uint32_t(made.lists.size()); made.lists.push_back({});
        }
        // The blend-index edit is the one the live plans accept (CharacterProgram::plan).
        made.edits.push_back({2, VK_FORMAT_R8G8B8A8_UINT, VK_FORMAT_R16G16B16A16_UINT});
        std::map<std::wstring, uint32_t> remaps, meshes;
        std::vector<uint8_t> payload;
        std::vector<CharacterSource> files(made.streams.size());
        auto add = [&](const std::filesystem::path& path, uint32_t kind, uint32_t stride) {
            auto data = read(path, 128ull << 20);
            if (data.empty() || !stride || data.size() % stride || payload.size() + data.size() > (512ull << 20))
                throw std::runtime_error("Mesh size or stride invalid: " + path.u8string());
            if (made.streams.size() >= 256) throw std::runtime_error("More mesh files than one character holds");
            while (payload.size() % 16) payload.push_back(0);
            CharacterStream s;
            s.kind = kind; s.stride = stride; s.offset = payload.size(); s.size = data.size();
            payload.insert(payload.end(), data.begin(), data.end());
            made.streams.push_back(s);
            files.push_back({path.u8string(), 0, 0});
            return uint32_t(made.streams.size() - 1);
        };
        for (size_t i = 0; i < made.components.size(); ++i)
            if (!found.remap_files[i].empty()) {
                const auto k = key(found.remap_files[i]);
                auto it = remaps.find(k);
                if (it == remaps.end()) it = remaps.emplace(k, add(found.remap_files[i], CharacterStream::Remap, 2)).first;
                made.components[i].remap_stream = it->second;
            }
        // Every mesh file the generation declares (a folder Mod: under its folders); those of sections
        // the renderer has no part for are never bound by a replaced draw. In the order of their files,
        // whatever order the generation keeps its resources in: the same files make the same mesh, which
        // a successor generation keeps on the device (BUG-061).
        struct Declared { std::filesystem::path path; uint32_t kind, stride; };
        std::map<std::wstring, Declared> declared;
        for (const auto& file : runtime_->resources()) {
            if (!file->usage || remaps.count(key(file->path)) || !owns(file->path)) continue;
            const uint32_t kind = file->usage == VK_BUFFER_USAGE_INDEX_BUFFER_BIT ? CharacterStream::Index : CharacterStream::Vertex;
            const auto known = declared.emplace(key(file->path), Declared{file->path, kind, file->stride});
            if (!known.second && (known.first->second.kind != kind || known.first->second.stride != file->stride))
                throw std::runtime_error("One mesh file declared with two meanings: " + file->path.u8string());
        }
        for (const auto& file : declared) meshes.emplace(file.first, add(file.second.path, file.second.kind, file.second.stride));
        made.payload = std::move(payload);
        made.materials = true;
        if (!validate_refreshed(made, reason)) return false;
        for (const auto& mesh : meshes) streams_.emplace(mesh.first, mesh.second);
        catalogue(made, remaps);
        // Validated and catalogued: as the original frees a buffer's file bytes once the resource holds
        // them, the package keeps where they are and reads them again when it needs them (ISSUE-011).
        if (!made.release_payload(std::move(files), reason)) return false;
        package = std::move(made);
        left_to_game_ = found.left_to_game;
        identify_components_ = found.identify_components;
        element_offsets_ = found.element_offsets;
        efmittl_ = found.efmittl;
        return true;
    } catch (const std::exception& e) { reason = e.what(); return false; }
}
namespace {
// The original's include order (IniHandler.cpp ParseIniFilesRecursive): in each folder its `.ini`
// files by name, then its folders by name, both in ordinal order; names starting with DISABLED and
// desktop.ini left out (d3dx.ini's exclude_recursive). `wanted` keys select the files kept.
void include_order(const std::filesystem::path& folder, const std::set<std::wstring>& wanted, std::vector<std::filesystem::path>& out, unsigned depth = 0)
{
    if (depth > 32) return;
    std::set<std::wstring> files, folders;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        const auto name = entry.path().filename().wstring();
        if (!_wcsnicmp(name.c_str(), L"DISABLED", 8) || !_wcsicmp(name.c_str(), L"desktop.ini") || layer_folder_entry(entry)) continue;
        if (entry.is_directory(error)) folders.insert(name);
        else if (name.size() > 4 && !wcscmp(name.c_str() + name.size() - 4, L".ini")) files.insert(name);
    }
    for (const auto& name : files) if (wanted.count(key(folder / name))) out.push_back(folder / name);
    for (const auto& name : folders) include_order(folder / name, wanted, out, depth + 1);
}
std::wstring folder_key(const std::filesystem::path& folder)
{
    auto name = key(folder);
    if (!name.empty() && name.back() != L'\\') name += L'\\';
    return name;
}
}
bool CharacterProgram::discover_folder(const FolderSource& source, std::vector<FolderMod>& out, std::string& reason, const GpuFactory& factory)
{
    try {
        out.clear();
        if (!source.mods.is_absolute() || !source.core.is_absolute() || !source.settings.is_absolute())
            throw std::runtime_error("Folder generation paths must be absolute");
        // The original's root: d3dx.ini's folder, the Mods folder's parent; every INI is named by its path from it.
        const auto root = source.mods.parent_path();
        const auto inis = mods_characters(source.mods, reason);
        if (inis.empty()) return false;
        // Per character Mod: its namespace (declared, else the original's implicit one, the INI's path from
        // the root), its INIs and the libraries it calls.
        std::vector<std::vector<std::filesystem::path>> mod_inputs(inis.size());
        std::vector<std::map<std::string, std::filesystem::path>> called(inis.size());
        std::vector<std::vector<std::string>> notes(inis.size());
        const bool gpu = bool(factory);
        std::vector<std::wstring> names(inis.size());
        for (size_t i = 0; i < inis.size(); ++i) {
            FolderMod m; m.ini = inis[i]; m.mod_namespace = declared_namespace(inis[i]);
            // The namespace as the parser names the INI's sections: the declared one (UTF-8), else the
            // INI's path from the root, as the original's include names it; in lower case as it lowers them.
            std::wstring name;
            if (!m.mod_namespace.empty()) {
                const int n = MultiByteToWideChar(CP_UTF8, 0, m.mod_namespace.data(), int(m.mod_namespace.size()), nullptr, 0);
                name.resize(size_t(std::max(n, 0)));
                if (n) MultiByteToWideChar(CP_UTF8, 0, m.mod_namespace.data(), int(m.mod_namespace.size()), name.data(), n);
            } else {
                name = inis[i].lexically_relative(root).wstring();
                m.mod_namespace = inis[i].lexically_relative(root).u8string();
            }
            std::transform(name.begin(), name.end(), name.begin(), ::towlower);
            names[i] = std::move(name);
            mod_inputs[i].push_back(inis[i]);
            if (gpu) {
                // As a generation of one Mod: with the owned runtime the Mod folder's other enabled INIs
                // join, and the libraries it calls by namespace (D-112).
                std::error_code error;
                const auto folder = inis[i].parent_path();
                for (auto it = std::filesystem::recursive_directory_iterator(folder, error); !error && it != std::filesystem::recursive_directory_iterator();
                     it.increment(error)) {
                    const auto name = it->path().filename().wstring();
                    const bool settings = it->is_directory() && std::filesystem::equivalent(it->path(), source.settings, error);
                    if (settings || !_wcsnicmp(name.c_str(), L"DISABLED", 8) || layer_folder_entry(*it)) { if (it->is_directory()) it.disable_recursion_pending(); continue; }
                    if (!it->is_regular_file() || _wcsicmp(it->path().extension().c_str(), L".ini") || std::filesystem::equivalent(it->path(), inis[i], error)) continue;
                    mod_inputs[i].push_back(it->path());
                }
                for (const auto& library : libraries(source.mods, mod_inputs[i], efmi::frontend::character_core_libraries(), notes[i]))
                    called[i][declared_namespace(library)] = library;
            }
            out.push_back(std::move(m));
        }
        // A Mod the generation cannot take drops only itself (D-131): when the folder refuses, the Mods
        // that refuse in a generation of their own are left out with their reasons and the rest load again.
        std::vector<bool> left_out(inis.size(), false);
        for (int attempt = 0;; ++attempt) {
            std::vector<std::pair<std::wstring, std::shared_ptr<efmi::frontend::CharacterDiscovery>>> mods;
            std::vector<size_t> members;
            std::set<std::wstring> wanted;
            for (size_t i = 0; i < inis.size(); ++i) {
                if (left_out[i]) continue;
                members.push_back(i);
                mods.push_back({names[i], std::make_shared<efmi::frontend::CharacterDiscovery>()});
                for (const auto& input : mod_inputs[i]) wanted.insert(key(input));
                for (const auto& library : called[i]) wanted.insert(key(library.second));
            }
            if (members.empty()) { reason = "every character Mod of the folder refused"; return false; }
            efmi::frontend::RuntimeConfig options{root, {source.core / "API.ini", source.core / "MergedSkeleton.ini", source.core / "SpatialIdentification.ini",
                                                         source.core / "Helpers.ini"}, source.settings, source.settings_name};
            options.native_library_files = native_library_files(source.mods);
            std::vector<std::filesystem::path> ordered;
            include_order(source.mods, wanted, ordered);
            if (ordered.size() + options.inputs.size() > 128) throw std::runtime_error("The Mods folder has more INI files than one generation takes");
            options.inputs.insert(options.inputs.end(), ordered.begin(), ordered.end());
            CharacterProgram loader;
            if (gpu) loader.gpu_ = factory(source.core.parent_path().parent_path(), loader.gpu_reason_);
            if (loader.start(options, efmi::frontend::character_folder(mods), source.declarations_only, reason)) {
                for (size_t k = 0; k < members.size(); ++k) {
                    const size_t i = members[k];
                    auto program = std::make_unique<CharacterProgram>();
                    program->runtime_ = loader.runtime_; program->gpu_ = loader.gpu_; program->gpu_reason_ = loader.gpu_reason_;
                    program->folder_index_ = uint32_t(k);
                    program->libraries_ = called[i]; program->library_notes_ = notes[i];
                    program->folders_ = {folder_key(out[i].ini.parent_path()), folder_key(source.core)};
                    for (const auto& library : called[i]) program->folders_.push_back(folder_key(library.second.parent_path()));
                    out[i].reason.clear();
                    if (program->adopt(*mods[k].second, out[i].package, out[i].reason)) out[i].program = std::move(program);
                }
                return true;
            }
            if (attempt) return false;
            const std::string folder_reason = reason;
            bool any = false;
            for (size_t i : members) {
                CharacterProgram alone; CharacterPackage package; std::string why;
                Source own{inis[i], source.core, source.settings, declared_namespace(inis[i]), source.mods, source.declarations_only};
                if (alone.discover(own, package, why, factory)) continue;
                left_out[i] = true; any = true;
                out[i].reason = why; out[i].package = std::move(package); // the sections, for the dry run's listing
            }
            if (!any) { reason = folder_reason; return false; }
        }
    } catch (const std::exception& e) { reason = e.what(); return false; }
}
uint32_t CharacterProgram::stream_index(const std::shared_ptr<const efmi::assets::FileResource>& resource)
{
    const auto cached = stream_cache_.find(resource.get());
    if (cached != stream_cache_.end()) return cached->second;
    const auto found = streams_.find(key(resource->path));
    if (found == streams_.end()) throw std::runtime_error("INI mesh not in prepared stream profile");
    if (stream_cache_.size() < 4096) stream_cache_.emplace(resource.get(), found->second);
    return found->second;
}
uint32_t CharacterProgram::image_index(const std::shared_ptr<const efmi::assets::FileResource>& resource)
{
    const auto cached = image_cache_.find(resource.get());
    if (cached != image_cache_.end()) return cached->second;
    const auto found = images_.find(key(resource->path));
    uint32_t index = found == images_.end() ? kCharacterNone : found->second;
    // The original loads a texture file at its first use; where that fails it warns once and the
    // resource stays null, so every assignment of it binds nothing and the draw goes on
    // (CustomResource::LoadFromFile). A file that is not there is such an explicit null binding, in a
    // Mod's own generation and in the folder's alike (BUG-076: tangtang Ramune binds a normal map its
    // author never shipped; the folder generation refused the draw, the Mod's own one bound the file).
    std::error_code error;
    if (!std::filesystem::exists(resource->path, error)) {
        index = kCharacterNone;
        logf("INI texture file missing, bound as nothing as the original does: %s", resource->path.u8string().c_str());
    } else if (found == images_.end()) {
        throw std::runtime_error("INI image not in prepared resource catalogue");
    }
    if (image_cache_.size() < 4096) image_cache_.emplace(resource.get(), index);
    return index;
}
bool CharacterProgram::plan(const efmi::frontend::RuntimeDraw& request, CharacterPackage& package, CharacterList& output,
                            bool& skip, bool& added, std::string& reason, const VertexInputRecipe* vertex, bool* textures)
{
    try {
        added = false; efmi::assets::FilePlan& plan = file_plan_;
        if (textures) *textures = false;
#if defined(EFMI_COMMAND_TIMING) || defined(EFMI_PHASE_TIMING)
        struct Phase { CharacterProgram* p; std::chrono::steady_clock::time_point began;
                       ~Phase() { if (p) p->note_timing("phase list", uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - began).count())); } };
        Phase listing{nullptr, {}};
#endif
        if (!runtime_->draw(request, plan, reason)) return false;
#if defined(EFMI_COMMAND_TIMING) || defined(EFMI_PHASE_TIMING)
        listing.p = this; listing.began = std::chrono::steady_clock::now();
#endif
        skip = plan.skip_original;
        if (!skip) {
            output = {};
            if (!textures) return true;
            // The application's draw stays, with the textures the pre phase bound for it: under DX11
            // they are bound when the game's draw runs. Geometry or draws of the Mod make it no
            // texture-only plan; post commands come after the draw.
            auto& pixels = scratch_pixels_; pixels.clear();
            for (size_t i = 0; i < plan.items.size() && i < plan.pre_items; ++i) {
                const auto& item = plan.items[i];
                using Op = efmi::assets::FileOperation;
                if (item.operation == Op::BindPixelResource) {
                    if (item.application_slot >= 0) {
                        if (uint32_t(item.application_slot) != item.slot) throw std::runtime_error("An application texture moved to another pixel slot");
                        pixels.erase(item.slot); continue;
                    }
                    if (!item.resource) { pixels[item.slot] = kCharacterNone; continue; }
                    pixels[item.slot] = image_index(item.resource);
                } else if (item.operation != Op::External) return true;
            }
            if (pixels.empty()) return true;
            auto& signature = scratch_signature_; signature.clear(); signature.push_back('T');
            for (const auto& pixel : pixels) { put(signature, pixel.first); put(signature, pixel.second); }
            auto& id = scratch_id_; id.assign(signature.begin(), signature.end());
            const auto found = lists_.find(id);
            if (found == lists_.end()) {
                if (lists_.size() >= 1024 || package.draws.size() + 1 > 16384) throw std::runtime_error("Runtime draw-plan cache budget exceeded");
                CharacterDraw draw;
                draw.index_stream = kCharacterNone; draw.texture_only = true;
                std::vector<CharacterPixel> material;
                for (const auto& pixel : pixels) material.push_back({pixel.first, pixel.second});
                const CharacterList result{uint32_t(package.draws.size()), 1, 0};
                package.draws.push_back(draw); package.pixels.push_back(std::move(material));
                lists_.emplace(id, result); added = true; output = result;
            } else output = found->second;
            *textures = true;
            return true;
        }
        CharacterDraw bound;
        bound.index_stream = kCharacterNone;
        auto& pixels = scratch_pixels_; pixels.clear();
        auto& draws = scratch_draws_; draws.clear();
        auto& material = scratch_material_; size_t material_count = 0;
        uint32_t required = 0;
        auto& signature = scratch_signature_; signature.clear();
        for (const auto& item : plan.items) {
            using Op = efmi::assets::FileOperation;
            if (item.operation == Op::BindPixelResource) {
                if (item.application_slot >= 0) {
                    // The application's own texture at its own slot: what the draw binds anyway.
                    if (uint32_t(item.application_slot) != item.slot) throw std::runtime_error("An application texture moved to another pixel slot");
                    pixels.erase(item.slot);
                } else if (item.resource) {
                    pixels[item.slot] = image_index(item.resource);
                } else pixels[item.slot] = kCharacterNone;
            } else if (item.operation == Op::BindIndex || item.operation == Op::BindVertex) {
                if (item.operation == Op::BindVertex && item.slot < 4)
                    bound.owned.erase(std::remove_if(bound.owned.begin(), bound.owned.end(), [&](const auto& o) { return o.slot == item.slot; }),
                                      bound.owned.end());
                if (item.owned) {
                    // A buffer the owned GPU work of this draw wrote (P2, D-087).
                    if (item.operation != Op::BindVertex || item.slot >= 4) throw std::runtime_error("Owned GPU buffer outside a profiled vertex slot");
                    const auto& o = *item.owned;
                    if (!o.buffer || !o.stride || o.size < o.stride) throw std::runtime_error("Owned vertex buffer without a whole vertex");
                    bound.vb[item.slot] = kCharacterOwned;
                    bound.owned.push_back({item.slot, o.buffer, o.offset, o.size, o.stride, o.keep});
                    continue;
                }
                if (!item.resource) {
                    if (item.operation == Op::BindIndex) bound.index_stream = kCharacterNone;
                    else if (item.slot < 4) bound.vb[item.slot] = kCharacterNone;
                    else throw std::runtime_error("Runtime vertex slot outside profile");
                    continue;
                }
                const uint32_t found_stream = stream_index(item.resource);
                const auto& stream = package.streams[found_stream];
                if (item.resource->stride != stream.stride) throw std::runtime_error("Runtime binding stride mismatch");
                if (item.operation == Op::BindIndex) {
                    if (stream.kind != CharacterStream::Index) throw std::runtime_error("Runtime index kind mismatch");
                    bound.index_stream = found_stream;
                } else {
                    if (item.slot >= 4 || stream.kind != CharacterStream::Vertex) throw std::runtime_error("Runtime vertex binding outside profile");
                    bound.vb[item.slot] = found_stream;
                }
            } else if (item.operation == Op::DrawIndexed || item.operation == Op::Translucent) {
                if (draws.size() >= 64 || item.instance_count != 1 || item.first_instance || bound.index_stream == kCharacterNone)
                    throw std::runtime_error("Runtime draw outside the single-instance contract");
                // The blend-index widening EFMI exports use: BLENDINDICES of slot 2 as R16G16B16A16_UINT
                // (DXGI 12) or R32G32B32A32_UINT (DXGI 3); the shader reads four unsigned integers either way.
                // Besides it, element offset edits whose game offset the Mod's lists name (D-085).
                const efmi::assets::LayoutEdit* blend = nullptr;
                bound.offsets.clear();
                bool understood = true;
                auto same = [](const efmi::assets::LayoutEdit& a, const efmi::assets::LayoutEdit& b) {
                    const auto &x = a.match, &y = b.match; const auto &p = a.replace, &q = b.replace;
                    return x.semantic_name == y.semantic_name && x.semantic_index == y.semantic_index && x.input_slot == y.input_slot &&
                           x.format == y.format && x.format_byte_size == y.format_byte_size && x.aligned_byte_offset == y.aligned_byte_offset &&
                           p.semantic_name == q.semantic_name && p.semantic_index == q.semantic_index && p.format == q.format &&
                           p.aligned_byte_offset == q.aligned_byte_offset;
                };
                for (size_t e = 0; e < item.layout.size(); ++e) {
                    const auto& edit = item.layout[e];
                    const auto& m = edit.match; const auto& r = edit.replace;
                    const bool plain_match = !m.format && m.format_byte_size == UINT32_MAX && m.aligned_byte_offset == UINT32_MAX &&
                                             r.semantic_name.empty() && r.semantic_index == UINT32_MAX;
                    // The original applies every override in order to each element it matches; a plain one
                    // (by slot and semantic, keeping the semantic) given again later changes nothing more.
                    if (plain_match && std::any_of(item.layout.begin() + e + 1, item.layout.end(), [&](const auto& later) { return same(later, edit); }))
                        continue;
                    if (!blend && plain_match && m.input_slot == 2 && m.semantic_name == "BLENDINDICES" && (r.format == 12 || r.format == 3) &&
                        r.aligned_byte_offset == UINT32_MAX) { blend = &edit; continue; }
                    if (plain_match && !r.format && r.aligned_byte_offset != UINT32_MAX && m.input_slot < 4 && m.semantic_index != UINT32_MAX) {
                        const auto known = element_offsets_.find({m.input_slot, m.semantic_name, m.semantic_index});
                        if (known != element_offsets_.end() && known->second.size() == 2 && known->second.count(r.aligned_byte_offset)) {
                            const uint32_t game = *known->second.begin() == r.aligned_byte_offset ? *known->second.rbegin() : *known->second.begin();
                            if (game != r.aligned_byte_offset) { bound.offsets.push_back({m.input_slot, game, r.aligned_byte_offset}); continue; }
                        }
                    }
                    understood = false;
                }
                if (!blend || !understood) {
                    std::string edits;
                    for (const auto& edit : item.layout)
                        edits += " slot " + std::to_string(edit.match.input_slot) + " " + edit.match.semantic_name + " to " +
                                 (edit.replace.aligned_byte_offset != UINT32_MAX ? "offset " + std::to_string(edit.replace.aligned_byte_offset)
                                                                                : "format " + std::to_string(edit.replace.format)) + ";";
                    throw std::runtime_error("Runtime input-layout edits differ from the validated blend-index contract (" +
                                             std::to_string(item.layout.size()) + " edits:" + edits + ")");
                }
                bound.blend = blend->replace.format == 3 ? VK_FORMAT_R32G32B32A32_UINT : VK_FORMAT_R16G16B16A16_UINT;
                const uint32_t index_bytes = bound.blend == VK_FORMAT_R32G32B32A32_UINT ? 4 : 2;
                bound.count = item.index_count; bound.first = item.first_index; bound.base = item.vertex_offset;
                // The stream under the blend-index edit carries the bone indices where the pipeline reads
                // the element the edit widens (R8G8B8A8_UINT in binding 2, then 16-bit indices), and
                // 16-bit weights where it reads R16G16B16A16_UNORM in that binding. Without such an
                // element (offline, or a pipeline that does not read the stream) the packager's rule:
                // indices in the last 8 bytes of an 8- or 16-byte element, weights before them. A stream
                // first seen there is checked once, as decode checks a package's; one that fits neither
                // keeps no bone mask.
                if (bound.vb[2] == kCharacterOwned) throw std::runtime_error("An owned blend stream has no known bone indices");
                if (bound.vb[2] != kCharacterNone && package.streams[bound.vb[2]].bone_index_offset != kCharacterNone &&
                    package.streams[bound.vb[2]].bone_index_bytes != index_bytes)
                    throw std::runtime_error("One blend stream under two blend-index widths");
                if (bound.vb[2] != kCharacterNone && package.streams[bound.vb[2]].bone_index_offset == kCharacterNone) {
                    const uint32_t stride = package.streams[bound.vb[2]].stride;
                    uint32_t indices = kCharacterNone, weights = kCharacterNone, weight_bytes = 2;
                    if (vertex) {
                        // BLENDINDICES is the pipeline's unsigned four-component element of binding 2, of
                        // any width (BUG-038: 32-bit in Typhoeus' clothes pipeline), BLENDWEIGHT its
                        // normalized or float one.
                        for (const auto& a : vertex->attributes) {
                            if (a.binding != 2) continue;
                            if (blend_index_format(a.format)) indices = a.offset;
                            else if (const uint32_t bytes = blend_weight_bytes(a.format)) { weights = a.offset; weight_bytes = bytes; }
                        }
                    }
                    if (indices == kCharacterNone && index_bytes == 2 && (stride == 8 || stride == 16)) {
                        indices = stride - 8; weights = stride == 16 ? 0 : kCharacterNone; weight_bytes = 2;
                    }
                    std::string why;
                    if (indices != kCharacterNone && !package.adopt_bones(bound.vb[2], indices, weights, why, index_bytes, weight_bytes))
                        throw std::runtime_error("Blend stream refused: " + why);
                }
                const auto& index = package.streams[bound.index_stream];
                if (uint64_t(bound.first) + bound.count > index.size / index.stride) throw std::runtime_error("Runtime indexed draw exceeds file bounds");
                const auto extrema = index_bounds_[bound.index_stream];
                for (uint32_t slot = 0; slot < 4; ++slot) {
                    const uint32_t stream = bound.vb[slot];
                    if (stream == kCharacterNone) continue;
                    uint64_t vertices = 0;
                    if (stream == kCharacterOwned) {
                        const auto* o = bound.owned_stream(slot);
                        vertices = o ? o->size / o->stride : 0;
                    } else vertices = package.streams[stream].size / package.streams[stream].stride;
                    if (int64_t(extrema.first) + bound.base < 0 || int64_t(extrema.second) + bound.base >= int64_t(vertices))
                        throw std::runtime_error("Runtime indexed draw exceeds vertex file bounds");
                    if (stream != kCharacterOwned) required |= package.streams[stream].components;
                }
                // A translucent draw (D-113): captured into a layer at the pass end with its opacity and mask.
                bound.translucent = item.operation == Op::Translucent;
                bound.alpha = bound.translucent ? item.alpha : 1.0f;
                bound.mask_image = bound.translucent && item.resource ? image_index(item.resource) : kCharacterNone;
                bound.mask_channel = bound.translucent ? item.mask_channel : 0;
                bound.mask_invert = bound.translucent && item.mask_invert;
                draws.push_back(bound);
                if (material_count == material.size()) material.emplace_back();
                auto& slots = material[material_count++]; slots.clear();
                for (const auto& pixel : pixels) slots.push_back({pixel.first, pixel.second});
                put(signature, bound.index_stream); put(signature, bound.count); put(signature, bound.first); put(signature, bound.base);
                for (auto stream : bound.vb) put(signature, stream);
                put(signature, uint32_t(bound.owned.size()));
                for (const auto& o : bound.owned) { put(signature, o.slot); put(signature, o.buffer); put(signature, o.offset); put(signature, o.size); put(signature, o.stride); }
                put(signature, uint32_t(bound.blend));
                put(signature, uint32_t(bound.offsets.size()));
                for (const auto& o : bound.offsets) { put(signature, o.binding); put(signature, o.from); put(signature, o.to); }
                put(signature, uint32_t(pixels.size()));
                for (const auto& pixel : pixels) { put(signature, pixel.first); put(signature, pixel.second); }
                put(signature, uint8_t(bound.translucent));
                if (bound.translucent) { put(signature, bound.alpha); put(signature, bound.mask_image); put(signature, bound.mask_channel); put(signature, uint8_t(bound.mask_invert)); }
            } else throw std::runtime_error("Unresolved runtime native operation");
        }
        auto& id = scratch_id_; id.assign(signature.begin(), signature.end());
        auto found = lists_.find(id);
        if (found != lists_.end()) { output = found->second; return true; }
        if (lists_.size() >= 1024 || package.draws.size() + draws.size() > 16384) throw std::runtime_error("Runtime draw-plan cache budget exceeded");
        CharacterList result{uint32_t(package.draws.size()), uint32_t(draws.size()), required};
        package.draws.insert(package.draws.end(), draws.begin(), draws.end());
        package.pixels.insert(package.pixels.end(), material.begin(), material.begin() + material_count);
        lists_.emplace(id, result); added = !draws.empty(); output = result; return true;
    } catch (const std::exception& e) { reason = e.what(); return false; }
}
bool CharacterProgram::present(const efmi::frontend::RuntimeInput& input, uint64_t frame, float seconds, std::string& reason, bool keep_replay_state)
{
    return runtime_->present(input, frame, seconds, reason, keep_replay_state);
}

bool CharacterProgram::replay(const efmi::frontend::RuntimeInput& input, uint64_t frame, float seconds, std::string& reason)
{
    return runtime_->replay(input, frame, seconds, reason);
}
}
