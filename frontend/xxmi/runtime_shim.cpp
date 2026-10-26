// Runtime host boundary. Unadapted graphics operations fail before emission.
#include "CommandList.h"
#include "globals.h"
#include "HackerDevice.h"
#include "HackerContext.h"
#include "ShaderRegex.h"
#include "Hunting.h"
#include "Overlay.h"
#include "runtime.h"
#include "engine_host.h"
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <deque>

[[noreturn]] static void unsupported(const char* call) { throw std::runtime_error(call); }
// Only the reference's local GetModuleFileName adapter reads this token.
HINSTANCE migoto_handle=reinterpret_cast<HINSTANCE>(1);
CRITICAL_SECTION resource_creation_mode_lock;
DWORD tls_idx=TLS_OUT_OF_INDEXES;
// The resource root and the original warnings are the entered generation's (D-129): a load and
// another generation's execution run at the same time, and each keeps its own.
std::wstring HostResourceDirectory(){auto* host=EngineHost();return host?host->resource_root:std::wstring();}
size_t HostDiagnosticCount() { auto* host=EngineHost(); return host?host->diagnostic_count:0; }
static void diagnostic(std::string text)
{
    // The original engine shows its warnings on its overlay and carries on; so does the runtime
    // (D-067). A warning outside every generation has no generation to report it to.
    auto* host=EngineHost();
    if (!host) return;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    host->diagnostics.emplace_back(++host->diagnostic_count, std::move(text));
    if (host->diagnostics.size() > 64) host->diagnostics.pop_front();
}
std::vector<std::string> HostDiagnosticListSince(size_t count)
{
    std::vector<std::string> result;
    if (auto* host=EngineHost()) for (const auto& entry : host->diagnostics) if (entry.first > count) result.push_back(entry.second);
    return result;
}
std::string HostDiagnosticsSince(size_t count)
{
    std::string result;
    if (auto* host=EngineHost()) for (const auto& entry : host->diagnostics) if (entry.first > count) {
        if (!result.empty()) result += " | ";
        result += entry.second;
    }
    return result;
}
void HostResourceRoot(const std::wstring& root)
{
    auto* host=EngineHost();
    if(!host)unsupported("resource root outside a generation");
    if(root.empty() || root.size()+12>=MAX_PATH)unsupported("prototype resource root too long");
    host->resource_root=root;
    if(host->resource_root.back()!=L'\\')host->resource_root+=L'\\';
}
namespace MigotoPort {
void MigotoDir(wchar_t* out,unsigned long size) {
    const auto root=HostResourceDirectory();
    if(root.empty() || root.size()+1>size)unsupported("prototype module directory adapter");
    wcscpy_s(out,size,root.c_str());
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
    diagnostic(level == LOG_DIRE ? std::string("dire: ") + text : std::string(text));
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
    diagnostic(level == LOG_DIRE ? "dire: " + utf8 : std::move(utf8));
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
// The original calls these on G->hWnd. A generation answers them from the host's window and
// cursor sample (RuntimeInput) and never touches a real window; its hWnd is only a token.
static __declspec(thread) const efmi::frontend::RuntimeInput* window_sample = nullptr;
void RuntimeWindowSample(const efmi::frontend::RuntimeInput* sample) { window_sample = sample; }
HWND RuntimeWindowHandle() { return reinterpret_cast<HWND>(uintptr_t(0x45464d49)); }
int CursorUpscalingBypass_GetClientRect(HWND h,LPRECT r)
{
    if (!r) return FALSE;
    *r = {};
    if (!window_sample || !window_sample->window.present || h != RuntimeWindowHandle()) return FALSE;
    r->right = window_sample->window.client_width; r->bottom = window_sample->window.client_height;
    return TRUE;
}
int CursorUpscalingBypass_GetCursorInfo(PCURSORINFO info)
{
    if (!info || info->cbSize != sizeof(CURSORINFO)) return FALSE;
    info->flags = 0; info->hCursor = nullptr; info->ptScreenPos = {};
    if (!window_sample) return FALSE;
    info->flags = window_sample->cursor.showing ? CURSOR_SHOWING : 0;
    info->ptScreenPos = {window_sample->cursor.screen_x, window_sample->cursor.screen_y};
    return TRUE;
}
int CursorUpscalingBypass_ScreenToClient(HWND h,LPPOINT p)
{
    if (!p || !window_sample || !window_sample->window.present || h != RuntimeWindowHandle()) return FALSE;
    p->x -= window_sample->window.client_left; p->y -= window_sample->window.client_top;
    return TRUE;
}
void InstallMouseHooks(bool) { unsupported("prototype mouse hooks are disabled"); }
void enable_lock_dependency_checks() { unsupported("prototype lock hooks are disabled"); }
void _EnterCriticalSectionPretty(CRITICAL_SECTION* lock,char*,int) { ::EnterCriticalSection(lock); }
void _InitializeCriticalSectionPretty(CRITICAL_SECTION* lock,char*) { ::InitializeCriticalSection(lock); }
