// Included by the headless asset GPU fixture, sharing its actual device.
#include "../frontend/assets/texture_cache.h"
#include "../frontend/assets/png.h"
#include <chrono>
#include <future>

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
    check(entry->wait_until(Clock::now()).state==TextureState::Prepared &&
          unused->wait_until(Clock::now()).state==TextureState::Dormant,
          "resource wait timeout preserves a pending state; dormant resources return immediately");
    auto notified=std::async(std::launch::async,[entry]{return entry->wait_until(Clock::now()+std::chrono::seconds(10));});
    check(!budget.source->used() && budget.resident->used() && budget.staging->used(),
          "source bytes release before handoff while actual allocations remain charged");
    check(!cache.submit_prepared(VK_NULL_HANDLE) && entry->snapshot().state==TextureState::Prepared,
          "missing caller queue leaves a prepared request intact");
    write_fixture(file,fixture(98));
    auto ready=await_texture(cache,entry,g,true);
    const bool woke=notified.wait_for(std::chrono::seconds(1))==std::future_status::ready;
    auto notified_ready=notified.get();
    check(woke && notified_ready.state==TextureState::Ready && notified_ready.image==ready.image,
          "resource completion notifies a bounded waiter with the same ready owner");
    notified_ready={};
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
    check(absent->wait_until(Clock::now()).state==TextureState::Failed,
          "resource failure is a terminal wait result without an image");
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
        auto stopped=std::async(std::launch::async,[request]{return request->wait_until(Clock::now()+std::chrono::seconds(10));});
        cancelled.stop();
        const bool stop_woke=stopped.wait_for(std::chrono::seconds(1))==std::future_status::ready;
        const auto terminal=stopped.get().state;
        check(stop_woke && (terminal==TextureState::Stopped || (submitted && terminal==TextureState::Ready)),
              "resource cancellation wakes a waiter without waiting for its deadline");
        check(!budget.resident->used() && !budget.staging->used() && request->snapshot().state==TextureState::Stopped,
              "cancellation before/after submission drains owned work without publishing it");
    }
    {
        // A draw waiting for a queued file moves it ahead of earlier demands: with the worker held on
        // the first upload (Prepared until submitted), the third demand is loaded before the second.
        const auto second=folder/L"second.dds",third=folder/L"third.dds";
        write_fixture(second,fixture(98));write_fixture(third,fixture(99));
        TextureCache ordered(device,budget);
        auto a=ordered.declare_image(file,reason),b=ordered.declare_image(second,reason),c=ordered.declare_image(third,reason);
        ordered.demand(a);await_texture(ordered,a,g,false);ordered.demand(b);ordered.demand(c);
        check(ordered.prioritize(c) && !ordered.prioritize(a) && b->snapshot().state==TextureState::Queued,
              "a queued request moves to the front; one already loading does not");
        check(await_texture(ordered,a,g,true).state==TextureState::Ready,"held upload completes");
        const auto c_first=await_texture(ordered,c,g,false);
        check(c_first.state==TextureState::Prepared && b->snapshot().state==TextureState::Queued,
              "the prioritized request is prepared before the earlier demand");
        check(await_texture(ordered,c,g,true).state==TextureState::Ready && await_texture(ordered,b,g,true).state==TextureState::Ready,
              "both requests complete after the reorder");
        ordered.stop();
        std::filesystem::remove(second,ignored);std::filesystem::remove(third,ignored);
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
// D-130: with the layer's upload queue an image is read straight into the loaders' staging arena and
// copied with the worker's own command buffer and fence; every level arrives as the file holds it.
static std::atomic<unsigned> staged_pools{0};
static VKAPI_ATTR VkResult VKAPI_CALL staged_create_pool(VkDevice d,const VkCommandPoolCreateInfo* i,const VkAllocationCallbacks* a,VkCommandPool* p)
{
    ++staged_pools;return pCreateCommandPool(d,i,a,p);
}
static std::vector<uint8_t> staged_fixture(uint32_t size,uint32_t levels,uint8_t seed)
{
    // A BC7 square with its mip chain, every payload byte distinct per file and position.
    std::vector<uint8_t> data(148,0);
    put(data,0,0x20534444);put(data,4,124);put(data,8,0x21007);put(data,12,size);put(data,16,size);
    put(data,28,levels);put(data,76,32);put(data,80,4);put(data,84,0x30315844);put(data,108,0x1000);
    put(data,128,98);put(data,132,3);put(data,140,1);
    size_t bytes=0;for(uint32_t l=0,w=size;l<levels;++l,w=std::max(1u,w/2))bytes+=size_t((w+3)/4)*((w+3)/4)*16;
    for(size_t i=0;i<bytes;++i)data.push_back(static_cast<uint8_t>(i*7+seed+(i>>8)));
    return data;
}
static std::vector<uint8_t> staged_readback(Gpu& g,const ImageUpload& image)
{
    const auto& meta=image.metadata();const uint32_t skipped=image.skipped_levels();
    const size_t base=meta.levels[skipped].offset;size_t bytes=0;
    for(uint32_t i=skipped;i<meta.levels.size();++i)bytes+=meta.levels[i].bytes;
    auto b=g.readback(bytes);auto cb=g.begin();const uint32_t count=static_cast<uint32_t>(meta.levels.size())-skipped;
    barrier(cb,image.image(),count,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    std::vector<VkBufferImageCopy> copies;
    for(uint32_t i=skipped;i<meta.levels.size();++i){const auto& level=meta.levels[i];VkBufferImageCopy copy{};
        copy.bufferOffset=level.offset-base;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,i-skipped,0,1};copy.imageExtent={level.width,level.height,1};copies.push_back(copy);}
    pCmdCopyImageToBuffer(cb,image.image(),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,b.buffer,static_cast<uint32_t>(copies.size()),copies.data());
    barrier(cb,image.image(),count,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
    g.finish(cb,b);
    const auto* mapped=static_cast<const uint8_t*>(b.mapped);return {mapped,mapped+bytes};
}
// An 8-bit RGBA PNG in one stored zlib block, samples distinct per position.
static std::vector<uint8_t> png_fixture(uint32_t width,uint32_t height)
{
    auto crc=[](const uint8_t* p,size_t n){uint32_t c=0xffffffffu;for(size_t i=0;i<n;++i){c^=p[i];for(int k=0;k<8;++k)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}return ~c;};
    auto be=[](std::vector<uint8_t>& out,uint32_t v){for(int s=24;s>=0;s-=8)out.push_back(uint8_t(v>>s));};
    auto chunk=[&](std::vector<uint8_t>& out,const char* type,const std::vector<uint8_t>& data){
        be(out,uint32_t(data.size()));const size_t start=out.size();out.insert(out.end(),type,type+4);out.insert(out.end(),data.begin(),data.end());
        be(out,crc(out.data()+start,out.size()-start));};
    std::vector<uint8_t> rows;
    for(uint32_t y=0;y<height;++y){rows.push_back(0);for(uint32_t i=0;i<width*4;++i)rows.push_back(uint8_t((y*71+i*29+13)&0xff));}
    std::vector<uint8_t> out{137,80,78,71,13,10,26,10},header;be(header,width);be(header,height);header.insert(header.end(),{8,6,0,0,0});
    chunk(out,"IHDR",header);
    std::vector<uint8_t> z{0x78,0x01,1,uint8_t(rows.size()),uint8_t(rows.size()>>8),uint8_t(~rows.size()),uint8_t(~rows.size()>>8)};
    z.insert(z.end(),rows.begin(),rows.end());uint32_t a=1,b=0;for(auto c:rows){a=(a+c)%65521;b=(b+a)%65521;}be(z,b<<16|a);
    chunk(out,"IDAT",z);chunk(out,"IEND",{});return out;
}
// D-139: a file not named .dds is decoded as the original's WIC loader decodes it, on the worker's own
// staging and through the staging arena, and arrives as the decoder made it; a file WIC makes nothing of fails.
void wic_tests(Gpu& g)
{
    const auto folder=std::filesystem::current_path()/L"wic-fixtures";std::filesystem::create_directories(folder);
    const auto png=png_fixture(8,4);write_fixture(folder/L"image.png",png);
    write_fixture(folder/L"broken.png",{'n','o','t',' ','a','n',' ','i','m','a','g','e'});
    PngPixels expected;std::string reason;
    check(decode_wic(png.data(),png.size(),expected,reason),"the WIC fixture decodes");
    std::mutex queue_mutex;
    for(const bool staged:{false,true}){
        TextureDevice device=cache_device(g);
        if(staged){device.dispatch=g.dispatch;device.dispatch.CreateCommandPool=staged_create_pool;device.upload_queue=g.queue;device.upload_family=g.family;
            device.upload_mutex=&queue_mutex;device.readback=true;
#define X(name) device.buffer_dispatch.name=p##name;
            EFMI_BUFFER_UPLOAD_FUNCS(X)
#undef X
        }
        TextureCache cache(device,{},16,1);
        auto image=cache.declare_image(folder/L"image.png",reason),broken=cache.declare_image(folder/L"broken.png",reason);
        check(image && broken && cache.demand(image) && cache.demand(broken),"WIC fixtures declared and demanded");
        auto settle=[&](const TextureHandle& h){return staged?h->wait_until(Clock::now()+std::chrono::seconds(15)):await_texture(cache,h,g,true);};
        const auto ready=settle(image),failed=settle(broken);
        const bool loaded=ready.state==TextureState::Ready && ready.image && ready.image->metadata().format==expected.metadata.format &&
                          ready.image->metadata().width==8 && ready.image->metadata().height==4;
        check(loaded,staged?"a PNG through the staging arena loads as the decoder made it":"a PNG on the worker's staging loads as the decoder made it");
        if(staged && loaded)check(staged_readback(g,*ready.image)==expected.bytes,"the PNG's pixels arrive as decoded");
        check(failed.state==TextureState::Failed,"a file WIC makes nothing of fails its load");
        // D-140: a declaration's sRGB colour space is the DDS loader's forceSRGB, another texture of the same file.
        const auto dds=fixture(98);write_fixture(folder/L"bc7.dds",dds);
        auto srgb=cache.declare_image(folder/L"bc7.dds",reason,0,png_force_srgb),plain=cache.declare_image(folder/L"bc7.dds",reason);
        check(srgb && plain && srgb!=plain && cache.demand(srgb) && cache.demand(plain),"a file declared in two colour spaces is two textures");
        const auto srgb_ready=settle(srgb),plain_ready=settle(plain);
        const bool formats=srgb_ready.state==TextureState::Ready && plain_ready.state==TextureState::Ready && srgb_ready.image && plain_ready.image &&
            srgb_ready.image->metadata().format==VK_FORMAT_BC7_SRGB_BLOCK && plain_ready.image->metadata().format==VK_FORMAT_BC7_UNORM_BLOCK;
        check(formats,"the sRGB declaration loads as BC7_SRGB, the other as BC7_UNORM");
        if(staged && formats){const std::vector<uint8_t> payload(dds.begin()+srgb_ready.image->metadata().levels[0].offset,dds.end());
            check(staged_readback(g,*srgb_ready.image)==payload,"the sRGB texture holds the file's blocks as they are");}
        cache.stop();
    }
}
void staged_tests(Gpu& g)
{
    TextureDevice device;device.device=g.device;device.physical=g.physical;device.graphics_family=g.family;device.bc_enabled=g.bc;
    device.memory=g.memory;device.dispatch=g.dispatch;device.dispatch.CreateCommandPool=staged_create_pool;
    device.format_properties=pGetPhysicalDeviceFormatProperties;device.image_properties=pGetPhysicalDeviceImageFormatProperties;
#define X(name) device.buffer_dispatch.name=p##name;
    EFMI_BUFFER_UPLOAD_FUNCS(X)
#undef X
    std::mutex queue_mutex;device.upload_queue=g.queue;device.upload_family=g.family;device.upload_mutex=&queue_mutex;
    device.arena_idle_us=200000;device.readback=true;
    const auto folder=std::filesystem::current_path()/L"staged-fixtures";std::filesystem::create_directories(folder);
    const std::vector<std::vector<uint8_t>> files={staged_fixture(64,7,1),staged_fixture(32,6,2),fixture(99),staged_fixture(64,7,3)};
    TextureBudgets budget;budget.staging=std::make_shared<AllocationBudget>(160ull<<20);
    {
        TextureCache cache(device,budget,16,2);std::string reason;
        std::vector<TextureHandle> handles;
        for(size_t i=0;i<files.size();++i){
            const auto path=folder/(L"staged"+std::to_wstring(i)+L".dds");write_fixture(path,files[i]);
            handles.push_back(cache.declare_image(path,reason));
        }
        // The last one twice: once whole, once without its top level (as a texture loads under pressure).
        auto reduced=cache.declare_image(folder/L"staged3.dds",reason,7);
        bool demanded=reduced && cache.demand(reduced,1);
        for(auto& h:handles)demanded=demanded && h && cache.demand(h);
        check(demanded,"staged fixtures declared and demanded");
        auto wait=[](const TextureHandle& h){return h->wait_until(Clock::now()+std::chrono::seconds(15));};
        // Every load done before the test's own submissions: the queue is the loaders' too.
        std::vector<TextureSnapshot> loaded;for(auto& h:handles)loaded.push_back(wait(h));
        const auto small=wait(reduced);
        bool exact=true;
        for(size_t i=0;i<handles.size();++i){
            const auto& ready=loaded[i];
            if(ready.state!=TextureState::Ready || !ready.image){exact=false;continue;}
            const auto& meta=ready.image->metadata();
            exact=exact && staged_readback(g,*ready.image)==std::vector<uint8_t>(files[i].begin()+meta.levels[0].offset,files[i].end());
        }
        check(exact,"every level of every image read through the staging arena arrives as the file holds it");
        const bool reduced_ok=small.state==TextureState::Ready && small.image && small.image->skipped_levels()==1 &&
            staged_readback(g,*small.image)==std::vector<uint8_t>(files[3].begin()+small.image->metadata().levels[1].offset,files[3].end());
        check(reduced_ok,"an image loaded without its top level takes the file's levels from the second on");
        const auto stats=cache.stats();
        check(stats.ready==files.size()+1 && stats.arenas==1 && budget.staging->peak()==budget.staging->limit(),
              "one staging arena serves every load of a busy period, charged to the staging budget");
        check(staged_pools<=2,"each worker keeps one command pool across its loads");
        const auto until=Clock::now()+std::chrono::seconds(5);
        while(budget.staging->used() && Clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(!budget.staging->used(),"the arena is released once no load used it for the idle time");
        auto again=cache.declare_image(folder/L"staged1.dds",reason,9);
        const bool reloaded=again && cache.demand(again) && wait(again).state==TextureState::Ready && cache.stats().arenas==2;
        check(reloaded,"a later load makes a new arena");
        cache.stop();
    }
    check(!budget.staging->used() && !budget.source->used(),"stopping the loaders returns the arena");
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
