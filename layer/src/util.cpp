// Logging, configuration and CRC-32C.
#include "common.h"

#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <map>
#include <mutex>
#include <intrin.h>
#include <nmmintrin.h>

namespace efvk {

Config g_cfg;
AttachInfo g_attach;

static std::wstring g_module_dir;
static std::string g_exe_name;
static std::mutex g_log_mx;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static bool g_hw_crc = false;
static uint32_t g_crc_table[8][256];

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

    logf("efmi_vk_layer %s loaded into %s, mode=%s, entry=%s, crc=%s, resources=%s, shader_budget=%llu", EFVK_VERSION,
         g_exe_name.c_str(), g_cfg.mode == Mode::Passthrough ? "passthrough" : "probe",
         g_cfg.entry == Entry::Inject ? "inject" : "layer", g_hw_crc ? "sse4.2" : "table",
         g_cfg.track_resources ? "on" : "off", static_cast<unsigned long long>(g_cfg.shader_budget));
}

} // namespace efvk
