#include "buffer_upload.h"
#include <cstring>
#include <algorithm>

namespace efmi::assets {
BufferUpload::BufferUpload(VkDevice device,const BufferUploadDispatch& dispatch,
                           std::shared_ptr<AllocationBudget> resident,std::shared_ptr<AllocationBudget> staging)
    :device_(device),d_(dispatch),resident_budget_(std::move(resident)),staging_budget_(std::move(staging)) {}
BufferUpload::~BufferUpload()
{
    if(in_flight_)d_.WaitForFences(device_,1,&fence_,VK_TRUE,UINT64_MAX);
    release_staging();
    if(buffer_)d_.DestroyBuffer(device_,buffer_,nullptr);
    if(memory_buffer_)d_.FreeMemory(device_,memory_buffer_,nullptr);
    if(resident_budget_)resident_budget_->release(resident_charged_);
}
bool BufferUpload::result(VkResult value,const char* operation)
{
    if(value==VK_SUCCESS)return true;
    reason_=std::string(operation)+": "+std::to_string(value);state_=UploadState::Failed;return false;
}
VkDeviceMemory BufferUpload::allocate(const VkMemoryRequirements& req,VkMemoryPropertyFlags flags,
                                      const std::shared_ptr<AllocationBudget>& budget,uint64_t& charged)
{
    if(req.size>(160ull<<20)){reason_="Buffer allocation budget exceeded";return VK_NULL_HANDLE;}
    for(uint32_t i=0;i<memory_.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i)) && (memory_.memoryTypes[i].propertyFlags&flags)==flags){
        if(budget && !budget->acquire(req.size)){reason_="Aggregate buffer memory budget exceeded";return VK_NULL_HANDLE;}
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};info.allocationSize=req.size;info.memoryTypeIndex=i;
        VkDeviceMemory memory=VK_NULL_HANDLE;
        if(result(d_.AllocateMemory(device_,&info,nullptr,&memory),"allocate mesh resource memory")){charged=req.size;return memory;}
        if(budget)budget->release(req.size);return VK_NULL_HANDLE;
    }
    reason_="Mesh resource memory type unavailable";return VK_NULL_HANDLE;
}
void BufferUpload::release_staging()
{
    if(pool_)d_.DestroyCommandPool(device_,pool_,nullptr);
    if(fence_)d_.DestroyFence(device_,fence_,nullptr);
    if(stage_)d_.DestroyBuffer(device_,stage_,nullptr);
    if(memory_stage_)d_.FreeMemory(device_,memory_stage_,nullptr);
    if(staging_budget_)staging_budget_->release(staging_charged_);
    staging_charged_=0;command_=VK_NULL_HANDLE;pool_=VK_NULL_HANDLE;fence_=VK_NULL_HANDLE;stage_=VK_NULL_HANDLE;memory_stage_=VK_NULL_HANDLE;
}
bool BufferUpload::index_extents(VkIndexType type,uint32_t& low,uint32_t& high) const
{
    if(state_!=UploadState::Ready || !(usage_&VK_BUFFER_USAGE_INDEX_BUFFER_BIT))return false;
    const auto* values=type==VK_INDEX_TYPE_UINT16?&indices16_:type==VK_INDEX_TYPE_UINT32?&indices32_:nullptr;
    if(!values || !values->valid)return false;
    low=values->low;high=values->high;return true;
}
bool BufferUpload::prepare(const uint8_t* data,size_t size,VkBufferUsageFlags usage,
                            const VkPhysicalDeviceMemoryProperties& memory,bool readback)
{
    if(state_!=UploadState::Empty){reason_="Buffer ticket cannot be prepared again";return false;}
#define X(name) if(!d_.name){reason_="Missing buffer upload entry: vk" #name;state_=UploadState::Failed;return false;}
    EFMI_BUFFER_UPLOAD_FUNCS(X)
#undef X
    const VkBufferUsageFlags supported=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if(!data || !size || size>(128ull<<20) || !usage || (usage&~supported)){
        reason_="Mesh bytes or usage outside supported limits";state_=UploadState::Failed;return false;
    }
    memory_=memory;size_=size;usage_=usage;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=size;
    ci.usage=usage|VK_BUFFER_USAGE_TRANSFER_DST_BIT|(readback?VK_BUFFER_USAGE_TRANSFER_SRC_BIT:0);
    if(!result(d_.CreateBuffer(device_,&ci,nullptr,&buffer_),"create mesh buffer"))return false;
    VkMemoryRequirements req{};d_.GetBufferMemoryRequirements(device_,buffer_,&req);
    memory_buffer_=allocate(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,resident_budget_,resident_charged_);
    if(!memory_buffer_){state_=UploadState::Failed;return false;}
    if(!result(d_.BindBufferMemory(device_,buffer_,memory_buffer_,0),"bind mesh buffer"))return false;
    ci.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if(!result(d_.CreateBuffer(device_,&ci,nullptr,&stage_),"create mesh staging buffer"))return false;
    d_.GetBufferMemoryRequirements(device_,stage_,&req);
    memory_stage_=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,staging_budget_,staging_charged_);
    if(!memory_stage_){state_=UploadState::Failed;return false;}
    if(!result(d_.BindBufferMemory(device_,stage_,memory_stage_,0),"bind mesh staging buffer"))return false;
    void* mapped=nullptr;
    if(!result(d_.MapMemory(device_,memory_stage_,0,size,0,&mapped),"map mesh staging buffer"))return false;
    std::memcpy(mapped,data,size);d_.UnmapMemory(device_,memory_stage_);
    if(usage&VK_BUFFER_USAGE_INDEX_BUFFER_BIT){
        if(!(size%2)){
            for(size_t i=0;i<size;i+=2){const uint32_t v=data[i]|(uint32_t(data[i+1])<<8);
                indices16_.low=std::min(indices16_.low,v);indices16_.high=std::max(indices16_.high,v);}
            indices16_.valid=true;
        }
        if(!(size%4)){
            for(size_t i=0;i<size;i+=4){const uint32_t v=data[i]|(uint32_t(data[i+1])<<8)|(uint32_t(data[i+2])<<16)|(uint32_t(data[i+3])<<24);
                indices32_.low=std::min(indices32_.low,v);indices32_.high=std::max(indices32_.high,v);}
            indices32_.valid=true;
        }
    }
    state_=UploadState::Prepared;return true;
}
bool BufferUpload::record(uint32_t graphics_family)
{
    if(state_!=UploadState::Prepared){reason_="Buffer ticket is not ready for recording";return false;}
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=graphics_family;
    pool.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    if(!result(d_.CreateCommandPool(device_,&pool,nullptr,&pool_),"create mesh upload pool"))return false;
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool=pool_;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    if(!result(d_.AllocateCommandBuffers(device_,&ai,&command_),"allocate mesh upload command"))return false;
    if(d_.SetDeviceLoaderData && !result(d_.SetDeviceLoaderData(device_,command_),"initialize mesh upload dispatch"))return false;
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if(!result(d_.BeginCommandBuffer(command_,&bi),"begin mesh upload"))return false;
    VkBufferCopy copy{0,0,size_};d_.CmdCopyBuffer(command_,stage_,buffer_,1,&copy);
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask=((usage_&VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)?VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT:0)|
                          ((usage_&VK_BUFFER_USAGE_INDEX_BUFFER_BIT)?VK_ACCESS_INDEX_READ_BIT:0);
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=buffer_;barrier.size=size_;
    d_.CmdPipelineBarrier(command_,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,0,nullptr,1,&barrier,0,nullptr);
    if(!result(d_.EndCommandBuffer(command_),"end mesh upload"))return false;
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if(!result(d_.CreateFence(device_,&fi,nullptr,&fence_),"create mesh upload fence"))return false;
    state_=UploadState::Recorded;return true;
}
bool BufferUpload::submit(VkQueue queue,VkSemaphore timeline,uint64_t value)
{
    if(state_!=UploadState::Recorded || !queue){reason_="Buffer ticket is not ready for submission";return false;}
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&command_;
    VkTimelineSemaphoreSubmitInfo values={VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    if(timeline){values.signalSemaphoreValueCount=1;values.pSignalSemaphoreValues=&value;si.pNext=&values;
        si.signalSemaphoreCount=1;si.pSignalSemaphores=&timeline;}
    if(!result(d_.QueueSubmit(queue,1,&si,fence_),"submit mesh upload"))return false;
    in_flight_=true;state_=UploadState::Submitted;return true;
}
UploadState BufferUpload::poll()
{
    if(state_!=UploadState::Submitted)return state_;
    const auto status=d_.GetFenceStatus(device_,fence_);if(status==VK_NOT_READY)return state_;
    if(result(status,"poll mesh upload fence")){in_flight_=false;release_staging();state_=UploadState::Ready;}
    return state_;
}
}
