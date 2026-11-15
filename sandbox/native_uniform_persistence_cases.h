#include "../layer/src/native_descriptors.h"
namespace persistent_uniform_fixture {
static VkBuffer actual=VK_NULL_HANDLE;
static unsigned binds=0;
static uint64_t offset=0,range=0;
static std::map<VkDescriptorSet,VkDescriptorBufferInfo> values;
static void VKAPI_CALL update(VkDevice,uint32_t count,const VkWriteDescriptorSet* writes,uint32_t,const VkCopyDescriptorSet*){
    for(uint32_t i=0;i<count;++i)if(writes[i].pBufferInfo)values[writes[i].dstSet]=writes[i].pBufferInfo[0];
}
static void VKAPI_CALL bind(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,uint32_t,uint32_t count,const VkDescriptorSet* sets,uint32_t dynamic_count,const uint32_t* dynamic){
    for(uint32_t i=0;i<count;++i){const auto& value=values[sets[i]];actual=value.buffer;
        offset=value.offset+(dynamic_count?dynamic[0]:0);range=value.range;++binds;}
}
static void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,
    uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*){}
}
static void native_uniform_persistence_cases()
{
    using Resource=efmi::frontend::NativeResource;
    Device d;d.application_queue_count=1;d.physical_properties.limits.maxUniformBufferRange=1024;
    d.physical_properties.limits.bufferImageGranularity=256;d.track_image_layouts=true;d.native_binding_order=true;
    d.native_barrier=d.d.CmdPipelineBarrier=persistent_uniform_fixture::barrier;
    d.d.CreatePipelineLayout=[](VkDevice,const VkPipelineLayoutCreateInfo*,const VkAllocationCallbacks*,VkPipelineLayout* out){*out=h<VkPipelineLayout>(14001);return VK_SUCCESS;};
    d.d.DestroyPipelineLayout=[](VkDevice,VkPipelineLayout,const VkAllocationCallbacks*){};
    d.d.CreateDescriptorPool=[](VkDevice,const VkDescriptorPoolCreateInfo*,const VkAllocationCallbacks*,VkDescriptorPool* out){*out=h<VkDescriptorPool>(14100);return VK_SUCCESS;};
    d.d.DestroyDescriptorPool=[](VkDevice,VkDescriptorPool,const VkAllocationCallbacks*){};
    d.d.AllocateDescriptorSets=[](VkDevice,const VkDescriptorSetAllocateInfo* info,VkDescriptorSet* out){static uint64_t next=14200;for(uint32_t i=0;i<info->descriptorSetCount;++i)out[i]=h<VkDescriptorSet>(next++);return VK_SUCCESS;};
    d.d.UpdateDescriptorSets=persistent_uniform_fixture::update;d.d.CmdBindDescriptorSets=persistent_uniform_fixture::bind;
    auto layout=std::make_shared<DescriptorLayoutInfo>();
    layout->bindings[0]={VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_VERTEX_BIT};
    layout->lifetime=std::make_shared<SetLayoutLifetime>();layout->lifetime->handle=h<VkDescriptorSetLayout>(14002);
    auto pipeline=std::make_shared<PipelineLayoutInfo>();pipeline->sets.push_back(layout);pipeline->prefix.push_back(14003);
    const auto original=h<VkBuffer>(14004);BufInfo buffer;buffer.id=14005;buffer.size=buffer.mem_size=256;
    buffer.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;buffer.mem=h<VkDeviceMemory>(14006);
    buffer.lifetime=std::make_shared<BufferLifetime>();buffer.lifetime->dependencies.push_back(std::make_shared<MemoryLifetime>());d.bufs[original]=buffer;
    auto contents=std::make_shared<DescriptorSetInfo>();contents->layout=layout;contents->counts[0]=1;
    DescriptorElement value;value.known=true;value.type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;value.buffer=original;value.buffer_id=buffer.id;value.offset=16;value.range=32;
    contents->values[{0,0}]=value;
    DrawRec draw;draw.kind=DK_DRAW;draw.pipeline=h<VkPipeline>(14007);draw.sets[0].layout=pipeline;
    draw.sets[0].set=h<VkDescriptorSet>(14015);draw.sets[0].id=contents->id=14016;
    d.descriptor_sets[draw.sets[0].set]=*contents;
    draw.sets[0].dyn_count=1;
    d.pipes[draw.pipeline].layout=pipeline;
    auto resource=std::make_shared<Resource>();resource->bound=true;resource->buffer=14008;resource->identity=uint64_t(14009)<<2;
    resource->size=256;resource->offset=32;resource->range=16;resource->usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;resource->keep=std::make_shared<int>(1);
    resource->memory=14010;resource->memory_size=256;
    efmi::assets::FilePlan plan;plan.items.emplace_back();auto& item=plan.items.back();item.operation=efmi::assets::FileOperation::NativeBind;
    item.native_target.kind=efmi::frontend::NativeBinding::Constant;item.native_target.stage=0;item.native_resource=resource;
    CmdBuf cb;cb.in_rp=cb.native_graphics_ordered=true;cb.gfx_pipeline=draw.pipeline;cb.active_targets=1;
    cb.dyn={64};
    RenderTargets targets;targets.known=targets.dynamic=true;targets.layers=1;cb.target_contexts.push_back(targets);
    NativeDescriptorPlan descriptors;std::string why;persistent_uniform_fixture::actual=VK_NULL_HANDLE;persistent_uniform_fixture::binds=0;persistent_uniform_fixture::values.clear();
    const bool prepared=prepare_native_descriptors(&d,&cb,draw,plan,descriptors,why);
    check(prepared,"independent application uniform prepares before a later attachment conflict");if(!prepared)return;
    record_native_descriptor(&d,&cb,draw,descriptors,0);
    check(persistent_uniform_fixture::actual==VkBuffer(resource->buffer) && persistent_uniform_fixture::offset==32,
          "initial retained uniform and resolved range actually reach the driver binding");
    native_images_end(&d,&cb);
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};color.imageView=h<VkImageView>(14011);
    targets.dynamic_colors.push_back(color);targets.views.resize(2);auto& attachment=targets.views[0];
    attachment.known=true;attachment.handle=color.imageView;attachment.view.image=h<VkImage>(14012);attachment.image.id=14013;
    attachment.image.mem=VkDeviceMemory(resource->memory);attachment.image.mem_size=1024;cb.target_contexts[0]=targets;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};native_images_begin(&d,&cb,&rendering);native_images_call(&d,&cb,false);
    check(persistent_uniform_fixture::actual==original && persistent_uniform_fixture::offset==80 && persistent_uniform_fixture::range==32 && !cb.native_image_blocked,
          "a later conflicting pass restores the application uniform with its original dynamic offset folded once");
    native_images_end(&d,&cb);cb.target_contexts[0].views[0].image.mem=h<VkDeviceMemory>(14014);
    native_images_begin(&d,&cb,&rendering);native_images_call(&d,&cb,false);
    check(persistent_uniform_fixture::actual==VkBuffer(resource->buffer) && persistent_uniform_fixture::offset==32 && persistent_uniform_fixture::range==16 &&
          persistent_uniform_fixture::binds>=3 && !cb.native_image_blocked,
          "a later independent pass restores the same retained request without another native list");
}
