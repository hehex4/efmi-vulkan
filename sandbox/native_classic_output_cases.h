#include "../layer/src/native_targets.h"
static uint32_t classic_creates=0,classic_fb_creates=0,classic_destroys=0,classic_fb_destroys=0;
static bool classic_fail_fb=false;
static uint32_t classic_expected_count=1;
static VkFramebuffer classic_bound=VK_NULL_HANDLE;
static VkResult VKAPI_CALL classic_create(VkDevice,const VkRenderPassCreateInfo* info,const VkAllocationCallbacks*,VkRenderPass* out){
    ++classic_creates;*out=h<VkRenderPass>(9900+classic_creates);
    check(info->attachmentCount==classic_expected_count && info->pAttachments[0].loadOp==VK_ATTACHMENT_LOAD_OP_LOAD &&
        info->pAttachments[0].initialLayout==(classic_expected_count==1?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) && info->pAttachments[0].finalLayout==info->pAttachments[0].initialLayout,
        "classic private pass preserves a compatible stored LOAD contract");return VK_SUCCESS;
}
static VkResult VKAPI_CALL classic_framebuffer(VkDevice,const VkFramebufferCreateInfo* info,const VkAllocationCallbacks*,VkFramebuffer* out){
    ++classic_fb_creates;if(classic_fail_fb)return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    check(info->attachmentCount==classic_expected_count && info->width==64 && info->height==32 && info->layers==1,"classic private framebuffer covers the retained view");
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
        d.image_views[view.handle].lifetime=std::make_shared<ImageViewLifetime>();
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
    check(!prepare() && output_commands.empty() && classic_creates==1,"unproven selected pipeline refuses a later classic null before its first split or allocation");plan=saved;
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

static void native_classic_attachment_cases(){
    using Resource=efmi::frontend::NativeResource;using Binding=efmi::frontend::NativeBinding;
    Device d;CmdBuf cb;DrawRec draw;NativeTargetCache cache;NativeTargetPlan prepared;std::string why;
    d.application_queue_count=1;d.material_queues.push_back(h<VkQueue>(11000));cb.in_rp=true;
    d.d.CreateRenderPass=classic_create;d.d.CreateFramebuffer=classic_framebuffer;d.d.DestroyRenderPass=classic_destroy;d.d.DestroyFramebuffer=classic_destroy_fb;
    d.d.CmdBeginRenderPass=classic_begin;d.d.CmdEndRenderPass=output_end;d.d.CmdPipelineBarrier=mrt_barrier;
    classic_expected_count=3;
    TargetView views[5];std::shared_ptr<Resource> resources[5];
    for(uint32_t i=0;i<5;++i){auto& v=views[i];const bool depth=i==0 || i==4;v.known=true;v.handle=h<VkImageView>(11100+i);
        v.image.id=11200+i;v.image.type=VK_IMAGE_TYPE_2D;v.image.format=depth?VK_FORMAT_D32_SFLOAT_S8_UINT:VK_FORMAT_R8G8B8A8_UNORM;
        v.image.capture_safe=true;v.image.samples=VK_SAMPLE_COUNT_1_BIT;v.image.extent={64,32,1};v.image.levels=v.image.layers=1;
        v.view.id=11100+i;v.view.image=h<VkImage>(11200+i);v.view.image_id=v.image.id;v.view.type=VK_IMAGE_VIEW_TYPE_2D;v.view.format=v.image.format;
        v.view.range={VkImageAspectFlags(depth?VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,1};
        d.images[v.view.image]=v.image;d.image_views[v.handle]=v.view;d.image_views[v.handle].lifetime=std::make_shared<ImageViewLifetime>();
        auto r=std::make_shared<Resource>();r->bound=r->image_safe=true;r->keep=std::make_shared<int>(1);r->image=uint64_t(v.view.image);
        r->image_view=uint64_t(v.handle);r->image_view_id=v.view.id;r->identity=(uint64_t(v.image.id)<<2)|2;
        r->usage=depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;resources[i]=r;
        cb.image_layouts.set(v.image.id,1,1,v.view.range,VK_IMAGE_LAYOUT_GENERAL);
    }
    auto pass=std::make_shared<RenderPassRecipe>();pass->complete=true;pass->attachments.resize(3);pass->subpasses.resize(1);
    for(size_t i=0;i<3;++i){auto& a=pass->attachments[i];a.sType=VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;a.format=views[i].view.format;a.samples=VK_SAMPLE_COUNT_1_BIT;
        a.loadOp=a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;
        a.finalLayout=i==0?VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:i==1?VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;}
    VkAttachmentReference2 c0{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2},c1{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2},depth{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
    c0.attachment=2;c1.attachment=1;c0.layout=c1.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;depth.attachment=0;depth.layout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    auto& sub=pass->subpasses[0];sub.colors={c0,c1};sub.has_depth=true;sub.depth=depth;
    auto pipeline=std::make_shared<GraphicsRecipe>();pipeline->geometry_complete=true;pipeline->render_pass=pass;pipeline->multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    draw.pipeline=h<VkPipeline>(11300);d.pipes[draw.pipeline].recipe=pipeline;
    auto fb=std::make_shared<FramebufferInfo>();fb->known=true;fb->layers=1;fb->width=64;fb->height=32;fb->attachment_count=3;fb->views={views[0],views[1],views[2]};
    RenderTargets targets;targets.known=true;targets.recipe=pass;targets.framebuffer=fb;targets.layers=1;targets.area.extent={64,32};cb.target_contexts={targets};cb.active_targets=1;
    cb.pass.info=fb;cb.pass.recipe=pass;cb.pass.area=targets.area;cb.native_image_state=std::make_shared<NativeImageState>();
    efmi::assets::FilePlan plan;efmi::assets::FilePlanItem color;color.operation=efmi::assets::FileOperation::NativeBind;color.native_target.kind=Binding::ColorTarget;
    color.native_target.slot=1;color.native_resource=resources[3];auto dsv=color;dsv.native_target.kind=Binding::DepthTarget;dsv.native_target.slot=0;dsv.native_resource=resources[4];plan.items={color,dsv};
    auto prepare=[&]{prepared={};return prepare_native_targets(&d,&cb,draw,plan,prepared,why,&cache);};output_commands.clear();
    bool ready=prepare();check(ready && output_commands.empty(),"classic permuted MRT and combined DSV prepare together without recording");if(!ready){classic_expected_count=1;return;}
    const auto& next=prepared.events[0]->targets.framebuffer->views;
    check(next.size()==3 && next[0].handle==views[0].handle && next[1].handle==views[3].handle && next[2].handle==views[2].handle,
        "classic color slot maps through the subpass reference instead of its attachment index");
    check(prepared.events[1]->images->attachments==std::set<uint32_t>{views[2].image.id,views[3].image.id,views[4].image.id},
        "classic MRT/DSV conflict state contains the complete actual attachment set");
    d.image_views[views[2].handle].id++;check(!prepare() && output_commands.empty(),"stale untouched classic view refuses before cached framebuffer reuse");d.image_views[views[2].handle].id--;
    ready=prepare();if(!ready){check(false,"classic multiple attachment plan recovers after stale-view refusal");classic_expected_count=1;return;}
    record_native_target(&d,&cb,prepared,0);
    check(mrt_image_barriers.size()==3,"first classic split transitions the new color and every preserved original from their final layouts");
    const auto both=VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
    check(mrt_image_barriers[0].subresourceRange.aspectMask==both && mrt_image_barriers[0].oldLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
        mrt_image_barriers[0].newLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,"preserved combined aspects transition together before private LOAD");
    VkImageLayout value;check(cb.image_layouts.get(views[1].image.id,1,1,views[1].view.range,value) && value==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        "inactive original color remains in its application's final layout");
    record_native_target(&d,&cb,prepared,1);
    check(mrt_image_barriers.size()==2 && mrt_image_barriers[0].newLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
        mrt_image_barriers[1].newLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,"later classic DSV move restores the original and introduces the retained view");
    native_targets_end(&d,&cb);
    check(mrt_image_barriers.size()==3,"classic host End restores all current attachment layouts including unchanged originals");
    check(cb.image_layouts.get(views[2].image.id,1,1,views[2].view.range,value) && value==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
        cb.image_layouts.get(views[4].image.id,1,1,views[4].view.range,value) && value==VK_IMAGE_LAYOUT_GENERAL,
        "classic host End preserves distinct original color and introduced depth final layouts");
    prepared={};for(auto& owner:cb.owners)owner->reset_after_completion();cache.classic.clear();cb.owners.clear();
    cb.target_contexts={targets};cb.active_targets=1;cb.pass.info=fb;cb.pass.recipe=pass;cb.native_target_state.reset();output_commands.clear();
    for(size_t i=0;i<5;++i)cb.image_layouts.set(views[i].image.id,1,1,views[i].view.range,
        i==0?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:i<3?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL);
    auto moved=color;moved.native_target.slot=0;moved.native_resource=resources[1];plan.items={color,moved};
    ready=prepare();check(ready,"classic original image can move to another vacated compatible color slot");
    if(ready){
        record_native_target(&d,&cb,prepared,0);record_native_target(&d,&cb,prepared,1);
        check(mrt_image_barriers.size()==2 && mrt_image_barriers[1].oldLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            "cross-slot original uses its own implicit final layout after the first classic End");
        native_targets_end(&d,&cb);
        check(cb.image_layouts.get(views[1].image.id,1,1,views[1].view.range,value) && value==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            "cross-slot original returns to its own final layout at host End");
    }
    classic_expected_count=1;
}
