// A sampling profiler of the threads that call into the layer, on the probe's request (`profile`).
// Each sample suspends one thread, reads its registers and copies the top of its stack, and resumes it
// at once: nothing is allocated and no lock is taken while a thread is held, so a thread suspended
// inside the heap or the loader cannot stop the sampler. The stack is unwound afterwards on the copy
// (RtlVirtualUnwind with the stack registers moved into the copy). Frames are reported as module and
// offset, for the build's PDBs offline (scripts/profile_game.py).
#include "state.h"
#include "json.h"
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <vector>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace efvk {
namespace {
constexpr size_t kStackCopy = 128 * 1024; // bytes of a thread's stack copied per sample
constexpr int kMaxDepth = 48;

struct Sampled {
    DWORD id = 0;
    HANDLE handle = nullptr;
    uintptr_t stack_base = 0, stack_limit = 0;
    uint64_t samples = 0;
};

// The thread's stack bounds from its TEB (NT_TIB), read once before sampling.
bool stack_bounds(HANDLE thread, uintptr_t& base, uintptr_t& limit)
{
    using Query = LONG(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);
    static const auto query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    struct BasicInformation { LONG exit; PVOID teb; HANDLE process, thread; KAFFINITY affinity; LONG priority, base_priority; } info{};
    if (!query || query(thread, 0 /* ThreadBasicInformation */, &info, sizeof(info), nullptr) < 0 || !info.teb) return false;
    const auto* tib = static_cast<const NT_TIB*>(info.teb);
    base = reinterpret_cast<uintptr_t>(tib->StackBase); limit = reinterpret_cast<uintptr_t>(tib->StackLimit);
    return base > limit;
}

// The copy of [rsp, rsp + size) as the thread had it; false when the registers or the stack cannot be read.
bool take(Sampled& t, CONTEXT& context, uint8_t* copy, size_t& size)
{
    size = 0;
    if (SuspendThread(t.handle) == DWORD(-1)) return false;
    context = {};
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    bool ok = GetThreadContext(t.handle, &context) != 0;
    if (ok && context.Rsp >= t.stack_limit && context.Rsp < t.stack_base) {
        size = std::min<size_t>(t.stack_base - context.Rsp, kStackCopy);
        __try { memcpy(copy, reinterpret_cast<const void*>(context.Rsp), size); }
        __except (EXCEPTION_EXECUTE_HANDLER) { size = 0; }
    }
    ResumeThread(t.handle);
    return ok;
}

// A stack address of the thread, moved into the copy; return addresses and code stay as they are.
inline void rebase(DWORD64& r, uint64_t low, uint64_t high, int64_t delta) { if (r >= low && r < high) r = DWORD64(int64_t(r) + delta); }

int unwind(CONTEXT context, const uint8_t* copy, size_t size, uint64_t* frames)
{
    const uint64_t low = context.Rsp, high = context.Rsp + size;
    const int64_t delta = int64_t(reinterpret_cast<uintptr_t>(copy)) - int64_t(low);
    const uint64_t copy_low = reinterpret_cast<uintptr_t>(copy), copy_high = copy_low + size;
    rebase(context.Rsp, low, high, delta); rebase(context.Rbp, low, high, delta);
    int depth = 0;
    frames[depth++] = context.Rip;
    __try {
        while (depth < kMaxDepth) {
            DWORD64 image = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
            if (!function) {
                // A leaf function: its return address is at the top of the stack.
                if (context.Rsp < copy_low || context.Rsp + 8 > copy_high) break;
                context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
                context.Rsp += 8;
            } else {
                PVOID handler = nullptr; DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context, &handler, &establisher, nullptr);
            }
            rebase(context.Rbp, low, high, delta); rebase(context.Rsp, low, high, delta);
            if (!context.Rip || context.Rsp < copy_low || context.Rsp >= copy_high) break;
            frames[depth++] = context.Rip;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return depth;
}

struct Module { uintptr_t base = 0, end = 0; std::string name; };

std::vector<Module> modules()
{
    std::vector<Module> out;
    std::vector<HMODULE> handles(1024);
    DWORD needed = 0;
    if (!EnumProcessModulesEx(GetCurrentProcess(), handles.data(), DWORD(handles.size() * sizeof(HMODULE)), &needed, LIST_MODULES_64BIT)) return out;
    handles.resize(std::min<size_t>(needed / sizeof(HMODULE), handles.size()));
    for (HMODULE h : handles) {
        MODULEINFO info{};
        wchar_t path[MAX_PATH] = {};
        if (!GetModuleInformation(GetCurrentProcess(), h, &info, sizeof(info)) || !GetModuleFileNameW(h, path, MAX_PATH)) continue;
        Module m;
        m.base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll); m.end = m.base + info.SizeOfImage;
        const int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
        m.name.resize(n > 0 ? n - 1 : 0);
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, path, -1, m.name.data(), n, nullptr, nullptr);
        out.push_back(std::move(m));
    }
    std::sort(out.begin(), out.end(), [](const Module& a, const Module& b) { return a.base < b.base; });
    return out;
}
}

std::string probe_profile(uint32_t milliseconds, uint32_t hz)
{
    milliseconds = std::clamp<uint32_t>(milliseconds, 100, 30000);
    hz = std::clamp<uint32_t>(hz, 10, 1000);
    std::vector<Sampled> threads;
    {
        DWORD ids[256];
        const size_t count = thread_trace_ids(ids, 256);
        for (size_t i = 0; i < count; ++i) {
            if (ids[i] == GetCurrentThreadId()) continue;
            Sampled t; t.id = ids[i];
            t.handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, ids[i]);
            if (!t.handle) continue;
            if (!stack_bounds(t.handle, t.stack_base, t.stack_limit)) { CloseHandle(t.handle); continue; }
            threads.push_back(t);
        }
    }
    std::vector<uint8_t> copy(kStackCopy + 64);
    std::map<std::vector<uint64_t>, uint64_t> stacks; // thread id first, then the frames
    uint64_t samples = 0, failed = 0;
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    const auto began = std::chrono::steady_clock::now(), deadline = began + std::chrono::milliseconds(milliseconds);
    const int64_t period = -int64_t(10000000 / hz); // 100 ns units, relative
    uint64_t frames[kMaxDepth];
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto& t : threads) {
            CONTEXT context;
            size_t size = 0;
            if (!take(t, context, copy.data(), size)) { ++failed; continue; }
            const int depth = size ? unwind(context, copy.data(), size, frames) : (frames[0] = context.Rip, 1);
            std::vector<uint64_t> key;
            key.reserve(depth + 1);
            key.push_back(t.id);
            key.insert(key.end(), frames, frames + depth);
            ++stacks[std::move(key)];
            ++t.samples; ++samples;
        }
        if (timer) {
            LARGE_INTEGER due; due.QuadPart = period;
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, 1000);
        } else {
            Sleep(1000 / hz);
        }
    }
    const uint64_t elapsed = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count());
    if (timer) CloseHandle(timer);
    for (auto& t : threads) CloseHandle(t.handle);

    const auto mods = modules();
    auto where = [&](uint64_t address, JsonWriter& w) {
        auto it = std::upper_bound(mods.begin(), mods.end(), address, [](uint64_t a, const Module& m) { return a < m.base; });
        if (it != mods.begin() && address < (it - 1)->end) {
            const auto& m = *(it - 1);
            char text[32]; snprintf(text, sizeof(text), "%u+0x%llx", unsigned(it - 1 - mods.begin()), static_cast<unsigned long long>(address - m.base));
            w.str(text);
        } else {
            char text[32]; snprintf(text, sizeof(text), "?+0x%llx", static_cast<unsigned long long>(address));
            w.str(text);
        }
    };
    // The most frequent stacks, at most 20000.
    std::vector<std::pair<const std::vector<uint64_t>*, uint64_t>> order;
    order.reserve(stacks.size());
    for (const auto& s : stacks) order.push_back({&s.first, s.second});
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (order.size() > 20000) order.resize(20000);
    JsonWriter w;
    w.begin_object();
    w.kb("ok", true);
    w.key("result"); w.begin_object();
    w.kn("milliseconds", elapsed); w.kn("hz", hz); w.kn("samples", samples); w.kn("failed", failed); w.kn("distinct_stacks", stacks.size());
    w.key("threads"); w.begin_array();
    for (const auto& t : threads) { w.begin_object(); w.kn("id", t.id); w.kn("samples", t.samples); w.end_object(); }
    w.end_array();
    w.key("modules"); w.begin_array();
    for (const auto& m : mods) { w.begin_object(); w.kv("path", m.name); w.kn("base", m.base); w.kn("size", m.end - m.base); w.end_object(); }
    w.end_array();
    w.key("stacks"); w.begin_array();
    for (const auto& s : order) {
        w.begin_object(); w.kn("n", s.second); w.kn("thread", (*s.first)[0]);
        w.key("frames"); w.begin_array();
        for (size_t i = 1; i < s.first->size(); ++i) where((*s.first)[i], w);
        w.end_array(); w.end_object();
    }
    w.end_array();
    w.end_object();
    w.end_object();
    return w.out;
}

} // namespace efvk
