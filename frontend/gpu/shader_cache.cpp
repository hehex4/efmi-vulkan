#include "shader_cache.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#pragma comment(lib, "bcrypt.lib")

// The original engine's port (forced into the runtime's sources) answers its own DLL's file name with
// the 3DMigoto folder; the cache's identity needs the files themselves.
#pragma push_macro("GetModuleFileNameW")
#undef GetModuleFileNameW

namespace efmi::gpu {
namespace {
// Bumped whenever the entry layout or what a program holds changes.
constexpr uint32_t kFormat = 1;
constexpr char kMagic[8] = {'E', 'F', 'V', 'K', 'S', 'H', 'D', '1'};
constexpr size_t kMaxEntry = 160u << 20, kMaxSource = 64u << 20, kMaxCount = 1u << 20;

// SHA-256 through the system's CNG provider, opened once per process.
class Digest {
public:
    Digest()
    {
        static const BCRYPT_ALG_HANDLE algorithm = [] {
            BCRYPT_ALG_HANDLE handle = nullptr;
            return BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 ? handle : nullptr;
        }();
        if (!algorithm || BCryptCreateHash(algorithm, &hash_, nullptr, 0, nullptr, 0, 0) < 0) throw std::runtime_error("SHA-256 unavailable");
    }
    ~Digest() { if (hash_) BCryptDestroyHash(hash_); }
    Digest(const Digest&) = delete;
    Digest& operator=(const Digest&) = delete;
    Digest& add(const void* data, size_t size)
    {
        auto* bytes = static_cast<const uint8_t*>(data);
        while (size) {
            const ULONG part = ULONG(std::min<size_t>(size, 1u << 30));
            if (BCryptHashData(hash_, const_cast<PUCHAR>(bytes), part, 0) < 0) throw std::runtime_error("SHA-256 failed");
            bytes += part; size -= part;
        }
        return *this;
    }
    // Length-prefixed, so that no two sequences of fields hash alike.
    Digest& text(const std::string& value) { const uint64_t size = value.size(); add(&size, 8); return add(value.data(), value.size()); }
    Digest& word(uint64_t value) { return add(&value, 8); }
    std::string hex()
    {
        unsigned char out[32];
        if (BCryptFinishHash(hash_, out, sizeof(out), 0) < 0) throw std::runtime_error("SHA-256 failed");
        static const char digits[] = "0123456789abcdef";
        std::string result;
        for (unsigned char c : out) { result += digits[c >> 4]; result += digits[c & 15]; }
        return result;
    }
private:
    BCRYPT_HASH_HANDLE hash_ = nullptr;
};

bool read_file(const std::filesystem::path& path, std::vector<uint8_t>& bytes, size_t limit)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > limit) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    bytes.resize(size_t(size));
    in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    return bool(in) || bytes.empty();
}

// The compiler libraries, the system's FXC (the reference bytecode and reflection come from it) and
// the module this code is part of, by path, size and write time: a new build or another tool
// version keys every entry differently.
std::string identity_of(const std::vector<std::filesystem::path>& tools)
{
    std::vector<std::filesystem::path> files = tools;
    for (const wchar_t* library : {L"d3dcompiler_47.dll"})
        if (const HMODULE loaded = GetModuleHandleW(library)) {
            std::wstring path(32768, L'\0');
            const DWORD length = GetModuleFileNameW(loaded, path.data(), DWORD(path.size()));
            if (length && length < path.size()) { path.resize(length); files.push_back(path); }
        }
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&serialize_program), &self))
        throw std::runtime_error("Shader cache cannot name its own module");
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(self, path.data(), DWORD(path.size()));
    if (!length || length >= path.size()) throw std::runtime_error("Shader cache cannot name its own module");
    path.resize(length);
    files.push_back(path);
    Digest digest;
    digest.word(kFormat);
    for (const auto& file : files) {
        std::error_code error;
        const auto size = std::filesystem::file_size(file, error);
        if (error) throw std::runtime_error("Shader cache tool missing: " + file.u8string());
        const auto time = std::filesystem::last_write_time(file, error);
        if (error) throw std::runtime_error("Shader cache tool unreadable: " + file.u8string());
        digest.text(std::filesystem::absolute(file).lexically_normal().u8string()).word(size).word(uint64_t(time.time_since_epoch().count()));
    }
    return digest.hex();
}

struct Writer {
    std::vector<uint8_t> out;
    void bytes(const void* data, size_t size) { const auto* p = static_cast<const uint8_t*>(data); out.insert(out.end(), p, p + size); }
    void u32(uint32_t value) { bytes(&value, 4); }
    void u64(uint64_t value) { bytes(&value, 8); }
    void text(const std::string& value) { u64(value.size()); bytes(value.data(), value.size()); }
    template <class T> void blob(const std::vector<T>& values) { u64(values.size()); bytes(values.data(), values.size() * sizeof(T)); }
};
struct Reader {
    const uint8_t* p; size_t left; bool ok = true;
    bool bytes(void* data, size_t size)
    {
        if (!ok || size > left) { ok = false; return false; }
        if (size) std::memcpy(data, p, size);
        p += size; left -= size; return true;
    }
    uint32_t u32() { uint32_t value = 0; bytes(&value, 4); return value; }
    uint64_t u64() { uint64_t value = 0; bytes(&value, 8); return value; }
    size_t count(size_t element)
    {
        const uint64_t n = u64();
        if (!ok || n > kMaxCount * 64 || (element && n > left / element)) { ok = false; return 0; }
        return size_t(n);
    }
    std::string text() { std::string value(count(1), '\0'); bytes(value.data(), value.size()); return value; }
    template <class T> void blob(std::vector<T>& values) { values.resize(count(sizeof(T))); bytes(values.data(), values.size() * sizeof(T)); }
};

void write_signatures(Writer& w, const std::vector<ShaderSignature>& list)
{
    w.u64(list.size());
    for (const auto& s : list) {
        w.text(s.semantic);
        for (uint32_t v : {s.index, s.register_index, s.component_type, s.system_value, s.stream, s.mask, s.read_write_mask}) w.u32(v);
    }
}
void read_signatures(Reader& r, std::vector<ShaderSignature>& list)
{
    list.resize(r.count(4));
    for (auto& s : list) {
        s.semantic = r.text();
        for (uint32_t* v : {&s.index, &s.register_index, &s.component_type, &s.system_value, &s.stream, &s.mask, &s.read_write_mask}) *v = r.u32();
    }
}
void write_interfaces(Writer& w, const std::vector<ShaderInterface>& list)
{
    w.u64(list.size());
    for (const auto& s : list) { w.text(s.name); w.text(s.semantic); w.u32(s.location); w.u32(s.builtin); w.u32(uint32_t(s.format)); w.u32(s.spirv_id); }
}
void read_interfaces(Reader& r, std::vector<ShaderInterface>& list)
{
    list.resize(r.count(4));
    for (auto& s : list) { s.name = r.text(); s.semantic = r.text(); s.location = r.u32(); s.builtin = r.u32(); s.format = VkFormat(r.u32()); s.spirv_id = r.u32(); }
}
} // namespace

std::vector<uint8_t> serialize_program(const HlslProgram& p)
{
    Writer w;
    w.u32(uint32_t(p.stage)); w.u32(p.vulkan_version);
    w.blob(p.capabilities); w.blob(p.spirv); w.blob(p.dxbc);
    w.u64(p.bindings.size());
    for (const auto& b : p.bindings) {
        w.text(b.name); w.u32(uint32_t(uint8_t(b.source_type)));
        for (uint32_t v : {b.slot, b.count, b.binding, uint32_t(b.type), b.image_dimension, b.image_format, b.image_depth, b.block_size, b.stride,
                           b.scalar_type, b.scalar_width, uint32_t(b.image_array), uint32_t(b.image_multisample), uint32_t(b.counter),
                           uint32_t(b.source_buffer), b.source_stride})
            w.u32(v);
    }
    write_interfaces(w, p.inputs); write_interfaces(w, p.outputs);
    write_signatures(w, p.input_signature); write_signatures(w, p.output_signature); write_signatures(w, p.patch_signature);
    w.u64(p.dependencies.size());
    for (const auto& d : p.dependencies) { w.text(d.path.u8string()); w.blob(d.bytes); }
    for (uint32_t v : p.threads) w.u32(v);
    w.text(p.diagnostics);
    return std::move(w.out);
}

bool deserialize_program(const uint8_t* data, size_t size, HlslProgram& out)
{
    Reader r{data, size};
    HlslProgram p;
    p.stage = VkShaderStageFlagBits(r.u32()); p.vulkan_version = r.u32();
    r.blob(p.capabilities); r.blob(p.spirv); r.blob(p.dxbc);
    p.bindings.resize(r.count(4));
    for (auto& b : p.bindings) {
        b.name = r.text(); b.source_type = char(r.u32());
        b.slot = r.u32(); b.count = r.u32(); b.binding = r.u32(); b.type = VkDescriptorType(r.u32());
        b.image_dimension = r.u32(); b.image_format = r.u32(); b.image_depth = r.u32(); b.block_size = r.u32(); b.stride = r.u32();
        b.scalar_type = r.u32(); b.scalar_width = r.u32(); b.image_array = r.u32() != 0; b.image_multisample = r.u32() != 0;
        b.counter = r.u32() != 0; b.source_buffer = ShaderBufferKind(r.u32()); b.source_stride = r.u32();
    }
    read_interfaces(r, p.inputs); read_interfaces(r, p.outputs);
    read_signatures(r, p.input_signature); read_signatures(r, p.output_signature); read_signatures(r, p.patch_signature);
    p.dependencies.resize(r.count(8));
    for (auto& d : p.dependencies) { d.path = std::filesystem::u8path(r.text()); r.blob(d.bytes); }
    for (auto& v : p.threads) v = r.u32();
    p.diagnostics = r.text();
    if (!r.ok || r.left || p.spirv.empty()) return false;
    out = std::move(p);
    return true;
}

ShaderCache::ShaderCache(std::filesystem::path folder, const std::vector<std::filesystem::path>& tools)
    : folder_(std::move(folder)), identity_(identity_of(tools))
{
    static std::mutex mutex;
    static std::set<std::wstring> pruned;
    std::lock_guard<std::mutex> lock(mutex);
    if (pruned.insert(std::filesystem::absolute(folder_).lexically_normal().wstring()).second) prune();
}

void ShaderCache::prune()
{
    std::error_code error;
    const auto now = std::filesystem::file_time_type::clock::now();
    for (std::filesystem::directory_iterator it(folder_, error), end; !error && it != end; it.increment(error)) {
        const auto& path = it->path();
        if (!it->is_regular_file(error)) continue;
        bool remove = false;
        if (path.extension() == L".tmp") {
            // A writer's temporary file: one an hour old is left from a process that ended mid-write.
            const auto written = std::filesystem::last_write_time(path, error);
            remove = !error && now - written > std::chrono::hours(1);
        } else if (path.extension() == L".efvkshader") {
            char header[12 + 64] = {};
            std::ifstream in(path, std::ios::binary);
            in.read(header, sizeof(header));
            uint32_t format = 0;
            std::memcpy(&format, header + 8, 4);
            remove = !in || std::memcmp(header, kMagic, sizeof(kMagic)) || format != kFormat || std::memcmp(header + 12, identity_.data(), 64);
        }
        if (remove && std::filesystem::remove(path, error)) ++stats_.removed;
    }
}

std::string ShaderCache::hlsl_key(const HlslRequest& request) const
{
    std::vector<uint8_t> source;
    if (!read_file(request.source, source, kMaxSource)) return {};
    Digest digest;
    digest.text(identity_).text("hlsl").word(uint8_t(request.stage)).word(request.flags).word(request.vulkan_version)
        .word(uint64_t(int64_t(request.recursive_include)))
        .text(std::filesystem::absolute(request.working_directory).lexically_normal().u8string())
        .text(std::filesystem::absolute(request.source).lexically_normal().u8string())
        .word(source.size()).add(source.data(), source.size());
    return digest.hex();
}

std::string ShaderCache::bytecode_key(const std::vector<uint8_t>& bytecode, char stage, uint32_t mode) const
{
    Digest digest;
    digest.text(identity_).text("dxbc").word(uint8_t(stage)).word(mode).word(bytecode.size()).add(bytecode.data(), bytecode.size());
    return digest.hex();
}

bool ShaderCache::load(const std::string& key, HlslProgram& out)
{
    std::vector<uint8_t> entry;
    if (key.empty() || !read_file(folder_ / (key + ".efvkshader"), entry, kMaxEntry)) { ++stats_.misses; return false; }
    const size_t header = sizeof(kMagic) + 4 + 64 + 64 + 8;
    uint32_t format = 0; uint64_t payload = 0;
    if (entry.size() >= header) { std::memcpy(&format, entry.data() + 8, 4); std::memcpy(&payload, entry.data() + header - 8, 8); }
    HlslProgram program;
    if (entry.size() < header || std::memcmp(entry.data(), kMagic, sizeof(kMagic)) || format != kFormat ||
        std::memcmp(entry.data() + 12, identity_.data(), 64) || std::memcmp(entry.data() + 76, key.data(), 64) ||
        payload != entry.size() - header || !deserialize_program(entry.data() + header, size_t(payload), program)) {
        ++stats_.stale; return false;
    }
    // The includes as the compiler read them: any that changed or went makes the entry stale.
    std::vector<uint8_t> now;
    for (const auto& dependency : program.dependencies)
        if (!read_file(dependency.path, now, kMaxSource) || now != dependency.bytes) { ++stats_.stale; return false; }
    out = std::move(program);
    ++stats_.hits;
    return true;
}

void ShaderCache::store(const std::string& key, const HlslProgram& program)
{
    if (key.size() != 64 || identity_.size() != 64) return;
    try {
        const auto payload = serialize_program(program);
        Writer w;
        w.bytes(kMagic, sizeof(kMagic)); w.u32(kFormat); w.bytes(identity_.data(), 64); w.bytes(key.data(), 64); w.u64(payload.size());
        w.bytes(payload.data(), payload.size());
        if (w.out.size() > kMaxEntry) return;
        std::error_code error;
        std::filesystem::create_directories(folder_, error);
        const auto target = folder_ / (key + ".efvkshader");
        const auto temporary = folder_ / (key + "." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(GetCurrentThreadId()) + ".tmp");
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(w.out.data()), std::streamsize(w.out.size()));
            if (!file) { file.close(); std::filesystem::remove(temporary, error); return; }
        }
        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) { std::filesystem::remove(temporary, error); return; }
        ++stats_.stored;
    } catch (const std::exception&) {}
}
} // namespace efmi::gpu
#pragma pop_macro("GetModuleFileNameW")
