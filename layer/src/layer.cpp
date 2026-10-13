// Entry points and the intercepted Vulkan functions.
//
// Ordinary probe hooks observe and forward. An explicitly configured M2 package can replace
// its one matching component through the separately gated experimental backend.
//
// The hooks get in front of the application in one of two ways:
//   layer  - the Vulkan loader loads the DLL as a layer and chains it in through
//            vkNegotiateLoaderLayerInterfaceVersion and the link info of create calls;
//   inject - the DLL was loaded with LoadLibrary; bootstrap.cpp redirects the exports of
//            vulkan-1.dll to the hooks, which then call the original exports.
#include "state.h"
#include "m2_backend.h"
#include "character_backend.h"

#include <cstddef>
#include <mutex>

using namespace efvk;

#pragma comment(linker, "/EXPORT:vkNegotiateLoaderLayerInterfaceVersion")
#pragma comment(linker, "/EXPORT:efvkGetInstanceProcAddr")
#pragma comment(linker, "/EXPORT:efvkGetDeviceProcAddr")

namespace {

struct InstanceData {
    VkInstance handle = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    PFN_vkDestroyInstance DestroyInstance = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties = nullptr;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties = nullptr;
};

std::mutex g_inst_mx;
std::unordered_map<void*, InstanceData*> g_insts;
std::once_flag g_pin_once;

// Inject entry: the addresses the redirected exports of vulkan-1.dll had.
PFN_vkGetInstanceProcAddr g_real_gipa = nullptr;
PFN_vkGetDeviceProcAddr g_real_gdpa = nullptr;
PFN_vkCreateInstance g_real_create_instance = nullptr;
PFN_vkDestroyInstance g_real_destroy_instance = nullptr;
PFN_vkCreateDevice g_real_create_device = nullptr;
Device g_passive_dev; // its dispatch table holds the original exports of the device functions

bool injected()
{
    return g_cfg.entry == Entry::Inject;
}

template <typename T> void* key_of(T handle)
{
    return *reinterpret_cast<void**>(handle);
}

InstanceData* inst_of_key(void* key)
{
    std::lock_guard<std::mutex> lock(g_inst_mx);
    auto it = g_insts.find(key);
    return it == g_insts.end() ? nullptr : it->second;
}

// The pipe thread lives until the process exits, so the module must not be
// unloaded when the loader releases the layer.
void pin_module()
{
    std::call_once(g_pin_once, [] {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&pin_module), &self);
    });
}

uint8_t index_size_of(VkIndexType t)
{
    switch (t) {
    case VK_INDEX_TYPE_UINT16: return 2;
    case VK_INDEX_TYPE_UINT32: return 4;
    case VK_INDEX_TYPE_UINT8: return 1;
    default: return 0;
    }
}

DrawRec make_rec(const CmdBuf* cb, uint8_t kind, bool compute)
{
    DrawRec r;
    r.kind = kind;
    r.cb_id = cb->id;
    r.tid = cb->tid;
    r.rp = cb->in_rp ? cb->rp_count + cb->dyn_rp_count : 0;
    if (compute) {
        r.pipeline = cb->comp_pipeline;
        for (uint32_t i = 0; i < kMaxSets; i++)
            r.sets[i] = cb->comp_sets[i];
        return r;
    }
    r.pipeline = cb->gfx_pipeline;
    r.ib = cb->ib;
    r.ib_off = cb->ib_off;
    r.index_size = cb->index_size;
    for (uint32_t i = 0; i < kMaxVb; i++)
        r.vb[i] = cb->vb[i];
    for (uint32_t i = 0; i < kMaxSets; i++)
        r.sets[i] = cb->sets[i];
    return r;
}

// ---------------------------------------------------------------------------
// Memory

VKAPI_ATTR VkResult VKAPI_CALL hk_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* info,
                                                 const VkAllocationCallbacks* alloc, VkDeviceMemory* mem)
{
    count_call(HI_AllocateMemory);
    Device* dev = dev_of(device);
    VkResult r = dev->d.AllocateMemory(device, info, alloc, mem);
    if (r == VK_SUCCESS)
        on_alloc_memory(dev, *mem, info);
    return r;
}

VKAPI_ATTR void VKAPI_CALL hk_FreeMemory(VkDevice device, VkDeviceMemory mem, const VkAllocationCallbacks* alloc)
{
    count_call(HI_FreeMemory);
    Device* dev = dev_of(device);
    if (mem)
        on_free_memory(dev, mem);
    dev->d.FreeMemory(device, mem, alloc);
}

VKAPI_ATTR VkResult VKAPI_CALL hk_MapMemory(VkDevice device, VkDeviceMemory mem, VkDeviceSize off, VkDeviceSize size,
                                            VkMemoryMapFlags flags, void** data)
{
    count_call(HI_MapMemory);
    Device* dev = dev_of(device);
    VkResult r = dev->d.MapMemory(device, mem, off, size, flags, data);
    if (r == VK_SUCCESS)
        on_map_memory(dev, mem, off, size, *data);
    return r;
}

VKAPI_ATTR void VKAPI_CALL hk_UnmapMemory(VkDevice device, VkDeviceMemory mem)
{
    count_call(HI_UnmapMemory);
    Device* dev = dev_of(device);
    on_unmap_memory(dev, mem);
    dev->d.UnmapMemory(device, mem);
}

VkResult map2(Device* dev, PFN_vkMapMemory2 next, VkDevice device, const VkMemoryMapInfo* info, void** data)
{
    VkResult r = next(device, info, data);
    if (r == VK_SUCCESS)
        on_map_memory(dev, info->memory, info->offset, info->size, *data);
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL hk_MapMemory2(VkDevice device, const VkMemoryMapInfo* info, void** data)
{
    count_call(HI_MapMemory2);
    Device* dev = dev_of(device);
    return map2(dev, dev->d.MapMemory2, device, info, data);
}

VKAPI_ATTR VkResult VKAPI_CALL hk_MapMemory2KHR(VkDevice device, const VkMemoryMapInfo* info, void** data)
{
    count_call(HI_MapMemory2KHR);
    Device* dev = dev_of(device);
    return map2(dev, dev->d.MapMemory2KHR, device, info, data);
}

VKAPI_ATTR VkResult VKAPI_CALL hk_UnmapMemory2(VkDevice device, const VkMemoryUnmapInfo* info)
{
    count_call(HI_UnmapMemory2);
    Device* dev = dev_of(device);
    on_unmap_memory(dev, info->memory);
    return dev->d.UnmapMemory2(device, info);
}

VKAPI_ATTR VkResult VKAPI_CALL hk_UnmapMemory2KHR(VkDevice device, const VkMemoryUnmapInfo* info)
{
    count_call(HI_UnmapMemory2KHR);
    Device* dev = dev_of(device);
    on_unmap_memory(dev, info->memory);
    return dev->d.UnmapMemory2KHR(device, info);
}

// ---------------------------------------------------------------------------
// Buffers and pipelines

VKAPI_ATTR VkResult VKAPI_CALL hk_CreateBuffer(VkDevice device, const VkBufferCreateInfo* info,
                                               const VkAllocationCallbacks* alloc, VkBuffer* buf)
{
    count_call(HI_CreateBuffer);
    Device* dev = dev_of(device);
    VkResult r = dev->d.CreateBuffer(device, info, alloc, buf);
    if (r == VK_SUCCESS)
        on_create_buffer(dev, *buf, info);
    return r;
}

VKAPI_ATTR void VKAPI_CALL hk_DestroyBuffer(VkDevice device, VkBuffer buf, const VkAllocationCallbacks* alloc)
{
    count_call(HI_DestroyBuffer);
    Device* dev = dev_of(device);
    if (buf)
        on_destroy_buffer(dev, buf);
    dev->d.DestroyBuffer(device, buf, alloc);
}

VKAPI_ATTR VkResult VKAPI_CALL hk_BindBufferMemory(VkDevice device, VkBuffer buf, VkDeviceMemory mem, VkDeviceSize off)
{
    count_call(HI_BindBufferMemory);
    Device* dev = dev_of(device);
    VkResult r = dev->d.BindBufferMemory(device, buf, mem, off);
    if (r == VK_SUCCESS)
        on_bind_buffer(dev, buf, mem, off);
    return r;
}

VkResult bind2(Device* dev, PFN_vkBindBufferMemory2 next, VkDevice device, uint32_t count, const VkBindBufferMemoryInfo* infos)
{
    VkResult r = next(device, count, infos);
    if (r == VK_SUCCESS)
        for (uint32_t i = 0; i < count; i++)
            on_bind_buffer(dev, infos[i].buffer, infos[i].memory, infos[i].memoryOffset);
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL hk_BindBufferMemory2(VkDevice device, uint32_t count, const VkBindBufferMemoryInfo* infos)
{
    count_call(HI_BindBufferMemory2);
    Device* dev = dev_of(device);
    return bind2(dev, dev->d.BindBufferMemory2, device, count, infos);
}

VKAPI_ATTR VkResult VKAPI_CALL hk_BindBufferMemory2KHR(VkDevice device, uint32_t count, const VkBindBufferMemoryInfo* infos)
{
    count_call(HI_BindBufferMemory2KHR);
    Device* dev = dev_of(device);
    return bind2(dev, dev->d.BindBufferMemory2KHR, device, count, infos);
}

VKAPI_ATTR VkResult VKAPI_CALL hk_CreateGraphicsPipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
                                                          const VkGraphicsPipelineCreateInfo* infos,
                                                          const VkAllocationCallbacks* alloc, VkPipeline* pipes)
{
    count_call(HI_CreateGraphicsPipelines);
    Device* dev = dev_of(device);
    VkResult r = dev->d.CreateGraphicsPipelines(device, cache, count, infos, alloc, pipes);
    // A batch can return an error after successfully creating some pipelines.
    for (uint32_t i = 0; i < count; i++)
        if (pipes[i])
            on_create_graphics_pipeline(dev, pipes[i], &infos[i]);
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL hk_CreateComputePipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
                                                         const VkComputePipelineCreateInfo* infos,
                                                         const VkAllocationCallbacks* alloc, VkPipeline* pipes)
{
    count_call(HI_CreateComputePipelines);
    Device* dev = dev_of(device);
    VkResult r = dev->d.CreateComputePipelines(device, cache, count, infos, alloc, pipes);
    for (uint32_t i = 0; i < count; i++)
        if (pipes[i])
            on_create_compute_pipeline(dev, pipes[i], &infos[i]);
    return r;
}

VKAPI_ATTR void VKAPI_CALL hk_DestroyPipeline(VkDevice device, VkPipeline pipe, const VkAllocationCallbacks* alloc)
{
    count_call(HI_DestroyPipeline);
    Device* dev = dev_of(device);
    if (pipe) {
        m2_retire(dev, pipe);
        on_destroy_pipeline(dev, pipe);
    }
    dev->d.DestroyPipeline(device, pipe, alloc);
}

// ---------------------------------------------------------------------------
#include "resource_hooks.h"

// Command buffer lifetime

VKAPI_ATTR VkResult VKAPI_CALL hk_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* info,
                                                         VkCommandBuffer* cbs)
{
    count_call(HI_AllocateCommandBuffers);
    Device* dev = dev_of(device);
    VkResult r = dev->d.AllocateCommandBuffers(device, info, cbs);
    if (r == VK_SUCCESS)
        on_alloc_cbs(dev, info, cbs);
    return r;
}

VKAPI_ATTR void VKAPI_CALL hk_FreeCommandBuffers(VkDevice device, VkCommandPool pool, uint32_t count,
                                                 const VkCommandBuffer* cbs)
{
    count_call(HI_FreeCommandBuffers);
    Device* dev = dev_of(device);
    on_free_cbs(dev, count, cbs);
    dev->d.FreeCommandBuffers(device, pool, count, cbs);
}

VKAPI_ATTR void VKAPI_CALL hk_DestroyCommandPool(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks* alloc)
{
    count_call(HI_DestroyCommandPool);
    Device* dev = dev_of(device);
    if (pool)
        on_destroy_pool(dev, pool);
    dev->d.DestroyCommandPool(device, pool, alloc);
}

VKAPI_ATTR VkResult VKAPI_CALL hk_ResetCommandPool(VkDevice device, VkCommandPool pool, VkCommandPoolResetFlags flags)
{
    count_call(HI_ResetCommandPool);
    Device* dev = dev_of(device);
    VkResult result = dev->d.ResetCommandPool(device, pool, flags);
    if (result == VK_SUCCESS) on_reset_pool(dev, pool);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL hk_BeginCommandBuffer(VkCommandBuffer cmd, const VkCommandBufferBeginInfo* info)
{
    count_call(HI_BeginCommandBuffer);
    Device* dev = dev_of(cmd);
    VkResult result = dev->d.BeginCommandBuffer(cmd, info);
    if (result != VK_SUCCESS) return result;
    if (CmdBuf* cb = cb_of(dev, cmd)) {
        WorkScope work(dev->work, WK_RESET);
        cb->reset_recording();
        cb->tid = GetCurrentThreadId();
        cb->threads.push_back(cb->tid);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL hk_EndCommandBuffer(VkCommandBuffer cmd)
{
    count_call(HI_EndCommandBuffer);
    Device* dev = dev_of(cmd);
    return dev->d.EndCommandBuffer(cmd);
}

VKAPI_ATTR VkResult VKAPI_CALL hk_ResetCommandBuffer(VkCommandBuffer cmd, VkCommandBufferResetFlags flags)
{
    count_call(HI_ResetCommandBuffer);
    Device* dev = dev_of(cmd);
    VkResult result = dev->d.ResetCommandBuffer(cmd, flags);
    if (result != VK_SUCCESS) return result;
    if (CmdBuf* cb = cb_of(dev, cmd)) {
        WorkScope work(dev->work, WK_RESET);
        cb->reset_recording();
    }
    return result;
}

// ---------------------------------------------------------------------------
// Bindings

VKAPI_ATTR void VKAPI_CALL hk_CmdBindPipeline(VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipeline pipeline)
{
    count_call(HI_CmdBindPipeline);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd)) {
        if (point == VK_PIPELINE_BIND_POINT_GRAPHICS)
            cb->gfx_pipeline = pipeline;
        else if (point == VK_PIPELINE_BIND_POINT_COMPUTE)
            cb->comp_pipeline = pipeline;
    }
    dev->d.CmdBindPipeline(cmd, point, pipeline);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdBindIndexBuffer(VkCommandBuffer cmd, VkBuffer buf, VkDeviceSize off, VkIndexType type)
{
    count_call(HI_CmdBindIndexBuffer);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd)) {
        cb->ib = buf;
        cb->ib_off = off;
        cb->index_size = index_size_of(type);
    }
    dev->d.CmdBindIndexBuffer(cmd, buf, off, type);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdBindIndexBuffer2(VkCommandBuffer cmd, VkBuffer buf, VkDeviceSize off, VkDeviceSize size,
                                                  VkIndexType type)
{
    count_call(HI_CmdBindIndexBuffer2);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd)) {
        cb->m2_extended_bind = true;
        cb->ib = buf;
        cb->ib_off = off;
        cb->index_size = index_size_of(type);
    }
    dev->d.CmdBindIndexBuffer2(cmd, buf, off, size, type);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdBindIndexBuffer2KHR(VkCommandBuffer cmd, VkBuffer buf, VkDeviceSize off,
                                                     VkDeviceSize size, VkIndexType type)
{
    count_call(HI_CmdBindIndexBuffer2KHR);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd)) {
        cb->m2_extended_bind = true;
        cb->ib = buf;
        cb->ib_off = off;
        cb->index_size = index_size_of(type);
    }
    dev->d.CmdBindIndexBuffer2KHR(cmd, buf, off, size, type);
}

void track_vbs(CmdBuf* cb, uint32_t first, uint32_t count, const VkBuffer* bufs, const VkDeviceSize* offs)
{
    for (uint32_t i = 0; i < count; i++) {
        uint32_t slot = first + i;
        if (slot >= kMaxVb)
            break;
        cb->vb[slot].buf = bufs ? bufs[i] : VK_NULL_HANDLE;
        cb->vb[slot].off = offs ? offs[i] : 0;
    }
}

VKAPI_ATTR void VKAPI_CALL hk_CmdBindVertexBuffers(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                                                   const VkBuffer* bufs, const VkDeviceSize* offs)
{
    count_call(HI_CmdBindVertexBuffers);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd))
        track_vbs(cb, first, count, bufs, offs);
    dev->d.CmdBindVertexBuffers(cmd, first, count, bufs, offs);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdBindVertexBuffers2(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                                                    const VkBuffer* bufs, const VkDeviceSize* offs,
                                                    const VkDeviceSize* sizes, const VkDeviceSize* strides)
{
    count_call(HI_CmdBindVertexBuffers2);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd)) {
        cb->m2_extended_bind = true;
        track_vbs(cb, first, count, bufs, offs);
    }
    dev->d.CmdBindVertexBuffers2(cmd, first, count, bufs, offs, sizes, strides);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdBindVertexBuffers2EXT(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                                                       const VkBuffer* bufs, const VkDeviceSize* offs,
                                                       const VkDeviceSize* sizes, const VkDeviceSize* strides)
{
    count_call(HI_CmdBindVertexBuffers2EXT);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd)) {
        cb->m2_extended_bind = true;
        track_vbs(cb, first, count, bufs, offs);
    }
    dev->d.CmdBindVertexBuffers2EXT(cmd, first, count, bufs, offs, sizes, strides);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdBindDescriptorSets(VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipelineLayout layout,
                                                    uint32_t first, uint32_t count, const VkDescriptorSet* sets,
                                                    uint32_t dyn_count, const uint32_t* dyn)
{
    count_call(HI_CmdBindDescriptorSets);
    Device* dev = dev_of(cmd);
    CmdBuf* cb = cb_of(dev, cmd);
    if (cb)
        on_bind_sets(dev, cb, point, layout, first, count, sets, dyn_count, dyn);
    dev->d.CmdBindDescriptorSets(cmd, point, layout, first, count, sets, dyn_count, dyn);
}

// ---------------------------------------------------------------------------
// Draws and dispatches

VKAPI_ATTR void VKAPI_CALL hk_CmdDraw(VkCommandBuffer cmd, uint32_t vertices, uint32_t instances, uint32_t first_vertex,
                                      uint32_t first_instance)
{
    count_call(HI_CmdDraw);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd)) {
        WorkScope work(dev->work, WK_DRAW);
        DrawRec r = make_rec(cb, DK_DRAW, false);
        r.count = vertices;
        r.instances = instances;
        r.first = first_vertex;
        r.first_instance = first_instance;
        cb->draws.push_back(r);
    }
    dev->d.CmdDraw(cmd, vertices, instances, first_vertex, first_instance);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdDrawIndexed(VkCommandBuffer cmd, uint32_t indices, uint32_t instances, uint32_t first_index,
                                             int32_t vertex_offset, uint32_t first_instance)
{
    count_call(HI_CmdDrawIndexed);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd)) {
        WorkScope work(dev->work, WK_DRAW);
        DrawRec r = make_rec(cb, DK_INDEXED, false);
        r.count = indices;
        r.instances = instances;
        r.first = first_index;
        r.vertex_offset = vertex_offset;
        r.first_instance = first_instance;
        cb->draws.push_back(r);
        on_record_indexed(dev, cb);
        if (m2_record(dev, cb, r)) return;
    }
    dev->d.CmdDrawIndexed(cmd, indices, instances, first_index, vertex_offset, first_instance);
}

void track_indirect(Device* dev, VkCommandBuffer cmd, uint8_t kind, VkBuffer buf, VkDeviceSize off, uint32_t count)
{
    if (CmdBuf* cb = cb_of(dev, cmd)) {
        WorkScope work(dev->work, WK_DRAW);
        DrawRec r = make_rec(cb, kind, kind == DK_DISPATCH_INDIRECT);
        r.count = count;
        r.indirect = buf;
        r.indirect_off = off;
        cb->draws.push_back(r);
    }
}

VKAPI_ATTR void VKAPI_CALL hk_CmdDrawIndirect(VkCommandBuffer cmd, VkBuffer buf, VkDeviceSize off, uint32_t count,
                                              uint32_t stride)
{
    count_call(HI_CmdDrawIndirect);
    Device* dev = dev_of(cmd);
    track_indirect(dev, cmd, DK_INDIRECT, buf, off, count);
    dev->d.CmdDrawIndirect(cmd, buf, off, count, stride);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdDrawIndexedIndirect(VkCommandBuffer cmd, VkBuffer buf, VkDeviceSize off, uint32_t count,
                                                     uint32_t stride)
{
    count_call(HI_CmdDrawIndexedIndirect);
    Device* dev = dev_of(cmd);
    track_indirect(dev, cmd, DK_INDEXED_INDIRECT, buf, off, count);
    dev->d.CmdDrawIndexedIndirect(cmd, buf, off, count, stride);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdDrawIndirectCount(VkCommandBuffer cmd, VkBuffer buf, VkDeviceSize off, VkBuffer count_buf,
                                                   VkDeviceSize count_off, uint32_t max_count, uint32_t stride)
{
    count_call(HI_CmdDrawIndirectCount);
    Device* dev = dev_of(cmd);
    track_indirect(dev, cmd, DK_INDIRECT, buf, off, max_count);
    dev->d.CmdDrawIndirectCount(cmd, buf, off, count_buf, count_off, max_count, stride);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdDrawIndirectCountKHR(VkCommandBuffer cmd, VkBuffer buf, VkDeviceSize off, VkBuffer count_buf,
                                                      VkDeviceSize count_off, uint32_t max_count, uint32_t stride)
{
    count_call(HI_CmdDrawIndirectCountKHR);
    Device* dev = dev_of(cmd);
    track_indirect(dev, cmd, DK_INDIRECT, buf, off, max_count);
    dev->d.CmdDrawIndirectCountKHR(cmd, buf, off, count_buf, count_off, max_count, stride);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdDrawIndexedIndirectCount(VkCommandBuffer cmd, VkBuffer buf, VkDeviceSize off,
                                                          VkBuffer count_buf, VkDeviceSize count_off, uint32_t max_count,
                                                          uint32_t stride)
{
    count_call(HI_CmdDrawIndexedIndirectCount);
    Device* dev = dev_of(cmd);
    track_indirect(dev, cmd, DK_INDEXED_INDIRECT, buf, off, max_count);
    dev->d.CmdDrawIndexedIndirectCount(cmd, buf, off, count_buf, count_off, max_count, stride);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdDrawIndexedIndirectCountKHR(VkCommandBuffer cmd, VkBuffer buf, VkDeviceSize off,
                                                             VkBuffer count_buf, VkDeviceSize count_off,
                                                             uint32_t max_count, uint32_t stride)
{
    count_call(HI_CmdDrawIndexedIndirectCountKHR);
    Device* dev = dev_of(cmd);
    track_indirect(dev, cmd, DK_INDEXED_INDIRECT, buf, off, max_count);
    dev->d.CmdDrawIndexedIndirectCountKHR(cmd, buf, off, count_buf, count_off, max_count, stride);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdDispatch(VkCommandBuffer cmd, uint32_t x, uint32_t y, uint32_t z)
{
    count_call(HI_CmdDispatch);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd)) {
        WorkScope work(dev->work, WK_DRAW);
        DrawRec r = make_rec(cb, DK_DISPATCH, true);
        r.count = x;
        r.instances = y;
        r.first = z;
        cb->draws.push_back(r);
    }
    dev->d.CmdDispatch(cmd, x, y, z);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdDispatchIndirect(VkCommandBuffer cmd, VkBuffer buf, VkDeviceSize off)
{
    count_call(HI_CmdDispatchIndirect);
    Device* dev = dev_of(cmd);
    track_indirect(dev, cmd, DK_DISPATCH_INDIRECT, buf, off, 1);
    dev->d.CmdDispatchIndirect(cmd, buf, off);
}

// ---------------------------------------------------------------------------
// Buffer writes recorded into command buffers

VKAPI_ATTR void VKAPI_CALL hk_CmdCopyBuffer(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst, uint32_t count,
                                            const VkBufferCopy* regions)
{
    count_call(HI_CmdCopyBuffer);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd))
        for (uint32_t i = 0; i < count; i++)
            on_copy_buffer(dev, cb, src, dst, regions[i].srcOffset, regions[i].dstOffset, regions[i].size);
    dev->d.CmdCopyBuffer(cmd, src, dst, count, regions);
}

void track_copy2(Device* dev, VkCommandBuffer cmd, const VkCopyBufferInfo2* info)
{
    if (CmdBuf* cb = cb_of(dev, cmd))
        for (uint32_t i = 0; i < info->regionCount; i++)
            on_copy_buffer(dev, cb, info->srcBuffer, info->dstBuffer, info->pRegions[i].srcOffset,
                           info->pRegions[i].dstOffset, info->pRegions[i].size);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdCopyBuffer2(VkCommandBuffer cmd, const VkCopyBufferInfo2* info)
{
    count_call(HI_CmdCopyBuffer2);
    Device* dev = dev_of(cmd);
    track_copy2(dev, cmd, info);
    dev->d.CmdCopyBuffer2(cmd, info);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdCopyBuffer2KHR(VkCommandBuffer cmd, const VkCopyBufferInfo2* info)
{
    count_call(HI_CmdCopyBuffer2KHR);
    Device* dev = dev_of(cmd);
    track_copy2(dev, cmd, info);
    dev->d.CmdCopyBuffer2KHR(cmd, info);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdCopyBufferToImage(VkCommandBuffer cmd, VkBuffer src, VkImage dst, VkImageLayout layout,
                                                   uint32_t count, const VkBufferImageCopy* regions)
{
    count_call(HI_CmdCopyBufferToImage);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd))
        for (uint32_t i = 0; i < count; i++)
            on_copy_buffer_to_image(dev, cb, src, dst, regions[i].bufferOffset, regions[i].bufferRowLength,
                                    regions[i].bufferImageHeight, regions[i].imageSubresource, regions[i].imageOffset,
                                    regions[i].imageExtent);
    dev->d.CmdCopyBufferToImage(cmd, src, dst, layout, count, regions);
}

void track_image_copy2(Device* dev, VkCommandBuffer cmd, const VkCopyBufferToImageInfo2* info)
{
    if (CmdBuf* cb = cb_of(dev, cmd))
        for (uint32_t i = 0; i < info->regionCount; i++) {
            const VkBufferImageCopy2& r = info->pRegions[i];
            on_copy_buffer_to_image(dev, cb, info->srcBuffer, info->dstImage, r.bufferOffset, r.bufferRowLength,
                                    r.bufferImageHeight, r.imageSubresource, r.imageOffset, r.imageExtent);
        }
}

VKAPI_ATTR void VKAPI_CALL hk_CmdCopyBufferToImage2(VkCommandBuffer cmd, const VkCopyBufferToImageInfo2* info)
{
    count_call(HI_CmdCopyBufferToImage2);
    Device* dev = dev_of(cmd);
    track_image_copy2(dev, cmd, info);
    dev->d.CmdCopyBufferToImage2(cmd, info);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdCopyBufferToImage2KHR(VkCommandBuffer cmd, const VkCopyBufferToImageInfo2* info)
{
    count_call(HI_CmdCopyBufferToImage2KHR);
    Device* dev = dev_of(cmd);
    track_image_copy2(dev, cmd, info);
    dev->d.CmdCopyBufferToImage2KHR(cmd, info);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdUpdateBuffer(VkCommandBuffer cmd, VkBuffer dst, VkDeviceSize off, VkDeviceSize size,
                                              const void* data)
{
    count_call(HI_CmdUpdateBuffer);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd))
        on_update_buffer(dev, cb, dst, off, size, data);
    dev->d.CmdUpdateBuffer(cmd, dst, off, size, data);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdFillBuffer(VkCommandBuffer cmd, VkBuffer dst, VkDeviceSize off, VkDeviceSize size,
                                            uint32_t value)
{
    count_call(HI_CmdFillBuffer);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd))
        on_fill_buffer(dev, cb, dst, off, size, value);
    dev->d.CmdFillBuffer(cmd, dst, off, size, value);
}

// ---------------------------------------------------------------------------
// Render passes

void rp_begin(Device* dev, VkCommandBuffer cmd, bool dynamic)
{
    if (CmdBuf* cb = cb_of(dev, cmd)) {
        cb->in_rp = true;
        if (dynamic)
            cb->dyn_rp_count++;
        else
            cb->rp_count++;
    }
}

void rp_end(Device* dev, VkCommandBuffer cmd)
{
    if (CmdBuf* cb = cb_of(dev, cmd))
        cb->in_rp = false;
}

VKAPI_ATTR void VKAPI_CALL hk_CmdBeginRenderPass(VkCommandBuffer cmd, const VkRenderPassBeginInfo* info,
                                                 VkSubpassContents contents)
{
    count_call(HI_CmdBeginRenderPass);
    Device* dev = dev_of(cmd);
    rp_begin(dev, cmd, false);
    dev->d.CmdBeginRenderPass(cmd, info, contents);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdBeginRenderPass2(VkCommandBuffer cmd, const VkRenderPassBeginInfo* info,
                                                  const VkSubpassBeginInfo* sub)
{
    count_call(HI_CmdBeginRenderPass2);
    Device* dev = dev_of(cmd);
    rp_begin(dev, cmd, false);
    dev->d.CmdBeginRenderPass2(cmd, info, sub);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdBeginRenderPass2KHR(VkCommandBuffer cmd, const VkRenderPassBeginInfo* info,
                                                     const VkSubpassBeginInfo* sub)
{
    count_call(HI_CmdBeginRenderPass2KHR);
    Device* dev = dev_of(cmd);
    rp_begin(dev, cmd, false);
    dev->d.CmdBeginRenderPass2KHR(cmd, info, sub);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdEndRenderPass(VkCommandBuffer cmd)
{
    count_call(HI_CmdEndRenderPass);
    Device* dev = dev_of(cmd);
    rp_end(dev, cmd);
    dev->d.CmdEndRenderPass(cmd);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdEndRenderPass2(VkCommandBuffer cmd, const VkSubpassEndInfo* info)
{
    count_call(HI_CmdEndRenderPass2);
    Device* dev = dev_of(cmd);
    rp_end(dev, cmd);
    dev->d.CmdEndRenderPass2(cmd, info);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdEndRenderPass2KHR(VkCommandBuffer cmd, const VkSubpassEndInfo* info)
{
    count_call(HI_CmdEndRenderPass2KHR);
    Device* dev = dev_of(cmd);
    rp_end(dev, cmd);
    dev->d.CmdEndRenderPass2KHR(cmd, info);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdBeginRendering(VkCommandBuffer cmd, const VkRenderingInfo* info)
{
    count_call(HI_CmdBeginRendering);
    Device* dev = dev_of(cmd);
    rp_begin(dev, cmd, true);
    dev->d.CmdBeginRendering(cmd, info);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdBeginRenderingKHR(VkCommandBuffer cmd, const VkRenderingInfo* info)
{
    count_call(HI_CmdBeginRenderingKHR);
    Device* dev = dev_of(cmd);
    rp_begin(dev, cmd, true);
    dev->d.CmdBeginRenderingKHR(cmd, info);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdEndRendering(VkCommandBuffer cmd)
{
    count_call(HI_CmdEndRendering);
    Device* dev = dev_of(cmd);
    rp_end(dev, cmd);
    dev->d.CmdEndRendering(cmd);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdEndRenderingKHR(VkCommandBuffer cmd)
{
    count_call(HI_CmdEndRenderingKHR);
    Device* dev = dev_of(cmd);
    rp_end(dev, cmd);
    dev->d.CmdEndRenderingKHR(cmd);
}

VKAPI_ATTR void VKAPI_CALL hk_CmdExecuteCommands(VkCommandBuffer cmd, uint32_t count, const VkCommandBuffer* cbs)
{
    count_call(HI_CmdExecuteCommands);
    Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd))
        on_execute_commands(dev, cb, count, cbs);
    dev->d.CmdExecuteCommands(cmd, count, cbs);
}

// ---------------------------------------------------------------------------
// Submission and frame boundary

VKAPI_ATTR VkResult VKAPI_CALL hk_QueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo* submits, VkFence fence)
{
    count_call(HI_QueueSubmit);
    Device* dev = dev_of(queue);
    {
        const auto result = m2_submit(dev, queue);
        if (result != VK_SUCCESS) return result;
    }
    for (uint32_t i = 0; i < count; i++) {
        m2_seal(dev, submits[i].commandBufferCount, submits[i].pCommandBuffers);
        on_submit(dev, submits[i].commandBufferCount, submits[i].pCommandBuffers);
    }
    return dev->d.QueueSubmit(queue, count, submits, fence);
}

void track_submit2(Device* dev, uint32_t count, const VkSubmitInfo2* submits)
{
    std::vector<VkCommandBuffer> list;
    for (uint32_t i = 0; i < count; i++) {
        list.clear();
        for (uint32_t k = 0; k < submits[i].commandBufferInfoCount; k++)
            list.push_back(submits[i].pCommandBufferInfos[k].commandBuffer);
        on_submit(dev, static_cast<uint32_t>(list.size()), list.data());
        m2_seal(dev, static_cast<uint32_t>(list.size()), list.data());
    }
}

VKAPI_ATTR VkResult VKAPI_CALL hk_QueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits, VkFence fence)
{
    count_call(HI_QueueSubmit2);
    Device* dev = dev_of(queue);
    track_submit2(dev, count, submits);
    {
        const auto result = m2_submit(dev, queue);
        if (result != VK_SUCCESS) return result;
    }
    return dev->d.QueueSubmit2(queue, count, submits, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL hk_QueueSubmit2KHR(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits, VkFence fence)
{
    count_call(HI_QueueSubmit2KHR);
    Device* dev = dev_of(queue);
    track_submit2(dev, count, submits);
    {
        const auto result = m2_submit(dev, queue);
        if (result != VK_SUCCESS) return result;
    }
    return dev->d.QueueSubmit2KHR(queue, count, submits, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL hk_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info)
{
    count_call(HI_QueuePresentKHR);
    Device* dev = dev_of(queue);
    on_present(dev);
    return dev->d.QueuePresentKHR(queue, info);
}

VKAPI_ATTR void VKAPI_CALL hk_DestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc)
{
    count_call(HI_DestroyDevice);
    if (!device)
        return;
    Device* dev = dev_of(device);
    if (!dev)
        return;
    PFN_vkDestroyDevice next = dev->d.DestroyDevice;
    if (!dev->passive) {
        m2_stop(dev);
        logf("device destroyed after %llu frames", static_cast<unsigned long long>(dev->frame_no));
        dev_destroy(dev);
    }
    next(device, alloc);
}

// ---------------------------------------------------------------------------
// Counted-only functions: one generic hook that counts and forwards

template <typename Fn, Fn DeviceDispatch::*Member, int Index> struct CountedHook;

template <typename R, typename First, typename... Args, R (VKAPI_PTR* DeviceDispatch::*Member)(First, Args...), int Index>
struct CountedHook<R (VKAPI_PTR*)(First, Args...), Member, Index> {
    static VKAPI_ATTR R VKAPI_CALL hook(First first, Args... args)
    {
        Device* dev = dev_of(first);
        count_call(Index);
        return (dev->d.*Member)(first, args...);
    }
};

#define X(n) constexpr PFN_vk##n hk_##n = &CountedHook<PFN_vk##n, &DeviceDispatch::n, HI_##n>::hook;
EFVK_COUNTED_FUNCS(X)
#undef X

// ---------------------------------------------------------------------------
// Hook table

struct HookEntry {
    const char* name;
    PFN_vkVoidFunction hook;
    size_t next_offset;
    bool always; // also installed in passthrough mode
};

#define X(n) {"vk" #n, reinterpret_cast<PFN_vkVoidFunction>(hk_##n), offsetof(DeviceDispatch, n), false},
HookEntry g_hooks[] = {EFVK_DEVICE_FUNCS(X)};
#undef X

bool hook_is_always(const char* name)
{
    return !strcmp(name, "vkDestroyDevice") || !strcmp(name, "vkQueuePresentKHR");
}

const HookEntry* find_hook(const char* name)
{
    for (const HookEntry& h : g_hooks)
        if (!strcmp(h.name, name)) {
            if (g_cfg.mode == Mode::Passthrough && !hook_is_always(name))
                return nullptr;
            return &h;
        }
    return nullptr;
}

PFN_vkVoidFunction& next_slot(DeviceDispatch& d, const HookEntry& h)
{
    return *reinterpret_cast<PFN_vkVoidFunction*>(reinterpret_cast<char*>(&d) + h.next_offset);
}

// ---------------------------------------------------------------------------
// Instance and device creation

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hk_GetDeviceProcAddr(VkDevice device, const char* name);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hk_GetInstanceProcAddr(VkInstance instance, const char* name);

VKAPI_ATTR VkResult VKAPI_CALL hk_CreateInstance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks* alloc,
                                                 VkInstance* instance)
{
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkCreateInstance next;
    if (injected()) {
        gipa = g_real_gipa;
        next = g_real_create_instance;
    } else {
        VkLayerInstanceCreateInfo* link = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(info->pNext));
        while (link &&
               !(link->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO))
            link = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(link->pNext));
        if (!link || !link->u.pLayerInfo)
            return VK_ERROR_INITIALIZATION_FAILED;
        gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        link->u.pLayerInfo = link->u.pLayerInfo->pNext;
        next = reinterpret_cast<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    }
    if (!gipa || !next)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult r = next(info, alloc, instance);
    if (r != VK_SUCCESS)
        return r;

    pin_module();
    g_attach.instances_created.fetch_add(1);
    InstanceData* inst = new InstanceData();
    inst->handle = *instance;
    inst->gipa = gipa;
    inst->DestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(gipa(*instance, "vkDestroyInstance"));
    inst->EnumerateDeviceExtensionProperties = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        gipa(*instance, "vkEnumerateDeviceExtensionProperties"));
    inst->GetPhysicalDeviceProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(*instance, "vkGetPhysicalDeviceProperties"));
    inst->GetPhysicalDeviceMemoryProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(*instance, "vkGetPhysicalDeviceMemoryProperties"));
    {
        std::lock_guard<std::mutex> lock(g_inst_mx);
        g_insts[key_of(*instance)] = inst;
    }
    const VkApplicationInfo* app = info->pApplicationInfo;
    logf("instance created: app=%s engine=%s api=0x%x", app && app->pApplicationName ? app->pApplicationName : "?",
         app && app->pEngineName ? app->pEngineName : "?", app ? app->apiVersion : 0);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL hk_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* alloc)
{
    if (!instance)
        return;
    InstanceData* inst;
    {
        std::lock_guard<std::mutex> lock(g_inst_mx);
        auto it = g_insts.find(key_of(instance));
        if (it == g_insts.end())
            return;
        inst = it->second;
        g_insts.erase(it);
    }
    PFN_vkDestroyInstance next = inst->DestroyInstance;
    delete inst;
    next(instance, alloc);
}

VKAPI_ATTR VkResult VKAPI_CALL hk_EnumerateDeviceExtensionProperties(VkPhysicalDevice phys, const char* layer_name,
                                                                     uint32_t* count, VkExtensionProperties* props)
{
    if (layer_name && !strcmp(layer_name, EFVK_LAYER_NAME)) {
        if (count)
            *count = 0;
        return VK_SUCCESS;
    }
    InstanceData* inst = inst_of_key(key_of(phys));
    if (!inst || !inst->EnumerateDeviceExtensionProperties)
        return VK_ERROR_INITIALIZATION_FAILED;
    return inst->EnumerateDeviceExtensionProperties(phys, layer_name, count, props);
}

VKAPI_ATTR VkResult VKAPI_CALL hk_CreateDevice(VkPhysicalDevice phys, const VkDeviceCreateInfo* info,
                                               const VkAllocationCallbacks* alloc, VkDevice* device)
{
    InstanceData* inst = inst_of_key(key_of(phys));
    PFN_vkSetDeviceLoaderData set_loader_data = nullptr;
    for (auto p = static_cast<const VkBaseInStructure*>(info->pNext); p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) {
            auto link = reinterpret_cast<const VkLayerDeviceCreateInfo*>(p);
            if (link->function == VK_LOADER_DATA_CALLBACK) set_loader_data = link->u.pfnSetDeviceLoaderData;
        }
    PFN_vkGetDeviceProcAddr gdpa;
    PFN_vkCreateDevice next;
    if (injected()) {
        gdpa = g_real_gdpa;
        next = g_real_create_device;
    } else {
        VkLayerDeviceCreateInfo* link = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(info->pNext));
        while (link &&
               !(link->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO))
            link = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(link->pNext));
        if (!link || !link->u.pLayerInfo)
            return VK_ERROR_INITIALIZATION_FAILED;
        PFN_vkGetInstanceProcAddr gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
        link->u.pLayerInfo = link->u.pLayerInfo->pNext;
        next = reinterpret_cast<PFN_vkCreateDevice>(gipa(inst ? inst->handle : VK_NULL_HANDLE, "vkCreateDevice"));
    }
    if (!gdpa || !next)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult r = next(phys, info, alloc, device);
    if (r != VK_SUCCESS)
        return r;

    g_attach.devices_created.fetch_add(1);
    Device* dev = dev_create(*device);
    dev->phys = phys;
    dev->next_gdpa = gdpa;
    dev->set_loader_data = set_loader_data;
    for (const HookEntry& h : g_hooks)
        next_slot(dev->d, h) = gdpa(*device, h.name);
    if (inst && inst->GetPhysicalDeviceProperties) {
        VkPhysicalDeviceProperties props;
        inst->GetPhysicalDeviceProperties(phys, &props);
        memcpy(dev->gpu_name, props.deviceName, sizeof(dev->gpu_name));
        dev->api_version = props.apiVersion;
        dev->vendor_id = props.vendorID;
        dev->device_id = props.deviceID;
        dev->driver_version = props.driverVersion;
        dev->max_bound_descriptor_sets = props.limits.maxBoundDescriptorSets;
        if (inst->GetPhysicalDeviceMemoryProperties && !g_cfg.m2_package.empty()) {
            VkPhysicalDeviceMemoryProperties memory;
            inst->GetPhysicalDeviceMemoryProperties(phys, &memory);
            character_device_setup(dev, info, inst->handle, inst->gipa);
            m2_create(dev, props, memory);
        }
    }
    for (uint32_t i = 0; i < info->enabledExtensionCount; i++)
        dev->extensions.push_back(info->ppEnabledExtensionNames[i]);
    logf("device created on %s, %u extensions enabled", dev->gpu_name, info->enabledExtensionCount);
    if (g_cfg.mode == Mode::Probe)
        ipc_start();
    return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hk_GetDeviceProcAddr(VkDevice device, const char* name)
{
    if (!strcmp(name, "vkGetDeviceProcAddr"))
        return reinterpret_cast<PFN_vkVoidFunction>(hk_GetDeviceProcAddr);
    Device* dev = device ? dev_of(device) : nullptr;
    if (!dev || dev->passive)
        return injected() && g_real_gdpa ? g_real_gdpa(device, name) : nullptr;
    if (g_cfg.mode == Mode::Probe)
        note_resolved(name);
    if (const HookEntry* h = find_hook(name)) {
        // Offer a hook only for what the next element of the chain really implements.
        if (next_slot(dev->d, *h))
            return h->hook;
    }
    return dev->next_gdpa(device, name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hk_GetInstanceProcAddr(VkInstance instance, const char* name)
{
    if (!strcmp(name, "vkGetInstanceProcAddr"))
        return reinterpret_cast<PFN_vkVoidFunction>(hk_GetInstanceProcAddr);
    if (!strcmp(name, "vkCreateInstance"))
        return reinterpret_cast<PFN_vkVoidFunction>(hk_CreateInstance);
    if (!strcmp(name, "vkDestroyInstance"))
        return reinterpret_cast<PFN_vkVoidFunction>(hk_DestroyInstance);
    if (!strcmp(name, "vkCreateDevice"))
        return reinterpret_cast<PFN_vkVoidFunction>(hk_CreateDevice);
    if (!strcmp(name, "vkEnumerateDeviceExtensionProperties") && !injected())
        return reinterpret_cast<PFN_vkVoidFunction>(hk_EnumerateDeviceExtensionProperties);
    if (!strcmp(name, "vkGetDeviceProcAddr"))
        return reinterpret_cast<PFN_vkVoidFunction>(hk_GetDeviceProcAddr);
    if (const HookEntry* h = find_hook(name))
        return h->hook;
    if (injected())
        return g_real_gipa ? g_real_gipa(instance, name) : nullptr;
    if (!instance)
        return nullptr;
    InstanceData* inst = inst_of_key(key_of(instance));
    return inst ? inst->gipa(instance, name) : nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Inject entry: which exports of vulkan-1.dll lead to which hook

namespace efvk {

void collect_export_hooks(std::vector<ExportHook>& out)
{
    out.push_back({"vkGetInstanceProcAddr", reinterpret_cast<void*>(hk_GetInstanceProcAddr),
                   reinterpret_cast<void**>(&g_real_gipa)});
    out.push_back({"vkGetDeviceProcAddr", reinterpret_cast<void*>(hk_GetDeviceProcAddr),
                   reinterpret_cast<void**>(&g_real_gdpa)});
    out.push_back({"vkCreateInstance", reinterpret_cast<void*>(hk_CreateInstance),
                   reinterpret_cast<void**>(&g_real_create_instance)});
    out.push_back({"vkDestroyInstance", reinterpret_cast<void*>(hk_DestroyInstance),
                   reinterpret_cast<void**>(&g_real_destroy_instance)});
    out.push_back({"vkCreateDevice", reinterpret_cast<void*>(hk_CreateDevice),
                   reinterpret_cast<void**>(&g_real_create_device)});
    // A device function may be called through its export for a device that was created before
    // the redirection. The original address goes into the dispatch table of the passive device.
    for (const HookEntry& h : g_hooks) {
        if (g_cfg.mode == Mode::Passthrough && !hook_is_always(h.name))
            continue;
        out.push_back({h.name, reinterpret_cast<void*>(h.hook), reinterpret_cast<void**>(&next_slot(g_passive_dev.d, h))});
    }
}

// Called when every original address is known and before the first export is redirected.
void exports_resolved()
{
    dev_set_passive(&g_passive_dev);
}

} // namespace efvk

// ---------------------------------------------------------------------------
// Exports

extern "C" {

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL efvkGetInstanceProcAddr(VkInstance instance, const char* name)
{
    return hk_GetInstanceProcAddr(instance, name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL efvkGetDeviceProcAddr(VkDevice device, const char* name)
{
    return hk_GetDeviceProcAddr(device, name);
}

VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* v)
{
    if (!v || v->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (injected()) {
        // Hooking twice would record everything twice; the export redirection wins.
        logf("the loader offered to use this DLL as a layer, refused because the entry is inject");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (v->loaderLayerInterfaceVersion > CURRENT_LOADER_LAYER_INTERFACE_VERSION)
        v->loaderLayerInterfaceVersion = CURRENT_LOADER_LAYER_INTERFACE_VERSION;
    v->pfnGetInstanceProcAddr = hk_GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr = hk_GetDeviceProcAddr;
    v->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        util_init(self);
        if (g_cfg.entry == Entry::Inject) {
            pin_module();
            bootstrap_start();
            if (g_cfg.mode == Mode::Probe)
                ipc_start();
        }
    }
    return TRUE;
}

} // extern "C"
