#include "resource_commands.h"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace efmi::gpu {
namespace {
void check(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+": "+std::to_string(result));
}
}
BufferResource::~BufferResource() {
    if(buffer_)device_.dispatch.DestroyBuffer(device_.device,buffer_,nullptr);
    if(memory_)device_.dispatch.FreeMemory(device_.device,memory_,nullptr);
    if(allocation_)budget_->release(allocation_);
}
ResourceSubmission::~ResourceSubmission() {
    if(submitted_ && !complete_)device_.dispatch.WaitForFences(device_.device,1,&fence_,VK_TRUE,UINT64_MAX);
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
}
ResourceCommands::~ResourceCommands()=default;
Buffer ResourceCommands::allocate(VkDeviceSize size,VkBufferUsageFlags usage,bool staging) {
    if(!size || size>std::numeric_limits<size_t>::max())throw std::runtime_error("Resource buffer size invalid");
    auto result=std::make_shared<BufferResource>();result->device_=device_;result->domain_=domain_;
    result->size_=size;result->usage_=usage;result->budget_=staging?budgets_.staging:budgets_.resident;
    if(size>result->budget_->limit())throw std::runtime_error("Resource allocation exceeds budget");
    const auto& d=device_.dispatch;VkBufferCreateInfo ci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=size;ci.usage=usage;
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
    result->allocation_=req.size;result->coherent_=bool(device_.memory.memoryTypes[chosen].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=chosen;
    check(d.AllocateMemory(device_.device,&ai,nullptr,&result->memory_),"Allocate resource memory");
    check(d.BindBufferMemory(device_.device,result->buffer_,result->memory_,0),"Bind resource memory");return result;
}
void ResourceCommands::validate(const Buffer& buffer,VkDeviceSize offset,VkDeviceSize size) const {
    if(!buffer || buffer->domain_!=domain_)throw std::runtime_error("Resource belongs to another execution domain");
    if(!size || offset>=buffer->size_ || size>buffer->size_-offset)throw std::runtime_error("Resource range outside allocation");
}
Buffer ResourceCommands::create(VkDeviceSize size,VkBufferUsageFlags usage,const void* initial,size_t initial_size,bool zero) {
    if(initial_size>size || (initial_size && !initial))throw std::runtime_error("Resource initial data invalid");
    auto result=allocate(size,usage|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,false);
    if(zero || initial_size) {
        // Staging allocation is charged before the CPU writes, including padding.
        auto stage=allocate(size,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,true);void* mapped=nullptr;
        check(device_.dispatch.MapMemory(device_.device,stage->memory_,0,VK_WHOLE_SIZE,0,&mapped),"Map resource initial data");
        memset(mapped,0,size_t(size));if(initial_size)memcpy(mapped,initial,initial_size);
        VkMappedMemoryRange range={VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=stage->memory_;range.size=VK_WHOLE_SIZE;
        auto status=stage->coherent_?VK_SUCCESS:device_.dispatch.FlushMappedMemoryRanges(device_.device,1,&range);
        device_.dispatch.UnmapMemory(device_.device,stage->memory_);check(status,"Flush resource initial data");
        copy(stage,0,result,0,size);
    }
    return result;
}
VkCommandBuffer ResourceCommands::command() {
    if(pending_)return pending_->command_;
    auto p=std::make_shared<ResourceSubmission>();p->device_=device_;const auto& d=device_.dispatch;
    VkCommandPoolCreateInfo ci={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};ci.queueFamilyIndex=device_.queue_family;
    ci.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;check(d.CreateCommandPool(device_.device,&ci,nullptr,&p->pool_),"Create resource command pool");
    VkCommandBufferAllocateInfo ai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=p->pool_;ai.commandBufferCount=1;
    check(d.AllocateCommandBuffers(device_.device,&ai,&p->command_),"Allocate resource command buffer");
    if(d.SetDeviceLoaderData)check(d.SetDeviceLoaderData(device_.device,p->command_),"Initialize resource loader data");
    VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(d.BeginCommandBuffer(p->command_,&bi),"Begin resource command buffer");pending_=std::move(p);return pending_->command_;
}
void ResourceCommands::barrier(VkPipelineStageFlags destination,VkAccessFlags access) {
    VkMemoryBarrier b={VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=access;
    device_.dispatch.CmdPipelineBarrier(command(),VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,destination,0,1,&b,0,nullptr,0,nullptr);
}
void ResourceCommands::copy(const Buffer& source,VkDeviceSize from,const Buffer& destination,VkDeviceSize to,VkDeviceSize size) {
    validate(source,from,size);validate(destination,to,size);
    if(!(source->usage_&VK_BUFFER_USAGE_TRANSFER_SRC_BIT) || !(destination->usage_&VK_BUFFER_USAGE_TRANSFER_DST_BIT))throw std::runtime_error("Resource copy usage missing");
    if(source==destination && from<to+size && to<from+size)throw std::runtime_error("Overlapping resource copy");
    barrier();VkBufferCopy region{from,to,size};device_.dispatch.CmdCopyBuffer(command(),source->buffer_,destination->buffer_,1,&region);
    pending_->retained_.push_back(source);pending_->retained_.push_back(destination);
}
void ResourceCommands::fill(const Buffer& buffer,VkDeviceSize offset,VkDeviceSize size,uint32_t word) {
    validate(buffer,offset,size);
    if(offset%4 || size%4 || !(buffer->usage_&VK_BUFFER_USAGE_TRANSFER_DST_BIT))throw std::runtime_error("Resource fill requires an aligned transfer destination");
    barrier();device_.dispatch.CmdFillBuffer(command(),buffer->buffer_,offset,size,word);pending_->retained_.push_back(buffer);
}
void ResourceCommands::write(const Buffer& buffer,VkDeviceSize offset,const void* bytes,size_t size) {
    validate(buffer,offset,size);if(!bytes)throw std::runtime_error("Resource write source missing");
    auto stage=allocate(size,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,true);write_stage(stage,bytes,size);copy(stage,0,buffer,offset,size);
}
void ResourceCommands::write_stage(const Buffer& stage,const void* bytes,size_t size) {
    void* mapped=nullptr;
    check(device_.dispatch.MapMemory(device_.device,stage->memory_,0,VK_WHOLE_SIZE,0,&mapped),"Map resource upload");memcpy(mapped,bytes,size);
    VkMappedMemoryRange range={VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=stage->memory_;range.size=VK_WHOLE_SIZE;
    const auto status=stage->coherent_?VK_SUCCESS:device_.dispatch.FlushMappedMemoryRanges(device_.device,1,&range);
    device_.dispatch.UnmapMemory(device_.device,stage->memory_);check(status,"Flush resource upload");
}
std::shared_ptr<ResourceSubmission> ResourceCommands::submit(VkQueue queue,const SubmitSync& sync) {
    if(!queue || (queue_ && queue_!=queue))throw std::runtime_error("Resource execution requires one serialized queue");
    if(sync.waits.size()!=sync.stages.size() || sync.waits.size()>64 || sync.signals.size()>64)throw std::runtime_error("Invalid resource submission semaphores");
    if(!pending_)return {};
    // Publish a common layout only with the submission. Recording then discard
    // must leave each owner's last submitted layout unchanged.
    for(const auto& item:pending_->image_layouts_)transition(item.first,item.first->external_?item.first->external_layout_:VK_IMAGE_LAYOUT_GENERAL);
    barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
    const auto& d=device_.dispatch;auto p=std::move(pending_);
    check(d.EndCommandBuffer(p->command_),"End resource command buffer");
    VkFenceCreateInfo fi={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(d.CreateFence(device_.device,&fi,nullptr,&p->fence_),"Create resource fence");
    VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&p->command_;
    si.waitSemaphoreCount=uint32_t(sync.waits.size());si.pWaitSemaphores=sync.waits.data();si.pWaitDstStageMask=sync.stages.data();
    si.signalSemaphoreCount=uint32_t(sync.signals.size());si.pSignalSemaphores=sync.signals.data();
    check(d.QueueSubmit(queue,1,&si,p->fence_),"Submit resource commands");p->submitted_=true;queue_=queue;
    for(const auto& item:p->image_layouts_)item.first->initialized_=true;
    return p;
}
std::vector<uint8_t> ResourceCommands::readback(VkQueue queue,const Buffer& source,VkDeviceSize offset,VkDeviceSize size) {
    validate(source,offset,size);auto stage=allocate(size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true);copy(source,offset,stage,0,size);
    barrier(VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);auto ticket=submit(queue);ticket->wait();
    return read_stage(stage,size);
}
std::vector<uint8_t> ResourceCommands::read_stage(const Buffer& stage,VkDeviceSize size) {
    std::vector<uint8_t> result(size_t(size),0);void* mapped=nullptr;
    check(device_.dispatch.MapMemory(device_.device,stage->memory_,0,VK_WHOLE_SIZE,0,&mapped),"Map resource readback");
    VkMappedMemoryRange range={VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=stage->memory_;range.size=VK_WHOLE_SIZE;
    const auto status=stage->coherent_?VK_SUCCESS:device_.dispatch.InvalidateMappedMemoryRanges(device_.device,1,&range);
    if(status==VK_SUCCESS)memcpy(result.data(),mapped,result.size());device_.dispatch.UnmapMemory(device_.device,stage->memory_);
    check(status,"Invalidate resource readback");return result;
}
void ResourceCommands::discard(){pending_.reset();}
}
