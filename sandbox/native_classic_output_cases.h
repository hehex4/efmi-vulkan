#include "../layer/src/native_targets.h"
static uint32_t classic_creates=0,classic_fb_creates=0,classic_destroys=0,classic_fb_destroys=0;
static bool classic_fail_fb=false;
static VkFramebuffer classic_bound=VK_NULL_HANDLE;
static VkResult VKAPI_CALL classic_create(VkDevice,const VkRenderPassCreateInfo* info,const VkAllocationCallbacks*,VkRenderPass* out){
    ++classic_creates;*out=h<VkRenderPass>(9900+classic_creates);
    check(info->attachmentCount==1 && info->pAttachments[0].loadOp==VK_ATTACHMENT_LOAD_OP_LOAD &&
        info->pAttachments[0].initialLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && info->pAttachments[0].finalLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        "classic private pass preserves a compatible stored LOAD contract");return VK_SUCCESS;
}
static VkResult VKAPI_CALL classic_framebuffer(VkDevice,const VkFramebufferCreateInfo* info,const VkAllocationCallbacks*,VkFramebuffer* out){
    ++classic_fb_creates;if(classic_fail_fb)return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    check(info->attachmentCount==1 && info->width==64 && info->height==32 && info->layers==1,"classic private framebuffer covers the retained view");
    *out=h<VkFramebuffer>(10000+classic_fb_creates);return VK_SUCCESS;
}
static void VKAPI_CALL classic_destroy(VkDevice,VkRenderPass,const VkAllocationCallbacks*){++classic_destroys;}
static void VKAPI_CALL classic_destroy_fb(VkDevice,VkFramebuffer,const VkAllocationCallbacks*){++classic_fb_destroys;}
static void VKAPI_CALL classic_begin(VkCommandBuffer,const VkRenderPassBeginInfo* info,VkSubpassContents contents){
    output_commands.push_back(3);classic_bound=info->framebuffer;
    check(contents==VK_SUBPASS_CONTENTS_INLINE && info->clearValueCount==0 && info->renderArea.extent.width==64,"classic continuation begins inline with no repeated clear");
}
static void native_classic_output_cases(){
    using Resource=efmi::frontend::NativeResource;using Binding=efmi::frontend::NativeBinding;
    Device d;CmdBuf cb;DrawRec draw;NativeTargetPlan prepared;NativeTargetCache cache;std::string why;
    d.application_queue_count=1;d.material_queues.push_back(h<VkQueue>(9800));cb.in_rp=true;
    d.d.CreateRenderPass=classic_create;d.d.CreateFramebuffer=classic_framebuffer;d.d.DestroyRenderPass=classic_destroy;d.d.DestroyFramebuffer=classic_destroy_fb;
    d.d.CmdBeginRenderPass=classic_begin;d.d.CmdEndRenderPass=output_end;d.d.CmdPipelineBarrier=mrt_barrier;
    auto pass=std::make_shared<RenderPassRecipe>();pass->complete=true;pass->attachments.resize(1);
    auto& attachment=pass->attachments[0];attachment.sType=VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    attachment.format=VK_FORMAT_R8G8B8A8_UNORM;attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    pass->subpasses.resize(1);VkAttachmentReference2 ref{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};ref.attachment=0;ref.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    pass->subpasses[0].colors={ref};
    auto pipeline=std::make_shared<GraphicsRecipe>();pipeline->geometry_complete=true;pipeline->render_pass=pass;
    pipeline->multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;draw.pipeline=h<VkPipeline>(9801);d.pipes[draw.pipeline].recipe=pipeline;
    TargetView views[2];std::shared_ptr<Resource> resources[2];VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    for(uint32_t i=0;i<2;++i){auto& view=views[i];view.known=true;view.handle=h<VkImageView>(9810+i);
        view.image.id=9820+i;view.image.type=VK_IMAGE_TYPE_2D;view.image.format=attachment.format;view.image.samples=VK_SAMPLE_COUNT_1_BIT;
        view.image.capture_safe=true;view.image.extent={64,32,1};view.image.levels=view.image.layers=1;
        view.view.id=9810+i;view.view.image=h<VkImage>(9820+i);view.view.image_id=view.image.id;view.view.type=VK_IMAGE_VIEW_TYPE_2D;
        view.view.format=attachment.format;view.view.range=range;d.images[view.view.image]=view.image;d.image_views[view.handle]=view.view;
        auto resource=std::make_shared<Resource>();resource->bound=resource->image_safe=true;resource->keep=std::make_shared<int>(1);
        resource->image=uint64_t(view.view.image);resource->image_view=uint64_t(view.handle);resource->image_view_id=view.view.id;
        resource->identity=(uint64_t(view.image.id)<<2)|2;resource->usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;resources[i]=resource;
        cb.image_layouts.set(view.image.id,1,1,range,i?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    auto fb=std::make_shared<FramebufferInfo>();fb->known=true;fb->width=64;fb->height=32;fb->layers=fb->attachment_count=1;fb->views={views[0]};
    RenderTargets context;context.known=true;context.recipe=pass;context.framebuffer=fb;context.layers=1;context.area.extent={64,32};
    cb.target_contexts={context};cb.active_targets=1;cb.pass.recipe=pass;cb.pass.info=fb;cb.pass.area=context.area;
    cb.pass.render_pass=h<VkRenderPass>(9830);cb.pass.framebuffer=h<VkFramebuffer>(9831);cb.native_image_state=std::make_shared<NativeImageState>();
    efmi::assets::FilePlan plan;efmi::assets::FilePlanItem item;item.operation=efmi::assets::FileOperation::NativeBind;
    item.native_target.kind=Binding::ColorTarget;item.native_resource=resources[1];plan.items={item};
    auto prepare=[&]{prepared={};return prepare_native_targets(&d,&cb,draw,plan,prepared,why,&cache);};
    output_commands.clear();classic_creates=classic_fb_creates=classic_destroys=classic_fb_destroys=0;
    bool ready=prepare();check(ready && output_commands.empty() && !cb.native_target_state,"classic preflight allocates without recording or changing original state");if(!ready)return;
    check(!prepared.events[0]->targets.dynamic && prepared.events[0]->targets.dynamic_colors.empty() &&
        prepared.events[0]->targets.framebuffer->views[0].handle==views[1].handle,"classic actual metadata stays classic and names the new framebuffer attachment");
    check(prepare() && classic_creates==1 && classic_fb_creates==1,"classic repeated preparation reuses the bounded identity-keyed object cache");
    auto saved=plan;plan.items.push_back(item);plan.items.back().native_resource={};
    check(!prepare() && output_commands.empty() && classic_creates==1,"later classic null binding refuses the whole plan before its first split or allocation");plan=saved;
    attachment.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;check(!prepare(),"classic discarded color cannot be recovered by LOAD");attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    pass->subpasses.push_back(pass->subpasses[0]);check(!prepare(),"classic multi-subpass output needs its own split adapter");pass->subpasses.pop_back();
    d.image_views[views[1].handle].id++;check(!prepare() && output_commands.empty(),"classic cache reuse still proves the destination's live view identity");d.image_views[views[1].handle].id--;
    ready=prepare();check(ready,"classic valid plan recovers after refused preparation");if(!ready)return;
    record_native_target(&d,&cb,prepared,0);VkImageLayout current=VK_IMAGE_LAYOUT_UNDEFINED;
    check(output_commands.size()==3 && classic_bound==cb.pass.framebuffer && cb.pass.recipe==cb.target_contexts.back().recipe,"classic private End, dependency and Begin publish the actual pass instance");
    check(cb.image_layouts.get(views[0].image.id,1,1,range,current) && current==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,"first private End accounts for the original implicit final transition");
    check(mrt_image_barriers.size()==1 && mrt_image_barriers[0].oldLayout==VK_IMAGE_LAYOUT_GENERAL && mrt_image_barriers[0].newLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        "first classic binding moves only the new image from its proven saved layout");
    plan.items[0].native_resource=resources[0];ready=prepare();check(ready,"classic original-view restoration has a compatible prepared framebuffer");if(!ready)return;
    record_native_target(&d,&cb,prepared,0);
    check(mrt_image_barriers.size()==2 && mrt_image_barriers[0].newLayout==VK_IMAGE_LAYOUT_GENERAL &&
        mrt_image_barriers[1].oldLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && mrt_image_barriers[1].newLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        "classic roundtrip restores the introduced image and reactivates the original from its final layout");
    native_targets_end(&d,&cb);
    check(mrt_image_barriers.size()==1 && mrt_image_barriers[0].oldLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
        mrt_image_barriers[0].newLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,"host classic End restores the original final layout after a private roundtrip");
    prepared={};cache.classic.clear();check(classic_destroys==0 && classic_fb_destroys==0,"cache eviction keeps recorded private objects alive until completion");
    for(auto& owner:cb.owners)owner->reset_after_completion();check(classic_destroys==2 && classic_fb_destroys==2,"completion releases both retained pass/framebuffer pairs after cache eviction");
    cb.target_contexts={context};cb.active_targets=1;cb.native_target_state.reset();plan=saved;output_commands.clear();classic_fail_fb=true;
    const auto destroyed=classic_destroys;check(!prepare() && output_commands.empty() && classic_destroys==destroyed+1 && cache.classic.empty(),
        "framebuffer allocation failure rolls back its private render pass before recording");classic_fail_fb=false;
}
