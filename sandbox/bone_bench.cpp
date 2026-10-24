// CPU-only microbenchmark of the production importer; not a mapped GPU-memory or FPS benchmark.
#include "merged_skeleton.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>

int main()
{
    using namespace efvk;
    using Clock = std::chrono::steady_clock;
    std::printf("{\"scope\":\"cached CPU bone import\",\"cases\":[");
    bool first = true;
    for (uint32_t count : {16u, 64u, 256u, 1024u}) for (bool mapped : {false, true}) {
        MergedSkeleton skeleton; std::string reason;
        if (!skeleton.configure(count, 4, {{0, count}}, reason)) throw std::runtime_error(reason);
        InstanceEntry entry{}; const uint32_t offset = 2000;
        std::memcpy(entry.data() + 80, &offset, 4); std::memcpy(entry.data() + 84, &offset, 4);
        std::vector<uint8_t> bytes((size_t(count) + 1) * 48 + 1);
        for (size_t i = 0; i < (size_t(count) + 1) * 12; ++i) {
            const float value = float(int(i % 101) - 50) * 0.125f;
            std::memcpy(bytes.data() + 1 + i * 4, &value, 4);
        }
        std::vector<uint16_t> remap;
        if (mapped) for (uint32_t i = 0; i < count; ++i) remap.push_back(uint16_t(count - 1 - i));
        const BoneWindow window{bytes.data() + 1, bytes.size() - 1};
        const uint32_t iterations = 4000000 / count;
        auto import = [&](uint64_t serial) {
            skeleton.begin_update(serial, reason);
            if (skeleton.import_component(uint32_t(serial % 4), 0, 0, entry, window, window,
                                          remap, 1.25f, reason) != BoneImportResult::Imported)
                throw std::runtime_error(reason);
        };
        for (uint32_t i = 0; i < 100; ++i) import(i);
        const auto begin = Clock::now();
        for (uint32_t i = 0; i < iterations; ++i) import(100 + i);
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
        uint64_t checksum = 1469598103934665603ull;
        for (auto b : skeleton.bytes()) checksum = (checksum ^ b) * 1099511628211ull;
        std::printf("%s{\"bones\":%u,\"remapped\":%s,\"imports\":%u,\"ns_per_import\":%.3f,\"checksum\":\"%llu\"}",
                    first ? "" : ",", count, mapped ? "true" : "false", iterations,
                    double(ns) / iterations, static_cast<unsigned long long>(checksum));
        first = false;
    }
    std::puts("]}");
}
