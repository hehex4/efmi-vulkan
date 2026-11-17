#include "resource_commands.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace efmi::gpu {
namespace {
void check(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+": "+std::to_string(result));
}
// Upload buffers up to this size are kept for reuse, at most this many bytes of them.
constexpr VkDeviceSize kRecycledStaging=1ull<<20,kRecycledStagingTotal=32ull<<20;
}
BufferResource::~BufferResource() {
    if(buffer_ && !external_owner_)device_.dispatch.DestroyBuffer(device_.device,buffer_,nullptr);
    if(memory_ && !external_owner_)device_.dispatch.FreeMemory(device_.device,memory_,nullptr);
    if(allocation_)budget_->release(allocation_);
}
SubmissionRecycler::~SubmissionRecycler() {
    for(const auto& p:pools)device.dispatch.DestroyCommandPool(device.device,p.first,nullptr);
    for(auto f:fences)device.dispatch.DestroyFence(device.device,f,nullptr);
}
ResourceSubmission::~ResourceSubmission() {
    if(submitted_ && !complete_)device_.dispatch.WaitForFences(device_.device,1,&fence_,VK_TRUE,UINT64_MAX);
    // Retained objects go first: the pool's commands referenced them, and nothing runs any more.
    retained_.clear();shader_owners_.clear();image_layouts_.clear();
    initialization_packet_.reset();
    if(recycler_){
        std::lock_guard<std::mutex> lock(recycler_->mutex);
        for(auto& stage:staging_)if(recycler_->staging_bytes+stage->size()<=kRecycledStagingTotal){
            stage->budget_->release(stage->allocation_);stage->idle_=stage->allocation_;stage->allocation_=0;
            recycler_->staging_bytes+=stage->size();recycler_->staging.push_back(std::move(stage));
        }
        staging_.clear();
        if(pool_ && recycler_->pools.size()<8 && device_.dispatch.ResetCommandPool(device_.device,pool_,0)==VK_SUCCESS){
            recycler_->pools.push_back({pool_,command_});pool_=VK_NULL_HANDLE;
        }
        if(fence_ && recycler_->fences.size()<8 && device_.dispatch.ResetFences(device_.device,1,&fence_)==VK_SUCCESS){
            recycler_->fences.push_back(fence_);fence_=VK_NULL_HANDLE;
        }
    }
    if(pool_)device_.dispatch.DestroyCommandPool(device_.device,pool_,nullptr);
    if(fence_)device_.dispatch.DestroyFence(device_.device,fence_,nullptr);
}
bool ResourceSubmission::ready() {
    if(!submitted_)return false;
    if(complete_)return true;
    auto result=device_.dispatch.GetFenceStatus(device_.device,fence_);
    if(result==VK_NOT_READY)return false;
    check(result,"Resource fence status");complete_=true;return true;
}
void ResourceSubmission::wait() {
    if(!submitted_)throw std::runtime_error("Resource commands have not been submitted");
    if(!complete_)check(device_.dispatch.WaitForFences(device_.device,1,&fence_,VK_TRUE,UINT64_MAX),"Resource fence wait");
    complete_=true;
}
ResourceCommands::ResourceCommands(ResourceDevice device,ResourceBudgets budgets):device_(device),budgets_(std::move(budgets)) {
    if(!device_.device || !budgets_.resident || !budgets_.staging)throw std::runtime_error("Resource device/budgets missing");
#define X(name) if(!device_.dispatch.name)throw std::runtime_error("Resource dispatch missing vk" #name);
    EFMI_RESOURCE_COMMAND_FUNCS(X)
#undef X
    recycler_=std::make_shared<SubmissionRecycler>();recycler_->device=device_;
}
ResourceCommands::~ResourceCommands(){discard();}
void ResourceCommands::initial_layout(const Image& image){
    validate(image);
    if(image->external_ || image->initialized_)throw std::runtime_error("Independent layout initialization requires a new private image");
    reserve_prepared(1,0,1,1);
    transition(image,VK_IMAGE_LAYOUT_GENERAL);
}
std::unique_ptr<ResourceCommands> ResourceCommands::initialization_commands(){
    if(!independent_initialization_)return {};
    if(awaiting_admission_)throw std::runtime_error("Initialization cannot bypass an outstanding registry admission");
    auto device=device_;
    if(device.initialization_queue){
        if(!device.initialization_mutex || device.initialization_family==UINT32_MAX)throw std::runtime_error("Independent upload queue synchronization is missing");
        device.concurrent_family=device.queue_family;device.queue_family=device.initialization_family;device.queue_flags=device.initialization_flags;
    }
    auto child=std::make_unique<ResourceCommands>(device,budgets_);child->domain_=domain_;
    if(device.queue_family==device_.queue_family)child->recycler_=recycler_;
    child->begin_prepared();return child;
}
std::shared_ptr<ResourceInitialization> ResourceCommands::finish_initialization(std::unique_ptr<ResourceCommands> commands){
    if(!commands)return {};
    auto packet=commands->finish_prepared();if(!packet){commands->accept_prepared();return {};}
    if(packet->changes_compute_state() || !packet->initializations().empty())throw std::runtime_error("Independent initialization cannot execute shaders or depend on other producers");
    for(const auto& access:packet->buffer_accesses())if(access.buffer->creator_!=commands->creator_)
        throw std::runtime_error("Independent initialization cannot access an existing or application buffer");
    for(const auto& state:packet->image_states())if(state.image->creator_!=commands->creator_)
        throw std::runtime_error("Independent initialization cannot access an existing or application image");
    commands->accept_prepared();
    auto result=std::make_shared<ResourceInitialization>();result->commands_=std::move(commands);result->packet_=packet;
    for(const auto& access:packet->buffer_accesses())if(access.access&VK_ACCESS_TRANSFER_WRITE_BIT){
        access.buffer->initialization_=result;access.buffer->initial_contents_=true;
    }
    for(const auto& state:packet->image_states()){
        state.image->initialization_=result;state.image->initial_contents_=true;
    }
    if(result->commands_->device_.initialization_queue)result->upload();
    return result;
}
ResourceInitialization::~ResourceInitialization(){
    // The producer is private and independent of application waits. Consumers
    // retain allocations through their normal executable-recording lifetime.
    const auto& device=commands_->device_;
    if(submission_ && device.retire_initialization){
        device.retire_initialization(std::move(submission_),semaphore_,std::move(consumer_fences_));return;
    }
    submission_.reset();
    if(semaphore_){
            for(auto fence:consumer_fences_){device.dispatch.WaitForFences(device.device,1,&fence,VK_TRUE,UINT64_MAX);device.dispatch.DestroyFence(device.device,fence,nullptr);}
            device.dispatch.DestroySemaphore(device.device,semaphore_,nullptr);
    }
}
void ResourceInitialization::upload(){
    const auto& device=commands_->device_;VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo create{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};create.pNext=&type;
    check(device.dispatch.CreateSemaphore(device.device,&create,nullptr,&semaphore_),"Create first-use publication");
    packet_->record(commands_->command());ResourceCommands::SubmitSync sync;sync.signals={semaphore_};sync.signal_values={1};
    std::lock_guard<std::mutex> lock(*device.initialization_mutex);
    submission_=commands_->submit(device.initialization_queue,sync);submission_->initialization_packet_=packet_;queue_=device.initialization_queue;
}
VkResult ResourceInitialization::before_submit(VkQueue queue) noexcept{
    std::lock_guard<std::mutex> lock(mutex_);
    if(!queue)return VK_ERROR_INITIALIZATION_FAILED;
    if(semaphore_){
        try{
            auto inserted=consumers_.emplace(queue,false);if(inserted.first->second)return VK_SUCCESS;
            const uint64_t value=1;const VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};timeline.waitSemaphoreValueCount=1;timeline.pWaitSemaphoreValues=&value;
            VkSubmitInfo wait{VK_STRUCTURE_TYPE_SUBMIT_INFO};wait.pNext=&timeline;wait.waitSemaphoreCount=1;wait.pWaitSemaphores=&semaphore_;wait.pWaitDstStageMask=&stage;
            consumer_fences_.reserve(consumer_fences_.size()+1);const auto& device=commands_->device_;
            VkFenceCreateInfo create{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence=VK_NULL_HANDLE;
            auto result=device.dispatch.CreateFence(device.device,&create,nullptr,&fence);if(result!=VK_SUCCESS)return result;
            result=device.dispatch.QueueSubmit(queue,1,&wait,fence);
            if(result==VK_SUCCESS){consumer_fences_.push_back(fence);inserted.first->second=true;}
            else device.dispatch.DestroyFence(device.device,fence,nullptr);
            return result;
        }catch(...){return VK_ERROR_OUT_OF_HOST_MEMORY;}
    }
    if(queue_)return queue_==queue?VK_SUCCESS:VK_ERROR_INITIALIZATION_FAILED;
    try{
        const auto command=commands_->command();packet_->record(command);
        submission_=commands_->submit(queue);submission_->initialization_packet_=packet_;queue_=queue;return VK_SUCCESS;
    }catch(...){commands_->discard();return VK_ERROR_INITIALIZATION_FAILED;}
}
bool PreparedBufferAccess::overlaps(VkDeviceMemory memory,VkDeviceSize begin,VkDeviceSize length) const noexcept {
    if(!buffer || !memory || !length || begin>UINT64_MAX-length || buffer->memory()!=memory)return false;
    const auto own=buffer->memory_offset()+offset;
    return own<begin+length && begin<own+size;
}
void PreparedResourceCommands::record(VkCommandBuffer command) const {
    if(!admitted_)throw std::runtime_error("Prepared packet requires ordered host admission before recording");
    if(!command)throw std::runtime_error("Prepared commands need a recording command buffer");
    for(const auto& emit:commands_)emit(command);
}
void ResourceCommands::begin_prepared() {
    if(preparing_ || pending_ || awaiting_admission_)throw std::runtime_error("Prepared resource scope requires no pending work or admission");
    prepared_sequence_=std::make_shared<const uint8_t>(0);
    preparing_=true;prepared_compute_=false;prepared_.clear();prepared_accesses_.clear();
    prepared_image_accesses_.clear();prepared_image_states_.clear();
    observed_image_layouts_.clear();prepared_image_finals_.clear();sealed_segments_.clear();sealed_commands_=sealed_accesses_=0;
}
std::shared_ptr<const PreparedResourceCommands> ResourceCommands::cut_prepared() {
    if(!preparing_)throw std::runtime_error("No prepared resource scope is active");
    if(!pending_)return {};
    if(sealed_segments_.size()>=256)throw std::runtime_error("Prepared segment budget exceeded");
    // Optimal image byte placement cannot be derived from texel coordinates.
    // Distinct aliased image/buffer objects require an explicit alias scheduler.
    auto overlap=[](VkDeviceSize a,VkDeviceSize n,VkDeviceSize b,VkDeviceSize m){return a<b+m && b<a+n;};
    auto images_overlap=[&](const Image& a,const Image& b){return a!=b && a->memory()==b->memory() &&
        overlap(a->memory_offset(),a->memory_size(),b->memory_offset(),b->memory_size());};
    for(size_t i=0;i<prepared_image_states_.size();++i){const auto& image=prepared_image_states_[i].image;
        for(size_t j=0;j<i;++j){const auto& other=prepared_image_states_[j].image;
            if(images_overlap(image,other))
                throw std::runtime_error("Prepared aliased images require explicit allocation phase proof");
        }
        for(const auto& other:prepared_image_finals_)if(images_overlap(image,other.first))
            throw std::runtime_error("Prepared segments alias image allocations without phase proof");
        for(const auto& segment:sealed_segments_)for(const auto& access:segment->accesses_)
            if(access.overlaps(image->memory(),image->memory_offset(),image->memory_size()))
                throw std::runtime_error("Prepared segments alias image/buffer allocations without phase proof");
        for(const auto& access:prepared_accesses_)
            if(access.overlaps(image->memory(),image->memory_offset(),image->memory_size()))
                throw std::runtime_error("Prepared image/buffer allocation aliases require explicit phase proof");
    }
    for(const auto& image:prepared_image_finals_)for(const auto& access:prepared_accesses_)
        if(access.overlaps(image.first->memory(),image.first->memory_offset(),image.first->memory_size()))
            throw std::runtime_error("Prepared segments alias buffer/image allocations without phase proof");
    sealed_segments_.reserve(sealed_segments_.size()+1);
    // Every operation reserved its final layout transitions and publication.
    for(const auto& item:pending_->image_layouts_)
        transition(item.first,item.first->external_?item.first->external_layout_:VK_IMAGE_LAYOUT_GENERAL);
    barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
    auto result=std::make_shared<PreparedResourceCommands>();
    // Only private logical state advances between segments. Cancellation must
    // not publish an image initialization whose commands never execute.
    for(const auto& state:prepared_image_states_)prepared_image_finals_[state.image]=state.final;
    result->commands_=std::move(prepared_);result->owners_=std::move(pending_);
    result->accesses_=std::move(prepared_accesses_);
    result->image_accesses_=std::move(prepared_image_accesses_);
    result->image_states_=std::move(prepared_image_states_);
    result->device_=device_.device;result->queue_family_=device_.queue_family;result->compute_=prepared_compute_;
    result->sequence_=prepared_sequence_;result->segment_=sealed_segments_.size();
    sealed_commands_+=result->commands_.size();sealed_accesses_+=result->accesses_.size()+result->image_accesses_.size();
    sealed_segments_.push_back(result);prepared_.clear();prepared_accesses_.clear();
    prepared_image_accesses_.clear();prepared_image_states_.clear();prepared_compute_=false;return result;
}
std::shared_ptr<const PreparedResourceCommands> ResourceCommands::finish_prepared() {
    auto result=cut_prepared();preparing_=false;awaiting_admission_=true;return result;
}
void ResourceCommands::accept_prepared() {
    if(preparing_ || pending_ || !awaiting_admission_)throw std::runtime_error("Prepared admission requires a sealed scope");
    for(const auto& state:prepared_image_finals_)state.first->initialized_=true;
    for(const auto& segment:sealed_segments_)segment->admitted_=true;
    sealed_segments_.clear();prepared_image_finals_.clear();prepared_sequence_.reset();sealed_commands_=sealed_accesses_=0;
    awaiting_admission_=false;observed_image_layouts_.clear();index_undo_.clear();buffer_initial_undo_.clear();image_initial_undo_.clear();
}
Buffer ResourceCommands::import_buffer(VkBuffer handle,VkDeviceSize size,VkBufferUsageFlags usage,VkDeviceMemory memory,
    VkDeviceSize memory_offset,VkDeviceSize memory_size,std::shared_ptr<const void> owner,const Buffer& producer) {
    if(!preparing_)throw std::runtime_error("Application buffers require a prepared command scope");
    if(!handle || !memory || !size || size>memory_size || memory_offset>UINT64_MAX-memory_size || !owner)
        throw std::runtime_error("Imported buffer needs a complete retained allocation range");
    auto result=std::make_shared<BufferResource>();result->device_=device_;result->domain_=domain_;
    result->buffer_=handle;result->size_=size;result->usage_=usage;result->memory_=memory;
    result->memory_offset_=memory_offset;result->external_size_=memory_size;result->external_owner_=std::move(owner);
    if(producer){
        if(producer->device_.device!=device_.device || producer->buffer()!=handle || producer->size()!=size ||
           producer->memory()!=memory || producer->memory_offset()!=memory_offset || producer->memory_size()!=memory_size)
            throw std::runtime_error("Imported producer does not own the retained allocation");
        // Registry replacement changes the command domain, not the allocation's producer.
        result->initialization_=producer->initialization_;
    }
    return result;
}
Buffer ResourceCommands::allocate(VkDeviceSize size,VkBufferUsageFlags usage,bool staging) {
    if(!size || size>std::numeric_limits<size_t>::max())throw std::runtime_error("Resource buffer size invalid");
    auto result=std::make_shared<BufferResource>();result->device_=device_;result->domain_=domain_;result->creator_=creator_;
    result->size_=size;result->usage_=usage;result->budget_=staging?budgets_.staging:budgets_.resident;
    if(size>result->budget_->limit())throw std::runtime_error("Resource allocation exceeds budget");
    const auto& d=device_.dispatch;VkBufferCreateInfo ci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=size;ci.usage=usage;
    const uint32_t families[]={device_.queue_family,device_.concurrent_family};
    if(device_.concurrent_family!=UINT32_MAX && device_.concurrent_family!=device_.queue_family){
        ci.sharingMode=VK_SHARING_MODE_CONCURRENT;ci.queueFamilyIndexCount=2;ci.pQueueFamilyIndices=families;
    }
    check(d.CreateBuffer(device_.device,&ci,nullptr,&result->buffer_),"Create resource buffer");
    VkMemoryRequirements req{};d.GetBufferMemoryRequirements(device_.device,result->buffer_,&req);
    uint32_t chosen=UINT32_MAX;int best=-1;
    for(uint32_t i=0;i<device_.memory.memoryTypeCount;++i) {
        const auto flags=device_.memory.memoryTypes[i].propertyFlags;
        if(!(req.memoryTypeBits&(1u<<i)) || (staging && !(flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)))continue;
        const int rank=staging?((flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)?2:1):((flags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)?2:1);
        if(rank>best){chosen=i;best=rank;}
    }
    if(chosen==UINT32_MAX)throw std::runtime_error("Resource memory type unavailable");
    if(!result->budget_->acquire(req.size))throw std::runtime_error("Resource allocation budget exhausted");
    ++stats_.allocations;
    result->allocation_=req.size;result->coherent_=bool(device_.memory.memoryTypes[chosen].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=chosen;
    check(d.AllocateMemory(device_.device,&ai,nullptr,&result->memory_),"Allocate resource memory");
    check(d.BindBufferMemory(device_.device,result->buffer_,result->memory_,0),"Bind resource memory");return result;
}
void ResourceCommands::validate(const Buffer& buffer,VkDeviceSize offset,VkDeviceSize size) const {
    if(awaiting_admission_)throw std::runtime_error("Resource scope is awaiting prepared admission");
    if(!buffer || buffer->domain_!=domain_)throw std::runtime_error("Resource belongs to another execution domain");
    if(buffer->external_owner_ && !preparing_)throw std::runtime_error("Imported buffers cannot enter deferred queue submissions");
    if(!size || offset>=buffer->size_ || size>buffer->size_-offset)throw std::runtime_error("Resource range outside allocation");
}
namespace {
BufferResource::IndexBounds index_data(const void* data,size_t size){
    BufferResource::IndexBounds result;const auto* bytes=static_cast<const uint8_t*>(data);
    auto scan=[&](size_t width,uint32_t& low,uint32_t& high){low=UINT32_MAX;high=0;
        for(size_t at=0;at<size;at+=width){uint32_t value=0;memcpy(&value,bytes+at,width);low=std::min(low,value);high=std::max(high,value);}};
    if(size && size%2==0){result.valid16=true;scan(2,result.min16,result.max16);}
    if(size && size%4==0){result.valid32=true;scan(4,result.min32,result.max32);}
    return result;
}
}
Buffer ResourceCommands::create(VkDeviceSize size,VkBufferUsageFlags usage,const void* initial,size_t initial_size,bool zero) {
    if(initial_size>size || (initial_size && !initial))throw std::runtime_error("Resource initial data invalid");
    auto result=allocate(size,usage|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,false);
    if(zero || initial_size) {
        // Staging allocation is charged before the CPU writes, including padding.
        auto stage=allocate(size,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,true);void* mapped=nullptr;
        check(device_.dispatch.MapMemory(device_.device,stage->memory_,0,VK_WHOLE_SIZE,0,&mapped),"Map resource initial data");
        memset(mapped,0,size_t(size));if(initial_size)memcpy(mapped,initial,initial_size);
        const auto indices=(usage&VK_BUFFER_USAGE_INDEX_BUFFER_BIT)?index_data(mapped,size_t(size)):BufferResource::IndexBounds{};
        VkMappedMemoryRange range={VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=stage->memory_;range.size=VK_WHOLE_SIZE;
        auto status=stage->coherent_?VK_SUCCESS:device_.dispatch.FlushMappedMemoryRanges(device_.device,1,&range);
        device_.dispatch.UnmapMemory(device_.device,stage->memory_);check(status,"Flush resource initial data");
        copy(stage,0,result,0,size);
        result->indices_=indices;
    }
    return result;
}
VkCommandBuffer ResourceCommands::command() {
    if(preparing_ || awaiting_admission_)throw std::runtime_error("Prepared commands cannot enter private command recording");
    ++recorded_;
    if(pending_)return pending_->command_;
    auto p=std::make_shared<ResourceSubmission>();p->device_=device_;p->recycler_=recycler_;const auto& d=device_.dispatch;
    {
        std::lock_guard<std::mutex> lock(recycler_->mutex);
        if(!recycler_->pools.empty()){p->pool_=recycler_->pools.back().first;p->command_=recycler_->pools.back().second;recycler_->pools.pop_back();}
    }
    if(!p->pool_){
        VkCommandPoolCreateInfo ci={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};ci.queueFamilyIndex=device_.queue_family;
        ci.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;check(d.CreateCommandPool(device_.device,&ci,nullptr,&p->pool_),"Create resource command pool");
        VkCommandBufferAllocateInfo ai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=p->pool_;ai.commandBufferCount=1;
        check(d.AllocateCommandBuffers(device_.device,&ai,&p->command_),"Allocate resource command buffer");
        if(d.SetDeviceLoaderData)check(d.SetDeviceLoaderData(device_.device,p->command_),"Initialize resource loader data");
    }
    VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(d.BeginCommandBuffer(p->command_,&bi),"Begin resource command buffer");pending_=std::move(p);return pending_->command_;
}
void ResourceCommands::retain_pending() {
    if(!preparing_){command();return;}
    if(pending_)return;
    pending_=std::make_shared<ResourceSubmission>();pending_->device_=device_;pending_->recycler_=recycler_;
}
void ResourceCommands::emit_prepared(std::function<void(VkCommandBuffer)> operation) {
    if(sealed_commands_+prepared_.size()>=4096)throw std::runtime_error("Prepared resource command budget exceeded");
    retain_pending();prepared_.push_back(std::move(operation));++recorded_;
}
void ResourceCommands::reserve_prepared(size_t commands,size_t accesses,size_t image_accesses,size_t images) {
    if(!preparing_)return;
    // Conservative new-image count reserves final GENERAL transitions even if
    // this operation later finds an already retained image. Never omit closure.
    const size_t image_count=prepared_image_states_.size()+images;
    const size_t closure=1+image_count;
    if(sealed_segments_.size()>=256 || prepared_image_finals_.size()+image_count>256 || commands>4096-closure ||
       sealed_commands_+prepared_.size()>4096-closure-commands ||
       accesses>16384 || image_accesses>16384-accesses ||
       sealed_accesses_+prepared_accesses_.size()+prepared_image_accesses_.size()+image_count>16384-accesses-image_accesses)
        throw std::runtime_error("Prepared resource command/access budget exceeded");
    const auto grow=[](size_t capacity,size_t need,size_t maximum){return std::min(maximum,std::max(need,std::max(size_t(16),capacity*2)));};
    if(prepared_.capacity()<prepared_.size()+commands+closure)prepared_.reserve(grow(prepared_.capacity(),prepared_.size()+commands+closure,4096));
    if(prepared_accesses_.capacity()<prepared_accesses_.size()+accesses)
        prepared_accesses_.reserve(grow(prepared_accesses_.capacity(),prepared_accesses_.size()+accesses,16384));
    const size_t need=prepared_image_accesses_.size()+image_accesses+image_count;
    if(prepared_image_accesses_.capacity()<need)prepared_image_accesses_.reserve(grow(prepared_image_accesses_.capacity(),need,16384));
    if(prepared_image_states_.capacity()<image_count)prepared_image_states_.reserve(grow(prepared_image_states_.capacity(),image_count,256));
}
void ResourceCommands::image_access(const Image& image,VkImageSubresourceRange range,VkPipelineStageFlags stages,
    VkAccessFlags access,VkImageLayout layout,bool transition) {
    if(auto init=image->initialization_.lock()){
        retain_pending();auto& dependencies=pending_->initializations_;
        if(std::find(dependencies.begin(),dependencies.end(),init)==dependencies.end())dependencies.push_back(std::move(init));
    }
    if(!transition && (access&(VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT))){
        image_initial_undo_.try_emplace(image,image->initial_contents_);image->initial_contents_=false;
    }
    if(preparing_)prepared_image_accesses_.push_back({image,range,stages,access,layout,prepared_.size(),transition});
}
void ResourceCommands::buffer_access(const Buffer& buffer,VkDeviceSize offset,VkDeviceSize size,VkPipelineStageFlags stages,VkAccessFlags access) {
    if(auto init=buffer->initialization_.lock()){
        retain_pending();auto& dependencies=pending_->initializations_;
        if(std::find(dependencies.begin(),dependencies.end(),init)==dependencies.end())dependencies.push_back(std::move(init));
    }
    if(access&(VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT)){
        buffer_initial_undo_.try_emplace(buffer,buffer->initial_contents_);buffer->initial_contents_=false;
    }
    if((buffer->usage_&VK_BUFFER_USAGE_INDEX_BUFFER_BIT) && (access&(VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT))){
        index_undo_.try_emplace(buffer,buffer->indices_);buffer->indices_={};
    }
    if(preparing_)prepared_accesses_.push_back({buffer,offset,size,stages,access,prepared_.size()});
}
void ResourceCommands::barrier(VkPipelineStageFlags destination,VkAccessFlags access) {
    VkMemoryBarrier b={VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=access;
    const auto barrier=device_.dispatch.CmdPipelineBarrier;
    emit([barrier,destination,b](VkCommandBuffer command){barrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,destination,0,1,&b,0,nullptr,0,nullptr);});
}
void ResourceCommands::copy(const Buffer& source,VkDeviceSize from,const Buffer& destination,VkDeviceSize to,VkDeviceSize size) {
    validate(source,from,size);validate(destination,to,size);
    if(!(source->usage_&VK_BUFFER_USAGE_TRANSFER_SRC_BIT) || !(destination->usage_&VK_BUFFER_USAGE_TRANSFER_DST_BIT))throw std::runtime_error("Resource copy usage missing");
    const auto source_begin=source->memory_offset_+from,destination_begin=destination->memory_offset_+to;
    if(source->memory_==destination->memory_ && source_begin<destination_begin+size && destination_begin<source_begin+size)
        throw std::runtime_error("Overlapping resource copy");
    reserve_prepared(2,2);barrier();VkBufferCopy region{from,to,size};const auto copy=device_.dispatch.CmdCopyBuffer;
    buffer_access(source,from,size,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    buffer_access(destination,to,size,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    emit([copy,source,destination,region](VkCommandBuffer command){copy(command,source->buffer_,destination->buffer_,1,&region);});
    pending_->retained_.push_back(source);pending_->retained_.push_back(destination);
    if(!from && !to && size==source->size_ && size==destination->size_ && (destination->usage_&VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
        destination->indices_=source->indices_;
}
void ResourceCommands::fill(const Buffer& buffer,VkDeviceSize offset,VkDeviceSize size,uint32_t word) {
    validate(buffer,offset,size);
    if(offset%4 || size%4 || !(buffer->usage_&VK_BUFFER_USAGE_TRANSFER_DST_BIT))throw std::runtime_error("Resource fill requires an aligned transfer destination");
    reserve_prepared(2,1);barrier();const auto fill=device_.dispatch.CmdFillBuffer;
    buffer_access(buffer,offset,size,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    emit([fill,buffer,offset,size,word](VkCommandBuffer command){fill(command,buffer->buffer_,offset,size,word);});
    pending_->retained_.push_back(buffer);
    if(!offset && size==buffer->size_ && (buffer->usage_&VK_BUFFER_USAGE_INDEX_BUFFER_BIT)){
        auto& indices=buffer->indices_;indices.valid16=indices.valid32=true;
        indices.min16=std::min(word&65535,word>>16);indices.max16=std::max(word&65535,word>>16);
        indices.min32=indices.max32=word;
    }
}
Buffer ResourceCommands::stage(VkDeviceSize size) {
    retain_pending();
    if(size<=kRecycledStaging){
        // The smallest kept buffer that holds the bytes, if it is not more than twice as large.
        std::lock_guard<std::mutex> lock(recycler_->mutex);
        auto& kept=recycler_->staging;size_t best=kept.size();
        for(size_t i=0;i<kept.size();++i)
            if(kept[i]->size_>=size && kept[i]->size_<=size*2 && (best==kept.size() || kept[i]->size_<kept[best]->size_))best=i;
        if(best!=kept.size()){
            auto found=std::move(kept[best]);kept.erase(kept.begin()+best);recycler_->staging_bytes-=found->size_;
            // Charged again while it carries this upload; short of budget it is freed instead.
            if(found->budget_->acquire(found->idle_)){
                found->allocation_=found->idle_;found->idle_=0;
                found->creator_=creator_; // A completed, exclusively recycled CPU upload belongs to this initialization.
                ++stats_.staging_reused;pending_->staging_.push_back(found);return found;
            }
        }
    }
    // Sizes kept for reuse round up to a power of two, so the next frame's write of the same size fits.
    VkDeviceSize allocation=size;
    if(size<=kRecycledStaging){allocation=256;while(allocation<size)allocation<<=1;}
    auto result=allocate(allocation,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,true);
    if(size<=kRecycledStaging)pending_->staging_.push_back(result);
    return result;
}
void ResourceCommands::write(const Buffer& buffer,VkDeviceSize offset,const void* bytes,size_t size) {
    validate(buffer,offset,size);if(!bytes)throw std::runtime_error("Resource write source missing");
    auto stage=this->stage(size);write_stage(stage,bytes,size);copy(stage,0,buffer,offset,size);
    if(!offset && size==buffer->size_ && (buffer->usage_&VK_BUFFER_USAGE_INDEX_BUFFER_BIT))buffer->indices_=index_data(bytes,size);
}
void ResourceCommands::write_stage(const Buffer& stage,const void* bytes,size_t size) {
    void* mapped=nullptr;
    if(size>stage->size_)throw std::runtime_error("Resource upload exceeds its staging buffer");
    check(device_.dispatch.MapMemory(device_.device,stage->memory_,0,VK_WHOLE_SIZE,0,&mapped),"Map resource upload");memcpy(mapped,bytes,size);
    VkMappedMemoryRange range={VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=stage->memory_;range.size=VK_WHOLE_SIZE;
    const auto status=stage->coherent_?VK_SUCCESS:device_.dispatch.FlushMappedMemoryRanges(device_.device,1,&range);
    device_.dispatch.UnmapMemory(device_.device,stage->memory_);check(status,"Flush resource upload");
}
std::shared_ptr<ResourceSubmission> ResourceCommands::submit(VkQueue queue,const SubmitSync& sync) {
    if(preparing_ || awaiting_admission_)throw std::runtime_error("Prepared resource commands cannot be submitted ahead of application work");
    if(!queue || (queue_ && queue_!=queue))throw std::runtime_error("Resource execution requires one serialized queue");
    if(sync.waits.size()!=sync.stages.size() || sync.waits.size()>64 || sync.signals.size()>64)throw std::runtime_error("Invalid resource submission semaphores");
    if(!sync.signal_values.empty() && sync.signal_values.size()!=sync.signals.size())throw std::runtime_error("Invalid resource publication values");
    if(!pending_)return {};
    // Publish a common layout only with the submission. Recording then discard
    // must leave each owner's last submitted layout unchanged.
    for(const auto& item:pending_->image_layouts_)transition(item.first,item.first->external_?item.first->external_layout_:VK_IMAGE_LAYOUT_GENERAL);
    barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
    const auto& d=device_.dispatch;auto p=std::move(pending_);
    check(d.EndCommandBuffer(p->command_),"End resource command buffer");
    {
        std::lock_guard<std::mutex> lock(recycler_->mutex);
        if(!recycler_->fences.empty()){p->fence_=recycler_->fences.back();recycler_->fences.pop_back();}
    }
    VkFenceCreateInfo fi={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if(!p->fence_)check(d.CreateFence(device_.device,&fi,nullptr,&p->fence_),"Create resource fence");
    VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&p->command_;
    si.waitSemaphoreCount=uint32_t(sync.waits.size());si.pWaitSemaphores=sync.waits.data();si.pWaitDstStageMask=sync.stages.data();
    si.signalSemaphoreCount=uint32_t(sync.signals.size());si.pSignalSemaphores=sync.signals.data();
    std::vector<uint64_t> wait_values(sync.waits.size());VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    if(!sync.signal_values.empty()){
        timeline.waitSemaphoreValueCount=uint32_t(wait_values.size());timeline.pWaitSemaphoreValues=wait_values.data();
        timeline.signalSemaphoreValueCount=uint32_t(sync.signal_values.size());timeline.pSignalSemaphoreValues=sync.signal_values.data();si.pNext=&timeline;
    }
    for(const auto& init:p->initializations_)check(init->before_submit(queue),"Submit first-use initialization");
    check(d.QueueSubmit(queue,1,&si,p->fence_),"Submit resource commands");p->submitted_=true;queue_=queue;
    for(const auto& item:p->image_layouts_)item.first->initialized_=true;
    index_undo_.clear();buffer_initial_undo_.clear();image_initial_undo_.clear();
    return p;
}
std::vector<uint8_t> ResourceCommands::readback(VkQueue queue,const Buffer& source,VkDeviceSize offset,VkDeviceSize size) {
    if(preparing_)throw std::runtime_error("Prepared resource scopes cannot wait for readback");
    validate(source,offset,size);auto stage=allocate(size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true);copy(source,offset,stage,0,size);
    barrier(VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);auto ticket=submit(queue);ticket->wait();
    return read_stage(stage,size);
}
ResourceCommands::Feedback ResourceCommands::feedback(const Buffer& source,VkDeviceSize offset,VkDeviceSize size) {
    if(preparing_)throw std::runtime_error("Prepared resource feedback requires the host recording's completion proof");
    validate(source,offset,size);Feedback result;result.size=size;
    result.buffer=allocate(size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true);copy(source,offset,result.buffer,0,size);
    barrier(VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);command();result.submission=pending_;
    return result;
}
bool ResourceCommands::feedback_ready(Feedback& f,std::vector<uint8_t>& bytes) {
    if(!f.buffer || !f.submission || !f.submission->ready())return false;
    bytes=read_stage(f.buffer,f.size);return true;
}
std::vector<uint8_t> ResourceCommands::read_stage(const Buffer& stage,VkDeviceSize size) {
    std::vector<uint8_t> result(size_t(size),0);void* mapped=nullptr;
    check(device_.dispatch.MapMemory(device_.device,stage->memory_,0,VK_WHOLE_SIZE,0,&mapped),"Map resource readback");
    VkMappedMemoryRange range={VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=stage->memory_;range.size=VK_WHOLE_SIZE;
    const auto status=stage->coherent_?VK_SUCCESS:device_.dispatch.InvalidateMappedMemoryRanges(device_.device,1,&range);
    if(status==VK_SUCCESS)memcpy(result.data(),mapped,result.size());device_.dispatch.UnmapMemory(device_.device,stage->memory_);
    check(status,"Invalidate resource readback");return result;
}
void ResourceCommands::discard(){
    for(const auto& saved:index_undo_)saved.first->indices_=saved.second;index_undo_.clear();
    for(const auto& saved:buffer_initial_undo_)saved.first->initial_contents_=saved.second;buffer_initial_undo_.clear();
    for(const auto& saved:image_initial_undo_)saved.first->initial_contents_=saved.second;image_initial_undo_.clear();
    prepared_.clear();prepared_accesses_.clear();prepared_image_accesses_.clear();prepared_image_states_.clear();
    sealed_segments_.clear();prepared_image_finals_.clear();prepared_sequence_.reset();sealed_commands_=sealed_accesses_=0;
    pending_.reset();observed_image_layouts_.clear();preparing_=prepared_compute_=awaiting_admission_=false;
}
}
