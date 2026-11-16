#pragma once
#include "semaphore_lifetime.h"

namespace efvk {
// A binary semaphore can be reused for acquisition after its prior queue signal
// and wait have completed. That new signal covers image acquisition, not the old
// queue producer. Timeout/not-ready/error results do not replace its history.
inline void acquired_image_signal(Device* device,VkSemaphore semaphore,VkResult result)
{
    if(result==VK_SUCCESS || result==VK_SUBOPTIMAL_KHR)forget_application_semaphore(device,semaphore);
}
inline VkResult acquire_application_image(Device* device,VkDevice handle,VkSwapchainKHR swapchain,uint64_t timeout,
                                          VkSemaphore semaphore,VkFence fence,uint32_t* index)
{
    const auto result=device->d.AcquireNextImageKHR(handle,swapchain,timeout,semaphore,fence,index);
    acquired_image_signal(device,semaphore,result);
    return result;
}
inline VkResult acquire_application_image(Device* device,VkDevice handle,const VkAcquireNextImageInfoKHR* info,uint32_t* index)
{
    const auto result=device->d.AcquireNextImage2KHR(handle,info,index);
    acquired_image_signal(device,info->semaphore,result);
    return result;
}
}
