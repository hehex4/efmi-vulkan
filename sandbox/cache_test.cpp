// Deterministic concurrency/lifetime tests: the fake driver can pause pipeline creation.
#include "variant_fixture.h"
#include "../layer/src/variant_cache.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>

using namespace efvk;
static int passed = 0, failed = 0;
static void check(bool ok, const char* name) { if (ok) ++passed; else { ++failed; printf("FAIL: %s\n", name); } }
template<class T> static T handle(uintptr_t n) { return reinterpret_cast<T>(n); }
struct Driver {
    std::mutex mutex;
    std::condition_variable wake;
    uintptr_t next = 1;
    std::set<uintptr_t> live;
    uint32_t started = 0, finished = 0;
    bool pause = false, fail = false;
    template<class T> VkResult create(T* out) {
        std::lock_guard<std::mutex> lock(mutex); live.insert(next); *out = handle<T>(next++); return VK_SUCCESS;
    }
    template<class T> void destroy(T value) {
        std::lock_guard<std::mutex> lock(mutex); if (live.erase(reinterpret_cast<uintptr_t>(value)) != 1) std::abort();
    }
    void resume() { std::lock_guard<std::mutex> lock(mutex); pause = false; wake.notify_all(); }
    bool await_started(uint32_t n) {
        std::unique_lock<std::mutex> lock(mutex);
        return wake.wait_for(lock,std::chrono::seconds(5),[&]{return started>=n;});
    }
    size_t count() { std::lock_guard<std::mutex> lock(mutex); return live.size(); }
} driver;
#define CREATE(Name, Info, Type) \
static VKAPI_ATTR VkResult VKAPI_CALL create_##Name(VkDevice, const Info*, const VkAllocationCallbacks*, Type* out) { return driver.create(out); }
#define DESTROY(Name, Type) \
static VKAPI_ATTR void VKAPI_CALL destroy_##Name(VkDevice, Type h, const VkAllocationCallbacks*) { driver.destroy(h); }
CREATE(shader,VkShaderModuleCreateInfo,VkShaderModule)
CREATE(set,VkDescriptorSetLayoutCreateInfo,VkDescriptorSetLayout)
CREATE(layout,VkPipelineLayoutCreateInfo,VkPipelineLayout)
CREATE(pass,VkRenderPassCreateInfo,VkRenderPass)
DESTROY(shader,VkShaderModule)
DESTROY(set,VkDescriptorSetLayout)
DESTROY(layout,VkPipelineLayout)
DESTROY(pass,VkRenderPass)
DESTROY(pipe,VkPipeline)
static VKAPI_ATTR VkResult VKAPI_CALL create_pipe(VkDevice, VkPipelineCache, uint32_t,
    const VkGraphicsPipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline* out)
{
    std::unique_lock<std::mutex> lock(driver.mutex); ++driver.started; driver.wake.notify_all();
    driver.wake.wait(lock,[]{return !driver.pause;});
    ++driver.finished;
    if (driver.fail) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    driver.live.insert(driver.next); *out = handle<VkPipeline>(driver.next++); return VK_SUCCESS;
}
static VariantDispatch dispatch()
{
    VariantDispatch d;
    d.CreateShaderModule=create_shader; d.DestroyShaderModule=destroy_shader;
    d.CreateDescriptorSetLayout=create_set; d.DestroyDescriptorSetLayout=destroy_set;
    d.CreatePipelineLayout=create_layout; d.DestroyPipelineLayout=destroy_layout;
    d.CreateRenderPass=create_pass; d.DestroyRenderPass=destroy_pass;
    d.CreateGraphicsPipelines=create_pipe; d.DestroyPipeline=destroy_pipe;
    return d;
}
static VariantResult await(VariantCache& cache,uint64_t id,std::shared_ptr<const GraphicsRecipe> recipe,
                           std::shared_ptr<const PipelineLayoutInfo> layout,const VertexInputRecipe& vertex)
{
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    VariantResult result;
    do {
        result=cache.request(id,recipe,layout,vertex);
        if (result.state!=VariantState::Queued && result.state!=VariantState::Building) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while(std::chrono::steady_clock::now()<end);
    return result;
}
static bool await_live(size_t count)
{
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(driver.count()!=count && std::chrono::steady_clock::now()<end)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return driver.count()==count;
}
int main()
{
    variant_fixture::RecipeFixture f;
    auto recipe=std::make_shared<GraphicsRecipe>(f.capture());
    auto layout=std::make_shared<PipelineLayoutInfo>(f.layout);
    auto vertex=recipe->vertex;
    vertex.bindings.push_back({4,0,VK_VERTEX_INPUT_RATE_VERTEX});
    auto reordered=vertex;
    std::reverse(reordered.bindings.begin(),reordered.bindings.end());
    std::reverse(reordered.attributes.begin(),reordered.attributes.end());
    {
        driver.pause=true;
        VariantCache cache(handle<VkDevice>(1),dispatch(),4,2);
        auto first=cache.request(1,recipe,layout,vertex);
        check(first.state==VariantState::Queued && !first.variant,"first request returns pending without driver wait");
        check(driver.await_started(1),"worker enters paused driver");
        auto again=cache.request(1,recipe,layout,reordered);
        check(again.state==VariantState::Building && !again.variant,"canonical vertex key deduplicates reordered declaration");
        auto changed=vertex; changed.bindings[0].stride+=4;
        check(cache.request(1,recipe,layout,changed).state==VariantState::Queued,"changed stride is a distinct variant");
        check(cache.request(2,recipe,layout,vertex).state==VariantState::Saturated,"bounded cache retains original path when full");
        check(cache.request(0,recipe,layout,vertex).state==VariantState::Failed &&
              cache.request(3,{},layout,vertex).state==VariantState::Failed,"invalid identities and absent recipes rejected");
        driver.resume();
        auto ready=await(cache,1,recipe,layout,vertex);
        auto second=await(cache,1,recipe,layout,changed);
        check(ready.state==VariantState::Ready && second.state==VariantState::Ready &&
              ready.variant->pipeline!=second.variant->pipeline,"two complete variants publish atomically");
        check(driver.started==2,"duplicate requests cause no extra driver compilation");
        auto retained=ready.variant; ready.variant.reset(); second.variant.reset();
        cache.retire(1);
        check(await_live(6) && retained->pipeline,"retired variant lives only through command owner");
        retained.reset(); check(await_live(0),"final command owner releases all GPU objects");
        auto next=await(cache,2,recipe,layout,vertex);
        check(next.state==VariantState::Ready && driver.started==3,"new pipeline generation builds independently");
        next.variant.reset(); cache.stop();
        check(driver.count()==0 && cache.request(2,recipe,layout,vertex).state==VariantState::Stopped,
              "stop drains owned variants and rejects further requests");
    }
    {
        driver.pause=true;
        VariantCache cache(handle<VkDevice>(1),dispatch(),4,2);
        cache.request(10,recipe,layout,vertex); check(driver.await_started(4),"retirement scenario starts build");
        cache.request(11,recipe,layout,vertex); cache.retire(11); cache.retire(10);
        auto replacement=cache.request(10,recipe,layout,vertex);
        check(replacement.state==VariantState::Queued,"retired in-progress key can be requested afresh");
        driver.resume();
        auto ready=await(cache,10,recipe,layout,vertex);
        check(ready.state==VariantState::Ready && driver.started==5,"canceled queue skipped and stale completion not reinserted");
        ready.variant.reset(); cache.stop(); check(driver.count()==0,"retired build resources all released");
    }
    {
        driver.fail=true;
        VariantCache cache(handle<VkDevice>(1),dispatch(),4);
        auto result=await(cache,20,recipe,layout,vertex);
        check(result.state==VariantState::Failed && !result.variant && result.reason=="create_variant_pipeline",
              "driver failure publishes explicit failed state");
        check(cache.request(20,recipe,layout,vertex).state==VariantState::Failed && driver.started==6 && driver.count()==0,
              "failed variant is not retried on every draw and partial objects are released");
        driver.fail=false;
    }
    {
        driver.pause=true;
        VariantCache cache(handle<VkDevice>(1),dispatch(),4);
        cache.request(30,recipe,layout,vertex); check(driver.await_started(7),"stop scenario starts build");
        cache.request(31,recipe,layout,vertex);
        std::atomic<bool> stopped{false};
        std::thread stopper([&]{cache.stop();stopped=true;});
        auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(cache.request(30,recipe,layout,vertex).state!=VariantState::Stopped && std::chrono::steady_clock::now()<end)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(cache.request(30,recipe,layout,vertex).state==VariantState::Stopped && !stopped.load(),
              "device teardown waits for an active driver call");
        driver.resume(); stopper.join();
        check(stopped && driver.started==7 && driver.count()==0,"stop cancels pending builds and joins worker before teardown");
    }
    printf("variant cache: %d checks passed, %d failed\n",passed,failed);
    return failed?1:0;
}
