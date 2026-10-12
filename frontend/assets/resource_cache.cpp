#include "resource_cache.h"
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
struct ResourceCache::Upload {
    std::shared_ptr<ImageUpload> image;
    std::shared_ptr<BufferUpload> buffer;
    bool submit(VkQueue queue){return image?image->submit(queue):buffer->submit(queue);}
    UploadState poll(){return image?image->poll():buffer->poll();}
    UploadState state() const {return image?image->state():buffer->state();}
    const std::string& reason() const {return image?image->reason():buffer->reason();}
};
ResourceSnapshot ResourceRequest::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);return value_;
}
ResourceCache::ResourceCache(const ResourceDevice& device,ResourceBudgets budgets,size_t capacity)
    :device_(device),budgets_(std::move(budgets)),capacity_(std::min<size_t>(capacity,4096))
{
    if(!device_.device || !device_.physical || !device_.format_properties || !device_.image_properties ||
       !budgets_.resident || !budgets_.staging || !budgets_.source)
        throw std::invalid_argument("Incomplete resource cache device or budgets");
    worker_=std::thread(&ResourceCache::run,this);
}
ResourceCache::~ResourceCache(){stop();}
void ResourceCache::publish(const ResourceHandle& entry,ResourceState state,
                            std::shared_ptr<Upload> upload,std::string reason)
{
    std::lock_guard<std::mutex> lock(entry->mutex_);
    entry->value_={state,upload?upload->image:nullptr,upload?upload->buffer:nullptr,std::move(reason)};
}
ResourceHandle ResourceCache::declare_image(const std::filesystem::path& path,std::string& reason)
{
    return declare(path,0,reason);
}
ResourceHandle ResourceCache::declare_buffer(const std::filesystem::path& path,VkBufferUsageFlags usage,std::string& reason)
{
    const VkBufferUsageFlags allowed=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if(!usage || (usage&~allowed)){reason="Unsupported buffer declaration usage";return {};}
    return declare(path,usage,reason);
}
ResourceHandle ResourceCache::declare(const std::filesystem::path& path,VkBufferUsageFlags usage,std::string& reason)
{
    reason.clear();
    if(!path.is_absolute() || path.native().size()>32760 || path.native().find(L'\0')!=std::wstring::npos){
        reason="Resource path must be a bounded absolute path without nulls";return {};
    }
    const Key key{path.lexically_normal(),usage};
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopped_){reason="Resource cache stopped";return {};}
    auto found=entries_.find(key);if(found!=entries_.end())return found->second;
    if(entries_.size()>=capacity_){reason="Resource declaration capacity exceeded";return {};}
    auto entry=std::make_shared<ResourceRequest>();entry->path_=key.first;entry->buffer_usage_=usage;entry->owner_=identity_;
    entries_.emplace(key,entry);return entry;
}
bool ResourceCache::demand(const ResourceHandle& entry)
{
    if(!entry || entry->owner_.lock()!=identity_)return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopped_)return false;
    const auto state=entry->snapshot().state;
    if(state==ResourceState::Dormant){queue_.push_back(entry);publish(entry,ResourceState::Queued);wake_.notify_one();}
    return state!=ResourceState::Failed && state!=ResourceState::Stopped;
}
ResourceCacheStats ResourceCache::stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);return stats_;
}
std::shared_ptr<ResourceCache::Upload> ResourceCache::prepare(const ResourceHandle& entry)
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
        throw std::runtime_error("Aggregate resource source budget exceeded");
    SourceCharge charge{budgets_.source,static_cast<uint64_t>(size.QuadPart)};
    std::vector<uint8_t> bytes(static_cast<size_t>(size.QuadPart));DWORD read=0;
    if(!ReadFile(file.handle,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) || read!=bytes.size())
        throw std::runtime_error("Incomplete resource file read");
    {std::lock_guard<std::mutex> lock(mutex_);++stats_.file_reads;}
    auto ticket=std::make_shared<Upload>();
    if(entry->buffer_usage_){
        ticket->buffer=std::make_shared<BufferUpload>(device_.device,device_.buffer_dispatch,budgets_.resident,budgets_.staging);
        if(!ticket->buffer->prepare(bytes.data(),bytes.size(),entry->buffer_usage_,device_.memory) ||
           !ticket->buffer->record(device_.graphics_family))throw std::runtime_error(ticket->buffer->reason());
        return ticket;
    }
    DdsImage meta;std::string reason;
    if(!decode_dds(bytes.data(),bytes.size(),meta,reason))throw std::runtime_error(reason);
    VkFormatProperties features{};device_.format_properties(device_.physical,meta.format,&features);
    VkImageFormatProperties limits{};
    const auto result=device_.image_properties(device_.physical,meta.format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,
                                               VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,0,&limits);
    if(result!=VK_SUCCESS)throw std::runtime_error("Resource format unsupported for sampled upload");
    auto upload=std::make_shared<ImageUpload>(device_.device,device_.dispatch,budgets_.resident,budgets_.staging);
    if(!upload->prepare(bytes.data(),bytes.size(),device_.memory,limits,features.optimalTilingFeatures,device_.bc_enabled) ||
       !upload->record(device_.graphics_family))throw std::runtime_error(upload->reason());
    ticket->image=std::move(upload);return ticket;
}
bool ResourceCache::submit_prepared(VkQueue queue)
{
    if(!queue)return false;
    std::lock_guard<std::mutex> service(service_mutex_);
    std::shared_ptr<Upload> upload;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(stopped_ || phase_!=ResourceState::Prepared || !upload_)return false;
        if(submission_queue_ && submission_queue_!=queue)return false;
        submission_queue_=queue;
        upload=upload_;phase_=ResourceState::Submitting;publish(pending_,phase_);
    }
    const bool ok=upload->submit(queue);
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
    while(true){
        ResourceHandle entry;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock,[&]{return stopped_ || !queue_.empty();});
            if(stopped_)return;
            pending_=entry=queue_.front();queue_.pop_front();phase_=ResourceState::Loading;publish(entry,phase_);
        }
        std::shared_ptr<Upload> upload;std::string reason;
        try{upload=prepare(entry);}catch(const std::exception& error){reason=error.what();}
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
            if(upload->state()==UploadState::Ready){++stats_.ready;publish(entry,ResourceState::Ready,upload);}
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
        stopped_=true;retired.swap(entries_);queue_.clear();wake_.notify_one();
    }
    if(worker_.joinable())worker_.join();
    // Worker is done and no submission can start. Destruction may drain the one
    // outstanding upload. Command buffers retain their own Ready resource.
    upload_.reset();pending_.reset();
    for(const auto& pair:retired)publish(pair.second,ResourceState::Stopped,{},"Resource generation stopped");
}
}
