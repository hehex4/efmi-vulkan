// Runtime host boundary. Unadapted graphics operations fail before emission.
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
#include <deque>

[[noreturn]] static void unsupported(const char* call) { throw std::runtime_error(call); }
HINSTANCE migoto_handle=nullptr;
CRITICAL_SECTION resource_creation_mode_lock;
DWORD tls_idx=TLS_OUT_OF_INDEXES;
static std::wstring resource_root;
std::wstring HostResourceDirectory(){return resource_root;}
static size_t diagnostic_count=0;
static std::deque<std::pair<size_t, std::string>> diagnostics;
size_t HostDiagnosticCount() { return diagnostic_count; }
static void diagnostic(std::string text)
{
    diagnostics.emplace_back(++diagnostic_count, std::move(text));
    if (diagnostics.size() > 8) diagnostics.pop_front();
}
std::string HostDiagnosticsSince(size_t count)
{
    std::string result;
    for (const auto& entry : diagnostics) if (entry.first > count) {
        if (!result.empty()) result += " | ";
        result += entry.second;
    }
    return result;
}
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
void LogOverlay(LogLevel level, char* format, ...)
{
    // Upstream notices (such as a recognized deprecated option) are not parse errors.
    if (level > LOG_WARNING_MONOSPACE) return;
    char text[2048]{}; va_list args; va_start(args, format);
    _vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args); va_end(args);
    diagnostic(text);
}
void LogOverlayW(LogLevel level, wchar_t* format, ...)
{
    if (level > LOG_WARNING_MONOSPACE) return;
    wchar_t text[2048]{}; va_list args; va_start(args, format);
    _vsnwprintf_s(text, _countof(text), _TRUNCATE, format, args); va_end(args);
    const auto length = int(wcslen(text));
    const auto size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string utf8(size, '\0');
    if (size) WideCharToMultiByte(CP_UTF8, 0, text, length, utf8.data(), size, nullptr, nullptr);
    diagnostic(std::move(utf8));
}
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
bool ShaderRegexPattern::compile(std::string*) { unsupported("prototype shader regex"); }
bool ShaderRegexPattern::named_group_overlaps(ShaderRegexTemps&) { unsupported("prototype shader regex groups"); }
int CursorUpscalingBypass_GetClientRect(HWND h,LPRECT r) { return ::GetClientRect(h,r); }
int CursorUpscalingBypass_GetCursorInfo(PCURSORINFO info) { return ::GetCursorInfo(info); }
int CursorUpscalingBypass_ScreenToClient(HWND h,LPPOINT p) { return ::ScreenToClient(h,p); }
void InstallMouseHooks(bool) { unsupported("prototype mouse hooks are disabled"); }
void enable_lock_dependency_checks() { unsupported("prototype lock hooks are disabled"); }
void _EnterCriticalSectionPretty(CRITICAL_SECTION* lock,char*,int) { ::EnterCriticalSection(lock); }
void _InitializeCriticalSectionPretty(CRITICAL_SECTION* lock,char*) { ::InitializeCriticalSection(lock); }
