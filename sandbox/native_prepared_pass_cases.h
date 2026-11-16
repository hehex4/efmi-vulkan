#include "../frontend/gpu/resource_commands.h"
#include <cstdlib>
static void VKAPI_CALL work_unexpected_device_call(){std::abort();}
static void VKAPI_CALL work_fill(VkCommandBuffer,VkBuffer,VkDeviceSize,VkDeviceSize,uint32_t){output_commands.push_back('W');}
static VkResult VKAPI_CALL work_create_pass(VkDevice,const VkRenderPassCreateInfo* info,const VkAllocationCallbacks*,VkRenderPass* handle){
    for(uint32_t i=0;i<info->attachmentCount;++i){const auto& a=info->pAttachments[i];
        check(a.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD,"work-only classic continuation loads its stored attachment");
        check(a.initialLayout==a.finalLayout,"work-only private pass keeps its attachment layout");
    }
    *handle=h<VkRenderPass>(30100);return VK_SUCCESS;
}
static VkResult VKAPI_CALL work_create_framebuffer(VkDevice,const VkFramebufferCreateInfo* info,const VkAllocationCallbacks*,VkFramebuffer* handle){
    check(info->width==64 && info->height==32 && info->layers==1,"work-only private framebuffer preserves the original extent");
    *handle=h<VkFramebuffer>(30101);return VK_SUCCESS;
}
static void VKAPI_CALL work_begin_pass(VkCommandBuffer,const VkRenderPassBeginInfo* info,VkSubpassContents contents){
    check(info->clearValueCount==0 && contents==VK_SUBPASS_CONTENTS_INLINE,"work-only classic continuation does not clear again");output_commands.push_back('B');
}
static void native_prepared_pass_cases(bool classic,unsigned mode){
    using O=efmi::assets::FileOperation;using namespace efmi::gpu;
    Device d;CmdBuf cb;DrawRec draw;NativeTargetCache cache;NativeTargetPlan prepared;std::string why;
    d.handle=h<VkDevice>(30001);d.application_queue_count=1;d.material_queues.push_back(h<VkQueue>(30002));
    d.physical_properties.limits.bufferImageGranularity=256;cb.handle=h<VkCommandBuffer>(30003);cb.in_rp=true;
    d.d.CmdPipelineBarrier=output_barrier;d.d.CmdBeginRendering=output_begin;d.d.CmdEndRendering=output_end;
    d.d.CreateRenderPass=work_create_pass;d.d.DestroyRenderPass=classic_destroy;
    d.d.CreateFramebuffer=work_create_framebuffer;d.d.DestroyFramebuffer=classic_destroy_fb;
    d.d.CmdBeginRenderPass=work_begin_pass;d.d.CmdEndRenderPass=output_end;
    ResourceDevice resource;resource.device=d.handle;
#define X(name) resource.dispatch.name=reinterpret_cast<PFN_vk##name>(work_unexpected_device_call);
    EFMI_RESOURCE_COMMAND_FUNCS(X)
#undef X
    resource.dispatch.CmdPipelineBarrier=output_barrier;resource.dispatch.CmdFillBuffer=work_fill;
    auto commands=std::make_unique<ResourceCommands>(resource);
    auto owner=std::make_shared<int>(7);std::weak_ptr<int> lifetime=owner;
    commands->begin_prepared();auto buffer=commands->import_buffer(h<VkBuffer>(30004),16,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        h<VkDeviceMemory>(30005),0,256,owner);owner.reset();
    commands->fill(buffer,0,4,7);auto a=commands->cut_prepared();
    commands->fill(buffer,0,4,13);auto b=commands->finish_prepared();
    efmi::assets::FilePlan plan;plan.items.resize(2);plan.items[0].operation=plan.items[1].operation=O::PreparedWork;
    plan.items[0].prepared_work=a;plan.items[1].prepared_work=b;
    const bool attached=mode!=2,depth=mode==1 || mode==3,stencil=mode==3;
    const auto format=stencil?VK_FORMAT_D32_SFLOAT_S8_UINT:depth?VK_FORMAT_D32_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM;
    TargetView view;view.known=true;view.handle=h<VkImageView>(30006);view.view.id=30006;view.view.image=h<VkImage>(30007);
    view.view.image_id=view.image.id=30007;view.view.type=VK_IMAGE_VIEW_TYPE_2D;view.view.format=view.image.format=format;
    view.view.range={depth?VK_IMAGE_ASPECT_DEPTH_BIT|(stencil?VK_IMAGE_ASPECT_STENCIL_BIT:0u):VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    view.image.type=VK_IMAGE_TYPE_2D;view.image.extent={64,32,1};view.image.levels=view.image.layers=1;view.image.samples=VK_SAMPLE_COUNT_1_BIT;
    view.image.capture_safe=true;view.image.mem=h<VkDeviceMemory>(30008);view.image.mem_size=8192;
    d.image_views[view.handle]=view.view;d.image_views[view.handle].lifetime=std::make_shared<ImageViewLifetime>();d.images[view.view.image]=view.image;
    RenderTargets targets;targets.known=true;targets.layers=1;targets.area.extent={64,32};targets.dynamic=!classic;
    auto recipe=std::make_shared<GraphicsRecipe>();recipe->geometry_complete=true;recipe->multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    recipe->dynamic_rendering=!classic;draw.pipeline=h<VkPipeline>(30009);d.pipes[draw.pipeline].recipe=recipe;
    if(classic){
        auto pass=std::make_shared<RenderPassRecipe>();pass->complete=true;pass->subpasses.resize(1);
        if(attached){VkAttachmentDescription2 desc{VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};desc.format=format;desc.samples=VK_SAMPLE_COUNT_1_BIT;
            desc.loadOp=desc.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;desc.storeOp=desc.stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;
            desc.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;pass->attachments.push_back(desc);
            VkAttachmentReference2 ref{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};ref.attachment=0;
            ref.layout=depth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            if(depth){pass->subpasses[0].has_depth=true;pass->subpasses[0].depth=ref;}else pass->subpasses[0].colors.push_back(ref);
        }
        auto framebuffer=std::make_shared<FramebufferInfo>();framebuffer->known=true;framebuffer->layers=1;framebuffer->width=64;framebuffer->height=32;
        if(attached)framebuffer->views.push_back(view);framebuffer->attachment_count=uint32_t(framebuffer->views.size());
        targets.recipe=recipe->render_pass=cb.pass.recipe=pass;targets.framebuffer=cb.pass.info=framebuffer;
    }else if(attached){
        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};attachment.imageView=view.handle;
        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachment.imageLayout=depth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        targets.views={view,{}};
        if(depth){targets.has_depth=true;targets.depth=attachment;recipe->rendering_depth=format;
            if(stencil){targets.has_stencil=true;targets.stencil=attachment;targets.views.push_back(view);targets.views.push_back({});recipe->rendering_stencil=format;}}
        else {targets.dynamic_colors.push_back(attachment);recipe->rendering_colors.push_back(format);}
    }
    cb.target_contexts={targets};cb.active_targets=draw.targets=1;
    auto prepare=[&]{prepared={};return prepare_native_targets(&d,&cb,draw,plan,prepared,why,&cache);};
    output_commands.clear();check(prepare() && output_commands.empty(),"work-only pass preflight records no command");
    check(prepared.events.size()==2 && prepared.events[0] && prepared.events[1],"each prepared buffer segment owns a pass boundary");
    bool refused=false;try{record_native_target(&d,&cb,prepared,0);}catch(const std::exception&){refused=true;}
    check(refused && output_commands.empty() && !cb.native_target_state,"unadmitted work cannot end or alter the current pass");
    std::swap(plan.items[0],plan.items[1]);check(!prepare(),"out-of-order work segments refuse before recording");std::swap(plan.items[0],plan.items[1]);
    plan.items[1].prepared_work=a;check(!prepare(),"duplicate work segment refuses");plan.items[1].prepared_work=b;
    d.material_family=1;check(!prepare(),"work from another queue family refuses");d.material_family=0;
    cb.secondary=true;check(!prepare(),"work splitting refuses secondary command buffers");cb.secondary=false;
    cb.execution_scopes.query(9,0,0,true);check(!prepare(),"work splitting preserves active queries");cb.execution_scopes.query(9,0,0,false);
    if(attached){
        auto aliased=targets;
        if(classic){auto framebuffer=std::make_shared<FramebufferInfo>(*targets.framebuffer);framebuffer->views[0].image.mem=buffer->memory();aliased.framebuffer=framebuffer;}
        else {aliased.views[0].image.mem=buffer->memory();if(stencil)aliased.views[2].image.mem=buffer->memory();}
        cb.target_contexts[0]=aliased;
        check(!prepare() && why.find("memory")!=std::string::npos,"buffer work cannot alias retained color/depth attachment storage");
        cb.target_contexts[0]=targets;
        auto incomplete=targets;
        if(classic){auto framebuffer=std::make_shared<FramebufferInfo>(*targets.framebuffer);framebuffer->views[0].image.mem_size=0;incomplete.framebuffer=framebuffer;}
        else {incomplete.views[0].image.mem_size=0;if(stencil)incomplete.views[2].image.mem_size=0;}
        cb.target_contexts[0]=incomplete;
        check(prepare(),"distinct allocation identities remain disjoint without an attachment size");
        if(classic){auto framebuffer=std::make_shared<FramebufferInfo>(*incomplete.framebuffer);framebuffer->views[0].image.mem=buffer->memory();incomplete.framebuffer=framebuffer;}
        else {incomplete.views[0].image.mem=buffer->memory();if(stencil)incomplete.views[2].image.mem=buffer->memory();}
        cb.target_contexts[0]=incomplete;
        check(!prepare(),"unknown extent in the same allocation conservatively overlaps work");
        cb.target_contexts[0]=targets;
    }
    check(prepare(),"valid work recovers after failed pass preflight");commands->accept_prepared();
    output_commands.clear();record_native_target(&d,&cb,prepared,0);record_native_target(&d,&cb,prepared,1);
    const std::vector<char> expected={'E','D','D','W','D','B','E','D','D','W','D','B'};
    check(output_commands==expected,"both work segments run strictly between End and LOAD Begin with explicit dependencies");
    if(!classic && attached)check(depth?output_depth_load==VK_ATTACHMENT_LOAD_OP_LOAD:output_load==VK_ATTACHMENT_LOAD_OP_LOAD,
        "dynamic work-only continuation preserves attachment contents");
    if(!classic && stencil)check(output_stencil_load==VK_ATTACHMENT_LOAD_OP_LOAD,"dynamic work-only continuation preserves stencil contents");
    check(prepared.owner->used && prepared.owner->seal(),"recorded work seals with its command-buffer owner");
    commands.reset();buffer.reset();a.reset();b.reset();plan={};prepared={};
    check(!lifetime.expired(),"executable recording retains work allocations after preparation retires");
    for(const auto& retained:cb.owners)retained->reset_after_completion();
    check(lifetime.expired(),"legal recording reset releases its work allocations");
}
