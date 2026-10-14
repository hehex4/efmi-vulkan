#pragma once
#include "../assets/allocation_budget.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace efmi::gpu {
#define EFMI_RESOURCE_COMMAND_FUNCS(X) \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) \
    X(AllocateMemory) X(FreeMemory) X(MapMemory) X(UnmapMemory) \
    X(FlushMappedMemoryRanges) X(InvalidateMappedMemoryRanges) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdCopyBuffer) X(CmdFillBuffer) X(CmdPipelineBarrier) \
    X(CreateFence) X(DestroyFence) X(QueueSubmit) X(GetFenceStatus) X(WaitForFences)
struct ResourceDispatch {
    VkResult (VKAPI_PTR *SetDeviceLoaderData)(VkDevice, void*) = nullptr;
#define X(name) PFN_vk##name name=nullptr;
    EFMI_RESOURCE_COMMAND_FUNCS(X)
#undef X
};
struct ResourceDevice {
    VkDevice device=VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory{};
    uint32_t queue_family=0;
    ResourceDispatch dispatch;
};
struct ResourceBudgets {
    std::shared_ptr<assets::AllocationBudget> resident=std::make_shared<assets::AllocationBudget>(512ull<<20);
    std::shared_ptr<assets::AllocationBudget> staging=std::make_shared<assets::AllocationBudget>(128ull<<20);
};
class ResourceCommands;
// Device lifetime and host serialization are the caller's responsibility.
// These are mutable resources; an owner preserves allocation, not old contents.
class BufferResource {
public:
    BufferResource()=default;
    BufferResource(const BufferResource&)=delete;
    BufferResource& operator=(const BufferResource&)=delete;
    ~BufferResource();
    VkBuffer buffer() const {return buffer_;}
    VkDeviceSize size() const {return size_;}
    VkBufferUsageFlags usage() const {return usage_;}
private:
    friend class ResourceCommands;
    ResourceDevice device_;
    std::shared_ptr<const uint8_t> domain_;
    std::shared_ptr<assets::AllocationBudget> budget_;
    VkBuffer buffer_=VK_NULL_HANDLE;
    VkDeviceMemory memory_=VK_NULL_HANDLE;
    VkDeviceSize size_=0,allocation_=0;
    VkBufferUsageFlags usage_=0;
    bool coherent_=false;
};
using Buffer=std::shared_ptr<BufferResource>;
class ResourceSubmission {
public:
    ResourceSubmission()=default;
    ResourceSubmission(const ResourceSubmission&)=delete;
    ResourceSubmission& operator=(const ResourceSubmission&)=delete;
    ~ResourceSubmission();
    bool ready();
    void wait();
private:
    friend class ResourceCommands;
    ResourceDevice device_;
    VkCommandPool pool_=VK_NULL_HANDLE;
    VkCommandBuffer command_=VK_NULL_HANDLE;
    VkFence fence_=VK_NULL_HANDLE;
    bool submitted_=false,complete_=false;
    std::vector<Buffer> retained_;
};

// An explicit outside-render-pass execution domain. Records private primary
// command buffers and never touches the application's render pass or bindings.
// All operations and QueueSubmit calls are serialized by the caller on exactly
// one queue. No application-owned resources or cross-queue use is implied.
// Allocation/recording/readback are not suitable for an intercepted draw hook.
// The caller keeps returned submissions until complete; they retain resources
// even when INI references or their entire generation have been released.
class ResourceCommands {
public:
    ResourceCommands(ResourceDevice,ResourceBudgets={});
    ~ResourceCommands();
    ResourceCommands(const ResourceCommands&)=delete;
    ResourceCommands& operator=(const ResourceCommands&)=delete;
    Buffer create(VkDeviceSize,VkBufferUsageFlags,const void* initial=nullptr,size_t initial_size=0,bool zero=false);
    void copy(const Buffer& source,VkDeviceSize source_offset,const Buffer& destination,VkDeviceSize destination_offset,VkDeviceSize size);
    void fill(const Buffer&,VkDeviceSize offset,VkDeviceSize size,uint32_t word);
    void write(const Buffer&,VkDeviceSize offset,const void*,size_t);
    std::shared_ptr<ResourceSubmission> submit(VkQueue);
    // Synchronous feedback required by original store. Only explicit hosts
    // owning queue synchronization may call this; never an implicit draw wait.
    std::vector<uint8_t> readback(VkQueue,const Buffer&,VkDeviceSize offset,VkDeviceSize size);
    void discard();
private:
    Buffer allocate(VkDeviceSize,VkBufferUsageFlags,bool staging);
    void validate(const Buffer&,VkDeviceSize,VkDeviceSize) const;
    VkCommandBuffer command();
    void barrier(VkPipelineStageFlags destination=VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VkAccessFlags access=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
    ResourceDevice device_;
    ResourceBudgets budgets_;
    std::shared_ptr<const uint8_t> domain_=std::make_shared<const uint8_t>(0);
    std::shared_ptr<ResourceSubmission> pending_;
    VkQueue queue_=VK_NULL_HANDLE;
};
}
