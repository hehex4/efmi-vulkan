// Bounded asynchronous pipeline creation. Requests never wait for the driver.
#pragma once

#include "pipeline_variant.h"
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace efvk {

enum class VariantState { Queued, Building, Ready, Failed, Saturated, Stopped };
struct VariantResult {
    VariantState state = VariantState::Queued;
    std::shared_ptr<const PipelineVariant> variant;
    std::string reason;
};

class VariantCache {
public:
    VariantCache(VkDevice, const VariantDispatch&, uint32_t max_sets, size_t capacity = 128);
    ~VariantCache();
    VariantCache(const VariantCache&) = delete;
    VariantCache& operator=(const VariantCache&) = delete;
    // Identity is a monotonically assigned pipeline generation, not a reusable VkPipeline handle.
    // Recipes are owned immutable objects. The same identity must always refer to the same source.
    VariantResult request(uint64_t identity, std::shared_ptr<const GraphicsRecipe>,
                          std::shared_ptr<const PipelineLayoutInfo>, const VertexInputRecipe&);
    // Removes every vertex variant of an original. Existing command-buffer references keep a
    // ready variant alive; an in-progress build may finish but cannot reinsert its retired entry.
    void retire(uint64_t identity);
    // Call before destroying the device. Also release every command-buffer/GPU owner before
    // vkDestroyDevice. Stop joins the worker; it is not for the draw or submit hot path.
    void stop();

private:
    struct Key {
        uint64_t identity;
        std::vector<uint32_t> vertex;
        bool operator<(const Key& other) const {
            return identity < other.identity || (identity == other.identity && vertex < other.vertex);
        }
    };
    struct Job {
        Key key;
        std::shared_ptr<const GraphicsRecipe> recipe;
        std::shared_ptr<const PipelineLayoutInfo> layout;
        VertexInputRecipe vertex;
        VariantResult result;
    };
    static Key key_for(uint64_t, const VertexInputRecipe&);
    void run();
    VkDevice dev_;
    VariantDispatch dispatch_;
    uint32_t max_sets_;
    size_t capacity_;
    std::mutex mutex_, stop_mutex_;
    std::condition_variable wake_;
    bool stopped_ = false;
    std::map<Key, std::shared_ptr<Job>> jobs_;
    std::deque<std::shared_ptr<Job>> queue_;
    std::thread worker_;
};

} // namespace efvk
