#include "variant_fixture.h"
#include "../layer/src/native_execution.h"
static void native_classic_null_cases(unsigned hunting_case=0){
    using Resource=efmi::frontend::NativeResource;using Binding=efmi::frontend::NativeBinding;
    Device d;CmdBuf cb;DrawRec draw;NativeTargetCache cache;NativeTargetPlan prepared;std::string why;
    variant_fixture::RecipeFixture fixture;fixture.atts[1].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    auto recipe=std::make_shared<GraphicsRecipe>(fixture.capture());auto source_layout=std::make_shared<PipelineLayoutInfo>(fixture.layout);
    d.application_queue_count=1;d.material_queues.push_back(h<VkQueue>(12000));d.physical_properties.limits.maxBoundDescriptorSets=8;
    d.d.CreateRenderPass=[](VkDevice,const VkRenderPassCreateInfo* info,const VkAllocationCallbacks*,VkRenderPass* out){
        check(info->subpassCount==1 && info->pSubpasses[0].colorAttachmentCount==1,"classic null keeps the color slot count while compacting attachments");
        *out=h<VkRenderPass>(12001);return VK_SUCCESS;
    };
    d.d.DestroyRenderPass=[](VkDevice,VkRenderPass,const VkAllocationCallbacks*){};
    d.d.CreateFramebuffer=[](VkDevice,const VkFramebufferCreateInfo* info,const VkAllocationCallbacks*,VkFramebuffer* out){
        check(info->width==64 && info->height==32 && info->attachmentCount<=2,"classic null framebuffer omits unused descriptions with a valid empty extent");
        *out=h<VkFramebuffer>(12002);return VK_SUCCESS;
    };
    d.d.DestroyFramebuffer=[](VkDevice,VkFramebuffer,const VkAllocationCallbacks*){};
    d.d.CreateDescriptorSetLayout=[](VkDevice,const VkDescriptorSetLayoutCreateInfo*,const VkAllocationCallbacks*,VkDescriptorSetLayout* out){*out=h<VkDescriptorSetLayout>(12003);return VK_SUCCESS;};
    d.d.DestroyDescriptorSetLayout=[](VkDevice,VkDescriptorSetLayout,const VkAllocationCallbacks*){};
    d.d.CreatePipelineLayout=[](VkDevice,const VkPipelineLayoutCreateInfo*,const VkAllocationCallbacks*,VkPipelineLayout* out){*out=h<VkPipelineLayout>(12004);return VK_SUCCESS;};
    d.d.DestroyPipelineLayout=[](VkDevice,VkPipelineLayout,const VkAllocationCallbacks*){};
    d.d.CreateShaderModule=[](VkDevice,const VkShaderModuleCreateInfo*,const VkAllocationCallbacks*,VkShaderModule* out){*out=h<VkShaderModule>(12005);return VK_SUCCESS;};
    d.d.DestroyShaderModule=[](VkDevice,VkShaderModule,const VkAllocationCallbacks*){};
    static bool fail_pipeline;static uint32_t made,destroyed;fail_pipeline=false;made=destroyed=0;
    d.d.CreateGraphicsPipelines=[](VkDevice,VkPipelineCache,uint32_t,const VkGraphicsPipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline* out){
        if(fail_pipeline)return VK_ERROR_OUT_OF_DEVICE_MEMORY;*out=h<VkPipeline>(12100+(++made));return VK_SUCCESS;
    };
    d.d.DestroyPipeline=[](VkDevice,VkPipeline,const VkAllocationCallbacks*){++destroyed;};
    d.d.CmdBindPipeline=[](VkCommandBuffer,VkPipelineBindPoint,VkPipeline pipeline){check(bool(pipeline),"classic compatibility never records a null pipeline bind");};
    d.d.CmdBeginRenderPass=classic_begin;d.d.CmdEndRenderPass=output_end;d.d.CmdPipelineBarrier=mrt_barrier;
    draw.pipeline=draw.bound=h<VkPipeline>(12006);cb.gfx_pipeline=cb.gfx_bound=draw.pipeline;
    d.pipes[draw.pipeline].recipe=recipe;d.pipes[draw.pipeline].layout=source_layout;
    TargetView views[2];std::shared_ptr<Resource> resources[2];
    for(uint32_t i=0;i<2;++i){auto& view=views[i];view.known=true;view.handle=h<VkImageView>(12200+i);
        view.image.id=12300+i;view.image.type=VK_IMAGE_TYPE_2D;view.image.format=recipe->render_pass->attachments[i].format;
        view.image.samples=VK_SAMPLE_COUNT_1_BIT;view.image.capture_safe=true;view.image.extent={64,32,1};view.image.levels=view.image.layers=1;
        view.view.id=12200+i;view.view.image=h<VkImage>(12300+i);view.view.image_id=view.image.id;view.view.type=VK_IMAGE_VIEW_TYPE_2D;
        view.view.format=view.image.format;view.view.range={VkImageAspectFlags(i?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,1};
        view.image.mem=h<VkDeviceMemory>(view.image.id+20000);view.image.mem_size=8192;d.images[view.view.image]=view.image;d.image_views[view.handle]=view.view;d.image_views[view.handle].lifetime=std::make_shared<ImageViewLifetime>();
        auto resource=std::make_shared<Resource>();resource->bound=resource->image_safe=true;resource->keep=std::make_shared<int>(1);
        resource->image=uint64_t(view.view.image);resource->image_view=uint64_t(view.handle);resource->image_view_id=view.view.id;
        resource->identity=(uint64_t(view.image.id)<<2)|2;resource->usage=i?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;resources[i]=resource;
        cb.image_layouts.set(view.image.id,1,1,view.view.range,i?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    auto fb=std::make_shared<FramebufferInfo>();fb->known=true;fb->width=64;fb->height=32;fb->layers=1;fb->attachment_count=2;fb->views={views[0],views[1]};
    RenderTargets context;context.known=true;context.recipe=recipe->render_pass;context.framebuffer=fb;context.layers=1;context.area.extent={64,32};
    cb.in_rp=true;cb.target_contexts={context};cb.active_targets=1;cb.pass.recipe=context.recipe;cb.pass.info=fb;cb.pass.area=context.area;
    native_target_bind(&d,&cb,draw.pipeline);efmi::assets::FilePlan plan;efmi::assets::FilePlanItem item;
    item.operation=efmi::assets::FileOperation::NativeBind;item.native_target.kind=Binding::ColorTarget;plan.items={item};
    auto prepare=[&]{prepared={};draw.bound=cb.gfx_bound;return prepare_native_targets(&d,&cb,draw,plan,prepared,why,&cache);};
    if(hunting_case){
        static uint32_t calls;calls=0;draw.kind=DK_INDEXED;draw.count=3;draw.instances=1;
        d.pipes[draw.pipeline].shaders=fixture.shaders;
        d.d.CmdDrawIndexed=[](VkCommandBuffer,uint32_t,uint32_t,uint32_t,int32_t,uint32_t){++calls;};
        plan.pre_items=hunting_case==2?0:plan.items.size();
        if(hunting_case==3){
            efmi::assets::FilePlanItem hidden;hidden.operation=efmi::assets::FileOperation::DrawIndexed;
            hidden.index_count=UINT32_MAX;plan.items.push_back(hidden);
        }
        NativeCallCache executor;output_commands.clear();
        check(native_calls(executor,nullptr,&d,&cb,draw,plan,true,why),hunting_case==3?
            "hunting-hidden invalid draw arguments cannot block resource commands":"hunting-hidden native graphics list remains executable");
        check(native_target_masked(&cb) && !output_commands.empty(),hunting_case==2?
            "hunting-hidden native draw retains post output commands":"hunting-hidden native draw retains pre output commands");
        check(calls==0,"hunting-hidden caller is not issued while its resource commands run");
        cb.reset_recording();return;
    }
    output_commands.clear();fail_pipeline=true;
    check(!prepare() && output_commands.empty() && !cb.native_target_state,"failed compatible pipeline creation refuses before any pass split");
    fail_pipeline=false;bool ready=prepare();check(ready,"classic color null prepares without the dynamic-unused feature");if(!ready)return;
    VariantDispatch funcs;
#define X(n) funcs.n=d.d.n;
    EFVK_VARIANT_FUNCS(X)
#undef X
    auto mod=std::make_shared<PipelineVariant>(d.handle,funcs);
    check(mod->build(*recipe,*source_layout,variant_fixture::RecipeFixture::mod_vertex(),8),"merged interface prepares the private bone and instance descriptors");
    const auto original_context=cb.active_targets;
    output_commands.clear();fail_pipeline=true;
    check(!native_target_variant(&d,&cb,*mod,why,&prepared,1) && output_commands.empty() && !cb.native_target_state && cb.active_targets==original_context,
        "future Mod pass clone failure precedes the first output split and leaves actual targets unchanged");
    fail_pipeline=false;
    const auto future=native_target_variant(&d,&cb,*mod,why,&prepared,1);
    check(future && future!=mod->pipeline && output_commands.empty() && !cb.native_target_state && cb.active_targets==original_context,
        "future Mod pass clone prepares the exact output prefix without recording it");
    check(native_target_variant(&d,&cb,*mod,why,&prepared,0)==mod->pipeline,
        "post-only output events do not change the Mod draw's pass");
    auto oversized=prepared;oversized.events.resize(4096);
    check(!native_target_variant(&d,&cb,*mod,why,&oversized,1) && output_commands.empty() && !cb.native_target_state,
        "Mod retention budget includes later output events before recording the first split");
    oversized={};
    efmi::assets::FilePlan interleaved;
    efmi::assets::FilePlanItem marker,restore;
    marker.operation=efmi::assets::FileOperation::DrawIndexed;restore=item;restore.native_resource=resources[0];
    interleaved.items={marker,item,marker,restore,marker};
    NativeTargetPlan ordered;
    check(prepare_native_targets(&d,&cb,draw,interleaved,ordered,why,&cache),"all interleaved outputs prepare before any Mod draw");
    check(native_target_variant(&d,&cb,*mod,why,&ordered,0)==mod->pipeline &&
          native_target_variant(&d,&cb,*mod,why,&ordered,2)==future &&
          native_target_variant(&d,&cb,*mod,why,&ordered,4)==mod->pipeline && output_commands.empty() && !cb.native_target_state,
        "three successive Mod draws preflight original, masked and restored passes without commands");
    ordered={};
    record_native_target(&d,&cb,prepared,0);VkImageLayout current;
    check(native_target_masked(&cb) && cb.pass.info->views.size()==1 && cb.pass.recipe->subpasses[0].colors[0].attachment==VK_ATTACHMENT_UNUSED &&
        cb.pass.recipe->subpasses[0].depth.attachment==0,"classic null publishes compact actual attachments and a remapped preserved depth reference");
    check(cb.image_layouts.get(views[0].image.id,1,1,views[0].view.range,current) && current==fixture.atts[0].finalLayout,
        "unused original color stays at its first End final layout without an implicit placeholder transition");
    check(cb.gfx_bound!=draw.pipeline && cb.native_pipeline->bound==cb.gfx_bound,"null event binds the prepared compatible current-draw pipeline");
    check(!native_target_bind(&d,&cb,h<VkPipeline>(12999)) && cb.native_target_blocked,"unknown later pipeline is blocked instead of forwarded into an incompatible pass");
    check(native_target_bind(&d,&cb,draw.pipeline)==cb.gfx_bound && !cb.native_target_blocked,"known later pipeline recovers through the compatibility cache");
    const auto built=made;native_target_bind(&d,&cb,draw.pipeline);check(made==built,"repeated draws reuse the exact source and actual-pass pipeline");
    const auto selected=cb.native_pipeline;
    check(native_target_variant(&d,&cb,*mod,why)==future,"recorded prefix reuses its preflighted Mod pipeline");
    // A distinct valid interface forces the existing cold-clone failure check below.
    mod->effective_recipe=std::make_shared<GraphicsRecipe>(*mod->effective_recipe);
    output_commands.clear();fail_pipeline=true;
    check(!native_target_variant(&d,&cb,*mod,why) && output_commands.empty() && cb.native_pipeline==selected,
        "failed Mod pass clone emits nothing and retains the application's selected shaders");
    fail_pipeline=false;const auto compatible=native_target_variant(&d,&cb,*mod,why);
    check(compatible && compatible!=mod->pipeline && cb.native_pipeline==selected && !cb.native_target_blocked,
        "Mod pass clone retains the complete interface without replacing application selection");
    const auto with_mod=made;
    check(native_target_variant(&d,&cb,*mod,why)==compatible && made==with_mod,"Mod pass clone reuses its immutable interface cache identity");
    plan.items[0].native_target.kind=Binding::DepthTarget;ready=prepare();check(ready,"classic depth null follows color null in the same pass");if(!ready)return;
    record_native_target(&d,&cb,prepared,0);
    check(cb.pass.info->views.empty() && cb.pass.recipe->attachments.empty(),"fully unused classic output has no placeholder image or layout transition");
    plan.items[0].native_target.kind=Binding::ColorTarget;plan.items[0].native_resource=resources[0];ready=prepare();check(ready,"classic original color can be restored after every output was null");if(!ready)return;
    record_native_target(&d,&cb,prepared,0);
    check(cb.pass.info->views.size()==1 && cb.pass.info->views[0].handle==views[0].handle,"restored color uses the retained original slot contract");
    prepared={};cache.classic.clear();cache.pipeline_cache->pipelines.clear();
    check(made>0 && destroyed==0,"recording owners retain all compatible pipelines after cache eviction");
    native_targets_end(&d,&cb);check(cb.gfx_bound==draw.pipeline && !cb.native_target_blocked,"host End restores the selected source pipeline for the next application pass");
    check(native_target_variant(&d,&cb,*mod,why)==mod->pipeline,"outside a masked pass the Mod uses its original variant");
    cb.reset_recording();mod.reset();check(destroyed==made,"legal recording reset releases compatible pipeline handles after completion");
}
