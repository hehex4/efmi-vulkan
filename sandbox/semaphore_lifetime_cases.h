// Application semaphore identities must not survive handle destruction/reuse.
#include "../layer/src/semaphore_lifetime.h"
namespace semaphore_lifetime_fixture {
static Device* active=nullptr;
static const VkSemaphoreCreateInfo* expected_info=nullptr;
static const VkAllocationCallbacks* expected_alloc=nullptr;
static VkSemaphore reused{};
static bool fail_create=false,arguments=true,unlocked=true,forgotten=true;
static unsigned creates=0,destroys=0;
static bool known(VkSemaphore semaphore){return active->queue_signals->read().contains(semaphore);}
static void record(VkSemaphore semaphore){QueueSignalHistory::Attempt attempt(*active->queue_signals);attempt.signal(semaphore);attempt.finish(VK_SUCCESS);}
static VKAPI_ATTR VkResult VKAPI_CALL create(VkDevice device,const VkSemaphoreCreateInfo* info,
    const VkAllocationCallbacks* alloc,VkSemaphore* out)
{
    ++creates;arguments=arguments&&device==active->handle&&info==expected_info&&alloc==expected_alloc;
    const auto view=active->queue_signals->try_read();unlocked=unlocked&&view.owns_lock();
    if(fail_create)return VK_ERROR_OUT_OF_HOST_MEMORY;
    *out=reused;return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL destroy(VkDevice device,VkSemaphore semaphore,const VkAllocationCallbacks* alloc)
{
    ++destroys;arguments=arguments&&device==active->handle&&alloc==expected_alloc;
    const auto view=active->queue_signals->try_read();unlocked=unlocked&&view.owns_lock();
    if(view.owns_lock())forgotten=forgotten&&!view.contains(semaphore);
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
    device.queue_signals=std::make_shared<QueueSignalHistory>();
    const auto other=h<VkSemaphore>(9703);VkSemaphore output=h<VkSemaphore>(9704);
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};type.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;type.initialValue=7;
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};info.pNext=&type;
    VkAllocationCallbacks alloc{};expected_info=&info;expected_alloc=&alloc;
    device.d.CreateSemaphore=create;device.d.DestroySemaphore=destroy;
    creates=destroys=0;arguments=unlocked=forgotten=true;fail_create=false;
    record(reused);record(other);
    check(create_application_semaphore(&device,&info,&alloc,&output)==VK_SUCCESS&&output==reused&&
          !known(reused)&&known(other),
          "new semaphore creation clears recycled history and preserves unrelated signals");
    record(reused);fail_create=true;
    check(create_application_semaphore(&device,&info,&alloc,&output)==VK_ERROR_OUT_OF_HOST_MEMORY&&
          known(reused)&&known(other),
          "failed creation preserves existing signal history and the driver result");
    destroy_application_semaphore(&device,reused,&alloc);
    check(!known(reused)&&known(other)&&forgotten,
          "destruction forgets the object before forwarding to the driver");
    fail_create=false;
    check(create_application_semaphore(&device,&info,&alloc,&output)==VK_SUCCESS&&!known(output),
          "destroyed handle reused by the next object inherits no queue producer");
    record(output);
    check(known(output),"the new object can acquire its own later queue signal history");
    destroy_application_semaphore(&device,VK_NULL_HANDLE,&alloc);
    check(known(output)&&known(other)&&destroys==2,
          "null destruction is forwarded without deleting live objects");
    check(arguments&&creates==3,"semaphore calls preserve the exact device, create chain and allocator");
    check(unlocked,"semaphore bookkeeping never holds its lock across driver calls");
    active=nullptr;
}
