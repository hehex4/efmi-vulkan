// Submit2 private-resource support must not manufacture cross-stage dependencies.
#include "../layer/src/native_descriptors.h"
namespace submit_scope_fixture {
static unsigned scoped = 0, broad = 0;
static VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer, VkPipelineStageFlags src, VkPipelineStageFlags dst, VkDependencyFlags,
    uint32_t count, const VkMemoryBarrier* memory, uint32_t buffers, const VkBufferMemoryBarrier*, uint32_t images, const VkImageMemoryBarrier*)
{
    if (src == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT && dst == src && count == 1 && !buffers && !images &&
        memory->srcAccessMask == VK_ACCESS_SHADER_WRITE_BIT && memory->dstAccessMask == (VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)) ++scoped;
    else ++broad;
}
}
static void native_submit_scope_cases()
{
    using namespace submit_scope_fixture;
    Device d; d.synchronization2_enabled = true; d.application_queue_count = 1; d.physical_properties.limits.maxStorageBufferRange = 1024;
    d.d.CmdPipelineBarrier = barrier; d.native_barrier = barrier;
    d.d.CreatePipelineLayout=[](VkDevice,const VkPipelineLayoutCreateInfo*,const VkAllocationCallbacks*,VkPipelineLayout* out){*out=h<VkPipelineLayout>(9301);return VK_SUCCESS;};
    d.d.DestroyPipelineLayout=[](VkDevice,VkPipelineLayout,const VkAllocationCallbacks*){};
    d.d.CmdPushDescriptorSetKHR=[](VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,uint32_t,uint32_t,const VkWriteDescriptorSet*){};
    auto layout=std::make_shared<DescriptorLayoutInfo>();layout->flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    layout->bindings[2]={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT};
    layout->lifetime=std::make_shared<SetLayoutLifetime>();layout->lifetime->handle=h<VkDescriptorSetLayout>(9302);
    auto pipeline=std::make_shared<PipelineLayoutInfo>();pipeline->sets.push_back(layout);pipeline->prefix.push_back(9303);
    auto contents=std::make_shared<DescriptorSetInfo>();contents->layout=layout;contents->counts[2]=1;
    DrawRec draw;draw.kind=DK_DISPATCH;draw.pipeline=h<VkPipeline>(9304);draw.sets[0].layout=pipeline;draw.sets[0].pushed=contents;
    d.pipes[draw.pipeline].layout=pipeline;
    auto resource=std::make_shared<efmi::frontend::NativeResource>();resource->bound=resource->uploaded_mutable=true;
    resource->buffer=9305;resource->size=resource->range=16;resource->buffer_stride=4;resource->usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;resource->keep=std::make_shared<int>(1);
    efmi::assets::FilePlan plan;plan.items.emplace_back();auto& item=plan.items.back();item.operation=efmi::assets::FileOperation::NativeBind;
    item.native_target.kind=efmi::frontend::NativeBinding::Access;item.native_target.stage=5;item.native_target.binding=2;item.native_resource=resource;
    CmdBuf cb;NativeDescriptorPlan prepared;std::string why;
    check(prepare_native_descriptors(&d,&cb,draw,plan,prepared,why), "Submit2 direct compute accepts retained private writable files");
    draw.kind=DK_DISPATCH_INDIRECT;
    check(!prepare_native_descriptors(&d,&cb,draw,plan,prepared,why), "Submit2 private binding refuses the separate indirect argument domain");
    draw.kind=DK_DISPATCH;resource->uploaded_mutable=false;
    check(!prepare_native_descriptors(&d,&cb,draw,plan,prepared,why), "Submit2 new application references require their own stage proof");
    resource->uploaded_readonly=true;
    check(prepare_native_descriptors(&d,&cb,draw,plan,prepared,why), "Submit2 completed read-only publications retain support");
    cb.native_mutable=true;scoped=broad=0;
    native_images_call(&d,&cb,true);native_images_end(&d,&cb);
    check(scoped==2 && !broad, "persistent private files add only compute-to-compute dependencies");
    d.native_binding_order=true;scoped=broad=0;
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};native_images_begin(&d,&cb,&begin);
    check(!scoped && !broad && !cb.native_graphics_ordered, "Submit2 graphics begin gains no private-compute dependency or application proof");
    d.synchronization2_enabled=false;scoped=broad=0;
    native_images_call(&d,&cb,true);native_images_end(&d,&cb);
    check(broad==2 && !scoped, "legacy adapter retains its existing dependency contract");
}
