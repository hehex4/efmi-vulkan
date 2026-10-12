#include "texture_cache.h"
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
TextureSnapshot TextureRequest::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);return value_;
}
TextureCache::TextureCache(const TextureDevice& device,TextureBudgets budgets,size_t capacity)
    :device_(device),budgets_(std::move(budgets)),capacity_(std::min<size_t>(capacity,4096))
{
    if(!device_.device || !device_.physical || !device_.format_properties || !device_.image_properties ||
       !budgets_.resident || !budgets_.staging || !budgets_.source)
        throw std::invalid_argument("Incomplete texture cache device or budgets");
    worker_=std::thread(&TextureCache::run,this);
}
TextureCache::~TextureCache(){stop();}
void TextureCache::publish(const TextureHandle& entry,TextureState state,
                            std::shared_ptr<const ImageUpload> image,std::string reason)
{
    std::lock_guard<std::mutex> lock(entry->mutex_);
    entry->value_={state,std::move(image),std::move(reason)};
}
TextureHandle TextureCache::declare_image(const std::filesystem::path& path,std::string& reason)
{
    if(!path.is_absolute() || path.native().size()>32760 || path.native().find(L'\0')!=std::wstring::npos){
        reason="Texture path must be a bounded absolute path without nulls";return {};
    }
    const auto key=path.lexically_normal();
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopped_){reason="Texture cache stopped";return {};}
    auto found=entries_.find(key);if(found!=entries_.end())return found->second;
    if(entries_.size()>=capacity_){reason="Texture declaration capacity exceeded";return {};}
    auto entry=std::make_shared<TextureRequest>();entry->path_=key;entry->owner_=identity_;
    entries_.emplace(key,entry);return entry;
}
bool TextureCache::demand(const TextureHandle& entry)
{
    if(!entry || entry->owner_.lock()!=identity_)return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopped_)return false;
    const auto state=entry->snapshot().state;
    if(state==TextureState::Dormant){queue_.push_back(entry);publish(entry,TextureState::Queued);wake_.notify_one();}
    return state!=TextureState::Failed && state!=TextureState::Stopped;
}
TextureCacheStats TextureCache::stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);return stats_;
}
std::shared_ptr<ImageUpload> TextureCache::prepare(const TextureHandle& entry)
{
    // Deny concurrent writes/deletes while taking an immutable byte snapshot.
    File file;file.handle=CreateFileW(entry->path_.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,
                                    OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file.handle==INVALID_HANDLE_VALUE || GetFileType(file.handle)!=FILE_TYPE_DISK)
        throw std::runtime_error("Cannot open texture file for a stable read");
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file.handle,&size) || size.QuadPart<=0 || size.QuadPart>(128ll<<20))
        throw std::runtime_error("Texture file size outside source budget");
    if(!budgets_.source->acquire(static_cast<uint64_t>(size.QuadPart)))
        throw std::runtime_error("Aggregate texture source budget exceeded");
    SourceCharge charge{budgets_.source,static_cast<uint64_t>(size.QuadPart)};
    std::vector<uint8_t> bytes(static_cast<size_t>(size.QuadPart));DWORD read=0;
    if(!ReadFile(file.handle,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) || read!=bytes.size())
        throw std::runtime_error("Incomplete texture file read");
    {std::lock_guard<std::mutex> lock(mutex_);++stats_.file_reads;}
    DdsImage meta;std::string reason;
    if(!decode_dds(bytes.data(),bytes.size(),meta,reason))throw std::runtime_error(reason);
    VkFormatProperties features{};device_.format_properties(device_.physical,meta.format,&features);
    VkImageFormatProperties limits{};
    const auto result=device_.image_properties(device_.physical,meta.format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,
                                               VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,0,&limits);
    if(result!=VK_SUCCESS)throw std::runtime_error("Texture format unsupported for sampled upload");
    auto upload=std::make_shared<ImageUpload>(device_.device,device_.dispatch,budgets_.resident,budgets_.staging);
    if(!upload->prepare(bytes.data(),bytes.size(),device_.memory,limits,features.optimalTilingFeatures,device_.bc_enabled) ||
       !upload->record(device_.graphics_family))throw std::runtime_error(upload->reason());
    return upload;
}
bool TextureCache::submit_prepared(VkQueue queue)
{
    if(!queue)return false;
    std::lock_guard<std::mutex> service(service_mutex_);
    std::shared_ptr<ImageUpload> upload;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(stopped_ || phase_!=TextureState::Prepared || !upload_)return false;
        if(submission_queue_ && submission_queue_!=queue)return false;
        submission_queue_=queue;
        upload=upload_;phase_=TextureState::Submitting;publish(pending_,phase_);
    }
    const bool ok=upload->submit(queue);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        phase_=ok?TextureState::Submitted:TextureState::Failed;
        if(ok)++stats_.submissions;
        publish(pending_,phase_,{},ok?std::string{}:upload->reason());wake_.notify_one();
    }
    return ok;
}
void TextureCache::run()
{
    while(true){
        TextureHandle entry;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock,[&]{return stopped_ || !queue_.empty();});
            if(stopped_)return;
            pending_=entry=queue_.front();queue_.pop_front();phase_=TextureState::Loading;publish(entry,phase_);
        }
        std::shared_ptr<ImageUpload> upload;std::string reason;
        try{upload=prepare(entry);}catch(const std::exception& error){reason=error.what();}
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if(stopped_)return;
            if(!upload){++stats_.failed;phase_=TextureState::Failed;publish(entry,phase_,{},reason);pending_.reset();continue;}
            upload_=upload;phase_=TextureState::Prepared;publish(entry,phase_);
            wake_.wait(lock,[&]{return stopped_ || phase_==TextureState::Submitted || phase_==TextureState::Failed;});
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
            if(upload->state()==UploadState::Ready){++stats_.ready;publish(entry,TextureState::Ready,upload);}
            else{++stats_.failed;publish(entry,TextureState::Failed,{},upload->reason());}
            // Local ownership keeps any driver destruction outside mutex_.
            upload_.reset();pending_.reset();phase_=TextureState::Dormant;
        }
    }
}
void TextureCache::stop()
{
    // Serialized against queue handoff; stop is never called from a draw.
    std::lock_guard<std::mutex> service(service_mutex_);
    std::map<std::filesystem::path,TextureHandle> retired;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_=true;retired.swap(entries_);queue_.clear();wake_.notify_one();
    }
    if(worker_.joinable())worker_.join();
    // Worker is done and no submission can start. Destruction may drain the one
    // outstanding upload. Command buffers must retain their own Ready image.
    upload_.reset();pending_.reset();
    for(const auto& pair:retired)publish(pair.second,TextureState::Stopped,{},"Texture generation stopped");
}
}
