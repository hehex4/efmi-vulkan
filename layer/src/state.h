// Tracked state of one Vulkan device and the frame capture built from it.
#pragma once

#include "common.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace efvk {

// Device functions the layer intercepts and tracks. One next-pointer is kept per
// name; promoted functions and their extension aliases are separate entries
// because a driver may offer only one of them.
#define EFVK_TRACKED_FUNCS(X)                                                                                          \
    X(DestroyDevice) X(QueuePresentKHR)                                                                                \
    X(AllocateMemory) X(FreeMemory) X(MapMemory) X(UnmapMemory)                                                        \
    X(MapMemory2) X(MapMemory2KHR) X(UnmapMemory2) X(UnmapMemory2KHR)                                                  \
    X(CreateBuffer) X(DestroyBuffer) X(BindBufferMemory) X(BindBufferMemory2) X(BindBufferMemory2KHR)                  \
    X(CreateGraphicsPipelines) X(CreateComputePipelines) X(DestroyPipeline)                                            \
    X(AllocateCommandBuffers) X(FreeCommandBuffers) X(DestroyCommandPool) X(ResetCommandPool)                          \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(ResetCommandBuffer)                                                    \
    X(CmdBindPipeline) X(CmdBindIndexBuffer) X(CmdBindIndexBuffer2) X(CmdBindIndexBuffer2KHR)                          \
    X(CmdBindVertexBuffers) X(CmdBindVertexBuffers2) X(CmdBindVertexBuffers2EXT) X(CmdBindDescriptorSets)              \
    X(CmdDraw) X(CmdDrawIndexed) X(CmdDrawIndirect) X(CmdDrawIndexedIndirect)                                          \
    X(CmdDrawIndirectCount) X(CmdDrawIndirectCountKHR) X(CmdDrawIndexedIndirectCount) X(CmdDrawIndexedIndirectCountKHR) \
    X(CmdDispatch) X(CmdDispatchIndirect)                                                                              \
    X(CmdCopyBuffer) X(CmdCopyBuffer2) X(CmdCopyBuffer2KHR) X(CmdUpdateBuffer) X(CmdFillBuffer)                        \
    X(CmdBeginRenderPass) X(CmdBeginRenderPass2) X(CmdBeginRenderPass2KHR)                                             \
    X(CmdEndRenderPass) X(CmdEndRenderPass2) X(CmdEndRenderPass2KHR)                                                   \
    X(CmdBeginRendering) X(CmdBeginRenderingKHR) X(CmdEndRendering) X(CmdEndRenderingKHR)                              \
    X(CmdExecuteCommands)                                                                                              \
    X(QueueSubmit) X(QueueSubmit2) X(QueueSubmit2KHR)

// Device functions that are only counted and passed on. They show which paths an
// application uses (descriptor updates, images, shaders, uploads) before tracking
// for them exists.
#define EFVK_COUNTED_FUNCS(X)                                                                                          \
    X(CreateDescriptorSetLayout) X(CreatePipelineLayout) X(CreateDescriptorPool) X(ResetDescriptorPool)                \
    X(AllocateDescriptorSets) X(FreeDescriptorSets) X(UpdateDescriptorSets)                                            \
    X(CreateDescriptorUpdateTemplate) X(CreateDescriptorUpdateTemplateKHR)                                             \
    X(UpdateDescriptorSetWithTemplate) X(UpdateDescriptorSetWithTemplateKHR)                                           \
    X(CmdPushDescriptorSet) X(CmdPushDescriptorSetKHR)                                                                 \
    X(CmdPushDescriptorSetWithTemplate) X(CmdPushDescriptorSetWithTemplateKHR)                                         \
    X(CmdBindDescriptorBuffersEXT) X(CmdSetDescriptorBufferOffsetsEXT) X(CmdPushConstants)                             \
    X(CreateImage) X(CreateImageView) X(CreateBufferView) X(CreateSampler) X(CreateShaderModule)                       \
    X(CreateSwapchainKHR) X(CreateRenderPass) X(CreateRenderPass2) X(CreateRenderPass2KHR) X(CreateFramebuffer)        \
    X(CmdCopyBufferToImage) X(CmdCopyBufferToImage2) X(CmdCopyBufferToImage2KHR)                                       \
    X(CmdCopyImageToBuffer) X(CmdBlitImage) X(CmdNextSubpass) X(FlushMappedMemoryRanges)                               \
    X(CmdDrawMultiEXT) X(CmdDrawMultiIndexedEXT) X(CmdDrawMeshTasksEXT) X(CmdBindTransformFeedbackBuffersEXT)          \
    X(CmdBuildAccelerationStructuresKHR) X(CmdTraceRaysKHR)

#define EFVK_DEVICE_FUNCS(X) EFVK_TRACKED_FUNCS(X) EFVK_COUNTED_FUNCS(X)

struct DeviceDispatch {
#define X(n) PFN_vk##n n = nullptr;
    EFVK_DEVICE_FUNCS(X)
#undef X
};

// Index of every intercepted device function, for the call counters.
enum HookIndex {
#define X(n) HI_##n,
    EFVK_DEVICE_FUNCS(X)
#undef X
    HI_COUNT
};

extern std::atomic<uint64_t> g_api_calls[HI_COUNT];
extern const char* const g_api_names[HI_COUNT];

inline void count_call(int index)
{
    g_api_calls[index].fetch_add(1, std::memory_order_relaxed);
}

// Remembers a function name the application asked vkGetDeviceProcAddr for.
void note_resolved(const char* name);

// Sorted, disjoint byte ranges.
struct IntervalSet {
    struct Range {
        uint64_t off, end;
    };
    std::vector<Range> v;

    void add(uint64_t off, uint64_t size);
    void remove(uint64_t off, uint64_t size);
    bool covers(uint64_t off, uint64_t size) const;
    uint64_t total() const;
};

struct MemInfo {
    uint64_t size = 0;
    uint32_t type_index = 0;
    uint8_t* mapped = nullptr;
    uint64_t map_off = 0;
    uint64_t map_size = 0;
    std::vector<VkBuffer> buffers;
};

struct BufInfo {
    uint32_t id = 0;
    uint64_t size = 0;
    uint32_t usage = 0;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    uint64_t mem_off = 0;
    // CPU copy of what tracked writes put into the buffer; `known` says which bytes are valid.
    std::vector<uint8_t> shadow;
    IntervalSet known;
    uint64_t gen = 0;
    uint32_t unknown_writes = 0;
};

struct PipeInfo {
    struct Binding {
        uint32_t binding, stride, per_instance;
    };
    struct Attribute {
        uint32_t location, binding, format, offset;
    };
    uint32_t id = 0;
    bool compute = false;
    uint32_t topology = 0;
    uint32_t stages = 0;
    std::vector<Binding> bindings;
    std::vector<Attribute> attributes;
};

enum DrawKind : uint8_t {
    DK_DRAW = 0,
    DK_INDEXED = 1,
    DK_INDIRECT = 2,
    DK_INDEXED_INDIRECT = 3,
    DK_DISPATCH = 4,
    DK_DISPATCH_INDIRECT = 5,
};

enum ReadSrc : uint8_t {
    SRC_NONE = 0,   // the bytes are not available on the CPU
    SRC_MAPPED = 1, // read from the application's own memory mapping
    SRC_SHADOW = 2, // read from the layer's shadow copy
};

struct VbBind {
    VkBuffer buf = VK_NULL_HANDLE;
    uint64_t off = 0;
};

struct SetBind {
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint32_t dyn_first = 0;
    uint32_t dyn_count = 0;
};

constexpr uint32_t kMaxVb = 4;
constexpr uint32_t kMaxSets = 4;

// One draw or dispatch as it was recorded. Handles are resolved to ids at submit time.
struct DrawRec {
    uint8_t kind = 0;
    uint8_t index_size = 0;
    uint32_t count = 0;      // index count, vertex count, or draw count of an indirect call
    uint32_t instances = 0;
    uint32_t first = 0;      // first index or first vertex
    int32_t vertex_offset = 0;
    uint32_t first_instance = 0;
    uint32_t cb_id = 0;      // command buffer the call was recorded into
    uint32_t tid = 0;        // thread that recorded it
    uint32_t rp = 0;         // ordinal of the render pass inside the submitted command buffer, 0 when outside
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkBuffer ib = VK_NULL_HANDLE;
    uint64_t ib_off = 0;
    VkBuffer indirect = VK_NULL_HANDLE;
    uint64_t indirect_off = 0;
    VbBind vb[kMaxVb];
    SetBind sets[kMaxSets];
};

struct PendingWrite {
    VkBuffer dst = VK_NULL_HANDLE;
    uint64_t dst_off = 0;
    uint64_t size = 0;
    VkBuffer src = VK_NULL_HANDLE; // null for vkCmdUpdateBuffer / vkCmdFillBuffer
    uint64_t src_off = 0;
    bool have_data = false;
    std::vector<uint8_t> data;
};

struct CmdBuf {
    VkCommandBuffer handle = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    uint32_t id = 0;
    bool secondary = false;
    uint32_t tid = 0;

    // Current bindings while recording.
    VkPipeline gfx_pipeline = VK_NULL_HANDLE;
    VkPipeline comp_pipeline = VK_NULL_HANDLE;
    VkBuffer ib = VK_NULL_HANDLE;
    uint64_t ib_off = 0;
    uint8_t index_size = 0;
    VbBind vb[kMaxVb];
    SetBind sets[kMaxSets];
    bool in_rp = false;

    // What was recorded since the last begin / reset.
    std::vector<DrawRec> draws;
    std::vector<uint32_t> dyn;
    std::vector<PendingWrite> writes;
    std::vector<uint32_t> threads; // threads that recorded into this buffer or into executed secondaries
    uint32_t rp_count = 0;
    uint32_t dyn_rp_count = 0;
    uint32_t secondaries = 0;
    uint32_t copies = 0;

    void reset_recording();
};

struct FrameStats {
    uint64_t frame = 0;
    uint32_t submits = 0;
    uint32_t command_buffers = 0;
    uint32_t secondaries = 0;
    uint32_t render_passes = 0;
    uint32_t dynamic_render_passes = 0;
    uint32_t draws = 0;
    uint32_t indexed = 0;
    uint32_t indirect = 0;
    uint32_t dispatches = 0;
    uint32_t buffer_copies = 0;
    uint32_t unknown_writes = 0;
    uint32_t max_draws_per_cb = 0;
    std::vector<uint32_t> threads;
};

struct CapDraw {
    DrawRec r;
    uint32_t submit = 0;
    uint32_t ib_id = 0;
    uint32_t pipe_id = 0;
    uint32_t indirect_id = 0;
    uint32_t vb_id[kMaxVb] = {};
    uint32_t hash = 0;
    uint8_t hash_src = SRC_NONE;
    bool hash_valid = false;
    std::vector<uint32_t> dyn[kMaxSets];
};

struct Capture {
    uint64_t seq = 0;
    FrameStats stats;
    std::vector<CapDraw> draws;
};

struct Device {
    VkDevice handle = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    // Stands for every device that was created before the DLL attached: calls are
    // forwarded through the original exports and nothing is tracked.
    bool passive = false;
    PFN_vkGetDeviceProcAddr next_gdpa = nullptr;
    DeviceDispatch d;

    char gpu_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {};
    std::vector<std::string> extensions; // enabled at creation
    uint32_t api_version = 0;
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    uint32_t driver_version = 0;

    // Memory, buffers, pipelines. Readers of mapped memory hold the lock shared;
    // unmap, free and destroy take it exclusively before the call goes down.
    std::shared_mutex res_mx;
    std::unordered_map<VkDeviceMemory, MemInfo> mems;
    std::unordered_map<VkBuffer, BufInfo> bufs;
    std::unordered_map<uint32_t, VkBuffer> buf_by_id;
    std::unordered_map<VkPipeline, PipeInfo> pipes;
    std::unordered_map<uint32_t, VkPipeline> pipe_by_id;
    uint32_t next_buf_id = 1;
    uint32_t next_pipe_id = 1;

    // Command buffers. The content of one is touched only by the thread that
    // records it, or by the submitting thread once recording has ended.
    std::shared_mutex cb_mx;
    std::unordered_map<VkCommandBuffer, std::unique_ptr<CmdBuf>> cbs;
    std::atomic<uint64_t> cb_epoch{1};
    uint32_t next_cb_id = 1;

    // The frame being assembled and the capture state machine.
    std::mutex frame_mx;
    FrameStats cur;
    FrameStats last;
    uint32_t max_threads = 0;
    uint32_t max_command_buffers = 0;
    uint32_t max_submits = 0;
    uint32_t max_secondaries = 0;
    uint64_t frame_no = 0;
    uint64_t total_draws = 0;
    int cap_state = 0; // 0 idle, 1 waiting for the next present, 2 recording this frame
    std::shared_ptr<Capture> cap_building;
    std::shared_ptr<Capture> cap_last;
    uint64_t cap_seq = 0;
    HANDLE cap_event = nullptr;
};

// Device registry.
Device* dev_create(VkDevice handle);
void dev_destroy(Device* dev);
void dev_set_passive(Device* dev);
Device* dev_of_key(void* key);
template <typename T> inline Device* dev_of(T handle)
{
    return dev_of_key(*reinterpret_cast<void**>(handle));
}

// The device the probe answers for, kept alive while `DeviceRef` exists.
struct DeviceRef {
    Device* dev = nullptr;
    std::shared_lock<std::shared_mutex> lock;
    DeviceRef();
};

CmdBuf* cb_of(Device* dev, VkCommandBuffer cb);

// Resources.
void on_alloc_memory(Device* dev, VkDeviceMemory mem, const VkMemoryAllocateInfo* info);
void on_free_memory(Device* dev, VkDeviceMemory mem);
void on_map_memory(Device* dev, VkDeviceMemory mem, uint64_t off, uint64_t size, void* data);
void on_unmap_memory(Device* dev, VkDeviceMemory mem);
void on_create_buffer(Device* dev, VkBuffer buf, const VkBufferCreateInfo* info);
void on_destroy_buffer(Device* dev, VkBuffer buf);
void on_bind_buffer(Device* dev, VkBuffer buf, VkDeviceMemory mem, uint64_t off);
void on_create_graphics_pipeline(Device* dev, VkPipeline pipe, const VkGraphicsPipelineCreateInfo* info);
void on_create_compute_pipeline(Device* dev, VkPipeline pipe);
void on_destroy_pipeline(Device* dev, VkPipeline pipe);

// Command buffers.
void on_alloc_cbs(Device* dev, const VkCommandBufferAllocateInfo* info, const VkCommandBuffer* cbs);
void on_free_cbs(Device* dev, uint32_t count, const VkCommandBuffer* cbs);
void on_destroy_pool(Device* dev, VkCommandPool pool);
void on_reset_pool(Device* dev, VkCommandPool pool);
void on_copy_buffer(Device* dev, CmdBuf* cb, VkBuffer src, VkBuffer dst, uint64_t src_off, uint64_t dst_off, uint64_t size);
void on_update_buffer(Device* dev, CmdBuf* cb, VkBuffer dst, uint64_t off, uint64_t size, const void* data);
void on_fill_buffer(Device* dev, CmdBuf* cb, VkBuffer dst, uint64_t off, uint64_t size, uint32_t value);
void on_execute_commands(Device* dev, CmdBuf* cb, uint32_t count, const VkCommandBuffer* cbs);

// Frame assembly.
void on_submit(Device* dev, uint32_t count, const VkCommandBuffer* cbs);
void on_present(Device* dev);

// Reads `size` bytes of a buffer. The caller holds res_mx (shared is enough).
ReadSrc read_buffer_locked(Device* dev, const BufInfo& b, uint64_t off, uint64_t size, uint8_t* out);

// Probe requests, called on the pipe thread.
std::string probe_status();
std::string probe_capture(uint32_t timeout_ms, bool with_draws);
std::string probe_buffers(uint32_t usage_mask, uint64_t min_size, uint32_t limit);
std::string probe_read_buffer(uint32_t id, uint64_t off, uint64_t size, std::vector<uint8_t>& payload);
std::string probe_hash(uint32_t id, uint64_t off, uint64_t size);
std::string probe_pipeline(uint32_t id);
std::string probe_api_usage();

} // namespace efvk
