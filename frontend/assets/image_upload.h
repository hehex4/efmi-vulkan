#pragma once
#include "dds.h"
#include "allocation_budget.h"
#include <memory>

namespace efmi::assets {
#define EFMI_IMAGE_UPLOAD_FUNCS(X) \
    X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) \
    X(CreateImageView) X(DestroyImageView) X(CreateBuffer) X(DestroyBuffer) \
    X(GetBufferMemoryRequirements) X(BindBufferMemory) X(AllocateMemory) X(FreeMemory) \
    X(MapMemory) X(UnmapMemory) X(CreateCommandPool) X(DestroyCommandPool) \
    X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(CmdPipelineBarrier) X(CmdCopyBufferToImage) X(CreateFence) X(DestroyFence) \
    X(QueueSubmit) X(GetFenceStatus) X(WaitForFences) X(ResetCommandPool) X(ResetFences)
struct UploadDispatch {
    // A Vulkan layer initializes loader dispatch on internally allocated command buffers.
    // Ordinary application/inject entry points already do that through the loader.
    VkResult (VKAPI_PTR *SetDeviceLoaderData)(VkDevice, void*) = nullptr;
#define X(name) PFN_vk##name name=nullptr;
    EFMI_IMAGE_UPLOAD_FUNCS(X)
#undef X
};
enum class UploadState { Empty, Prepared, Recorded, Submitted, Ready, Failed };

// A single-queue-family upload ticket. All host calls are externally serialized;
// submit must run within the caller's ordinary queue synchronization, never on
// an independent worker racing application submissions. Device outlives owner.
// Use Ready images on that queue; cross-queue synchronization is not provided.
class ImageUpload {
public:
    ImageUpload(VkDevice device,const UploadDispatch& dispatch,
                std::shared_ptr<AllocationBudget> resident={},std::shared_ptr<AllocationBudget> staging={});
    ~ImageUpload();
    ImageUpload(const ImageUpload&)=delete;
    ImageUpload& operator=(const ImageUpload&)=delete;
    // skip_levels: the image is made from the DDS's levels from that one on (a smaller copy of a
    // texture when memory is tight). priority: VK_EXT_memory_priority of the image's memory, or
    // negative for none.
    bool prepare(const uint8_t*,size_t,const VkPhysicalDeviceMemoryProperties&,
                 const VkImageFormatProperties&,VkFormatFeatureFlags,
                 bool bc_enabled,bool readback=false,uint32_t skip_levels=0,float priority=-1.0f);
    // D-130: the image alone, for a loader that reads the DDS's payload into staging of its own and
    // records and submits the copy with its own command buffer and fence. After it, base_offset() is
    // where in the file the payload the image takes begins.
    bool prepare_image(const DdsImage&,const VkPhysicalDeviceMemoryProperties&,const VkImageFormatProperties&,
                       VkFormatFeatureFlags,bool bc_enabled,uint32_t skip_levels=0,float priority=-1.0f,bool readback=false);
    size_t base_offset() const { return decoded_.levels.empty()?0:decoded_.levels[skipped_].offset; }
    // The levels a load leaves out of `image` for `skip_levels` requested: never below 4 by 4, never the last.
    static uint32_t levels_skipped(const DdsImage& image,uint32_t skip_levels);
    // The copy from `stage` (the file's bytes from base_offset() on at `offset`) into the caller's
    // command buffer, between its begin and end.
    bool record_copy(VkCommandBuffer,VkBuffer stage,VkDeviceSize offset);
    // The caller's submission completed (Ready) or could not be made (Failed).
    void loaded(bool ok,const std::string& reason={});
    // Creates/records upload commands off the submission path. After record,
    // submit performs only QueueSubmit and changes ticket state. With a timeline semaphore the
    // submission signals it with `value` once the upload completed.
    bool record(uint32_t graphics_family);
    // Before prepare: the image is shared between these two queue families when they differ (D-103).
    void share(uint32_t a,uint32_t b){families_[0]=a;families_[1]=b;shared_=a!=b && a!=UINT32_MAX && b!=UINT32_MAX;}
    bool submit(VkQueue queue,VkSemaphore timeline=VK_NULL_HANDLE,uint64_t value=0);
    uint64_t resident_bytes() const { return resident_charged_; }
    uint32_t skipped_levels() const { return skipped_; }
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
    bool check_format(VkFormatFeatureFlags,const VkImageFormatProperties&,bool bc_enabled,bool readback);
    bool create_image(const VkImageFormatProperties&,bool readback,uint32_t skip_levels,float priority);
    VkDeviceMemory allocate(const VkMemoryRequirements&,VkMemoryPropertyFlags,
                            const std::shared_ptr<AllocationBudget>&,uint64_t& charged,float priority=-1.0f);
    void release_staging();
    VkDevice device_;
    UploadDispatch d_;
    std::shared_ptr<AllocationBudget> resident_budget_,staging_budget_;
    uint64_t resident_charged_=0,staging_charged_=0;
    VkPhysicalDeviceMemoryProperties memory_{};
    DdsImage decoded_;
    UploadState state_=UploadState::Empty;
    std::string reason_;
    VkImage image_=VK_NULL_HANDLE;
    VkImageView view_=VK_NULL_HANDLE;
    VkDeviceMemory image_memory_=VK_NULL_HANDLE,stage_memory_=VK_NULL_HANDLE;
    VkBuffer stage_=VK_NULL_HANDLE;
    VkCommandPool pool_=VK_NULL_HANDLE;
    VkCommandBuffer command_=VK_NULL_HANDLE;
    VkFence fence_=VK_NULL_HANDLE;
    bool in_flight_=false;
    uint32_t skipped_=0;
    uint32_t families_[2]={0,0};
    bool shared_=false;
};
}
