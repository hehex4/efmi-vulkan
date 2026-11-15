// Application semaphore identities must not survive handle destruction/reuse.
#include "../layer/src/semaphore_lifetime.h"
namespace semaphore_lifetime_fixture {
static Device* active=nullptr;
static const VkSemaphoreCreateInfo* expected_info=nullptr;
static const VkAllocationCallbacks* expected_alloc=nullptr;
static VkSemaphore reused{};
static bool fail_create=false,arguments=true,unlocked=true,forgotten=true;
static unsigned creates=0,destroys=0;
static VKAPI_ATTR VkResult VKAPI_CALL create(VkDevice device,const VkSemaphoreCreateInfo* info,
    const VkAllocationCallbacks* alloc,VkSemaphore* out)
{
    ++creates;arguments=arguments&&device==active->handle&&info==expected_info&&alloc==expected_alloc;
    const bool free=active->queue_signal_mx.try_lock();unlocked=unlocked&&free;
    if(free)active->queue_signal_mx.unlock();
    if(fail_create)return VK_ERROR_OUT_OF_HOST_MEMORY;
    *out=reused;return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy(VkDevice device,VkSemaphore semaphore,const VkAllocationCallbacks* alloc)
{
    ++destroys;arguments=arguments&&device==active->handle&&alloc==expected_alloc;
    const bool free=active->queue_signal_mx.try_lock();unlocked=unlocked&&free;
    if(free){forgotten=forgotten&&!active->queue_signalled.count(semaphore);active->queue_signal_mx.unlock();}
}
}
static void semaphore_lifetime_cases()
{
    using namespace semaphore_lifetime_fixture;
    auto hooked=[](const char* name){
        for(int i=0;i<HI_COUNT;++i)if(!strcmp(g_api_names[i],name))return true;
        return false;
    };
    check(hooked("vkCreateSemaphore"),"application semaphore creation has a tracked lifetime boundary");
    check(hooked("vkDestroySemaphore"),"application semaphore destruction has a tracked lifetime boundary");
    Device device;active=&device;device.handle=h<VkDevice>(9701);reused=h<VkSemaphore>(9702);
    const auto other=h<VkSemaphore>(9703);VkSemaphore output=h<VkSemaphore>(9704);
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};type.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;type.initialValue=7;
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};info.pNext=&type;
    VkAllocationCallbacks alloc{};expected_info=&info;expected_alloc=&alloc;
    device.d.CreateSemaphore=create;device.d.DestroySemaphore=destroy;
    creates=destroys=0;arguments=unlocked=forgotten=true;fail_create=false;
    device.queue_signalled.insert(reused);device.queue_signalled.insert(other);
    check(create_application_semaphore(&device,&info,&alloc,&output)==VK_SUCCESS&&output==reused&&
          !device.queue_signalled.count(reused)&&device.queue_signalled.count(other),
          "new semaphore creation clears recycled history and preserves unrelated signals");
    device.queue_signalled.insert(reused);fail_create=true;
    check(create_application_semaphore(&device,&info,&alloc,&output)==VK_ERROR_OUT_OF_HOST_MEMORY&&
          device.queue_signalled.count(reused)&&device.queue_signalled.count(other),
          "failed creation preserves existing signal history and the driver result");
    destroy_application_semaphore(&device,reused,&alloc);
    check(!device.queue_signalled.count(reused)&&device.queue_signalled.count(other)&&forgotten,
          "destruction forgets the object before forwarding to the driver");
    fail_create=false;
    check(create_application_semaphore(&device,&info,&alloc,&output)==VK_SUCCESS&&!device.queue_signalled.count(output),
          "destroyed handle reused by the next object inherits no queue producer");
    device.queue_signalled.insert(output);
    check(device.queue_signalled.count(output)==1,"the new object can acquire its own later queue signal history");
    destroy_application_semaphore(&device,VK_NULL_HANDLE,&alloc);
    check(device.queue_signalled.count(output)&&device.queue_signalled.count(other)&&destroys==2,
          "null destruction is forwarded without deleting live objects");
    check(arguments&&creates==3,"semaphore calls preserve the exact device, create chain and allocator");
    check(unlocked,"semaphore bookkeeping never holds its lock across driver calls");
    active=nullptr;
}
