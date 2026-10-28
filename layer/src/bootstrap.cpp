// Inject entry: getting in front of the Vulkan calls without being a registered layer.
//
// When the DLL is loaded with LoadLibrary (by a mod launcher, the way d3d11 mod
// loaders are brought into a game), nothing calls it. It then redirects, in
// memory only, the export table entries of vulkan-1.dll for the functions it
// hooks. Whoever looks one of them up afterwards gets the hook, and the hook
// calls the address the export had. vulkan-1.dll on disk is not touched.
//
// The redirection has to be in place before the application resolves its
// Vulkan functions, so the DLL asks the Windows loader to be told when
// vulkan-1.dll is mapped.
//
// The loader is the module that holds the code of those exports, not the first
// module of that name: a proxy DLL called vulkan-1.dll that forwards them (to the
// system's loader by its full path, or to a renamed copy) is mapped first and holds
// none (BUG-074, 2.0.9 on an AMD machine: "0 stubs", nothing redirected, no Mod).
// As 3DMigoto takes the original d3d11.dll from the system folder whatever else is
// called d3d11.dll (D3D11Wrapper.cpp), a module is patched only when it exports
// vkGetInstanceProcAddr as code; the module its forwarders name is waited for.
#include "common.h"

#include <mutex>
#include <psapi.h>
#include <string>
#include <vector>

namespace efvk {

namespace {

std::mutex g_patch_mx;
HMODULE g_patched = nullptr;
// Lower-case file names the loader may have besides vulkan-1.dll: the targets of a proxy's forwarders.
std::vector<std::wstring> g_loader_names;
std::vector<HMODULE> g_logged;                   // modules already described in the log

const wchar_t* base_name(const wchar_t* path)
{
    const wchar_t* base = wcsrchr(path, L'\\');
    return base ? base + 1 : path;
}

bool is_vulkan_loader(const wchar_t* path)
{
    const wchar_t* base = base_name(path);
    if (_wcsicmp(base, L"vulkan-1.dll") == 0)
        return true;
    std::lock_guard<std::mutex> lock(g_patch_mx);
    for (const std::wstring& name : g_loader_names)
        if (_wcsicmp(base, name.c_str()) == 0)
            return true;
    return false;
}

// Export table entries are 32-bit offsets from the module base, so the jump
// stubs have to live within 2 GB above it.
uint8_t* alloc_near(HMODULE mod, size_t size)
{
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(mod);
    const IMAGE_NT_HEADERS* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(mod) + dos->e_lfanew);
    uintptr_t base = reinterpret_cast<uintptr_t>(mod);
    uintptr_t addr = (base + nt->OptionalHeader.SizeOfImage + 0xFFFF) & ~static_cast<uintptr_t>(0xFFFF);
    uintptr_t limit = base + 0x7FF00000ull;
    while (addr < limit) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)))
            break;
        uintptr_t region = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if (mbi.State == MEM_FREE) {
            uintptr_t a = (region + 0xFFFF) & ~static_cast<uintptr_t>(0xFFFF);
            if (a + size <= region + mbi.RegionSize && a + size < limit) {
                void* p = VirtualAlloc(reinterpret_cast<void*>(a), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (p)
                    return static_cast<uint8_t*>(p);
            }
        }
        addr = region + mbi.RegionSize;
    }
    return nullptr;
}

const size_t kStubSize = 16;

void write_stub(uint8_t* at, void* target)
{
    // mov rax, imm64 ; jmp rax
    at[0] = 0x48;
    at[1] = 0xB8;
    memcpy(at + 2, &target, 8);
    at[10] = 0xFF;
    at[11] = 0xE0;
    at[12] = at[13] = at[14] = at[15] = 0xCC;
}

// The export table entry of a name: its slot when it is code; a forwarder's target
// ("module.function") in `forward` instead; nothing when the name is not exported.
DWORD* find_slot(HMODULE mod, const char* name, const char** forward = nullptr)
{
    uint8_t* base = reinterpret_cast<uint8_t*>(mod);
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(mod);
    const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress)
        return nullptr;
    const IMAGE_EXPORT_DIRECTORY* exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    const DWORD* names = reinterpret_cast<const DWORD*>(base + exp->AddressOfNames);
    const WORD* ordinals = reinterpret_cast<const WORD*>(base + exp->AddressOfNameOrdinals);
    DWORD* funcs = reinterpret_cast<DWORD*>(base + exp->AddressOfFunctions);
    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        if (strcmp(reinterpret_cast<const char*>(base + names[i]), name))
            continue;
        DWORD* slot = &funcs[ordinals[i]];
        if (*slot >= dir.VirtualAddress && *slot < dir.VirtualAddress + dir.Size) {
            if (forward)
                *forward = reinterpret_cast<const char*>(base + *slot);
            return nullptr; // forwarder, not code
        }
        return slot;
    }
    return nullptr;
}

DWORD exported_names(HMODULE mod)
{
    uint8_t* base = reinterpret_cast<uint8_t*>(mod);
    const IMAGE_NT_HEADERS* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(mod)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    return dir.VirtualAddress ? reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress)->NumberOfNames : 0;
}

// The module of a forwarder as the Windows loader looks it up:
// "C:\Windows\System32\vulkan-1.vkCreateInstance" -> "C:\Windows\System32\vulkan-1.dll".
std::wstring forward_module(const char* forward)
{
    std::string module(forward);
    const size_t dot = module.find_last_of('.');
    if (dot == std::string::npos || !dot)
        return std::wstring();
    module.resize(dot);
    std::wstring wide(module.size() + 1, L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, module.c_str(), -1, &wide[0], static_cast<int>(wide.size()));
    if (n <= 1)
        return std::wstring();
    wide.resize(static_cast<size_t>(n) - 1);
    if (!wcschr(base_name(wide.c_str()), L'.'))
        wide += L".dll";
    return wide;
}

std::string narrow(const wchar_t* path)
{
    char out[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, path, -1, out, sizeof(out) - 1, nullptr, nullptr);
    return out;
}

bool first_sight(HMODULE mod)
{
    for (HMODULE seen : g_logged)
        if (seen == mod)
            return false;
    g_logged.push_back(mod);
    return true;
}

void patch_loader(HMODULE mod, const wchar_t* path, bool was_loaded);

// A module named like the loader that does not hold its code. Its forwarders name the module that
// does; when that one is in the process already, it is the loader now, otherwise it is waited for.
void not_the_loader(HMODULE mod, const wchar_t* path, bool was_loaded, const char* forward)
{
    const std::wstring target = forward ? forward_module(forward) : std::wstring();
    {
        std::lock_guard<std::mutex> lock(g_patch_mx);
        if (!first_sight(mod))
            return;
        if (!target.empty()) {
            std::wstring name = base_name(target.c_str());
            for (wchar_t& ch : name)
                ch = static_cast<wchar_t>(towlower(ch));
            bool known = name == L"vulkan-1.dll";
            for (const std::wstring& other : g_loader_names)
                known |= other == name;
            if (!known)
                g_loader_names.push_back(name);
        }
    }
    logf("bootstrap: %s at %p is not the Vulkan loader: %lu exported names, vkGetInstanceProcAddr %s%s; waiting for the loader",
         narrow(path).c_str(), static_cast<void*>(mod), exported_names(mod), forward ? "forwarded to " : "not exported",
         forward ? forward : "");
    if (target.empty())
        return;
    // The forwarder's module is loaded at the first lookup through it; one already loaded is the loader now.
    HMODULE loaded = GetModuleHandleW(target.c_str());
    if (!loaded || loaded == mod)
        return;
    wchar_t loaded_path[MAX_PATH] = {};
    if (GetModuleFileNameW(loaded, loaded_path, MAX_PATH))
        patch_loader(loaded, loaded_path, was_loaded);
}

void patch_loader(HMODULE mod, const wchar_t* path, bool was_loaded)
{
    const char* forward = nullptr;
    if (!find_slot(mod, "vkGetInstanceProcAddr", &forward)) {
        not_the_loader(mod, path, was_loaded, forward);
        return;
    }
    std::lock_guard<std::mutex> lock(g_patch_mx);
    if (g_patched) {
        // A second loader (an embedded browser brings its own): the application's calls through it are
        // not seen. Said once, so that a missing device can be traced to it.
        if (mod != g_patched && first_sight(mod))
            logf("bootstrap: another Vulkan loader %s at %p; its exports are left as they are, the first one's are redirected",
                 narrow(path).c_str(), static_cast<void*>(mod));
        return;
    }
    g_patched = mod;
    first_sight(mod);
    g_attach.loader_found.store(1);
    g_attach.loader_was_loaded.store(was_loaded ? 1 : 0);

    std::vector<ExportHook> hooks;
    collect_export_hooks(hooks);

    // First learn every original address, then make the passive device known, and only then
    // redirect: from the first redirected entry on, a hook may be called at any moment.
    std::vector<DWORD*> slots(hooks.size(), nullptr);
    uint8_t* base = reinterpret_cast<uint8_t*>(mod);
    size_t found = 0;
    for (size_t i = 0; i < hooks.size(); i++) {
        slots[i] = find_slot(mod, hooks[i].name);
        if (!slots[i])
            continue;
        *hooks[i].original = base + *slots[i];
        found++;
    }
    exports_resolved();

    size_t bytes = (found * kStubSize + 0xFFF) & ~static_cast<size_t>(0xFFF);
    uint8_t* page = alloc_near(mod, bytes);
    if (!page) {
        logf("bootstrap: %s at %p: no free memory above it for %zu stubs; nothing redirected", narrow(path).c_str(),
             static_cast<void*>(mod), found);
        return;
    }
    uint8_t* stub = page;
    for (size_t i = 0; i < hooks.size(); i++) {
        if (!slots[i])
            continue;
        write_stub(stub, hooks[i].hook);
        stub += kStubSize;
    }
    DWORD old = 0;
    VirtualProtect(page, bytes, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), page, bytes);

    uint32_t redirected = 0;
    stub = page;
    for (size_t i = 0; i < hooks.size(); i++) {
        if (!slots[i])
            continue;
        DWORD prot = 0;
        if (VirtualProtect(slots[i], sizeof(DWORD), PAGE_READWRITE, &prot)) {
            *slots[i] = static_cast<DWORD>(stub - base);
            VirtualProtect(slots[i], sizeof(DWORD), prot, &prot);
            redirected++;
        }
        stub += kStubSize;
    }
    g_attach.exports_redirected.store(redirected);

    logf("bootstrap: %s at %p, %u of %zu exports redirected%s", narrow(path).c_str(), static_cast<void*>(mod), redirected,
         hooks.size(), was_loaded ? "; it was already loaded, so functions resolved earlier are not intercepted" : "");
}

// Loader notification, see LdrRegisterDllNotification.
struct UnicodeString {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
};

struct LdrNotifyData {
    ULONG Flags;
    const UnicodeString* FullDllName;
    const UnicodeString* BaseDllName;
    PVOID DllBase;
    ULONG SizeOfImage;
};

typedef VOID(CALLBACK* LdrNotifyFn)(ULONG reason, const LdrNotifyData* data, PVOID context);
typedef LONG(NTAPI* LdrRegisterFn)(ULONG flags, LdrNotifyFn fn, PVOID context, PVOID* cookie);

VOID CALLBACK on_dll_event(ULONG reason, const LdrNotifyData* data, PVOID)
{
    if (reason != 1 || !data || !data->FullDllName || !data->FullDllName->Buffer) // 1 = loaded
        return;
    const std::wstring path(data->FullDllName->Buffer, data->FullDllName->Length / sizeof(wchar_t));
    if (is_vulkan_loader(path.c_str()))
        patch_loader(static_cast<HMODULE>(data->DllBase), path.c_str(), false);
}

} // namespace

void bootstrap_start()
{
    // Be told when vulkan-1.dll gets mapped: the redirection has to be in place
    // before anybody looks an export up.
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    LdrRegisterFn reg =
        ntdll ? reinterpret_cast<LdrRegisterFn>(GetProcAddress(ntdll, "LdrRegisterDllNotification")) : nullptr;
    PVOID cookie = nullptr;
    LONG st = reg ? reg(0, on_dll_event, nullptr, &cookie) : -1;
    logf("bootstrap: loader notification %s (0x%lx)", st == 0 ? "registered" : "FAILED", st);

    // It may already be there, maybe behind a module of its name that is not.
    HMODULE mods[2048];
    DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        DWORD count = needed / sizeof(HMODULE);
        if (count > sizeof(mods) / sizeof(mods[0]))
            count = sizeof(mods) / sizeof(mods[0]);
        for (DWORD i = 0; i < count; i++) {
            wchar_t path[MAX_PATH];
            if (GetModuleFileNameW(mods[i], path, MAX_PATH) && is_vulkan_loader(path))
                patch_loader(mods[i], path, true);
        }
    }
    bool found;
    {
        std::lock_guard<std::mutex> lock(g_patch_mx);
        found = g_patched != nullptr;
    }
    if (!found)
        logf("bootstrap: the Vulkan loader is not loaded yet, waiting for it");
}

} // namespace efvk
