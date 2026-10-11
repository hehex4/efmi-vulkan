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
#include "common.h"

#include <mutex>
#include <psapi.h>

namespace efvk {

namespace {

std::mutex g_patch_mx;
HMODULE g_patched = nullptr;

bool is_vulkan_loader(const wchar_t* path)
{
    const wchar_t* base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    return _wcsicmp(base, L"vulkan-1.dll") == 0;
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

struct Export {
    DWORD* slot;
};

// Finds the export table slot of a name. Forwarders and missing names give nullptr.
DWORD* find_slot(HMODULE mod, const char* name)
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
        if (*slot >= dir.VirtualAddress && *slot < dir.VirtualAddress + dir.Size)
            return nullptr; // forwarder, not code
        return slot;
    }
    return nullptr;
}

void patch_loader(HMODULE mod, const wchar_t* path, bool was_loaded)
{
    std::lock_guard<std::mutex> lock(g_patch_mx);
    if (g_patched)
        return;
    g_patched = mod;
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
    uint8_t* page = found ? alloc_near(mod, bytes) : nullptr;
    if (!page) {
        logf("bootstrap: no free memory near vulkan-1.dll for %zu stubs; nothing redirected", found);
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

    char narrow[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
    logf("bootstrap: %s at %p, %u of %zu exports redirected%s", narrow, static_cast<void*>(mod), redirected, hooks.size(),
         was_loaded ? "; it was already loaded, so functions resolved earlier are not intercepted" : "");
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
    wchar_t path[MAX_PATH];
    size_t n = data->FullDllName->Length / sizeof(wchar_t);
    if (n >= MAX_PATH)
        return;
    memcpy(path, data->FullDllName->Buffer, n * sizeof(wchar_t));
    path[n] = 0;
    if (is_vulkan_loader(path))
        patch_loader(static_cast<HMODULE>(data->DllBase), path, false);
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

    // It may already be there.
    HMODULE mods[2048];
    DWORD needed = 0;
    bool found = false;
    if (K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        DWORD count = needed / sizeof(HMODULE);
        if (count > sizeof(mods) / sizeof(mods[0]))
            count = sizeof(mods) / sizeof(mods[0]);
        for (DWORD i = 0; i < count; i++) {
            wchar_t path[MAX_PATH];
            if (GetModuleFileNameW(mods[i], path, MAX_PATH) && is_vulkan_loader(path)) {
                patch_loader(mods[i], path, true);
                found = true;
                break;
            }
        }
    }
    if (!found)
        logf("bootstrap: vulkan-1.dll is not loaded yet, waiting for it");
}

} // namespace efvk
