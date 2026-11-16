#include "../layer/src/semaphore_acquire.h"
namespace semaphore_acquire_fixture {
static Device* active=nullptr;
static VkSwapchainKHR expected_swapchain{};
static VkSemaphore expected_semaphore{};
static VkFence expected_fence{};
static uint32_t* expected_index=nullptr;
static const VkAcquireNextImageInfoKHR* expected_info=nullptr;
static VkResult driver_result=VK_SUCCESS;
static bool arguments=true,unlocked=true,prior_visible=true;
static unsigned legacy_calls=0,second_calls=0;
static VkResult returned(VkSemaphore semaphore,uint32_t* index){
    const auto view=active->queue_signals->try_read();unlocked=unlocked&&view.owns_lock();
    if(semaphore)prior_visible=prior_visible&&view.contains(semaphore);
    if(driver_result==VK_SUCCESS || driver_result==VK_SUBOPTIMAL_KHR)*index=7;
    return driver_result;
}
static VKAPI_ATTR VkResult VKAPI_CALL acquire(VkDevice device,VkSwapchainKHR swapchain,uint64_t timeout,
    VkSemaphore semaphore,VkFence fence,uint32_t* index){
    ++legacy_calls;arguments=arguments&&device==active->handle&&swapchain==expected_swapchain&&timeout==123456&&
        semaphore==expected_semaphore&&fence==expected_fence&&index==expected_index;
    return returned(semaphore,index);
}
static VKAPI_ATTR VkResult VKAPI_CALL acquire2(VkDevice device,const VkAcquireNextImageInfoKHR* info,uint32_t* index){
    ++second_calls;arguments=arguments&&device==active->handle&&info==expected_info&&index==expected_index&&
        info->swapchain==expected_swapchain&&info->timeout==123456&&info->semaphore==expected_semaphore&&
        info->fence==expected_fence&&info->deviceMask==1&&info->pNext==nullptr;
    return returned(info->semaphore,index);
}
}
static void semaphore_acquire_cases()
{
    using namespace semaphore_acquire_fixture;
    Device device;active=&device;device.handle=h<VkDevice>(10101);device.queue_signals=std::make_shared<QueueSignalHistory>();
    device.d.AcquireNextImageKHR=acquire;device.d.AcquireNextImage2KHR=acquire2;
    const auto reused=h<VkSemaphore>(10102),other=h<VkSemaphore>(10103);
    expected_swapchain=h<VkSwapchainKHR>(10104);expected_fence=h<VkFence>(10105);
    uint32_t index=99;expected_index=&index;legacy_calls=second_calls=0;arguments=unlocked=prior_visible=true;
    auto record=[&](VkSemaphore semaphore){QueueSignalHistory::Attempt attempt(*device.queue_signals);attempt.signal(semaphore);attempt.finish(VK_SUCCESS);};
    auto known=[&](VkSemaphore semaphore){return device.queue_signals->read().contains(semaphore);};
    VkAcquireNextImageInfoKHR info{VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR};expected_info=&info;
    info.swapchain=expected_swapchain;info.timeout=123456;info.fence=expected_fence;info.deviceMask=1;
    record(other);
    for(bool second:{false,true}){
        auto call=[&]{return second?acquire_application_image(&device,device.handle,&info,&index):
            acquire_application_image(&device,device.handle,expected_swapchain,123456,expected_semaphore,expected_fence,&index);};
        for(const auto result:{VK_SUCCESS,VK_SUBOPTIMAL_KHR,VK_TIMEOUT,VK_NOT_READY,VK_ERROR_OUT_OF_DATE_KHR}){
            expected_semaphore=info.semaphore=reused;record(reused);index=99;driver_result=result;
            const auto actual=call();const bool acquired=result==VK_SUCCESS || result==VK_SUBOPTIMAL_KHR;
            check(actual==result&&index==(acquired?7u:99u)&&known(other)&&known(reused)!=acquired,
                second?(acquired?"acquire2 replaces the old queue producer with an image-acquisition signal":"unsuccessful acquire2 preserves signal history and its result"):
                       (acquired?"acquire replaces the old queue producer with an image-acquisition signal":"unsuccessful acquire preserves signal history and its result"));
        }
        expected_semaphore=info.semaphore=VK_NULL_HANDLE;driver_result=VK_SUCCESS;index=99;
        check(call()==VK_SUCCESS&&index==7&&known(reused)&&known(other),
              second?"fence-only acquire2 preserves live semaphore classifications":"fence-only acquire preserves live semaphore classifications");
    }
    expected_semaphore=info.semaphore=reused;driver_result=VK_SUCCESS;
    acquire_application_image(&device,device.handle,&info,&index);record(reused);
    check(known(reused),"an acquired semaphore may later gain a new queue signal classification");
    check(arguments&&legacy_calls==6&&second_calls==7,"both acquisition entries forward exact handles, timeout, output pointer and acquire info");
    check(unlocked,"acquisition does not hold the history lock across the driver call");
    check(prior_visible,"old history is preserved until the driver reports an accepted acquisition");
    active=nullptr;
}
