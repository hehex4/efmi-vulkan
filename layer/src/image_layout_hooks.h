// Application layout events. Internal commands restore their temporary transitions themselves.
VKAPI_ATTR void VKAPI_CALL hk_CmdPipelineBarrier(VkCommandBuffer cmd, VkPipelineStageFlags source, VkPipelineStageFlags destination,
    VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier* memory, uint32_t buffer_count,
    const VkBufferMemoryBarrier* buffers, uint32_t image_count, const VkImageMemoryBarrier* images)
{
    count_call(HI_CmdPipelineBarrier); Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd)) on_image_barriers(dev, cb, image_count, images);
    dev->d.CmdPipelineBarrier(cmd, source, destination, flags, memory_count, memory, buffer_count, buffers, image_count, images);
}
#define IMAGE_BARRIER2(Name) \
VKAPI_ATTR void VKAPI_CALL hk_##Name(VkCommandBuffer cmd, const VkDependencyInfo* dependency) { \
    count_call(HI_##Name); Device* dev = dev_of(cmd); \
    if (CmdBuf* cb = cb_of(dev, cmd)) on_image_barriers(dev, cb, dependency); \
    dev->d.Name(cmd, dependency); \
}
IMAGE_BARRIER2(CmdPipelineBarrier2)
IMAGE_BARRIER2(CmdPipelineBarrier2KHR)
#undef IMAGE_BARRIER2
VKAPI_ATTR void VKAPI_CALL hk_CmdWaitEvents(VkCommandBuffer cmd, uint32_t count, const VkEvent* events,
    VkPipelineStageFlags source, VkPipelineStageFlags destination, uint32_t memory_count, const VkMemoryBarrier* memory,
    uint32_t buffer_count, const VkBufferMemoryBarrier* buffers, uint32_t image_count, const VkImageMemoryBarrier* images)
{
    count_call(HI_CmdWaitEvents); Device* dev = dev_of(cmd);
    if (CmdBuf* cb = cb_of(dev, cmd)) on_image_barriers(dev, cb, image_count, images);
    dev->d.CmdWaitEvents(cmd, count, events, source, destination, memory_count, memory, buffer_count, buffers, image_count, images);
}
#define IMAGE_WAIT2(Name) \
VKAPI_ATTR void VKAPI_CALL hk_##Name(VkCommandBuffer cmd, uint32_t count, const VkEvent* events, const VkDependencyInfo* dependencies) { \
    count_call(HI_##Name); Device* dev = dev_of(cmd); \
    if (CmdBuf* cb = cb_of(dev, cmd)) for (uint32_t i = 0; i < count; ++i) on_image_barriers(dev, cb, dependencies + i); \
    dev->d.Name(cmd, count, events, dependencies); \
}
IMAGE_WAIT2(CmdWaitEvents2)
IMAGE_WAIT2(CmdWaitEvents2KHR)
#undef IMAGE_WAIT2
#define IMAGE_SET2(Name) \
VKAPI_ATTR void VKAPI_CALL hk_##Name(VkCommandBuffer cmd, VkEvent event, const VkDependencyInfo* dependency) { \
    count_call(HI_##Name); Device* dev = dev_of(cmd); \
    if (CmdBuf* cb = cb_of(dev, cmd)) on_image_barriers(dev, cb, dependency, true); \
    dev->d.Name(cmd, event, dependency); \
}
IMAGE_SET2(CmdSetEvent2)
IMAGE_SET2(CmdSetEvent2KHR)
#undef IMAGE_SET2
