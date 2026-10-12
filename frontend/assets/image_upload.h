#pragma once
#include "dds.h"

namespace efmi::assets {
#define EFMI_IMAGE_UPLOAD_FUNCS(X) \
    X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) \
    X(CreateImageView) X(DestroyImageView) X(CreateBuffer) X(DestroyBuffer) \
    X(GetBufferMemoryRequirements) X(BindBufferMemory) X(AllocateMemory) X(FreeMemory) \
    X(MapMemory) X(UnmapMemory) X(CreateCommandPool) X(DestroyCommandPool) \
    X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(CmdPipelineBarrier) X(CmdCopyBufferToImage) X(CreateFence) X(DestroyFence) \
    X(QueueSubmit) X(GetFenceStatus) X(WaitForFences)
struct UploadDispatch {
#define X(name) PFN_vk##name name=nullptr;
    EFMI_IMAGE_UPLOAD_FUNCS(X)
#undef X
};
enum class UploadState { Empty, Prepared, Submitted, Ready, Failed };

// A single-queue-family upload ticket. All host calls are externally serialized;
// submit must run within the caller's ordinary queue synchronization, never on
// an independent worker racing application submissions. Device outlives owner.
// Use Ready images on that queue; cross-queue synchronization is not provided.
class ImageUpload {
public:
    ImageUpload(VkDevice device,const UploadDispatch& dispatch);
    ~ImageUpload();
    ImageUpload(const ImageUpload&)=delete;
    ImageUpload& operator=(const ImageUpload&)=delete;
    bool prepare(const uint8_t*,size_t,const VkPhysicalDeviceMemoryProperties&,
                 const VkImageFormatProperties&,VkFormatFeatureFlags,
                 bool bc_enabled,bool readback=false);
    bool submit(VkQueue queue,uint32_t graphics_family);
    // Polls only; never waits on the normal readiness path. Releases staging
    // after the upload fence completes. Publication occurs only at Ready.
    UploadState poll();
    UploadState state() const { return state_; }
    VkImage image() const { return state_==UploadState::Ready?image_:VK_NULL_HANDLE; }
    VkImageView view() const { return state_==UploadState::Ready?view_:VK_NULL_HANDLE; }
    const DdsImage& metadata() const { return decoded_; }
    const std::string& reason() const { return reason_; }
    // A Ready object must be retained until all draws using it complete.
private:
    bool result(VkResult,const char*);
    VkDeviceMemory allocate(const VkMemoryRequirements&,VkMemoryPropertyFlags);
    void release_staging();
    VkDevice device_;
    UploadDispatch d_;
    VkPhysicalDeviceMemoryProperties memory_{};
    DdsImage decoded_;
    UploadState state_=UploadState::Empty;
    std::string reason_;
    VkImage image_=VK_NULL_HANDLE;
    VkImageView view_=VK_NULL_HANDLE;
    VkDeviceMemory image_memory_=VK_NULL_HANDLE,stage_memory_=VK_NULL_HANDLE;
    VkBuffer stage_=VK_NULL_HANDLE;
    VkCommandPool pool_=VK_NULL_HANDLE;
    VkFence fence_=VK_NULL_HANDLE;
    bool in_flight_=false;
};
}
