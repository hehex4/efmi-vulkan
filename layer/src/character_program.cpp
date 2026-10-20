#include "character_program.h"
#include "../../frontend/xxmi/character_core.h"
#include "json.h"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <sstream>

namespace efvk {
namespace {
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
        std::map<std::wstring, uint32_t> remaps;
        for (uint32_t i = 0; i < package.streams.size(); ++i) {
            auto& s = package.streams[i];
            const auto source = std::filesystem::u8path(config.get(("stream_" + std::to_string(i)).c_str()));
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
        if (!load(ini, core, manifest.parent_path() / "settings", config.get("settings", "mod"),
                  efmi::frontend::character_core(config.get("namespace"), package), factory, reason)) return false;
        catalogue(package, remaps);
        return true;
    } catch (const std::exception& e) { reason = e.what(); return false; }
}
bool CharacterProgram::load(const std::filesystem::path& ini, const std::filesystem::path& core, const std::filesystem::path& settings_folder,
                            const std::string& settings_name, std::shared_ptr<efmi::frontend::RuntimeExtension> extension,
                            const GpuFactory& factory, std::string& reason)
{
    {
        efmi::frontend::RuntimeConfig options{ini.parent_path(), {core / "API.ini", core / "MergedSkeleton.ini", core / "SpatialIdentification.ini", core / "Helpers.ini", ini},
                                             settings_folder, settings_name};
        // The layer answers texture queries of replaced draws (D-077).
        options.texture_identity = true;
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
                if (settings || !_wcsnicmp(name.c_str(), L"DISABLED", 8)) { if (it->is_directory()) it.disable_recursion_pending(); continue; }
                if (!it->is_regular_file() || _wcsicmp(it->path().extension().c_str(), L".ini") || std::filesystem::equivalent(it->path(), ini, error)) continue;
                if (options.inputs.size() >= 64) throw std::runtime_error("The Mod folder has too many INI files for one generation");
                options.inputs.push_back(it->path());
            }
            extension = gpu_->combine(std::move(extension));
            gpu_->begin_deferred();
        }
        runtime_ = efmi::frontend::IniRuntime::load(options, extension, reason);
        if (gpu_) {
            if (!runtime_ && gpu_->scope_active()) gpu_->discard();
            // Recorded work stays pending; replaced draws keep the plan until the first Present.
            if (runtime_ && gpu_->scope_active()) gpu_->suspend();
        }
        return bool(runtime_);
    }
}
void CharacterProgram::catalogue(CharacterPackage& package, const std::map<std::wstring, uint32_t>& remaps)
{
    {
        for (uint32_t i = 0; i < package.images.size(); ++i) images_.emplace(key(std::filesystem::u8path(package.images[i])), i);
        for (const auto& file : runtime_->resources()) {
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
            } else if (!images_.count(key(file->path))) {
                if (package.images.size() >= 64) throw std::runtime_error("Runtime image catalogue exceeds budget");
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
        if (!_wcsnicmp(name.c_str(), L"DISABLED", 8)) { if (it->is_directory(error)) it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(error) || _wcsicmp(it->path().extension().c_str(), L".ini")) continue;
        std::ifstream in(it->path(), std::ios::binary);
        std::string line;
        bool character = false;
        while (!character && std::getline(in, line)) {
            const auto start = line.find_first_not_of(" \t");
            if (start == std::string::npos || line[start] != '[') continue;
            const auto end = line.find(']', start);
            if (end == std::string::npos) continue;
            std::string section = line.substr(start + 1, end - start - 1);
            std::transform(section.begin(), section.end(), section.begin(), ::tolower);
            character = section == "commandlistinitializemergedskeleton";
        }
        if (character) found.push_back(it->path());
    }
    if (error) reason = "cannot read the Mods folder " + mods.u8string() + ": " + error.message();
    std::sort(found.begin(), found.end());
    return found;
}
bool CharacterProgram::discover(const Source& source, CharacterPackage& package, std::string& reason, const GpuFactory& factory)
{
    try {
        if (!source.ini.is_absolute() || !source.core.is_absolute() || !source.settings.is_absolute())
            throw std::runtime_error("Directory generation paths must be absolute");
        if (source.mod_namespace.empty()) throw std::runtime_error("The INI declares no namespace");
        auto found = std::make_shared<efmi::frontend::CharacterDiscovery>();
        if (!load(source.ini, source.core, source.settings, source.mod_namespace,
                  efmi::frontend::character_discovery(source.mod_namespace, found), factory, reason)) return false;
        CharacterPackage made = found->contract;
        made.lists.clear();
        for (auto& c : made.components) {
            c.lists[0] = uint32_t(made.lists.size()); made.lists.push_back({});
            c.lists[1] = uint32_t(made.lists.size()); made.lists.push_back({});
        }
        // The blend-index edit is the one the live plans accept (CharacterProgram::plan).
        made.edits.push_back({2, VK_FORMAT_R8G8B8A8_UINT, VK_FORMAT_R16G16B16A16_UINT});
        std::map<std::wstring, uint32_t> remaps, meshes;
        std::vector<uint8_t> payload;
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
            return uint32_t(made.streams.size() - 1);
        };
        for (size_t i = 0; i < made.components.size(); ++i)
            if (!found->remap_files[i].empty()) {
                const auto k = key(found->remap_files[i]);
                auto it = remaps.find(k);
                if (it == remaps.end()) it = remaps.emplace(k, add(found->remap_files[i], CharacterStream::Remap, 2)).first;
                made.components[i].remap_stream = it->second;
            }
        // Every mesh file the generation declares; those of sections the renderer has no part for
        // are never bound by a replaced draw.
        for (const auto& file : runtime_->resources()) {
            if (!file->usage || remaps.count(key(file->path))) continue;
            const uint32_t kind = file->usage == VK_BUFFER_USAGE_INDEX_BUFFER_BIT ? CharacterStream::Index : CharacterStream::Vertex;
            const auto known = meshes.find(key(file->path));
            if (known != meshes.end()) {
                const auto& s = made.streams[known->second];
                if (s.kind != kind || s.stride != file->stride) throw std::runtime_error("One mesh file declared with two meanings: " + file->path.u8string());
                continue;
            }
            meshes.emplace(key(file->path), add(file->path, kind, file->stride));
        }
        made.payload = std::move(payload);
        made.materials = true;
        if (!validate_refreshed(made, reason)) return false;
        package = std::move(made);
        for (const auto& mesh : meshes) streams_.emplace(mesh.first, mesh.second);
        catalogue(package, remaps);
        left_to_game_ = found->left_to_game;
        return true;
    } catch (const std::exception& e) { reason = e.what(); return false; }
}
bool CharacterProgram::plan(const efmi::frontend::RuntimeDraw& request, CharacterPackage& package, CharacterList& output,
                            bool& skip, bool& added, std::string& reason, const VertexInputRecipe* vertex)
{
    try {
        added = false; efmi::assets::FilePlan plan;
        if (!runtime_->draw(request, plan, reason)) return false;
        skip = plan.skip_original;
        if (!skip) { output = {}; return true; }
        CharacterDraw bound;
        bound.index_stream = kCharacterNone;
        std::map<uint32_t, uint32_t> pixels;
        std::vector<CharacterDraw> draws;
        std::vector<std::vector<CharacterPixel>> material;
        uint32_t required = 0;
        std::vector<uint8_t> signature;
        for (const auto& item : plan.items) {
            using Op = efmi::assets::FileOperation;
            if (item.operation == Op::BindPixelResource) {
                if (item.resource) {
                    const auto found = images_.find(key(item.resource->path));
                    if (found == images_.end()) throw std::runtime_error("INI image not in prepared resource catalogue");
                    pixels[item.slot] = found->second;
                } else pixels[item.slot] = kCharacterNone;
            } else if (item.operation == Op::BindIndex || item.operation == Op::BindVertex) {
                if (!item.resource) {
                    if (item.operation == Op::BindIndex) bound.index_stream = kCharacterNone;
                    else if (item.slot < 4) bound.vb[item.slot] = kCharacterNone;
                    else throw std::runtime_error("Runtime vertex slot outside profile");
                    continue;
                }
                const auto found = streams_.find(key(item.resource->path));
                if (found == streams_.end()) throw std::runtime_error("INI mesh not in prepared stream profile");
                const auto& stream = package.streams[found->second];
                if (item.resource->stride != stream.stride) throw std::runtime_error("Runtime binding stride mismatch");
                if (item.operation == Op::BindIndex) {
                    if (stream.kind != CharacterStream::Index) throw std::runtime_error("Runtime index kind mismatch");
                    bound.index_stream = found->second;
                } else {
                    if (item.slot >= 4 || stream.kind != CharacterStream::Vertex) throw std::runtime_error("Runtime vertex binding outside profile");
                    bound.vb[item.slot] = found->second;
                }
            } else if (item.operation == Op::DrawIndexed) {
                if (draws.size() >= 64 || item.instance_count != 1 || item.first_instance || bound.index_stream == kCharacterNone)
                    throw std::runtime_error("Runtime draw outside the single-instance contract");
                if (item.layout.size() != 1 || item.layout[0].match.input_slot != 2 || item.layout[0].match.semantic_name != "BLENDINDICES" ||
                    item.layout[0].replace.format != 12)
                    throw std::runtime_error("Runtime input-layout edits differ from the validated blend-index contract");
                bound.count = item.index_count; bound.first = item.first_index; bound.base = item.vertex_offset;
                // The stream under the blend-index edit carries the bone indices where the pipeline reads
                // the element the edit widens (R8G8B8A8_UINT in binding 2, then 16-bit indices), and
                // 16-bit weights where it reads R16G16B16A16_UNORM in that binding. Without such an
                // element (offline, or a pipeline that does not read the stream) the packager's rule:
                // indices in the last 8 bytes of an 8- or 16-byte element, weights before them. A stream
                // first seen there is checked once, as decode checks a package's; one that fits neither
                // keeps no bone mask.
                if (bound.vb[2] != kCharacterNone && package.streams[bound.vb[2]].bone_index_offset == kCharacterNone) {
                    const uint32_t stride = package.streams[bound.vb[2]].stride;
                    uint32_t indices = kCharacterNone, weights = kCharacterNone;
                    if (vertex) {
                        for (const auto& a : vertex->attributes) {
                            if (a.binding != 2) continue;
                            if (a.format == VK_FORMAT_R8G8B8A8_UINT) indices = a.offset;
                            else if (a.format == VK_FORMAT_R16G16B16A16_UNORM) weights = a.offset;
                        }
                    }
                    if (indices == kCharacterNone && (stride == 8 || stride == 16)) {
                        indices = stride - 8; weights = stride == 16 ? 0 : kCharacterNone;
                    }
                    std::string why;
                    if (indices != kCharacterNone && !package.adopt_bones(bound.vb[2], indices, weights, why))
                        throw std::runtime_error("Blend stream refused: " + why);
                }
                const auto& index = package.streams[bound.index_stream];
                if (uint64_t(bound.first) + bound.count > index.size / index.stride) throw std::runtime_error("Runtime indexed draw exceeds file bounds");
                const auto extrema = index_bounds_[bound.index_stream];
                for (uint32_t stream : bound.vb) if (stream != kCharacterNone) {
                    const auto& vertex = package.streams[stream];
                    if (int64_t(extrema.first) + bound.base < 0 || int64_t(extrema.second) + bound.base >= int64_t(vertex.size / vertex.stride))
                        throw std::runtime_error("Runtime indexed draw exceeds vertex file bounds");
                    required |= vertex.components;
                }
                draws.push_back(bound); material.emplace_back();
                for (const auto& pixel : pixels) material.back().push_back({pixel.first, pixel.second});
                put(signature, bound.index_stream); put(signature, bound.count); put(signature, bound.first); put(signature, bound.base);
                for (auto stream : bound.vb) put(signature, stream);
                put(signature, uint32_t(pixels.size()));
                for (const auto& pixel : pixels) { put(signature, pixel.first); put(signature, pixel.second); }
            } else throw std::runtime_error("Unresolved runtime native operation");
        }
        const std::string id(signature.begin(), signature.end());
        auto found = lists_.find(id);
        if (found != lists_.end()) { output = found->second; return true; }
        if (lists_.size() >= 1024 || package.draws.size() + draws.size() > 16384) throw std::runtime_error("Runtime draw-plan cache budget exceeded");
        CharacterList result{uint32_t(package.draws.size()), uint32_t(draws.size()), required};
        package.draws.insert(package.draws.end(), draws.begin(), draws.end());
        package.pixels.insert(package.pixels.end(), material.begin(), material.end());
        lists_.emplace(id, result); added = !draws.empty(); output = result; return true;
    } catch (const std::exception& e) { reason = e.what(); return false; }
}
bool CharacterProgram::present(const efmi::frontend::RuntimeInput& input, uint64_t frame, float seconds, std::string& reason)
{
    return runtime_->present(input, frame, seconds, reason);
}
}
