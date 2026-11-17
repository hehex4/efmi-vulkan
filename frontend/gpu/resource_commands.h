#pragma once
#include "../assets/allocation_budget.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <map>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <utility>

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
    X(CreateFence) X(DestroyFence) X(QueueSubmit) X(GetFenceStatus) X(WaitForFences) \
    X(CreateSemaphore) X(DestroySemaphore) \
    X(ResetCommandPool) X(ResetFences)
struct ResourceDispatch {
    VkResult (VKAPI_PTR *SetDeviceLoaderData)(VkDevice, void*) = nullptr;
#define X(name) PFN_vk##name name=nullptr;
    EFMI_RESOURCE_COMMAND_FUNCS(X)
#undef X
};
class ResourceSubmission;
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
    // Core1.3 or enabled KHR_format_feature_flags2 with its physical query.
    bool format_features2=false;
    PFN_vkGetPhysicalDeviceFormatProperties2 GetFormatProperties2=nullptr;
    // Optional independent upload queue. It never waits on application work.
    // The host enabled timeline semaphores and owns this queue's synchronization.
    VkQueue initialization_queue=VK_NULL_HANDLE;
    uint32_t initialization_family=UINT32_MAX;
    VkQueueFlags initialization_flags=0;
    std::mutex* initialization_mutex=nullptr;
    // Retire producer allocations and publication together without waiting on a
    // recording/presentation thread, including uploads with no semaphore.
    std::function<void(std::shared_ptr<ResourceSubmission>,VkSemaphore,std::vector<VkFence>)> retire_initialization;
    uint32_t concurrent_family=UINT32_MAX;
};
struct ResourceBudgets {
    std::shared_ptr<assets::AllocationBudget> resident=std::make_shared<assets::AllocationBudget>(512ull<<20);
    std::shared_ptr<assets::AllocationBudget> staging=std::make_shared<assets::AllocationBudget>(128ull<<20);
};
class ResourceCommands;
class ShaderCommands;
class ResourceInitialization;
// Device lifetime and host serialization are the caller's responsibility.
// These are mutable resources; an owner preserves allocation, not old contents.
class BufferResource {
public:
    struct IndexBounds {bool valid16=false,valid32=false;uint32_t min16=0,max16=0,min32=0,max32=0;};
    IndexBounds index_bounds() const {return indices_;}
    bool initial_contents() const {return initial_contents_;}
    BufferResource()=default;
    BufferResource(const BufferResource&)=delete;
    BufferResource& operator=(const BufferResource&)=delete;
    ~BufferResource();
    VkBuffer buffer() const {return buffer_;}
    VkDeviceSize size() const {return size_;}
    VkBufferUsageFlags usage() const {return usage_;}
    VkDeviceMemory memory() const {return memory_;}
    VkDeviceSize memory_offset() const {return memory_offset_;}
    VkDeviceSize memory_size() const {return external_owner_?external_size_:allocation_;}
private:
    friend class ResourceCommands;
    friend class ResourceSubmission;
    ResourceDevice device_;
    std::shared_ptr<const uint8_t> domain_;
    std::shared_ptr<assets::AllocationBudget> budget_;
    VkBuffer buffer_=VK_NULL_HANDLE;
    VkDeviceMemory memory_=VK_NULL_HANDLE;
    std::shared_ptr<const void> external_owner_;
    VkDeviceSize memory_offset_=0,external_size_=0;
    // allocation_ is charged to the budget; a kept upload buffer is not (idle_ holds its size).
    VkDeviceSize size_=0,allocation_=0,idle_=0;
    VkBufferUsageFlags usage_=0;
    bool coherent_=false;
    IndexBounds indices_;
    std::weak_ptr<ResourceInitialization> initialization_;
    bool initial_contents_=false;
    std::shared_ptr<const uint8_t> creator_;
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
// Proven complete application allocation, not merely a descriptor's view range.
// Layout is uniform across the entire image and restored after every packet.
struct ApplicationImage {
    VkImage image=VK_NULL_HANDLE;
    ImageDescription description;
    VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
    bool uniform_layout=false;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    VkDeviceSize memory_offset=0,memory_size=0;
    std::shared_ptr<const void> owner;
};
class ImageResource {
public:
    ImageResource()=default;
    ImageResource(const ImageResource&)=delete;
    ImageResource& operator=(const ImageResource&)=delete;
    ~ImageResource();
    VkImage image() const {return image_;}
    bool is_external() const {return external_;}
    bool initial_contents() const {return initial_contents_;}
    bool retained_external() const {return bool(external_owner_);}
    VkDeviceMemory memory() const {return memory_;}
    VkDeviceSize memory_offset() const {return memory_offset_;}
    VkDeviceSize memory_size() const {return external_owner_?external_size_:allocation_;}
    VkImageLayout external_layout() const {return external_layout_;}
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
    std::shared_ptr<const void> external_owner_;
    VkDeviceSize memory_offset_=0,external_size_=0;
    // True after successful submission or explicit ordered packet admission.
    // Admission is logical state; the host must execute its predecessor first.
    bool initialized_=false;
    // An imported image the caller owns: valid in this layout before and after every
    // submission that uses it, and never destroyed here.
    VkImageLayout external_layout_=VK_IMAGE_LAYOUT_UNDEFINED;
    bool external_=false;
    std::weak_ptr<ResourceInitialization> initialization_;
    bool initial_contents_=false;
    std::shared_ptr<const uint8_t> creator_;
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
// The command pools (with their one command buffer) and fences of completed submissions, for the
// next submissions of the same ResourceCommands (D-108): creating and destroying both for every
// submission cost about 50 us per frame for a Mod with GPU work at every Present. Shared by the
// submissions, which may outlive their ResourceCommands; what it holds goes with the last owner.
struct SubmissionRecycler {
    ResourceDevice device;
    std::mutex mutex;
    std::vector<std::pair<VkCommandPool,VkCommandBuffer>> pools;
    std::vector<VkFence> fences;
    // Host-visible upload buffers of completed submissions, for the next writes: a scope that writes
    // what it wrote last frame (INI parameters, a panel's constants) allocates no memory (BUG-058).
    // While kept they are not charged to the staging budget, which counts uploads in flight.
    std::vector<Buffer> staging;
    VkDeviceSize staging_bytes=0;
    ~SubmissionRecycler();
};
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
    friend class PreparedResourceCommands;
    friend class ResourceInitialization;
    ResourceDevice device_;
    std::shared_ptr<SubmissionRecycler> recycler_;
    VkCommandPool pool_=VK_NULL_HANDLE;
    VkCommandBuffer command_=VK_NULL_HANDLE;
    VkFence fence_=VK_NULL_HANDLE;
    bool submitted_=false,complete_=false;
    std::vector<Buffer> retained_;
    std::vector<Buffer> staging_; // upload buffers handed back to the recycler once complete
    // Descriptor sets, pipelines, views and their owners share fence lifetime.
    std::vector<std::shared_ptr<void>> shader_owners_;
    std::map<Image,VkImageLayout,std::owner_less<Image>> image_layouts_;
    std::vector<std::shared_ptr<ResourceInitialization>> initializations_;
    std::shared_ptr<const void> initialization_packet_;
};

// Immutable commands and allocation owners, prepared without emitting a Vulkan
// command or submitting work. The host records them at a proven outside-pass
// position on this device/family, restores compute state when indicated, and
// retains this object for the complete executable/pending recording lifetime.
// This is not a queue-order proof and does not make mutable contents snapshots.
struct PreparedBufferAccess {
    Buffer buffer;
    VkDeviceSize offset=0,size=0;
    VkPipelineStageFlags stages=0;
    VkAccessFlags access=0;
    size_t command=0;
    // Allocation identity remains valid because buffer retains its memory owner.
    // Overflowing or empty caller spans are not valid overlap queries.
    bool overlaps(VkDeviceMemory,VkDeviceSize,VkDeviceSize) const noexcept;
};
struct PreparedImageAccess {
    Image image;
    VkImageSubresourceRange range{};
    VkPipelineStageFlags stages=0;
    VkAccessFlags access=0;
    VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;
    size_t command=0;
    // Transitions conservatively touch the full allocation's subresource range.
    bool transition=false;
};
struct PreparedImageState {
    Image image;
    VkImageLayout initial=VK_IMAGE_LAYOUT_UNDEFINED,final=VK_IMAGE_LAYOUT_GENERAL;
};
class PreparedResourceCommands {
public:
    void record(VkCommandBuffer) const;
    bool admitted() const {return admitted_;}
    bool changes_compute_state() const {return compute_;}
    VkDevice device() const {return device_;}
    uint32_t queue_family() const {return queue_family_;}
    size_t size() const {return commands_.size();}
    const void* sequence() const {return sequence_.get();}
    size_t segment() const {return segment_;}
    const std::vector<PreparedBufferAccess>& buffer_accesses() const {return accesses_;}
    const std::vector<PreparedImageAccess>& image_accesses() const {return image_accesses_;}
    const std::vector<PreparedImageState>& image_states() const {return image_states_;}
    const std::vector<std::shared_ptr<ResourceInitialization>>& initializations() const {return owners_->initializations_;}
private:
    friend class ResourceCommands;
    std::vector<std::function<void(VkCommandBuffer)>> commands_;
    std::vector<PreparedBufferAccess> accesses_;
    std::vector<PreparedImageAccess> image_accesses_;
    std::vector<PreparedImageState> image_states_;
    std::shared_ptr<ResourceSubmission> owners_;
    std::shared_ptr<const uint8_t> sequence_;
    size_t segment_=0;
    VkDevice device_=VK_NULL_HANDLE;
    uint32_t queue_family_=0;
    bool compute_=false,admitted_=false;
};

// First-use CPU data only: no application input and no mutable resource writes.
// Each consumer retains the initializer, so discarding the first recording does
// not discard the upload. A dedicated queue publishes through a timeline; the
// fallback submits once on the single serialized consumer queue.
class ResourceInitialization {
public:
    ~ResourceInitialization();
    VkResult before_submit(VkQueue) noexcept;
    bool across_queues() const {return semaphore_!=VK_NULL_HANDLE;}
private:
    friend class ResourceCommands;
    std::unique_ptr<ResourceCommands> commands_;
    std::shared_ptr<const PreparedResourceCommands> packet_;
    std::shared_ptr<ResourceSubmission> submission_;
    std::mutex mutex_;
    VkQueue queue_=VK_NULL_HANDLE;
    VkSemaphore semaphore_=VK_NULL_HANDLE;
    std::map<VkQueue,bool> consumers_;
    std::vector<VkFence> consumer_fences_;
    void upload();
};

// The default outside-render-pass execution domain records private primary
// command buffers. Prepared mode instead returns retained commands for an
// explicit host insertion point; it cannot submit or wait on its own.
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
    // Retained buffer/image transfer and compute preparation. No graphics,
    // feedback or submission. Seal, then admit only after the
    // host proves order/layouts and retains the packet with its recording.
    // Discard invalidates unadmitted packets and leaves image state unchanged.
    void begin_prepared();
    bool preparing() const {return preparing_;}
    // End one ordered segment without closing the scope. All segments remain
    // unrecordable until finish_prepared and one whole-scope acceptance. Host
    // calls may occur between them; every segment must keep its exact position.
    std::shared_ptr<const PreparedResourceCommands> cut_prepared();
    std::shared_ptr<const PreparedResourceCommands> finish_prepared();
    void accept_prepared();
    // The host serializes all consuming queues. Multiple consumers additionally
    // require an independent upload queue and timeline publication.
    void independent_initialization(bool enabled){independent_initialization_=enabled;}
    std::unique_ptr<ResourceCommands> initialization_commands();
    static std::shared_ptr<ResourceInitialization> finish_initialization(std::unique_ptr<ResourceCommands>);
    void initial_layout(const Image&);
    Buffer import_buffer(VkBuffer,VkDeviceSize,VkBufferUsageFlags,VkDeviceMemory,
                         VkDeviceSize memory_offset,VkDeviceSize memory_size,std::shared_ptr<const void>,const Buffer& producer={});
    bool owns(const Buffer& resource) const {return resource && resource->domain_==domain_;}
    bool owns(const Image& resource) const {return resource && resource->domain_==domain_;}
    Buffer create(VkDeviceSize,VkBufferUsageFlags,const void* initial=nullptr,size_t initial_size=0,bool zero=false);
    void copy(const Buffer& source,VkDeviceSize source_offset,const Buffer& destination,VkDeviceSize destination_offset,VkDeviceSize size);
    void fill(const Buffer&,VkDeviceSize offset,VkDeviceSize size,uint32_t word);
    void write(const Buffer&,VkDeviceSize offset,const void*,size_t);
    Image create_image(ImageDescription);
    // A caller-owned image (a swap chain image) for this domain: one mip, one layer, single
    // sample. Its contents are taken from `layout`, and each submission returns it there.
    Image import_image(VkImage,ImageDescription,VkImageLayout layout);
    // Retained application images are valid only in prepared execution.
    Image import_image(const ApplicationImage&);
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
    // Semaphores the submission waits on (with their stages) and signals, for a host that
    // orders it against application work such as a presentation.
    struct SubmitSync {
        std::vector<VkSemaphore> waits;
        std::vector<VkPipelineStageFlags> stages;
        std::vector<VkSemaphore> signals;
        std::vector<uint64_t> signal_values;
    };
    std::shared_ptr<ResourceSubmission> submit(VkQueue,const SubmitSync& sync={});
    // Work recorded and not yet submitted.
    bool pending() const {return bool(pending_);}
    // How often a command buffer was asked for to record into, since the domain was made: a caller that
    // reads it before and after a call knows whether that call recorded any GPU work.
    uint64_t recorded() const {return recorded_;}
    // Another queue of the same family for later submissions. The caller has waited for every earlier
    // submission of this domain; recorded, unsubmitted work goes to the new queue.
    void rebind(VkQueue queue){queue_=queue;}
    // Synchronous feedback required by original store. Only explicit hosts
    // owning queue synchronization may call this; never an implicit draw wait.
    std::vector<uint8_t> readback(VkQueue,const Buffer&,VkDeviceSize offset,VkDeviceSize size);
    // Feedback without a wait (D-147): `size` bytes of `source` copied into a host buffer by the pending
    // submission; feedback_ready gives them once that submission completed, false before.
    struct Feedback {Buffer buffer;VkDeviceSize size=0;std::shared_ptr<ResourceSubmission> submission;};
    Feedback feedback(const Buffer& source,VkDeviceSize offset,VkDeviceSize size);
    bool feedback_ready(Feedback&,std::vector<uint8_t>& bytes);
    void discard();
    // Driver allocations this domain made, and what it reused instead (BUG-058): device memory
    // allocations, descriptor pools, and upload buffers and descriptor sets taken again.
    struct Stats {std::atomic<uint64_t> allocations{0},descriptor_pools{0},staging_reused{0},sets_reused{0};};
    const Stats& stats() const {return stats_;}
private:
    friend class ShaderCommands;
    friend class ResourceInitialization;
    Buffer allocate(VkDeviceSize,VkBufferUsageFlags,bool staging);
    // An upload buffer of at least the size for the pending submission: a recycled one when one fits.
    Buffer stage(VkDeviceSize);
    void validate(const Buffer&,VkDeviceSize,VkDeviceSize) const;
    void validate(const Image&) const;
    VkImageSubresourceRange range(const Image&,VkImageSubresourceRange) const;
    void region(const Image&,VkImageSubresourceLayers,VkOffset3D,VkExtent3D) const;
    void transition(const Image&,VkImageLayout);
    void write_stage(const Buffer&,const void*,size_t);
    std::vector<uint8_t> read_stage(const Buffer&,VkDeviceSize);
    VkCommandBuffer command();
    void retain_pending();
    void emit_prepared(std::function<void(VkCommandBuffer)>);
    void reserve_prepared(size_t commands,size_t accesses,size_t image_accesses=0,size_t images=0);
    void buffer_access(const Buffer&,VkDeviceSize,VkDeviceSize,VkPipelineStageFlags,VkAccessFlags);
    void image_access(const Image&,VkImageSubresourceRange,VkPipelineStageFlags,VkAccessFlags,VkImageLayout,bool transition=false);
    template<class Operation> void emit(Operation&& operation) {
        if(!preparing_){operation(command());return;}
        emit_prepared(std::forward<Operation>(operation));
    }
    void barrier(VkPipelineStageFlags destination=VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VkAccessFlags access=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
    ResourceDevice device_;
    ResourceBudgets budgets_;
    std::shared_ptr<const uint8_t> domain_=std::make_shared<const uint8_t>(0);
    std::shared_ptr<const uint8_t> creator_=std::make_shared<const uint8_t>(0);
    std::shared_ptr<ResourceSubmission> pending_;
    std::vector<std::function<void(VkCommandBuffer)>> prepared_;
    std::vector<PreparedBufferAccess> prepared_accesses_;
    std::vector<PreparedImageAccess> prepared_image_accesses_;
    std::vector<PreparedImageState> prepared_image_states_;
    std::vector<std::shared_ptr<PreparedResourceCommands>> sealed_segments_;
    std::shared_ptr<const uint8_t> prepared_sequence_;
    std::map<Image,VkImageLayout,std::owner_less<Image>> prepared_image_finals_;
    std::map<Buffer,BufferResource::IndexBounds,std::owner_less<Buffer>> index_undo_;
    std::map<Buffer,bool,std::owner_less<Buffer>> buffer_initial_undo_;
    std::map<Image,bool,std::owner_less<Image>> image_initial_undo_;
    bool independent_initialization_=false;
    size_t sealed_commands_=0,sealed_accesses_=0;
    bool awaiting_admission_=false;
    bool preparing_=false,prepared_compute_=false;
    uint64_t recorded_=0;
    std::shared_ptr<SubmissionRecycler> recycler_;
    Buffer image_copy_scratch_;
    std::map<VkImage,std::weak_ptr<ImageResource>> imported_images_;
    std::map<VkImage,VkImageLayout> observed_image_layouts_;
    VkQueue queue_=VK_NULL_HANDLE;
    Stats stats_;
};
}
