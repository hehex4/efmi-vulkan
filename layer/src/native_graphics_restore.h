// Prepared graphics changes the graphics bind point, IA and dynamic state.
#pragma once
#include "native_descriptor_restore.h"
#include "graphics_dynamic_state.h"

namespace efvk {
struct CmdBuf;
struct NativeGraphicsRestore : NativeDescriptorRestore {
    struct Vertex {uint32_t slot=0;VkBuffer buffer=VK_NULL_HANDLE;VkDeviceSize offset=0;};
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkBuffer index=VK_NULL_HANDLE;
    VkDeviceSize index_offset=0;
    VkIndexType index_type=VK_INDEX_TYPE_UINT32;
    std::vector<Vertex> vertices;
    std::vector<std::shared_ptr<const void>> resources;
    GraphicsDynamicState dynamic;
    std::vector<VkDynamicState> required;
    uint32_t viewport_count=0,scissor_count=0;
    // A pass continuation may select an equivalent pipeline for its LOAD pass.
    void record(Device*,VkCommandBuffer,VkPipeline selected=VK_NULL_HANDLE) const;
};
bool prepare_native_graphics_restore(Device*,const CmdBuf*,NativeGraphicsRestore&,std::string&,bool resource_lock_held=false);
}
