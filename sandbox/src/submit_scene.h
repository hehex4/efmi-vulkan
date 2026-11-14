// Actual core/KHR Submit2 with application-owned stage-limited timeline operations.
static PFN_vkQueueSubmit2 scene_submit2;
static PFN_vkGetSemaphoreCounterValue scene_semaphore_value;
static bool create_submit_scene()
{
    if (!g.submit2) return true;
    scene_submit2 = reinterpret_cast<PFN_vkQueueSubmit2>(vkGetDeviceProcAddr(g.device, g.submit2 == 2 ? "vkQueueSubmit2KHR" : "vkQueueSubmit2"));
    scene_semaphore_value = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(vkGetDeviceProcAddr(g.device, "vkGetSemaphoreCounterValue"));
    if (!scene_submit2 || !scene_semaphore_value) return false;
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO}; type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; info.pNext = &type;
    CHECK(vkCreateSemaphore(g.device, &info, nullptr, &g.graphics_progress));
    return true;
}
static bool submit_scene(uint32_t image)
{
    // render_frame has already waited for the previous frame's fence.
    uint64_t completed = 0; CHECK(scene_semaphore_value(g.device, g.graphics_progress, &completed));
    if (completed < g.submit_serial) { printf("FAILED: Submit2 graphics signal did not complete\n"); return false; }
    const uint64_t value = ++g.submit_serial;
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO}; command.commandBuffer = g.primary;
    VkSemaphoreSubmitInfo waits[2]{}, signals[2]{};
    for (auto& item : waits) item.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    for (auto& item : signals) item.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waits[0].semaphore = g.sem_acquire; waits[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    waits[1].semaphore = g.graphics_progress; waits[1].value = value - 1; waits[1].stageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    signals[0].semaphore = g.graphics_progress; signals[0].value = value; signals[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    signals[1].semaphore = g.sem_render[image]; signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1; submit.pCommandBufferInfos = &command;
    submit.waitSemaphoreInfoCount = 2; submit.pWaitSemaphoreInfos = waits;
    submit.signalSemaphoreInfoCount = 2; submit.pSignalSemaphoreInfos = signals;
    CHECK(scene_submit2(g.queue, 1, &submit, g.fence));
    if (value == 1) printf("Submit2 %s: fragment wait and color signal\n", g.submit2 == 2 ? "KHR" : "core");
    return true;
}
static void destroy_submit_scene()
{
    if (g.graphics_progress) vkDestroySemaphore(g.device, g.graphics_progress, nullptr);
}
