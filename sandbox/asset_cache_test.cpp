// Included by the headless asset GPU fixture, sharing its actual device.
#include "../frontend/assets/texture_cache.h"
#include <chrono>

namespace {
using Clock=std::chrono::steady_clock;
static std::atomic<unsigned> wrong_worker_calls{0},wrong_submit_calls{0};
static DWORD caller_thread=0;
static VKAPI_ATTR VkResult VKAPI_CALL cache_create_image(VkDevice d,const VkImageCreateInfo* ci,
                                                         const VkAllocationCallbacks* a,VkImage* out)
{
    if(GetCurrentThreadId()==caller_thread)++wrong_worker_calls;
    return pCreateImage(d,ci,a,out);
}
static VKAPI_ATTR VkResult VKAPI_CALL cache_create_buffer(VkDevice d,const VkBufferCreateInfo* ci,
                                                          const VkAllocationCallbacks* a,VkBuffer* out)
{
    if(GetCurrentThreadId()==caller_thread)++wrong_worker_calls;
    return pCreateBuffer(d,ci,a,out);
}
static VKAPI_ATTR VkResult VKAPI_CALL cache_poll(VkDevice d,VkFence f)
{
    if(GetCurrentThreadId()==caller_thread)++wrong_worker_calls;
    return pGetFenceStatus(d,f);
}
static VKAPI_ATTR VkResult VKAPI_CALL cache_submit(VkQueue q,uint32_t n,const VkSubmitInfo* s,VkFence f)
{
    if(GetCurrentThreadId()!=caller_thread)++wrong_submit_calls;
    return pQueueSubmit(q,n,s,f);
}
TextureDevice cache_device(Gpu& g)
{
    TextureDevice d;d.device=g.device;d.physical=g.physical;d.graphics_family=g.family;d.bc_enabled=g.bc;
    d.memory=g.memory;d.dispatch=g.dispatch;d.format_properties=pGetPhysicalDeviceFormatProperties;
    d.image_properties=pGetPhysicalDeviceImageFormatProperties;
    d.dispatch.CreateImage=cache_create_image;d.dispatch.GetFenceStatus=cache_poll;d.dispatch.QueueSubmit=cache_submit;
    d.dispatch.CreateBuffer=cache_create_buffer;
#define X(name) d.buffer_dispatch.name=p##name;
    EFMI_BUFFER_UPLOAD_FUNCS(X)
#undef X
    d.buffer_dispatch.CreateBuffer=cache_create_buffer;d.buffer_dispatch.GetFenceStatus=cache_poll;d.buffer_dispatch.QueueSubmit=cache_submit;
    caller_thread=GetCurrentThreadId();return d;
}
void write_fixture(const std::filesystem::path& path,const std::vector<uint8_t>& data)
{
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()),data.size());
    if(!file)throw std::runtime_error("Cannot write project cache fixture");
}
TextureSnapshot await_texture(TextureCache& cache,const TextureHandle& entry,Gpu& g,bool submit)
{
    const auto deadline=Clock::now()+std::chrono::seconds(15);
    while(Clock::now()<deadline){
        auto value=entry->snapshot();
        if(value.state==TextureState::Ready || value.state==TextureState::Failed ||
           value.state==TextureState::Stopped || (!submit && value.state==TextureState::Prepared))return value;
        if(submit)cache.submit_prepared(g.queue);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Texture cache fixture deadline exceeded");
}
void cache_tests(Gpu& g)
{
    auto device=cache_device(g);std::string reason;
    const auto folder=std::filesystem::current_path()/L"cache-fixtures";
    std::filesystem::create_directories(folder);
    const auto file=folder/L"owned.dds",missing=folder/L"missing.dds";
    // This test owns only these named files under the runner's build folder.
    std::error_code ignored;std::filesystem::remove(missing,ignored);
    write_fixture(file,fixture(99));TextureBudgets budget;
    TextureCache cache(device,budget,2);
    auto embedded=file.native();embedded+=L'\0';embedded+=L".dds";
    check(!cache.declare_image(std::filesystem::path(embedded),reason),"embedded null cannot redirect a declared file path");
    auto entry=cache.declare_image(file,reason);
    check(entry && cache.declare_image(folder/L"."/L"owned.dds",reason)==entry,"normalized declarations share one texture request");
    auto unused=cache.declare_image(missing,reason);
    check(unused && unused->snapshot().state==TextureState::Dormant && !cache.stats().file_reads,
          "missing unused declarations neither read nor fail");
    check(!cache.declare_image(folder/L"third.dds",reason) && !cache.declare_image(L"relative.dds",reason),
          "declaration capacity and absolute-path requirements refuse early");
    std::atomic<unsigned> accepted{0};std::vector<std::thread> callers;
    for(int i=0;i<8;++i)callers.emplace_back([&]{for(int n=0;n<32;++n)if(cache.demand(entry))++accepted;});
    for(auto& thread:callers)thread.join();
    auto prepared=await_texture(cache,entry,g,false);
    check(accepted==256 && cache.stats().file_reads==1,"concurrent duplicate demands perform one file read");
    check(prepared.state==TextureState::Prepared && !prepared.image && !cache.stats().submissions,
          "worker preparation cannot submit or expose a texture");
    check(!budget.source->used() && budget.resident->used() && budget.staging->used(),
          "source bytes release before handoff while actual allocations remain charged");
    check(!cache.submit_prepared(VK_NULL_HANDLE) && entry->snapshot().state==TextureState::Prepared,
          "missing caller queue leaves a prepared request intact");
    write_fixture(file,fixture(98));
    auto ready=await_texture(cache,entry,g,true);
    check(ready.state==TextureState::Ready && ready.image && ready.image->metadata().format==VK_FORMAT_BC7_SRGB_BLOCK,
          "prepared bytes survive a later source-file edit");
    check(cache.stats().submissions==1 && cache.stats().ready==1 && !budget.staging->used(),
          "one caller submission completes and worker releases staging");
    auto retained=ready.image;ready.image.reset();const auto first_bytes=budget.resident->used();
    cache.stop();
    check(!cache.demand(entry) && entry->snapshot().state==TextureState::Stopped && retained->view() &&
          budget.resident->used()==first_bytes,"retired generation cannot demand but retained images stay charged and alive");
    TextureCache next(device,budget);auto next_entry=next.declare_image(file,reason);
    check(!next.demand(entry) && next.demand(next_entry),"generation ownership rejects a foreign request");
    auto changed=await_texture(next,next_entry,g,true);
    check(changed.state==TextureState::Ready && changed.image->metadata().format==VK_FORMAT_BC7_UNORM_BLOCK &&
          changed.image->image()!=retained->image() && budget.resident->used()>first_bytes,
          "new generation reads changed bytes without replacing retained old image");
    retained.reset();check(budget.resident->used()==first_bytes,"old GPU allocation releases only after last retained owner");
    changed.image.reset();next.stop();
    check(!budget.resident->used() && !budget.staging->used() && !budget.source->used(),"generation shutdown balances all budgets");

    TextureCache failures(device,budget);auto absent=failures.declare_image(missing,reason);failures.demand(absent);
    auto failed=await_texture(failures,absent,g,true);
    check(failed.state==TextureState::Failed && !failed.reason.empty() && !failed.image,"used missing file becomes an explicit failure");
    write_fixture(missing,fixture());
    check(!failures.demand(absent) && failures.stats().failed==1 && !failures.stats().file_reads,
          "failure is sticky without repeated draw-path file retries");
    failures.stop();
    {
        TextureCache reload(device,budget);auto fixed=reload.declare_image(missing,reason);reload.demand(fixed);
        check(await_texture(reload,fixed,g,true).state==TextureState::Ready,"fresh generation retries a repaired file");
    }
    for(int kind=0;kind<3;++kind){
        TextureBudgets limited;
        if(kind==0)limited.resident=std::make_shared<AllocationBudget>(1);
        if(kind==1)limited.staging=std::make_shared<AllocationBudget>(1);
        if(kind==2)limited.source=std::make_shared<AllocationBudget>(1);
        TextureCache bounded(device,limited);auto request=bounded.declare_image(file,reason);bounded.demand(request);
        const auto result=await_texture(bounded,request,g,true);bounded.stop();
        check(result.state==TextureState::Failed && !result.image && !limited.resident->used() &&
              !limited.staging->used() && !limited.source->used(),"source/resident/staging refusal releases all partial resources");
    }
    for(bool submitted:{false,true}){
        TextureCache cancelled(device,budget);auto request=cancelled.declare_image(file,reason);cancelled.demand(request);
        await_texture(cancelled,request,g,false);if(submitted)cancelled.submit_prepared(g.queue);
        cancelled.stop();
        check(!budget.resident->used() && !budget.staging->used() && request->snapshot().state==TextureState::Stopped,
              "cancellation before/after submission drains owned work without publishing it");
    }
    auto broken=device;broken.dispatch.QueueSubmit=[](VkQueue,uint32_t,const VkSubmitInfo*,VkFence)->VkResult {
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    };
    {
        TextureCache refusal(broken,budget);auto request=refusal.declare_image(file,reason);refusal.demand(request);
        check(await_texture(refusal,request,g,true).state==TextureState::Failed && !refusal.stats().submissions,
              "queue refusal never publishes an unsubmitted texture");
    }
    check(!budget.resident->used() && !budget.staging->used() && !budget.source->used(),"queue failure balances retained allocations");
    check(!wrong_worker_calls && !wrong_submit_calls,"GPU preparation/polling stay on worker; submission stays on caller");
}
void cache_sample(Gpu& g,int count,wchar_t** paths)
{
    auto device=cache_device(g);TextureBudgets budget;TextureCache cache(device,budget);std::string reason;
    std::vector<TextureHandle> requests;
    for(int i=0;i<count;++i){
        auto entry=cache.declare_image(std::filesystem::path(paths[i]),reason);
        if(!entry || !cache.demand(entry))throw std::runtime_error("Sample cache demand refused: "+reason);
        requests.push_back(entry);
    }
    for(const auto& entry:requests){
        const auto ready=await_texture(cache,entry,g,true);
        if(ready.state!=TextureState::Ready)throw std::runtime_error("Sample cache upload failed: "+ready.reason);
    }
    check(cache.stats().file_reads==count && cache.stats().submissions==count && cache.stats().ready==count,
          "all selected sample images become ready once through bounded cache");
    std::vector<std::shared_ptr<const ImageUpload>> owners;
    for(const auto& request:requests)owners.push_back(request->snapshot().image);
    cache.stop();
    check(budget.resident->used() && !budget.staging->used() && !budget.source->used(),
          "whole-character image owners survive retired cache with no staging left");
    printf("asset_cache: %d images; resident peak=%llu staging peak=%llu source peak=%llu\n",count,
           budget.resident->peak(),budget.staging->peak(),budget.source->peak());
    owners.clear();check(!budget.resident->used(),"last actual texture owners release the resident budget");
    check(!wrong_worker_calls && !wrong_submit_calls,"sample cache preserves caller/worker API separation");
}
}
