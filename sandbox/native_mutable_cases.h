#include "../layer/src/texture_residency.h"
#include "../layer/src/native_descriptors.h"

namespace mutable_residency_fixture {
template<class Tag,auto Member> struct Expose {friend auto member(Tag){return Member;}};
#define EXPOSE(Tag,Type,Field) struct Tag {friend auto member(Tag);};template struct Expose<Tag,&Type::Field>;
EXPOSE(Images,TextureResidency,images_)
EXPOSE(Cache,TextureResidency,cache_)
EXPOSE(Usable,TextureResidency,usable_)
EXPOSE(Bytes,TextureResidency,resident_bytes_)
EXPOSE(Cap,TextureResidency,cap_bytes_)
EXPOSE(Frame,TextureResidency,frame_)
EXPOSE(Idle,TextureResidency,evict_idle)
EXPOSE(Pressure,TextureResidency,evict_for)
EXPOSE(Demote,TextureResidency,demote_one)
EXPOSE(Value,efmi::assets::ResourceRequest,value_)
#undef EXPOSE
}
static void native_mutable_cases()
{
    using namespace mutable_residency_fixture;using namespace efmi::assets;
    Device device;VkPhysicalDeviceMemoryProperties memory{};TextureResidency residency(&device,memory);
    ResourceDevice fake;fake.device=h<VkDevice>(1);fake.physical=h<VkPhysicalDevice>(2);
    fake.format_properties=[](VkPhysicalDevice,VkFormat,VkFormatProperties*){};
    fake.image_properties=[](VkPhysicalDevice,VkFormat,VkImageType,VkImageTiling,VkImageUsageFlags,VkImageCreateFlags,VkImageFormatProperties*){return VK_ERROR_FORMAT_NOT_SUPPORTED;};
    auto& cache=residency.*member(Cache());cache=std::make_unique<ResourceCache>(fake);
    std::string why;auto request=cache->declare_buffer(L"F:/efmi vulkan/build/unused-pinned.buf",VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,why,0,true);
    request.get()->*member(Value())=ResourceSnapshot{ResourceState::Ready};
    auto& images=residency.*member(Images());images.emplace_back();auto& allocation=images.back();
    allocation.handle=request;allocation.resident=allocation.writable=true;allocation.declared=1;allocation.charged=16;
    allocation.buffer_usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    residency.*member(Usable())=true;residency.*member(Bytes())=16;residency.*member(Cap())=0;residency.*member(Frame())=1000;
    (residency.*member(Idle()))(0,1,0);
    check(allocation.resident && request->snapshot().state==ResourceState::Ready,"idle eviction cannot discard writable GPU contents");
    (residency.*member(Pressure()))(1);
    check(allocation.resident && residency.*member(Bytes())==16,"allocation pressure keeps writable residency charged instead of rereading its file");
    allocation.writable=false;(residency.*member(Idle()))(0,1,0);
    check(!allocation.resident && ! (residency.*member(Bytes())) && request->snapshot().state==ResourceState::Dormant,
        "the same unused immutable allocation remains evictable");
    request.get()->*member(Value())=ResourceSnapshot{ResourceState::Ready};
    allocation.resident=allocation.writable=true;allocation.charged=16;residency.*member(Bytes())=16;
    residency.forget({0});
    check(!allocation.resident && !allocation.declared && !(residency.*member(Bytes())),"last declaration releases pinned mutable residency");
    const std::string image_path="F:/efmi vulkan/build/unused-pinned.dds";
    const auto a=residency.declare(image_path,why,0,0,true,true),b=residency.declare(image_path,why,0,0,true,true);
    check(a!=TextureResidency::kNone && b!=TextureResidency::kNone && a!=b,"writable image declarations receive independent residency identities");
    if(a==TextureResidency::kNone || b==TextureResidency::kNone)return;
    auto& image=images[a];image.handle.get()->*member(Value())=ResourceSnapshot{ResourceState::Ready};
    image.resident=true;image.charged=image.file_bytes=4ull<<20;residency.*member(Bytes())=image.charged;
    const auto saved_reduce=g_cfg.m2_texture_reduce_mb;g_cfg.m2_texture_reduce_mb=1;
    (residency.*member(Idle()))(0,1,0);(residency.*member(Pressure()))(1);
    check(image.resident && !image.skip_levels,"idle and pressure eviction preserve writable image contents");
    check(!(residency.*member(Demote()))() && image.resident && !image.reduce_next,"mip demotion cannot reconstruct a writable image from its initial file");
    g_cfg.m2_texture_reduce_mb=saved_reduce;
    residency.forget({a,b});
    check(!image.resident && !image.declared && !(residency.*member(Bytes())),"final image declarations release pinned residency charges");
}

namespace mutable_dependency_fixture {
static unsigned barriers=0;
static VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags source,VkPipelineStageFlags destination,VkDependencyFlags,
    uint32_t count,const VkMemoryBarrier* memory,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*)
{
    if(source==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT && destination==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT && count==1 &&
       (memory->srcAccessMask&VK_ACCESS_MEMORY_WRITE_BIT) && (memory->dstAccessMask&VK_ACCESS_MEMORY_READ_BIT) &&
       (memory->dstAccessMask&VK_ACCESS_MEMORY_WRITE_BIT))++barriers;
}
}
static void native_mutable_dependencies()
{
    Device d;d.application_queue_count=1;d.physical_properties.limits.maxStorageBufferRange=1024;
    d.d.CmdPipelineBarrier=mutable_dependency_fixture::barrier;
    d.d.CreatePipelineLayout=[](VkDevice,const VkPipelineLayoutCreateInfo*,const VkAllocationCallbacks*,VkPipelineLayout* out){*out=h<VkPipelineLayout>(73);return VK_SUCCESS;};
    d.d.DestroyPipelineLayout=[](VkDevice,VkPipelineLayout,const VkAllocationCallbacks*){};
    d.d.CmdPushDescriptorSetKHR=[](VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,uint32_t,uint32_t,const VkWriteDescriptorSet*){};
    auto layout=std::make_shared<DescriptorLayoutInfo>();layout->flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    layout->bindings[2]={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT};
    layout->lifetime=std::make_shared<SetLayoutLifetime>();layout->lifetime->handle=h<VkDescriptorSetLayout>(74);
    auto pipeline=std::make_shared<PipelineLayoutInfo>();pipeline->sets.push_back(layout);pipeline->prefix.push_back(75);
    auto contents=std::make_shared<DescriptorSetInfo>();contents->layout=layout;contents->counts[2]=1;
    DrawRec draw;draw.kind=DK_DISPATCH;draw.pipeline=h<VkPipeline>(76);draw.sets[0].layout=pipeline;draw.sets[0].pushed=contents;
    d.pipes[draw.pipeline].layout=pipeline;
    auto resource=std::make_shared<efmi::frontend::NativeResource>();resource->bound=resource->uploaded_mutable=true;
    resource->buffer=77;resource->size=resource->range=16;resource->buffer_stride=4;resource->usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;resource->keep=std::make_shared<int>(1);
    efmi::assets::FilePlan plan;plan.items.emplace_back();auto& item=plan.items.back();item.operation=efmi::assets::FileOperation::NativeBind;
    item.native_target.kind=efmi::frontend::NativeBinding::Access;item.native_target.stage=5;item.native_target.binding=2;item.native_resource=resource;
    CmdBuf cb;NativeDescriptorPlan descriptors;std::string why;
    const bool prepared=prepare_native_descriptors(&d,&cb,draw,plan,descriptors,why);
    check(prepared,"mutable dependency fixture prepares a retained native descriptor");
    if(!prepared)return;
    record_native_descriptor(&d,&cb,draw,descriptors,0);
    mutable_dependency_fixture::barriers=0;native_images_call(&d,&cb,true);native_images_end(&d,&cb);
    check(mutable_dependency_fixture::barriers==2,"persistent mutable buffers receive dependencies around an application dispatch without a native plan");
    const auto child=h<VkCommandBuffer>(78);d.cbs[child]=std::make_unique<CmdBuf>();d.cbs[child]->native_mutable=true;
    CmdBuf parent;on_execute_commands(&d,&parent,1,&child);
    check(parent.native_mutable,"secondary execution carries mutable file dependency state into the primary");
    parent.reset_recording();cb.reset_recording();
    check(!parent.native_mutable && !cb.native_mutable,"completed recording reset clears mutable file dependency state");
}
