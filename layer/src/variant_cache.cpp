#include "variant_cache.h"
#include <algorithm>
#include <stdexcept>

namespace efvk {

VariantCache::Key VariantCache::key_for(uint64_t identity, const VertexInputRecipe& vertex)
{
    Key key{identity, {}};
    auto bindings = vertex.bindings;
    auto attributes = vertex.attributes;
    std::sort(bindings.begin(), bindings.end(), [](const auto& a, const auto& b) { return a.binding < b.binding; });
    std::sort(attributes.begin(), attributes.end(), [](const auto& a, const auto& b) { return a.location < b.location; });
    key.vertex.push_back(static_cast<uint32_t>(bindings.size()));
    for (const auto& b : bindings) key.vertex.insert(key.vertex.end(), {b.binding, b.stride, uint32_t(b.inputRate)});
    key.vertex.push_back(static_cast<uint32_t>(attributes.size()));
    for (const auto& a : attributes) key.vertex.insert(key.vertex.end(), {a.location, a.binding, uint32_t(a.format), a.offset});
    return key;
}

VariantCache::VariantCache(VkDevice dev, const VariantDispatch& dispatch, uint32_t max_sets, size_t capacity)
    : dev_(dev), dispatch_(dispatch), max_sets_(max_sets), capacity_(std::min<size_t>(capacity, 4096))
{
    worker_ = std::thread(&VariantCache::run, this);
}
VariantCache::~VariantCache() { stop(); }

VariantResult VariantCache::request(uint64_t identity, std::shared_ptr<const GraphicsRecipe> recipe,
                                   std::shared_ptr<const PipelineLayoutInfo> layout, const VertexInputRecipe& vertex)
{
    if (!identity || !recipe || !layout || vertex.bindings.size() > 32 || vertex.attributes.size() > 32)
        return {VariantState::Failed, {}, "invalid variant request"};
    auto key = key_for(identity, vertex);
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) return {VariantState::Stopped, {}, "variant cache stopped"};
    auto found = jobs_.find(key);
    if (found != jobs_.end()) return found->second->result;
    if (jobs_.size() >= capacity_) return {VariantState::Saturated, {}, "variant cache capacity reached"};
    auto job = std::make_shared<Job>();
    job->key = std::move(key); job->recipe = std::move(recipe); job->layout = std::move(layout); job->vertex = vertex;
    jobs_.emplace(job->key, job); queue_.push_back(job); wake_.notify_one();
    return job->result;
}

void VariantCache::retire(uint64_t identity)
{
    // GPU destruction may call into a driver. Do not perform it under the cache mutex.
    std::vector<std::shared_ptr<Job>> released;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = jobs_.begin(); it != jobs_.end();) {
            if (it->first.identity == identity) { released.push_back(it->second); it = jobs_.erase(it); }
            else ++it;
        }
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                     [identity](const auto& job) { return job->key.identity == identity; }), queue_.end());
    }
}

void VariantCache::stop()
{
    // Serializes concurrent stop callers without holding the worker's publication mutex.
    std::lock_guard<std::mutex> stop_lock(stop_mutex_);
    std::map<Key, std::shared_ptr<Job>> released;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true; released.swap(jobs_); queue_.clear(); wake_.notify_all();
    }
    if (worker_.joinable()) worker_.join();
}

void VariantCache::run()
{
    while (true) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stopped_ || !queue_.empty(); });
            if (stopped_) return;
            job = queue_.front(); queue_.pop_front(); job->result.state = VariantState::Building;
        }
        VariantResult result;
        try {
            auto variant = std::make_shared<PipelineVariant>(dev_, dispatch_);
            if (variant->build(*job->recipe, *job->layout, job->vertex, max_sets_))
                result = {VariantState::Ready, std::move(variant), {}};
            else result = {VariantState::Failed, {}, variant->reason};
        } catch (const std::exception& e) { result = {VariantState::Failed, {}, e.what()}; }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto found = jobs_.find(job->key);
            if (!stopped_ && found != jobs_.end() && found->second == job) {
                job->result = std::move(result);
                job->recipe.reset(); job->layout.reset();
            }
        }
        // A retired build's temporary GPU resources are destroyed outside the lock here.
    }
}

} // namespace efvk
