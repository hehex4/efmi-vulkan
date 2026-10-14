// CPU prototype only. Unsupported graphics integration fails loudly if reached.
#include "CommandList.h"
#include "globals.h"
#include "HackerDevice.h"
#include "HackerContext.h"
#include "ShaderRegex.h"
#include "Hunting.h"
#include "Overlay.h"
#include <cstdarg>
#include <cstdio>
#include <stdexcept>

[[noreturn]] static void unsupported(const char* call) { throw std::runtime_error(call); }
HINSTANCE migoto_handle=nullptr;
CRITICAL_SECTION resource_creation_mode_lock;
DWORD tls_idx=TLS_OUT_OF_INDEXES;
static std::wstring resource_root;
static size_t diagnostic_count=0;
size_t HostDiagnosticCount() { return diagnostic_count; }
void HostResourceRoot(const std::wstring& root)
{
    if(root.empty() || root.size()+12>=MAX_PATH)unsupported("prototype resource root too long");
    resource_root=root;
    if(resource_root.back()!=L'\\')resource_root+=L'\\';
    // Only the reference's local GetModuleFileName adapter reads this token.
    migoto_handle=reinterpret_cast<HINSTANCE>(1);
}
namespace MigotoPort {
void MigotoDir(wchar_t* out,unsigned long size) {
    if(resource_root.empty() || resource_root.size()+1>size)unsupported("prototype module directory adapter");
    wcscpy_s(out,size,resource_root.c_str());
}
Dx12Context* Context() { unsupported("prototype graphics context adapter"); }
Dx12Device* Device() { unsupported("prototype graphics device adapter"); }
ID3D11Texture2D* BackBufferAsD3D11() { unsupported("prototype backbuffer adapter"); }
}
void LogOverlay(LogLevel,char* format,...) { ++diagnostic_count;va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args); }
void LogOverlayW(LogLevel,wchar_t* format,...) { ++diagnostic_count;va_list args;va_start(args,format);vfwprintf(stderr,format,args);va_end(args); }
void ClearNotices() {}
void ParseHuntingSection() { unsupported("prototype hunting settings"); }
void CleanupShaderMaps(ID3D11DeviceChild*) { unsupported("prototype shader lifetime"); }
void Overlay::DrawOverlay() { unsupported("prototype overlay"); }
void HackerContext::InitIniParams() { unsupported("prototype GPU parameter buffer"); }
void HackerContext::Bind3DMigotoResources() { unsupported("prototype GPU binding"); }
const void* HackerInputLayout::GetShaderSignature() const { unsupported("prototype input layout signature"); }
SIZE_T HackerInputLayout::GetShaderSignatureSize() const { unsupported("prototype input layout signature size"); }
UINT HackerInputLayout::GetElementCount() const { unsupported("prototype input layout elements"); }
const D3D11_INPUT_ELEMENT_DESC* HackerInputLayout::GetElements() const { unsupported("prototype input layout data"); }
ULONG STDMETHODCALLTYPE HackerInputLayout::Release() { unsupported("prototype input layout release"); }
MigotoIncludeHandler::MigotoIncludeHandler(const char*) {}
HRESULT MigotoIncludeHandler::Open(D3D_INCLUDE_TYPE,LPCSTR,LPCVOID,LPCVOID*,UINT*) { unsupported("prototype HLSL include"); }
HRESULT MigotoIncludeHandler::Close(LPCVOID) { unsupported("prototype HLSL close"); }
std::map<std::wstring,ShaderRegexGroup> shader_regex_groups;
std::vector<ShaderRegexGroup*> shader_regex_group_index;
unsigned int shader_regex_hash=0;
ShaderRegexPattern::ShaderRegexPattern():regex(nullptr),do_replace(false) {}
ShaderRegexPattern::~ShaderRegexPattern() { if(regex)pcre2_code_free(regex); }
// Same PCRE2 options and capture-name handling as pinned ShaderRegex.cpp. Only
// parse validation; this host never rewrites or installs a graphics shader.
bool ShaderRegexPattern::compile(std::string* pattern) {
    int error; PCRE2_SIZE offset;
    regex = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern->data()), pattern->size(),
                          PCRE2_CASELESS | PCRE2_MULTILINE, &error, &offset, nullptr);
    if (!regex) {
        ++diagnostic_count;
        fprintf(stderr, "ShaderRegex PCRE2 error %d at byte %zu\n", error, size_t(offset)); return false;
    }
    uint32_t count, size; PCRE2_SPTR table;
    pcre2_pattern_info(regex, PCRE2_INFO_NAMECOUNT, &count);
    pcre2_pattern_info(regex, PCRE2_INFO_NAMEENTRYSIZE, &size);
    pcre2_pattern_info(regex, PCRE2_INFO_NAMETABLE, &table);
    for (uint32_t i = 0; i < count; ++i)
        named_capture_groups.insert(reinterpret_cast<const char*>(table + i * size + 2));
    return true;
}
bool ShaderRegexPattern::named_group_overlaps(ShaderRegexTemps& names) {
    for (const auto& name : named_capture_groups) if (names.count(name)) return true;
    return false;
}
int CursorUpscalingBypass_GetClientRect(HWND h,LPRECT r) { return ::GetClientRect(h,r); }
int CursorUpscalingBypass_GetCursorInfo(PCURSORINFO info) { return ::GetCursorInfo(info); }
int CursorUpscalingBypass_ScreenToClient(HWND h,LPPOINT p) { return ::ScreenToClient(h,p); }
void InstallMouseHooks(bool) { unsupported("prototype mouse hooks are disabled"); }
void enable_lock_dependency_checks() { unsupported("prototype lock hooks are disabled"); }
void _EnterCriticalSectionPretty(CRITICAL_SECTION* lock,char*,int) { ::EnterCriticalSection(lock); }
void _InitializeCriticalSectionPretty(CRITICAL_SECTION* lock,char*) { ::InitializeCriticalSection(lock); }
