#pragma once
#include <atomic>
#include <cstdint>

namespace efmi::assets {
// Share this ledger across generations. Retained GPU owners remain charged
// after their cache retires, until the actual Vulkan allocation is released.
class AllocationBudget {
public:
    explicit AllocationBudget(uint64_t limit):limit_(limit) {}
    bool acquire(uint64_t bytes) {
        auto used=used_.load(std::memory_order_relaxed);
        do {
            if(bytes>limit_ || used>limit_-bytes)return false;
        } while(!used_.compare_exchange_weak(used,used+bytes,std::memory_order_relaxed));
        auto peak=peak_.load(std::memory_order_relaxed);
        while(peak<used+bytes && !peak_.compare_exchange_weak(peak,used+bytes,std::memory_order_relaxed)) {}
        return true;
    }
    void release(uint64_t bytes) { used_.fetch_sub(bytes,std::memory_order_relaxed); }
    uint64_t used() const { return used_.load(std::memory_order_relaxed); }
    uint64_t peak() const { return peak_.load(std::memory_order_relaxed); }
    uint64_t limit() const { return limit_; }
private:
    const uint64_t limit_;
    std::atomic<uint64_t> used_{0},peak_{0};
};
}
