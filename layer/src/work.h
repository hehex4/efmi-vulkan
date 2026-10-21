// Optional timings of tracker work only, excluding forwarded driver calls.
#pragma once

#include "common.h"
#include <array>
#include <mutex>

namespace efvk {

inline uint64_t host_time_us()
{
    static const uint64_t frequency = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart; }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const auto ticks = static_cast<uint64_t>(now.QuadPart);
    return ticks / frequency * 1000000 + ticks % frequency * 1000000 / frequency;
}

enum WorkKind { WK_UNMAP, WK_COPY_RECORD, WK_BUFFER_SUBMIT, WK_SUBMIT, WK_RESET, WK_DRAW, WK_PRESENT, WK_COUNT };
inline const char* work_name(WorkKind kind)
{
    static const char* names[] = {"buffer_unmap", "buffer_copy_record", "buffer_submit", "submit_tracking",
                                  "command_reset", "draw_record", "present_work"};
    return names[kind];
}

struct WorkTotals { uint64_t calls = 0, us = 0, max_us = 0, bytes = 0; };
struct WorkEvent {
    WorkKind kind = WK_UNMAP;
    uint64_t start_us = 0, us = 0, bytes = 0;
};
struct WorkSnapshot {
    static constexpr size_t capacity = 64;
    std::array<WorkTotals, WK_COUNT> totals{};
    std::array<WorkEvent, capacity> events{};
    size_t size = 0;
    uint64_t slow_count = 0;
};

class TrackerWork {
    struct Counters {
        std::atomic<uint64_t> calls{0}, us{0}, max_us{0}, bytes{0};
    };
    std::array<Counters, WK_COUNT> totals;
    std::mutex mx;
    std::array<WorkEvent, WorkSnapshot::capacity> events{};
    size_t size = 0, next = 0;
    uint64_t slow_count = 0;

public:
    static constexpr uint64_t slow_us = 2000;

    void record(WorkKind kind, uint64_t start_us, uint64_t end_us, uint64_t bytes)
    {
        const uint64_t elapsed = end_us >= start_us ? end_us - start_us : 0;
        auto& c = totals[kind];
        c.us.fetch_add(elapsed, std::memory_order_relaxed);
        c.bytes.fetch_add(bytes, std::memory_order_relaxed);
        auto peak = c.max_us.load(std::memory_order_relaxed);
        while (peak < elapsed && !c.max_us.compare_exchange_weak(peak, elapsed, std::memory_order_relaxed)) {}
        c.calls.fetch_add(1, std::memory_order_relaxed);
        if (elapsed < slow_us) return;
        // Fast calls never lock or allocate. Only slow operations enter this bounded history.
        std::lock_guard<std::mutex> lock(mx);
        events[next] = {kind, start_us, elapsed, bytes};
        next = (next + 1) % events.size();
        if (size < events.size()) ++size;
        ++slow_count;
    }

    WorkSnapshot snapshot()
    {
        WorkSnapshot out;
        // Concurrent writers can advance between loads: counters are observations, not a transaction.
        for (size_t i = 0; i < totals.size(); ++i) {
            auto& c = totals[i];
            out.totals[i] = {c.calls.load(std::memory_order_relaxed), c.us.load(std::memory_order_relaxed),
                             c.max_us.load(std::memory_order_relaxed), c.bytes.load(std::memory_order_relaxed)};
        }
        std::lock_guard<std::mutex> lock(mx);
        out.size = size; out.slow_count = slow_count;
        const size_t first = size == events.size() ? next : 0;
        for (size_t i = 0; i < size; ++i) out.events[i] = events[(first + i) % events.size()];
        return out;
    }
};

struct WorkScope {
    TrackerWork* work;
    WorkKind kind;
    uint64_t start_us = 0, bytes = 0;
    WorkScope(TrackerWork& target, WorkKind category) : work(g_cfg.profile_tracker ? &target : nullptr), kind(category)
    {
        if (work) start_us = host_time_us();
    }
    ~WorkScope() { if (work) work->record(kind, start_us, host_time_us(), bytes); }
    WorkScope(const WorkScope&) = delete;
    WorkScope& operator=(const WorkScope&) = delete;
};

} // namespace efvk
