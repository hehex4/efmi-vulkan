// Logging, configuration and CRC-32C.
#include "common.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <map>
#include <mutex>
#include <set>
#include <intrin.h>
#include <nmmintrin.h>
#include <smmintrin.h>

namespace efvk {

Config g_cfg;
AttachInfo g_attach;

static std::wstring g_module_dir;
static std::string g_exe_name;
static std::mutex g_log_mx;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static bool g_hw_crc = false;
static uint32_t g_crc_table[8][256];

bool streaming_reads_available()
{
    static const bool available = [] {
        int cpu[4];
        __cpuid(cpu, 1);
        return (cpu[2] & (1 << 19)) != 0; // SSE4.1, independently of CRC's SSE4.2
    }();
    return available;
}

bool mapped_range_is_write_combined(const void* data, size_t size)
{
    if (!data || !size) return false;
    MEMORY_BASIC_INFORMATION info = {};
    if (VirtualQuery(data, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
        !(info.Protect & PAGE_WRITECOMBINE) || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return false;
    const auto offset = reinterpret_cast<uintptr_t>(data) - reinterpret_cast<uintptr_t>(info.BaseAddress);
    return offset <= info.RegionSize && size <= info.RegionSize - offset;
}

// MOVNTDQA can collect adjacent reads from WC memory in a streaming-load buffer.
// Align only within the requested range; never round a read past either end.
// MFENCE orders the WC accesses before the CPU shadow is published. This does not
// replace the application's Vulkan synchronization or noncoherent cache management.
__declspec(noinline) static void copy_write_combined(void* dst, const void* src, size_t size)
{
    auto* d = static_cast<uint8_t*>(dst);
    auto* s = static_cast<const uint8_t*>(src);
    _mm_mfence();
    while (size && (reinterpret_cast<uintptr_t>(s) & 15)) { *d++ = *s++; --size; }
    // Finish a partial streaming line before the four-load body.
    while (size >= 16 && (reinterpret_cast<uintptr_t>(s) & 63)) {
        const auto a = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<uint8_t*>(s)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), a);
        s += 16; d += 16; size -= 16;
    }
    while (size >= 64) {
        const auto a = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<uint8_t*>(s)));
        const auto b = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<uint8_t*>(s + 16)));
        const auto c = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<uint8_t*>(s + 32)));
        const auto e = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<uint8_t*>(s + 48)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), a);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 16), b);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 32), c);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 48), e);
        s += 64; d += 64; size -= 64;
    }
    while (size >= 16) {
        const auto a = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<uint8_t*>(s)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), a);
        s += 16; d += 16; size -= 16;
    }
    while (size--) *d++ = *s++;
    _mm_mfence();
}

// A hardware fault anywhere in the process is logged with its module and offset and the first
// frames of its stack (module+offset, symbolized offline with the build's PDBs), then left to the
// handlers the process has, as before. The first eight distinct fault addresses: an application that
// raises the same handled fault again and again (the game does at start-up) does not use the log up. The game's crash of BUG-046 (inside DXC) would have named the module.
namespace {
void module_offset(const void* address, char* out, size_t size)
{
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module) && module) {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(module, path, MAX_PATH);
        const wchar_t* name = wcsrchr(path, L'\\'); name = name ? name + 1 : path;
        snprintf(out, size, "%ls+0x%llx", name, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module)));
    } else snprintf(out, size, "%p", address);
}
// Whether an address is in a file of this DLL's folder.
bool own_code(const void* address)
{
    static const std::wstring own = [] {
        HMODULE self = nullptr;
        wchar_t path[MAX_PATH] = {};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&own_code), &self) && self)
            GetModuleFileNameW(self, path, MAX_PATH);
        std::wstring folder = path;
        return folder.substr(0, folder.find_last_of(L'\\') + 1);
    }();
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = {};
    if (own.empty() || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           reinterpret_cast<LPCWSTR>(address), &module) || !module)
        return false;
    GetModuleFileNameW(module, path, MAX_PATH);
    const std::wstring file = path;
    return file.size() > own.size() && !_wcsnicmp(file.c_str(), own.c_str(), own.size()) && file.find(L'\\', own.size()) == std::wstring::npos;
}
LONG WINAPI crash_logger(EXCEPTION_POINTERS* info)
{
    const auto& record = *info->ExceptionRecord;
    const DWORD code = record.ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_IN_PAGE_ERROR)
        return EXCEPTION_CONTINUE_SEARCH;
    static std::mutex seen_mx;
    static void* seen[8]; static int seen_count = 0;
    {
        std::lock_guard<std::mutex> lock(seen_mx);
        for (int i = 0; i < seen_count; ++i) if (seen[i] == record.ExceptionAddress) return EXCEPTION_CONTINUE_SEARCH;
        if (seen_count >= 8) return EXCEPTION_CONTINUE_SEARCH;
        seen[seen_count++] = record.ExceptionAddress;
    }
    char where[300];
    module_offset(record.ExceptionAddress, where, sizeof(where));
    // In a file of the layer's own folder (the layer, its module, its GPU tools) a fault is ours and a crash;
    // anywhere else it is the process's, seen first: the game raises one at start-up and handles it.
    const char* const kind = own_code(record.ExceptionAddress) ? "crash" : "exception";
    const char* const note = own_code(record.ExceptionAddress) ? "" : " (first chance; the process may handle it)";
    if (code == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
        logf("%s: access violation %s address %p at %s, thread %lu%s", kind, record.ExceptionInformation[0] == 1 ? "writing" : record.ExceptionInformation[0] == 8 ? "executing" : "reading",
             reinterpret_cast<void*>(record.ExceptionInformation[1]), where, GetCurrentThreadId(), note);
    else logf("%s: exception 0x%08lx at %s, thread %lu%s", kind, code, where, GetCurrentThreadId(), note);
    void* frames[24] = {};
    const USHORT count = CaptureStackBackTrace(0, 24, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) { module_offset(frames[i], where, sizeof(where)); logf("%s: frame %u: %s", kind, i, where); }
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

bool live_gpu_bones() { return g_cfg.m2_gpu_bones; }

void install_crash_log()
{
    static bool installed = false;
    if (installed) return;
    installed = true;
    AddVectoredExceptionHandler(1, crash_logger);
}

void copy_buffer_bytes(void* dst, const void* src, size_t size, bool write_combined)
{
    if (!size) return;
    if (write_combined && size >= 64 && streaming_reads_available())
        copy_write_combined(dst, src, size);
    else
        memmove(dst, src, size);
}

static std::string env(const char* name)
{
    char buf[1024];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
        return std::string();
    return std::string(buf, n);
}

// Settings file next to the DLL: efmi_vk.cfg with "key = value" lines, '#' starts a comment.
// A process that is started by a launcher cannot easily be given environment variables, so the
// file is the way to configure the DLL there. An environment variable wins over the file.
static std::map<std::string, std::string> g_file_settings;

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static void read_settings_file(const std::wstring& path)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    std::string text;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(f, buf, sizeof(buf), &got, nullptr) && got && text.size() < (1u << 16))
        text.append(buf, got);
    CloseHandle(f);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        size_t hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = trim(line.substr(0, eq));
        if (!key.empty())
            g_file_settings[key] = trim(line.substr(eq + 1));
    }
}

static std::string setting(const char* env_name, const char* key)
{
    std::string v = env(env_name);
    if (!v.empty())
        return v;
    auto it = g_file_settings.find(key);
    return it == g_file_settings.end() ? std::string() : it->second;
}

static void crc_init()
{
    const uint32_t poly = 0x82F63B78u;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (c >> 1) ^ poly : c >> 1;
        g_crc_table[0][n] = c;
    }
    for (int t = 1; t < 8; t++)
        for (uint32_t n = 0; n < 256; n++)
            g_crc_table[t][n] = (g_crc_table[t - 1][n] >> 8) ^ g_crc_table[0][g_crc_table[t - 1][n] & 0xFF];

    int info[4] = {0, 0, 0, 0};
    __cpuid(info, 1);
    g_hw_crc = (info[2] & (1 << 20)) != 0; // SSE4.2
}

bool crc32c_uses_hardware()
{
    return g_hw_crc;
}

uint32_t crc32c(uint32_t seed, const void* data, size_t size)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = ~seed;
    if (g_hw_crc) {
        uint64_t c = crc;
        while (size >= 8) {
            uint64_t v;
            memcpy(&v, p, 8);
            c = _mm_crc32_u64(c, v);
            p += 8;
            size -= 8;
        }
        crc = static_cast<uint32_t>(c);
        while (size--)
            crc = _mm_crc32_u8(crc, *p++);
        return ~crc;
    }
    while (size >= 8) {
        uint32_t a, b;
        memcpy(&a, p, 4);
        memcpy(&b, p + 4, 4);
        a ^= crc;
        crc = g_crc_table[7][a & 0xFF] ^ g_crc_table[6][(a >> 8) & 0xFF] ^ g_crc_table[5][(a >> 16) & 0xFF] ^
              g_crc_table[4][a >> 24] ^ g_crc_table[3][b & 0xFF] ^ g_crc_table[2][(b >> 8) & 0xFF] ^
              g_crc_table[1][(b >> 16) & 0xFF] ^ g_crc_table[0][b >> 24];
        p += 8;
        size -= 8;
    }
    while (size--)
        crc = (crc >> 8) ^ g_crc_table[0][(crc ^ *p++) & 0xFF];
    return ~crc;
}

const std::wstring& module_dir()
{
    return g_module_dir;
}

const std::string& exe_name()
{
    return g_exe_name;
}

void logf(const char* fmt, ...)
{
    char body[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n >= static_cast<int>(sizeof(body)))
        n = sizeof(body) - 1;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[2200];
    int m = snprintf(line, sizeof(line), "%02u:%02u:%02u.%03u [%lu:%lu] %.*s\r\n", st.wHour, st.wMinute, st.wSecond,
                     st.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId(), n, body);
    if (m <= 0)
        return;
    if (m >= static_cast<int>(sizeof(line)))
        m = sizeof(line) - 1;

    std::lock_guard<std::mutex> lock(g_log_mx);
    if (g_log == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    SetFilePointer(g_log, 0, nullptr, FILE_END);
    WriteFile(g_log, line, static_cast<DWORD>(m), &written, nullptr);
}

namespace {
struct NoticeEntry {
    NoticeLevel level;
    std::string text;
    uint64_t shown_ms = 0; // the tick of its first showing; 0 until then
};
std::mutex g_notice_mx;
std::vector<NoticeEntry> g_notices;
std::set<std::string> g_notice_keys;
uint64_t g_notice_version = 1;
std::vector<std::string> g_notice_recent; // the last texts queued, for the status
uint64_t g_notice_count = 0;
// Overlay.cpp log_levels: DIRE 20 s, WARNING 10 s, NOTICE 5 s, INFO 2 s; MAX_SIMULTANEOUS_NOTICES 10.
const uint64_t kNoticeMs[] = {20000, 10000, 5000, 2000};
const size_t kNoticesShown = 10;
} // namespace

void notice(NoticeLevel level, const char* key, const char* text)
{
    if (!text) return;
    const int index = static_cast<int>(level);
    if (index < 0 || index > 3) return;
    {
        std::lock_guard<std::mutex> lock(g_notice_mx);
        if (key && *key && !g_notice_keys.insert(key).second) return;
        // Notices wait for their turn without a bound in the original; a burst keeps the latest.
        if (g_notices.size() >= 256) g_notices.erase(g_notices.begin());
        g_notices.push_back({level, text});
        ++g_notice_version; ++g_notice_count;
        if (g_notice_recent.size() >= 8) g_notice_recent.erase(g_notice_recent.begin());
        g_notice_recent.push_back(text);
    }
    logf("notice: %s", text);
}

uint64_t notices_queued(std::vector<std::string>& recent)
{
    std::lock_guard<std::mutex> lock(g_notice_mx);
    recent = g_notice_recent;
    return g_notice_count;
}

bool notices_visible(std::vector<NoticeLine>& lines, uint64_t& version)
{
    const uint64_t now = GetTickCount64();
    std::lock_guard<std::mutex> lock(g_notice_mx);
    const size_t before = g_notices.size();
    g_notices.erase(std::remove_if(g_notices.begin(), g_notices.end(), [now](const NoticeEntry& e) {
                        return e.shown_ms && now - e.shown_ms > kNoticeMs[static_cast<int>(e.level)];
                    }), g_notices.end());
    if (g_notices.size() != before) ++g_notice_version;
    if (version == g_notice_version) return false;
    lines.clear();
    for (int level = 0; level < 4; ++level)
        for (auto& e : g_notices) {
            if (static_cast<int>(e.level) != level || lines.size() >= kNoticesShown) continue;
            if (!e.shown_ms) e.shown_ms = now;
            lines.push_back({e.level, e.text});
        }
    version = g_notice_version;
    return true;
}

void util_init(HMODULE self)
{
    crc_init();

    wchar_t path[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(self, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    std::wstring full(path, n);
    size_t slash = full.find_last_of(L"\\/");
    g_module_dir = slash == std::wstring::npos ? L"." : full.substr(0, slash);

    char exe[MAX_PATH * 4];
    DWORD en = GetModuleFileNameA(nullptr, exe, sizeof(exe));
    std::string exe_full(exe, en);
    size_t es = exe_full.find_last_of("\\/");
    g_exe_name = es == std::string::npos ? exe_full : exe_full.substr(es + 1);

    read_settings_file(g_module_dir + L"\\efmi_vk.cfg");

    g_cfg.m2_package = setting("EFMI_VK_M2_PACKAGE", "m2_package");
    if (g_cfg.m2_package == "off") g_cfg.m2_package.clear();
    g_cfg.m2_mods = setting("EFMI_VK_M2_MODS", "m2_mods");
    if (g_cfg.m2_mods == "off") g_cfg.m2_mods.clear();
    g_cfg.m2_core = setting("EFMI_VK_M2_CORE", "m2_core");
    g_cfg.m2_settings = setting("EFMI_VK_M2_SETTINGS", "m2_settings");
    {
        const std::string zero = setting("EFMI_VK_M2_DIMENSION_ZERO", "m2_dimension_zero");
        unsigned vendor = 0, device = 0;
        if (sscanf(zero.c_str(), "%u:%u", &vendor, &device) == 2) {
            g_cfg.m2_dimension_zero_vendor = vendor; g_cfg.m2_dimension_zero_device = device;
        }
    }
    g_cfg.m2_owned = setting("EFMI_VK_M2_OWNED", "m2_owned") != "0";
    g_cfg.m2_folder_generation = setting("EFMI_VK_M2_FOLDER_GENERATION", "m2_folder_generation") != "0";
    g_cfg.m2_reserve_mb = strtoull(setting("EFMI_VK_M2_RESERVE_MB", "m2_reserve_mb").c_str(), nullptr, 10);
    {
        const std::string expiry = setting("EFMI_VK_M2_IDENTITY_EXPIRY_MS", "m2_identity_expiry_ms");
        if (!expiry.empty()) g_cfg.m2_identity_expiry_ms = strtoull(expiry.c_str(), nullptr, 10);
        const std::string save = setting("EFMI_VK_SETTINGS_SAVE_SECONDS", "settings_save_seconds");
        if (!save.empty()) g_cfg.settings_save_seconds = strtoull(save.c_str(), nullptr, 10);
        g_cfg.shader_cache = setting("EFMI_VK_SHADER_CACHE", "shader_cache") != "0";
    }
    g_cfg.m2_texture_budget_mb = strtoull(setting("EFMI_VK_M2_TEXTURE_BUDGET_MB", "m2_texture_budget_mb").c_str(), nullptr, 10);
    {
        const std::string reduce = setting("EFMI_VK_M2_TEXTURE_REDUCE_MB", "m2_texture_reduce_mb");
        if (!reduce.empty()) g_cfg.m2_texture_reduce_mb = strtoull(reduce.c_str(), nullptr, 10);
    }
    g_cfg.m2_texture_preload = setting("EFMI_VK_M2_TEXTURE_PRELOAD", "m2_texture_preload") == "1";
    g_cfg.m2_pageable = setting("EFMI_VK_M2_PAGEABLE", "m2_pageable") != "0";
    g_cfg.m2_gpu_bones = setting("EFMI_VK_M2_GPU_BONES", "m2_gpu_bones") != "0";
    std::string m2_enabled = setting("EFMI_VK_M2_ENABLED", "m2_enabled");
    g_cfg.m2_enabled = m2_enabled == "1" || m2_enabled == "on";

    std::string mode = setting("EFMI_VK_MODE", "mode");
    if (mode == "passthrough")
        g_cfg.mode = Mode::Passthrough;
    else
        g_cfg.mode = Mode::Probe;

    // The file name decides the entry: the Vulkan loader is pointed at efmi_vk_layer.dll by the
    // manifest, an injector is pointed at efmi_vk_inject.dll. EFMI_VK_ENTRY overrides it.
    std::wstring file = slash == std::wstring::npos ? full : full.substr(slash + 1);
    for (wchar_t& ch : file)
        ch = static_cast<wchar_t>(towlower(ch));
    g_cfg.entry = file.find(L"inject") != std::wstring::npos ? Entry::Inject : Entry::Layer;
    std::string entry = setting("EFMI_VK_ENTRY", "entry");
    if (entry == "inject")
        g_cfg.entry = Entry::Inject;
    else if (entry == "layer")
        g_cfg.entry = Entry::Layer;

    // Index buffers get a shadow by default; EFMI_VK_SHADOW_USAGE overrides the mask (hex).
    g_cfg.shadow_usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    std::string usage = setting("EFMI_VK_SHADOW_USAGE", "shadow_usage");
    if (!usage.empty())
        g_cfg.shadow_usage = static_cast<uint32_t>(strtoul(usage.c_str(), nullptr, 16));

    std::string limit = setting("EFMI_VK_UNMAP_SNAPSHOT_LIMIT", "unmap_snapshot_limit");
    if (!limit.empty())
        g_cfg.unmap_snapshot_limit = strtoull(limit.c_str(), nullptr, 10);

    std::string resources = setting("EFMI_VK_RESOURCES", "resources");
    if (resources == "0" || resources == "off")
        g_cfg.track_resources = false;

    std::string budget = setting("EFMI_VK_SHADER_BUDGET", "shader_budget");
    if (!budget.empty())
        g_cfg.shader_budget = strtoull(budget.c_str(), nullptr, 10);

    std::string image_hash = setting("EFMI_VK_IMAGE_HASH", "image_hash");
    if (image_hash == "0" || image_hash == "off")
        g_cfg.image_hash = false;
    std::string image_limit = setting("EFMI_VK_IMAGE_HASH_LIMIT", "image_hash_limit");
    if (!image_limit.empty())
        g_cfg.image_hash_limit = strtoull(image_limit.c_str(), nullptr, 10);
    std::string profile = setting("EFMI_VK_PROFILE_TRACKER", "profile_tracker");
    g_cfg.profile_tracker = profile == "1" || profile == "on";
    std::string snapshot_all = setting("EFMI_VK_UNMAP_SNAPSHOT_ALL", "unmap_snapshot_all");
    if (snapshot_all == "0" || snapshot_all == "off")
        g_cfg.unmap_snapshot_all = false;
    else if (snapshot_all == "1" || snapshot_all == "on")
        g_cfg.unmap_snapshot_all = true;

    std::wstring log_path;
    wchar_t wbuf[1024];
    DWORD wn = GetEnvironmentVariableW(L"EFMI_VK_LOG", wbuf, 1024);
    auto file_log = g_file_settings.find("log");
    if (wn > 0 && wn < 1024) {
        log_path.assign(wbuf, wn);
    } else if (file_log != g_file_settings.end() && !file_log->second.empty()) {
        int len = MultiByteToWideChar(CP_UTF8, 0, file_log->second.c_str(), -1, wbuf, 1024);
        if (len > 1)
            log_path.assign(wbuf, static_cast<size_t>(len) - 1);
    }
    if (log_path.empty())
        log_path = g_module_dir + L"\\efmi_vk_layer.log";
    g_log = CreateFileW(log_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    install_crash_log();

#ifdef EFVK_NO_PROBE
    const char* const build_kind = " (release, no probe)";
#else
    const char* const build_kind = "";
#endif
    logf("efmi_vk_layer %s%s loaded into %s, mode=%s, entry=%s, crc=%s, resources=%s, shader_budget=%llu, image_hash=%s, profile_tracker=%s, "
         "unmap_snapshot=%s up to %llu",
         EFVK_VERSION, build_kind, g_exe_name.c_str(), g_cfg.mode == Mode::Passthrough ? "passthrough" : "probe",
         g_cfg.entry == Entry::Inject ? "inject" : "layer", g_hw_crc ? "sse4.2" : "table",
         g_cfg.track_resources ? "on" : "off", static_cast<unsigned long long>(g_cfg.shader_budget),
         g_cfg.image_hash ? "on" : "off", g_cfg.profile_tracker ? "on" : "off",
         g_cfg.unmap_snapshot_all ? "all sources" : "staging only", static_cast<unsigned long long>(g_cfg.unmap_snapshot_limit));
}

} // namespace efvk
