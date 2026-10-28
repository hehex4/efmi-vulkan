#include "resource_cache.h"
#include "png.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace efmi::assets {
namespace {
struct File {
    HANDLE handle=INVALID_HANDLE_VALUE;
    ~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
struct SourceCharge {
    std::shared_ptr<AllocationBudget> budget;
    uint64_t bytes=0;
    ~SourceCharge(){if(bytes)budget->release(bytes);}
};
}
// D-130: what a worker keeps between its loads: a command pool, a command buffer and a fence on the
// upload family, made at its first load (an image's upload made and freed them every time).
struct ResourceCache::Loader {
    const ResourceDevice& d;
    VkCommandPool pool=VK_NULL_HANDLE;
    VkCommandBuffer command=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    bool in_flight=false;
    explicit Loader(const ResourceDevice& device):d(device) {}
    ~Loader()
    {
        // Only on stop or shutdown with a copy outstanding: the arena it reads outlives the workers.
        if(in_flight)d.dispatch.WaitForFences(d.device,1,&fence,VK_TRUE,UINT64_MAX);
        if(fence)d.dispatch.DestroyFence(d.device,fence,nullptr);
        if(pool)d.dispatch.DestroyCommandPool(d.device,pool,nullptr);
    }
    void check(VkResult result,const char* what){if(result!=VK_SUCCESS)throw std::runtime_error(std::string(what)+": "+std::to_string(result));}
    void begin()
    {
        if(!pool){
            VkCommandPoolCreateInfo info={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            info.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;info.queueFamilyIndex=d.upload_family;
            check(d.dispatch.CreateCommandPool(d.device,&info,nullptr,&pool),"create loader command pool");
            VkCommandBufferAllocateInfo ai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
            check(d.dispatch.AllocateCommandBuffers(d.device,&ai,&command),"allocate loader command buffer");
            if(d.dispatch.SetDeviceLoaderData)check(d.dispatch.SetDeviceLoaderData(d.device,command),"initialize loader command dispatch");
            VkFenceCreateInfo fi={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            check(d.dispatch.CreateFence(d.device,&fi,nullptr,&fence),"create loader fence");
        }else{
            check(d.dispatch.ResetCommandPool(d.device,pool,0),"reset loader command pool");
            check(d.dispatch.ResetFences(d.device,1,&fence),"reset loader fence");
        }
        VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(d.dispatch.BeginCommandBuffer(command,&begin),"begin loader commands");
    }
    void end(){check(d.dispatch.EndCommandBuffer(command),"end loader commands");}
    bool submit(VkSemaphore timeline,uint64_t value)
    {
        VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        VkTimelineSemaphoreSubmitInfo values={VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
        if(timeline){values.signalSemaphoreValueCount=1;values.pSignalSemaphoreValues=&value;submit.pNext=&values;
            submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&timeline;}
        in_flight=d.dispatch.QueueSubmit(d.upload_queue,1,&submit,fence)==VK_SUCCESS;
        return in_flight;
    }
};
// D-130: the staging every worker reads files into, one host-visible buffer kept while loads keep
// coming. The original reads a file into one buffer and the driver copies it from there; a staging
// allocation per load paid the system's zeroing of its pages and a second copy every time.
struct ResourceCache::Arena {
    VkBuffer buffer=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    uint8_t* mapped=nullptr;
    uint64_t size=0,charged=0;
    std::map<uint64_t,uint64_t> free; // offset to bytes, coalesced
    unsigned slices=0;
    uint64_t idle_since_us=0;
};
// One load's part of the arena, given back when its copy completed (or the load failed).
struct ResourceCache::Slice {
    ResourceCache* cache=nullptr;
    uint64_t offset=0,bytes=0;
    ~Slice(){if(cache && bytes)cache->arena_give(offset,bytes);}
};
struct ResourceCache::Upload {
    std::shared_ptr<ImageUpload> image;
    std::shared_ptr<BufferUpload> buffer;
    uint64_t serial=0;
    bool submit(VkQueue queue,VkSemaphore timeline,uint64_t value){
        serial=timeline?value:0;return image?image->submit(queue,timeline,value):buffer->submit(queue,timeline,value);}
    UploadState poll(){return image?image->poll():buffer->poll();}
    UploadState state() const {return image?image->state():buffer->state();}
    const std::string& reason() const {return image?image->reason():buffer->reason();}
};
ResourceSnapshot ResourceRequest::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);return value_;
}
ResourceSnapshot ResourceRequest::wait_until(std::chrono::steady_clock::time_point deadline) const
{
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait_until(lock, deadline, [&] {
        return value_.state == ResourceState::Ready || value_.state == ResourceState::Failed ||
               value_.state == ResourceState::Stopped || value_.state == ResourceState::Dormant;
    });
    return value_;
}
ResourceCache::ResourceCache(const ResourceDevice& device,ResourceBudgets budgets,size_t capacity,unsigned workers)
    :device_(device),budgets_(std::move(budgets)),capacity_(std::min<size_t>(capacity,4096))
{
    if(!device_.device || !device_.physical || !device_.format_properties || !device_.image_properties ||
       !budgets_.resident || !budgets_.staging || !budgets_.source)
        throw std::invalid_argument("Incomplete resource cache device or budgets");
    for(unsigned i=0;i<std::max(1u,workers);++i)workers_.emplace_back(&ResourceCache::run,this);
}
ResourceCache::~ResourceCache(){stop();}
void ResourceCache::publish(const ResourceHandle& entry,ResourceState state,
                            std::shared_ptr<Upload> upload,std::string reason)
{
    std::lock_guard<std::mutex> lock(entry->mutex_);
    entry->value_={state,upload?upload->image:nullptr,upload?upload->buffer:nullptr,std::move(reason),upload?upload->serial:0};
    entry->changed_.notify_all();
}
ResourceHandle ResourceCache::declare_image(const std::filesystem::path& path,std::string& reason,uint64_t tag,uint32_t loader_flags)
{
    return declare(path,0,reason,tag,loader_flags);
}
ResourceHandle ResourceCache::declare_buffer(const std::filesystem::path& path,VkBufferUsageFlags usage,std::string& reason)
{
    const VkBufferUsageFlags allowed=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if(!usage || (usage&~allowed)){reason="Unsupported buffer declaration usage";return {};}
    return declare(path,usage,reason);
}
ResourceHandle ResourceCache::declare(const std::filesystem::path& path,VkBufferUsageFlags usage,std::string& reason,uint64_t tag,
                                      uint32_t loader_flags)
{
    reason.clear();
    if(!path.is_absolute() || path.native().size()>32760 || path.native().find(L'\0')!=std::wstring::npos){
        reason="Resource path must be a bounded absolute path without nulls";return {};
    }
    const Key key{path.lexically_normal(),usage,tag,loader_flags};
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopped_){reason="Resource cache stopped";return {};}
    auto found=entries_.find(key);if(found!=entries_.end())return found->second;
    if(entries_.size()>=capacity_){reason="Resource declaration capacity exceeded";return {};}
    auto entry=std::make_shared<ResourceRequest>();entry->path_=std::get<0>(key);entry->buffer_usage_=usage;entry->owner_=identity_;
    entry->loader_flags_=loader_flags;
    entries_.emplace(key,entry);return entry;
}
bool ResourceCache::demand(const ResourceHandle& entry,uint32_t skip_levels)
{
    if(!entry || entry->owner_.lock()!=identity_)return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopped_)return false;
    const auto state=entry->snapshot().state;
    if(state==ResourceState::Dormant){entry->skip_levels_=skip_levels;entry->busy_since_us_=0;queue_.push_back(entry);publish(entry,ResourceState::Queued);wake_.notify_one();}
    return state!=ResourceState::Failed && state!=ResourceState::Stopped;
}
bool ResourceCache::release(const ResourceHandle& entry)
{
    if(!entry || entry->owner_.lock()!=identity_)return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopped_)return false;
    const auto state=entry->snapshot().state;
    if(state==ResourceState::Queued){
        const auto found=std::find(queue_.begin(),queue_.end(),entry);
        if(found!=queue_.end())queue_.erase(found);
    }else if(state!=ResourceState::Ready)return false;
    publish(entry,ResourceState::Dormant);++stats_.releases;return true;
}
bool ResourceCache::prioritize(const ResourceHandle& entry)
{
    if(!entry || entry->owner_.lock()!=identity_)return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopped_)return false;
    const auto found=std::find(queue_.begin(),queue_.end(),entry);
    if(found==queue_.end())return false;
    if(found!=queue_.begin()){queue_.erase(found);queue_.push_front(entry);}
    return true;
}
ResourceCacheStats ResourceCache::stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);return stats_;
}
namespace {
// A budget that other loads hold for the moment (the aggregate source, staging and memory budgets):
// the load goes again shortly. Every other failure (a file that is no DDS, a size outside a limit of
// its own) is final.
struct BudgetBusy:std::runtime_error{using std::runtime_error::runtime_error;};
[[noreturn]] void fail_upload(const std::string& reason)
{
    if(reason.rfind("Aggregate ",0)==0)throw BudgetBusy(reason);
    throw std::runtime_error(reason);
}
uint64_t steady_us(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
// As the original (CustomResource::LoadFromFile): a file named .dds is a DDS, any other the system's WIC decodes.
bool wic_image(const std::filesystem::path& path){return _wcsicmp(path.extension().c_str(),L".dds")!=0;}
PngPixels decoded_wic(const std::vector<uint8_t>& bytes,uint32_t flags)
{
    PngPixels pixels;std::string reason;
    if(!decode_wic(bytes.data(),bytes.size(),pixels,reason,{},flags))throw std::runtime_error("WIC image: "+reason);
    return pixels;
}
// The DDS loader's forced sRGB (CreateDDSTextureFromFileEx forceSRGB): the levels as they are, read as sRGB.
void dds_color_space(DdsImage& meta,uint32_t flags){if(flags&png_force_srgb)meta.format=srgb_format(meta.format);}
constexpr uint64_t kArenaBytes=256ull<<20;      // three loaders' 64 MiB maps at once, an 85 MiB one beside them
constexpr uint64_t kArenaAlignment=256;
}
uint64_t ResourceCache::arena_bytes() const
{
    return std::min<uint64_t>(kArenaBytes,budgets_.staging->limit());
}
bool ResourceCache::arena_take(uint64_t bytes,uint64_t& offset)
{
    bytes=(bytes+kArenaAlignment-1)&~(kArenaAlignment-1);
    std::lock_guard<std::mutex> lock(arena_mutex_);
    if(!arena_){
        const uint64_t size=arena_bytes();
        if(!budgets_.staging->acquire(size))return false; // others hold staging: the load goes again shortly
        auto arena=std::make_unique<Arena>();arena->charged=size;
        const auto& d=device_.dispatch;
        auto fail=[&](const char* what){
            if(arena->mapped)d.UnmapMemory(device_.device,arena->memory);
            if(arena->buffer)d.DestroyBuffer(device_.device,arena->buffer,nullptr);
            if(arena->memory)d.FreeMemory(device_.device,arena->memory,nullptr);
            budgets_.staging->release(size);throw std::runtime_error(what);
        };
        VkBufferCreateInfo info={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=size;info.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if(d.CreateBuffer(device_.device,&info,nullptr,&arena->buffer)!=VK_SUCCESS)fail("create staging arena");
        VkMemoryRequirements req{};d.GetBufferMemoryRequirements(device_.device,arena->buffer,&req);
        // Host memory the CPU writes and caches (the file read lands in it), else any host-visible type.
        const VkMemoryPropertyFlags wanted=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        int type=-1;
        for(int pass=0;pass<3 && type<0;++pass)
            for(uint32_t i=0;i<device_.memory.memoryTypeCount;++i){
                const auto flags=device_.memory.memoryTypes[i].propertyFlags;
                if(!(req.memoryTypeBits&(1u<<i)) || (flags&wanted)!=wanted)continue;
                if(pass==0 && (!(flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT) || (flags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))continue;
                if(pass==1 && (flags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))continue;
                type=static_cast<int>(i);break;
            }
        if(type<0)fail("staging arena memory type unavailable");
        VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=static_cast<uint32_t>(type);
        if(d.AllocateMemory(device_.device,&ai,nullptr,&arena->memory)!=VK_SUCCESS)fail("allocate staging arena");
        if(d.BindBufferMemory(device_.device,arena->buffer,arena->memory,0)!=VK_SUCCESS)fail("bind staging arena");
        void* mapped=nullptr;
        if(d.MapMemory(device_.device,arena->memory,0,VK_WHOLE_SIZE,0,&mapped)!=VK_SUCCESS)fail("map staging arena");
        arena->mapped=static_cast<uint8_t*>(mapped);arena->size=size;arena->free.emplace(0,size);
        arena_=std::move(arena);
        {std::lock_guard<std::mutex> stats(mutex_);++stats_.arenas;}
    }
    for(auto it=arena_->free.begin();it!=arena_->free.end();++it){
        if(it->second<bytes)continue;
        offset=it->first;const uint64_t rest=it->second-bytes;
        arena_->free.erase(it);
        if(rest)arena_->free.emplace(offset+bytes,rest);
        ++arena_->slices;return true;
    }
    return false;
}
void ResourceCache::arena_give(uint64_t offset,uint64_t bytes)
{
    bytes=(bytes+kArenaAlignment-1)&~(kArenaAlignment-1);
    std::lock_guard<std::mutex> lock(arena_mutex_);
    if(!arena_)return;
    auto next=arena_->free.emplace(offset,bytes).first;
    if(next!=arena_->free.begin()){
        auto before=std::prev(next);
        if(before->first+before->second==next->first){before->second+=next->second;arena_->free.erase(next);next=before;}
    }
    auto after=std::next(next);
    if(after!=arena_->free.end() && next->first+next->second==after->first){next->second+=after->second;arena_->free.erase(after);}
    if(!--arena_->slices)arena_->idle_since_us=steady_us();
}
void ResourceCache::arena_idle()
{
    std::unique_ptr<Arena> released;
    {
        std::lock_guard<std::mutex> lock(arena_mutex_);
        if(!arena_ || arena_->slices || steady_us()-arena_->idle_since_us<device_.arena_idle_us)return;
        released=std::move(arena_);
    }
    const auto& d=device_.dispatch;
    d.UnmapMemory(device_.device,released->memory);d.DestroyBuffer(device_.device,released->buffer,nullptr);
    d.FreeMemory(device_.device,released->memory,nullptr);budgets_.staging->release(released->charged);
}
std::shared_ptr<ResourceCache::Upload> ResourceCache::prepare(const ResourceHandle& entry,uint64_t& read_done_us)
{
    // Deny concurrent writes/deletes while taking an immutable byte snapshot.
    File file;file.handle=CreateFileW(entry->path_.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,
                                    OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file.handle==INVALID_HANDLE_VALUE || GetFileType(file.handle)!=FILE_TYPE_DISK)
        throw std::runtime_error("Cannot open resource file for a stable read");
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file.handle,&size) || size.QuadPart<=0 || size.QuadPart>(128ll<<20))
        throw std::runtime_error("Resource file size outside source budget");
    if(!budgets_.source->acquire(static_cast<uint64_t>(size.QuadPart)))
        throw BudgetBusy("Aggregate resource source budget exceeded");
    SourceCharge charge{budgets_.source,static_cast<uint64_t>(size.QuadPart)};
    std::vector<uint8_t> bytes(static_cast<size_t>(size.QuadPart));DWORD read=0;
    if(!ReadFile(file.handle,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) || read!=bytes.size())
        throw std::runtime_error("Incomplete resource file read");
    read_done_us=steady_us();
    {std::lock_guard<std::mutex> lock(mutex_);++stats_.file_reads;}
    auto ticket=std::make_shared<Upload>();
    // With the layer's upload queue the copy runs on its family, the object shared with the graphics family (D-103).
    const bool own_queue=device_.upload_queue!=VK_NULL_HANDLE && device_.upload_family!=UINT32_MAX;
    const uint32_t record_family=own_queue?device_.upload_family:device_.graphics_family;
    if(entry->buffer_usage_){
        ticket->buffer=std::make_shared<BufferUpload>(device_.device,device_.buffer_dispatch,budgets_.resident,budgets_.staging);
        if(own_queue)ticket->buffer->share(device_.graphics_family,device_.upload_family);
        if(!ticket->buffer->prepare(bytes.data(),bytes.size(),entry->buffer_usage_,device_.memory) ||
           !ticket->buffer->record(record_family,!own_queue || record_family==device_.graphics_family))fail_upload(ticket->buffer->reason());
        return ticket;
    }
    DdsImage meta;std::string reason;PngPixels pixels;
    if(wic_image(entry->path_))meta=(pixels=decoded_wic(bytes,entry->loader_flags_)).metadata;
    else if(!decode_dds(bytes.data(),bytes.size(),meta,reason))throw std::runtime_error(reason);
    else dds_color_space(meta,entry->loader_flags_);
    VkFormatProperties features{};device_.format_properties(device_.physical,meta.format,&features);
    VkImageFormatProperties limits{};
    const auto result=device_.image_properties(device_.physical,meta.format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,
                                               VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,0,&limits);
    if(result!=VK_SUCCESS)throw std::runtime_error("Resource format unsupported for sampled upload");
    auto upload=std::make_shared<ImageUpload>(device_.device,device_.dispatch,budgets_.resident,budgets_.staging);
    if(own_queue)upload->share(device_.graphics_family,device_.upload_family);
    // The levels as decoded: a DDS's in its file bytes (its format as the colour space has it), a WIC image's pixels.
    const auto& payload=pixels.bytes.empty()?bytes:pixels.bytes;
    if(!upload->prepare_decoded(meta,payload.data(),payload.size(),device_.memory,limits,features.optimalTilingFeatures,
                                device_.bc_enabled,false,entry->skip_levels_,device_.memory_priority) ||
       !upload->record(record_family))fail_upload(upload->reason());
    ticket->image=std::move(upload);return ticket;
}
std::shared_ptr<ResourceCache::Upload> ResourceCache::stage(const ResourceHandle& entry,Loader& loader,Slice& slice,uint64_t& read_done_us)
{
    File file;file.handle=CreateFileW(entry->path_.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,
                                    OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file.handle==INVALID_HANDLE_VALUE || GetFileType(file.handle)!=FILE_TYPE_DISK)
        throw std::runtime_error("Cannot open resource file for a stable read");
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file.handle,&size) || size.QuadPart<=0 || size.QuadPart>(128ll<<20))
        throw std::runtime_error("Resource file size outside source budget");
    // The header first: what the image is, and where in the file the levels it takes begin. A WIC image is
    // decoded whole, its pixels the payload.
    DWORD read=0;DdsImage meta;std::string reason;PngPixels pixels;
    if(wic_image(entry->path_)){
        std::vector<uint8_t> bytes(static_cast<size_t>(size.QuadPart));
        if(!ReadFile(file.handle,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) || read!=bytes.size())
            throw std::runtime_error("Incomplete resource file read");
        meta=(pixels=decoded_wic(bytes,entry->loader_flags_)).metadata;
    }else{
        uint8_t header[kDdsHeaderBytes]{};
        const DWORD header_bytes=static_cast<DWORD>(std::min<int64_t>(size.QuadPart,static_cast<int64_t>(kDdsHeaderBytes)));
        if(!ReadFile(file.handle,header,header_bytes,&read,nullptr) || read!=header_bytes)throw std::runtime_error("Incomplete resource file read");
        if(!decode_dds_header(header,read,static_cast<uint64_t>(size.QuadPart),meta,reason))throw std::runtime_error(reason);
        dds_color_space(meta,entry->loader_flags_);
    }
    VkFormatProperties features{};device_.format_properties(device_.physical,meta.format,&features);
    VkImageFormatProperties limits{};
    if(device_.image_properties(device_.physical,meta.format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(device_.readback?VK_IMAGE_USAGE_TRANSFER_SRC_BIT:0),0,&limits)!=VK_SUCCESS)
        throw std::runtime_error("Resource format unsupported for sampled upload");
    const auto base=meta.levels[ImageUpload::levels_skipped(meta,entry->skip_levels_)].offset;
    // The levels the image takes: the first layer's of an array or cube (D-149).
    const uint64_t payload=pixels.bytes.empty()?uint64_t(meta.levels.back().offset+meta.levels.back().bytes)-base:pixels.bytes.size();
    // The payload straight into the arena: one read, no copy of our own.
    uint64_t offset=0;
    if(!arena_take(payload,offset))throw BudgetBusy("Aggregate staging arena busy");
    slice.cache=this;slice.offset=offset;slice.bytes=payload;
    uint8_t* target;{std::lock_guard<std::mutex> lock(arena_mutex_);target=arena_->mapped+offset;}
    if(!pixels.bytes.empty())std::memcpy(target,pixels.bytes.data(),pixels.bytes.size());
    else{
        LARGE_INTEGER at{};at.QuadPart=static_cast<LONGLONG>(base);
        if(!SetFilePointerEx(file.handle,at,nullptr,FILE_BEGIN) || !ReadFile(file.handle,target,static_cast<DWORD>(payload),&read,nullptr) || read!=payload)
            throw std::runtime_error("Incomplete resource file read");
    }
    read_done_us=steady_us();
    {std::lock_guard<std::mutex> lock(mutex_);++stats_.file_reads;}
    auto upload=std::make_shared<ImageUpload>(device_.device,device_.dispatch,budgets_.resident,budgets_.staging);
    upload->share(device_.graphics_family,device_.upload_family);
    if(!upload->prepare_image(meta,device_.memory,limits,features.optimalTilingFeatures,device_.bc_enabled,entry->skip_levels_,device_.memory_priority,
                              device_.readback))
        fail_upload(upload->reason());
    VkBuffer buffer;{std::lock_guard<std::mutex> lock(arena_mutex_);buffer=arena_->buffer;}
    loader.begin();
    if(!upload->record_copy(loader.command,buffer,offset))fail_upload(upload->reason());
    loader.end();
    auto ticket=std::make_shared<Upload>();ticket->image=std::move(upload);return ticket;
}
bool ResourceCache::submit_prepared(VkQueue queue,VkSemaphore timeline,uint64_t value)
{
    if(!queue)return false;
    std::lock_guard<std::mutex> service(service_mutex_);
    std::shared_ptr<Upload> upload;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(stopped_ || phase_!=ResourceState::Prepared || !upload_)return false;
        // A Prepared upload has no predecessor in flight (the worker waited for the last one), so the
        // next may go to any queue of the family the application submits on (D-090).
        submission_queue_=queue;
        upload=upload_;phase_=ResourceState::Submitting;publish(pending_,phase_);
    }
    const bool ok=upload->submit(queue,timeline,value);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        phase_=ok?ResourceState::Submitted:ResourceState::Failed;
        if(ok)++stats_.submissions;
        publish(pending_,phase_,{},ok?std::string{}:upload->reason());wake_.notify_one();
    }
    return ok;
}
void ResourceCache::run()
{
    Loader loader(device_);
    // Images through the arena and the worker's own commands when the layer has an upload queue (D-130).
    const bool staging=device_.upload_queue!=VK_NULL_HANDLE && device_.upload_family!=UINT32_MAX && arena_bytes()>=(128ull<<20) &&
                       device_.dispatch.ResetCommandPool && device_.dispatch.ResetFences;
    while(true){
        ResourceHandle entry;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(!stopped_ && queue_.empty()){
                const auto tick=std::chrono::microseconds(std::clamp<uint64_t>(device_.arena_idle_us/4,1000,1000000));
                if(wake_.wait_for(lock,tick)==std::cv_status::timeout){lock.unlock();arena_idle();lock.lock();}
            }
            if(stopped_)return;
            pending_=entry=queue_.front();queue_.pop_front();phase_=ResourceState::Loading;publish(entry,phase_);
        }
        std::shared_ptr<Upload> upload;std::string reason;bool busy=false;
        const uint64_t began_us=steady_us();uint64_t read_us=began_us;
        const bool staged=staging && !entry->buffer_usage_;
        Slice slice;
        try{upload=staged?stage(entry,loader,slice,read_us):prepare(entry,read_us);}
        catch(const BudgetBusy& error){reason=error.what();busy=true;}
        catch(const std::exception& error){reason=error.what();}
        if(!upload && busy){
            // Another loader holds the staging, source or memory budget: this one goes again in a
            // moment, behind the others queued, for up to ten seconds (D-103). A file that is no
            // DDS or outside a limit of its own fails below at once.
            std::unique_lock<std::mutex> lock(mutex_);
            if(stopped_)return;
            const uint64_t now=steady_us();
            if(!entry->busy_since_us_)entry->busy_since_us_=now;
            if(now-entry->busy_since_us_<10000000ull){
                publish(entry,ResourceState::Queued);queue_.push_back(entry);
                wake_.wait_for(lock,std::chrono::milliseconds(2),[&]{return stopped_;});
                if(stopped_)return;
                continue;
            }
            reason+=" (waited 10 s)";
        }
        entry->busy_since_us_=0;
        const uint64_t prepared_us=steady_us();
        auto ready=[&]{
            const uint64_t now=steady_us();++stats_.ready;
            stats_.read_us+=read_us-began_us;stats_.prepare_us+=prepared_us-read_us;stats_.complete_us+=now-prepared_us;
            stats_.load_max_us=std::max(stats_.load_max_us,now-began_us);
        };
        if(upload && staged){
            // Submitted on the layer's queue with the next serial (taken under the queue's mutex, D-103),
            // then the worker waits on its own fence: Ready as soon as the copy completed.
            bool ok=false;
            {
                std::unique_lock<std::mutex> queue_lock(*device_.upload_mutex,std::defer_lock);
                if(device_.upload_mutex)queue_lock.lock();
                const uint64_t value=device_.serial?device_.serial->fetch_add(1)+1:0;
                upload->serial=value;
                ok=loader.submit(value?device_.timeline:VK_NULL_HANDLE,value);
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if(stopped_)return;
                if(!ok){++stats_.failed;publish(entry,ResourceState::Failed,{},"submit image upload");continue;}
                ++stats_.submissions;publish(entry,ResourceState::Submitted,upload);
            }
            VkResult waited=VK_TIMEOUT;
            while(waited==VK_TIMEOUT){
                waited=device_.dispatch.WaitForFences(device_.device,1,&loader.fence,VK_TRUE,2000000ull);
                std::lock_guard<std::mutex> lock(mutex_);
                if(stopped_)return;
            }
            loader.in_flight=false;
            upload->image->loaded(waited==VK_SUCCESS,"wait for image upload: "+std::to_string(waited));
            std::lock_guard<std::mutex> lock(mutex_);
            if(upload->image->state()==UploadState::Ready){ready();publish(entry,ResourceState::Ready,upload);}
            else{++stats_.failed;publish(entry,ResourceState::Failed,{},upload->image->reason());}
            continue;
        }
        if(upload && device_.upload_queue){
            // The layer's own queue: submitted here, signalling the device's timeline with the next serial.
            // The serial is taken under the queue's mutex: a timeline's signals must rise in submission
            // order, and three loaders submit at once.
            bool ok=false;
            {
                std::unique_lock<std::mutex> queue_lock(*device_.upload_mutex,std::defer_lock);
                if(device_.upload_mutex)queue_lock.lock();
                const uint64_t value=device_.serial?device_.serial->fetch_add(1)+1:0;
                ok=upload->submit(device_.upload_queue,value?device_.timeline:VK_NULL_HANDLE,value);
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if(stopped_)return;
                if(!ok){++stats_.failed;publish(entry,ResourceState::Failed,{},upload->reason());continue;}
                ++stats_.submissions;publish(entry,ResourceState::Submitted,upload);
            }
            while(upload->state()==UploadState::Submitted){
                if(upload->poll()!=UploadState::Submitted)break;
                std::unique_lock<std::mutex> lock(mutex_);
                if(wake_.wait_for(lock,std::chrono::milliseconds(1),[&]{return stopped_;}))return;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            if(stopped_)return;
            if(upload->state()==UploadState::Ready){ready();publish(entry,ResourceState::Ready,upload);}
            else{++stats_.failed;publish(entry,ResourceState::Failed,{},upload->reason());}
            continue;
        }
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if(stopped_)return;
            if(!upload){++stats_.failed;phase_=ResourceState::Failed;publish(entry,phase_,{},reason);pending_.reset();continue;}
            upload_=upload;phase_=ResourceState::Prepared;publish(entry,phase_);
            wake_.wait(lock,[&]{return stopped_ || phase_==ResourceState::Submitted || phase_==ResourceState::Failed;});
            if(stopped_)return;
        }
        // Only the worker polls and releases staging. The caller's queue path
        // already finished QueueSubmit before publishing Submitted.
        while(upload->state()==UploadState::Submitted){
            if(upload->poll()!=UploadState::Submitted)break;
            std::unique_lock<std::mutex> lock(mutex_);
            if(wake_.wait_for(lock,std::chrono::milliseconds(2),[&]{return stopped_;}))return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(stopped_)return;
            if(upload->state()==UploadState::Ready){ready();publish(entry,ResourceState::Ready,upload);}
            else{++stats_.failed;publish(entry,ResourceState::Failed,{},upload->reason());}
            // Local ownership keeps any driver destruction outside mutex_.
            upload_.reset();pending_.reset();phase_=ResourceState::Dormant;
        }
    }
}
void ResourceCache::stop()
{
    // Serialized against queue handoff; stop is never called from a draw.
    std::lock_guard<std::mutex> service(service_mutex_);
    std::map<Key,ResourceHandle> retired;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_=true;retired.swap(entries_);queue_.clear();wake_.notify_all(); // every loader (D-103)
    }
    for(auto& worker:workers_)if(worker.joinable())worker.join();
    // Every worker waited for its copy before it went (~Loader): the arena is unused.
    if(arena_){
        const auto& d=device_.dispatch;
        d.UnmapMemory(device_.device,arena_->memory);d.DestroyBuffer(device_.device,arena_->buffer,nullptr);
        d.FreeMemory(device_.device,arena_->memory,nullptr);budgets_.staging->release(arena_->charged);arena_.reset();
    }
    // Worker is done and no submission can start. Destruction may drain the one
    // outstanding upload. Command buffers retain their own Ready resource.
    upload_.reset();pending_.reset();
    for(const auto& pair:retired)publish(pair.second,ResourceState::Stopped,{},"Resource generation stopped");
}
}
