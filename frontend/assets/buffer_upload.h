#pragma once
#include "image_upload.h"

namespace efmi::assets {
#define EFMI_BUFFER_UPLOAD_FUNCS(X) \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) \
    X(AllocateMemory) X(FreeMemory) X(MapMemory) X(UnmapMemory) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdCopyBuffer) X(CmdPipelineBarrier) \
    X(CreateFence) X(DestroyFence) X(QueueSubmit) X(GetFenceStatus) X(WaitForFences)
struct BufferUploadDispatch {
#define X(name) PFN_vk##name name=nullptr;
    EFMI_BUFFER_UPLOAD_FUNCS(X)
#undef X
};

// Immutable file bytes for vertex/index bindings. Declaration stride, index
// format and draw bounds remain the frontend's contract, not inferred here.
// Same single-graphics-queue, serialized host-call and lifetime rules as images.
class BufferUpload {
public:
    BufferUpload(VkDevice,const BufferUploadDispatch&,std::shared_ptr<AllocationBudget> resident={},
                 std::shared_ptr<AllocationBudget> staging={});
    ~BufferUpload();
    BufferUpload(const BufferUpload&)=delete;
    BufferUpload& operator=(const BufferUpload&)=delete;
    bool prepare(const uint8_t*,size_t,VkBufferUsageFlags,const VkPhysicalDeviceMemoryProperties&,bool readback=false);
    bool record(uint32_t graphics_family);
    bool submit(VkQueue);
    UploadState poll();
    UploadState state() const {return state_;}
    VkBuffer buffer() const {return state_==UploadState::Ready?buffer_:VK_NULL_HANDLE;}
    VkDeviceSize size() const {return size_;}
    VkBufferUsageFlags usage() const {return usage_;}
    const std::string& reason() const {return reason_;}
    // Extrema of the entire uploaded index file, computed from the same source
    // snapshot during prepare. Constant-time conservative range proof, not a
    // scan of mapped GPU memory or an exact subrange query. Ready only.
    bool index_extents(VkIndexType,uint32_t& low,uint32_t& high) const;
private:
    bool result(VkResult,const char*);
    VkDeviceMemory allocate(const VkMemoryRequirements&,VkMemoryPropertyFlags,const std::shared_ptr<AllocationBudget>&,uint64_t&);
    void release_staging();
    VkDevice device_;
    BufferUploadDispatch d_;
    std::shared_ptr<AllocationBudget> resident_budget_,staging_budget_;
    uint64_t resident_charged_=0,staging_charged_=0;
    VkPhysicalDeviceMemoryProperties memory_{};
    UploadState state_=UploadState::Empty;
    std::string reason_;
    VkBuffer buffer_=VK_NULL_HANDLE,stage_=VK_NULL_HANDLE;
    VkDeviceMemory memory_buffer_=VK_NULL_HANDLE,memory_stage_=VK_NULL_HANDLE;
    VkCommandPool pool_=VK_NULL_HANDLE;
    VkCommandBuffer command_=VK_NULL_HANDLE;
    VkFence fence_=VK_NULL_HANDLE;
    VkDeviceSize size_=0;
    VkBufferUsageFlags usage_=0;
    bool in_flight_=false;
    struct Extents {uint32_t low=UINT32_MAX,high=0;bool valid=false;};
    Extents indices16_,indices32_;
};
}
