// Bounded host present-call intervals. These are not GPU/display frame times.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace efvk {

struct PresentSample {
    uint64_t frame = 0, us = 0, image_hash_us = 0;
    bool capture = false;
    uint64_t end_us = 0; // QPC clock, shared with tracker-work events; not wall time
};

struct PresentPacing {
    static constexpr size_t capacity = 2048;
    std::array<PresentSample, capacity> samples{};
    size_t size = 0, next = 0;
    uint64_t intervals = 0, over_16ms = 0, over_33ms = 0, over_50ms = 0, captures = 0;
    PresentSample peak;
    uint64_t last_us = 0, last_hash_us = 0;
    bool started = false;

    // Called under the existing frame lock: no allocation, sorting or resource lock.
    void observe(uint64_t now_us, uint64_t frame, uint64_t hash_us, bool capture)
    {
        if (started && now_us > last_us) {
            PresentSample sample{frame, now_us - last_us, hash_us >= last_hash_us ? hash_us - last_hash_us : 0, capture};
            sample.end_us = now_us;
            samples[next] = sample;
            next = (next + 1) % capacity;
            if (size < capacity) ++size;
            ++intervals;
            over_16ms += sample.us > 16000;
            over_33ms += sample.us > 33000;
            over_50ms += sample.us > 50000;
            captures += capture;
            if (sample.us > peak.us) peak = sample;
        }
        started = true;
        last_us = now_us;
        last_hash_us = hash_us;
    }
};

} // namespace efvk
