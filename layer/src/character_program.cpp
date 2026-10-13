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
bool CharacterProgram::init(const std::string& path, CharacterPackage& package, std::string& reason)
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
        efmi::frontend::RuntimeConfig options{ini.parent_path(), {core / "API.ini", core / "MergedSkeleton.ini", core / "SpatialIdentification.ini", core / "Helpers.ini", ini},
                                             manifest.parent_path() / "settings", config.get("settings", "mod")};
        runtime_ = efmi::frontend::IniRuntime::load(options, efmi::frontend::character_core(config.get("namespace"), package), reason);
        if (!runtime_) return false;
        for (uint32_t i = 0; i < package.images.size(); ++i) images_.emplace(key(std::filesystem::u8path(package.images[i])), i);
        for (const auto& file : runtime_->resources()) {
            if (file->usage) {
                const auto found = streams_.find(key(file->path));
                if (found == streams_.end()) {
                    if (remaps.count(key(file->path))) continue; // Validated by the native Core palette reader.
                    throw std::runtime_error("Referenced INI mesh is outside the prepared stream profile");
                }
                const auto& stream = package.streams[found->second];
                if (file->stride != stream.stride || (file->usage == VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != (stream.kind == CharacterStream::Index))
                    throw std::runtime_error("Original file declaration differs from the renderer stream profile");
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
        return true;
    } catch (const std::exception& e) { reason = e.what(); return false; }
}
bool CharacterProgram::plan(const efmi::frontend::RuntimeDraw& request, CharacterPackage& package, CharacterList& output,
                            bool& skip, bool& added, std::string& reason)
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
                if (!item.resource) throw std::runtime_error("Explicit null mesh stream is not supported");
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
