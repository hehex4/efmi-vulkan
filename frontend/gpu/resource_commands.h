#pragma once
#include "../assets/allocation_budget.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <map>
#include <vector>

namespace efmi::gpu {
#define EFMI_RESOURCE_COMMAND_FUNCS(X) \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) \
    X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements2) X(BindImageMemory) \
    X(CreateImageView) X(DestroyImageView) \
    X(AllocateMemory) X(FreeMemory) X(MapMemory) X(UnmapMemory) \
    X(FlushMappedMemoryRanges) X(InvalidateMappedMemoryRanges) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdCopyBuffer) X(CmdFillBuffer) X(CmdPipelineBarrier) \
    X(CmdCopyImage) X(CmdCopyBufferToImage) X(CmdCopyImageToBuffer) \
    X(CmdClearColorImage) X(CmdClearDepthStencilImage) X(CmdResolveImage) \
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
    // Image execution requires Vulkan 1.1 and an explicitly owned graphics
    // queue. Enabled features are the logical device's, not merely supported ones.
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    VkPhysicalDeviceFeatures enabled_features{};
    // Actual logical-device extension enablement, never physical support alone.
    bool enabled_depth_range_unrestricted=false;
    VkQueueFlags queue_flags=0;
    uint32_t api_version=0;
    PFN_vkGetPhysicalDeviceImageFormatProperties GetImageFormatProperties=nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties GetFormatProperties=nullptr;
};
struct ResourceBudgets {
    std::shared_ptr<assets::AllocationBudget> resident=std::make_shared<assets::AllocationBudget>(512ull<<20);
    std::shared_ptr<assets::AllocationBudget> staging=std::make_shared<assets::AllocationBudget>(128ull<<20);
};
class ResourceCommands;
class ShaderCommands;
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
struct ImageDescription {
    VkImageType type=VK_IMAGE_TYPE_2D;
    VkFormat format=VK_FORMAT_UNDEFINED;
    VkExtent3D extent{1,1,1};
    uint32_t mips=1,layers=1;
    VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage=0;
    VkImageCreateFlags flags=0;
};
class ImageResource {
public:
    ImageResource()=default;
    ImageResource(const ImageResource&)=delete;
    ImageResource& operator=(const ImageResource&)=delete;
    ~ImageResource();
    VkImage image() const {return image_;}
    const ImageDescription& description() const {return description_;}
    VkExtent3D extent(uint32_t mip=0) const;
    VkImageSubresourceRange full_range() const;
private:
    friend class ResourceCommands;
    friend class ImageViewResource;
    ResourceDevice device_;
    std::shared_ptr<const uint8_t> domain_;
    std::shared_ptr<assets::AllocationBudget> budget_;
    ImageDescription description_;
    VkImage image_=VK_NULL_HANDLE;
    VkDeviceMemory memory_=VK_NULL_HANDLE;
    VkDeviceSize allocation_=0;
    // True only after a successful submission. Discard never commits layouts.
    bool initialized_=false;
};
using Image=std::shared_ptr<ImageResource>;
class ImageViewResource {
public:
    ImageViewResource()=default;
    ImageViewResource(const ImageViewResource&)=delete;
    ImageViewResource& operator=(const ImageViewResource&)=delete;
    ~ImageViewResource();
    VkImageView view() const {return view_;}
    const Image& owner() const {return image_;}
    VkFormat format() const {return format_;}
    VkImageViewType type() const {return type_;}
    const VkImageSubresourceRange& range() const {return range_;}
    VkImageUsageFlags usage() const {return usage_;}
private:
    friend class ResourceCommands;
    Image image_;
    VkImageView view_=VK_NULL_HANDLE;
    VkFormat format_=VK_FORMAT_UNDEFINED;
    VkImageViewType type_=VK_IMAGE_VIEW_TYPE_2D;
    VkImageSubresourceRange range_{};
    VkImageUsageFlags usage_=0;
};
using ImageView=std::shared_ptr<ImageViewResource>;
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
    friend class ShaderCommands;
    ResourceDevice device_;
    VkCommandPool pool_=VK_NULL_HANDLE;
    VkCommandBuffer command_=VK_NULL_HANDLE;
    VkFence fence_=VK_NULL_HANDLE;
    bool submitted_=false,complete_=false;
    std::vector<Buffer> retained_;
    // Descriptor sets, pipelines, views and their owners share fence lifetime.
    std::vector<std::shared_ptr<void>> shader_owners_;
    std::map<Image,VkImageLayout,std::owner_less<Image>> image_layouts_;
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
    Image create_image(ImageDescription);
    // View creation does not initialize pixels or emit descriptors. Once a
    // submission using an image completes, its entire allocation is GENERAL.
    // Layout changes inside a pending batch are private and discarded with it.
    ImageView view(const Image&,VkImageViewType,VkFormat,VkImageSubresourceRange,VkImageUsageFlags usage=0);
    void copy(const Image& source,const Image& destination,const VkImageCopy&);
    // Bit-preserving single-sample R16/R32 color/depth transfers use a resident
    // buffer, without CPU mapping, submission or waiting. Unknown float bits
    // entering D32 require VK_EXT_depth_range_unrestricted to be enabled.
    void copy_color_depth(const Image& source,const Image& destination,const VkImageCopy&);
    void copy(const Buffer& source,const Image& destination,const VkBufferImageCopy&);
    void copy(const Image& source,const Buffer& destination,const VkBufferImageCopy&);
    void clear(const Image&,VkImageSubresourceRange,const VkClearColorValue&);
    void clear(const Image&,VkImageSubresourceRange,const VkClearDepthStencilValue&);
    void resolve(const Image& source,const Image& destination,const VkImageResolve&);
    // Host transfer data uses Vulkan's explicit row/image pitch; offset is zero.
    // transfer_size excludes bufferOffset and includes necessary row/slice padding.
    VkDeviceSize transfer_size(const Image&,const VkBufferImageCopy&) const;
    void write(const Image&,VkBufferImageCopy,const void*,size_t);
    std::vector<uint8_t> readback(VkQueue,const Image&,VkBufferImageCopy);
    std::shared_ptr<ResourceSubmission> submit(VkQueue);
    // Synchronous feedback required by original store. Only explicit hosts
    // owning queue synchronization may call this; never an implicit draw wait.
    std::vector<uint8_t> readback(VkQueue,const Buffer&,VkDeviceSize offset,VkDeviceSize size);
    void discard();
private:
    friend class ShaderCommands;
    Buffer allocate(VkDeviceSize,VkBufferUsageFlags,bool staging);
    void validate(const Buffer&,VkDeviceSize,VkDeviceSize) const;
    void validate(const Image&) const;
    VkImageSubresourceRange range(const Image&,VkImageSubresourceRange) const;
    void region(const Image&,VkImageSubresourceLayers,VkOffset3D,VkExtent3D) const;
    void transition(const Image&,VkImageLayout);
    void write_stage(const Buffer&,const void*,size_t);
    std::vector<uint8_t> read_stage(const Buffer&,VkDeviceSize);
    VkCommandBuffer command();
    void barrier(VkPipelineStageFlags destination=VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VkAccessFlags access=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
    ResourceDevice device_;
    ResourceBudgets budgets_;
    std::shared_ptr<const uint8_t> domain_=std::make_shared<const uint8_t>(0);
    std::shared_ptr<ResourceSubmission> pending_;
    Buffer image_copy_scratch_;
    VkQueue queue_=VK_NULL_HANDLE;
};
}
