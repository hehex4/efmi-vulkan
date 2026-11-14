#include "../layer/src/native_descriptors.h"
namespace uniform_dependency_fixture {
static unsigned barriers=0;
static VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags source,VkPipelineStageFlags destination,VkDependencyFlags,
    uint32_t count,const VkMemoryBarrier* memory,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*)
{
    if(source==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT && destination==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT && count==1 &&
       (memory->srcAccessMask&VK_ACCESS_MEMORY_WRITE_BIT) && (memory->dstAccessMask&VK_ACCESS_MEMORY_READ_BIT))++barriers;
}
}
static void native_uniform_prepass_cases()
{
    Device d;d.application_queue_count=1;d.physical_properties.limits.maxUniformBufferRange=1024;
    d.d.CreatePipelineLayout=[](VkDevice,const VkPipelineLayoutCreateInfo*,const VkAllocationCallbacks*,VkPipelineLayout* out){*out=h<VkPipelineLayout>(83);return VK_SUCCESS;};
    d.d.DestroyPipelineLayout=[](VkDevice,VkPipelineLayout,const VkAllocationCallbacks*){};
    d.d.CmdPushDescriptorSetKHR=[](VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,uint32_t,uint32_t,const VkWriteDescriptorSet*){};
    auto layout=std::make_shared<DescriptorLayoutInfo>();layout->flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    layout->bindings[0]={VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT};
    layout->lifetime=std::make_shared<SetLayoutLifetime>();layout->lifetime->handle=h<VkDescriptorSetLayout>(84);
    auto pipeline=std::make_shared<PipelineLayoutInfo>();pipeline->sets.push_back(layout);pipeline->prefix.push_back(85);
    auto contents=std::make_shared<DescriptorSetInfo>();contents->layout=layout;contents->counts[0]=1;
    DrawRec draw;draw.kind=DK_DRAW;draw.pipeline=h<VkPipeline>(86);draw.sets[0].layout=pipeline;draw.sets[0].pushed=contents;
    d.pipes[draw.pipeline].layout=pipeline;
    auto resource=std::make_shared<efmi::frontend::NativeResource>();resource->bound=true;
    resource->buffer=87;resource->size=resource->range=16;resource->usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;resource->keep=std::make_shared<int>(1);
    efmi::assets::FilePlan plan;plan.items.emplace_back();auto& item=plan.items.back();item.operation=efmi::assets::FileOperation::NativeBind;
    item.native_target.kind=efmi::frontend::NativeBinding::Constant;item.native_target.stage=0;item.native_resource=resource;
    CmdBuf cb;cb.in_rp=true;NativeDescriptorPlan descriptors;std::string why;
    check(!prepare_native_descriptors(&d,&cb,draw,plan,descriptors,why),
        "a graphics application-buffer reference requires an actual dependency before this pass began");
    d.native_barrier=uniform_dependency_fixture::barrier;d.native_binding_order=true;
    VkRenderPassBeginInfo classic{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    native_images_begin(&d,&cb,&classic);
    check(cb.native_graphics_ordered && uniform_dependency_fixture::barriers==1,"classic pass begin records and proves the native resource dependency");
    const bool prepared=prepare_native_descriptors(&d,&cb,draw,plan,descriptors,why);
    check(prepared,"a proven primary pass accepts the retained application uniform reference");
    if(prepared)record_native_descriptor(&d,&cb,draw,descriptors,0);
    check(cb.native_uniforms,"accepted graphics references retain their later-pass ordering requirement");
    native_images_end(&d,&cb);
    check(!cb.native_graphics_ordered && cb.native_uniforms,"pass end clears only the current dependency proof");
    d.native_binding_order=false;VkRenderingInfo dynamic{VK_STRUCTURE_TYPE_RENDERING_INFO};
    native_images_begin(&d,&cb,&dynamic);
    check(cb.native_graphics_ordered && uniform_dependency_fixture::barriers==2,"persistent references order the next dynamic pass after aliases are disabled");
    cb.inherited_rp=true;
    check(!prepare_native_descriptors(&d,&cb,draw,plan,descriptors,why),"an inherited secondary cannot borrow another recording's dependency proof");
    cb.reset_recording();
    check(!cb.native_graphics_ordered && !cb.native_uniforms,"recording reset clears proof and persistent uniform requirements");
    const auto child=h<VkCommandBuffer>(88);d.cbs[child]=std::make_unique<CmdBuf>();d.cbs[child]->native_uniforms=d.cbs[child]->native_graphics_ordered=true;
    CmdBuf parent;on_execute_commands(&d,&parent,1,&child);
    check(parent.native_uniforms && !parent.native_graphics_ordered,"secondary composition carries requirements but cannot manufacture a primary pass proof");
    d.native_barrier=nullptr;d.native_binding_order=true;native_images_begin(&d,&cb,&classic);
    check(!cb.native_graphics_ordered,"an unavailable barrier entry point cannot claim a dependency");
    resource->uploaded_readonly=true;cb.inherited_rp=true;
    check(prepare_native_descriptors(&d,&cb,draw,plan,descriptors,why),"published immutable file constants keep their secondary-safe publication exemption");
}
