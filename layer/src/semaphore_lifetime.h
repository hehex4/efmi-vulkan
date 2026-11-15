#pragma once
#include "state.h"

namespace efvk {
// Queue-signal history is scoped to one application object, never a recycled raw
// handle. This does not establish payload/value provenance or cross-queue hazards.
inline void forget_application_semaphore(Device* device,VkSemaphore semaphore)
{
    if(!semaphore)return;
    std::lock_guard<std::mutex> lock(device->queue_signal_mx);
    device->queue_signalled.erase(semaphore);
}
inline VkResult create_application_semaphore(Device* device,const VkSemaphoreCreateInfo* info,
                                             const VkAllocationCallbacks* alloc,VkSemaphore* semaphore)
{
    const auto result=device->d.CreateSemaphore(device->handle,info,alloc,semaphore);
    if(result==VK_SUCCESS)forget_application_semaphore(device,*semaphore);
    return result;
}
inline void destroy_application_semaphore(Device* device,VkSemaphore semaphore,const VkAllocationCallbacks* alloc)
{
    forget_application_semaphore(device,semaphore);
    // No bookkeeping lock crosses the driver call. Submitted uses must already
    // have completed before an application is allowed to destroy this object.
    device->d.DestroySemaphore(device->handle,semaphore,alloc);
}
}
