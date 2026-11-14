#include "../layer/src/native_targets.h"
#include "../layer/src/native_images.h"
static VkImageView mrt_views[8];static uint32_t mrt_count=0,mrt_barriers=0;
static void VKAPI_CALL mrt_begin(VkCommandBuffer cmd,const VkRenderingInfo* info){
    output_begin(cmd,info);mrt_count=info->colorAttachmentCount;
    for(uint32_t i=0;i<mrt_count;++i)mrt_views[i]=info->pColorAttachments[i].imageView;
}
static void VKAPI_CALL mrt_barrier(VkCommandBuffer cmd,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t memory_count,const VkMemoryBarrier* memory,uint32_t buffer_count,const VkBufferMemoryBarrier* buffers,uint32_t image_count,const VkImageMemoryBarrier* images){
    output_barrier(cmd,src,dst,flags,memory_count,memory,buffer_count,buffers,image_count,images);mrt_barriers=image_count;
}
static void native_mrt_output_cases()
{
    using Resource=efmi::frontend::NativeResource;using Binding=efmi::frontend::NativeBinding;
    Device d;CmdBuf cb;DrawRec draw;NativeTargetPlan prepared;std::string why;
    d.application_queue_count=1;d.material_queues.push_back(h<VkQueue>(9700));d.dynamic_unused_attachments=true;
    d.d.CmdBeginRendering=mrt_begin;d.d.CmdEndRendering=output_end;d.d.CmdPipelineBarrier=mrt_barrier;cb.in_rp=true;
    auto recipe=std::make_shared<GraphicsRecipe>();recipe->multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;recipe->geometry_complete=recipe->dynamic_rendering=true;
    recipe->rendering_colors={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM};recipe->rendering_depth=VK_FORMAT_D32_SFLOAT;
    draw.pipeline=h<VkPipeline>(9701);d.pipes[draw.pipeline].recipe=recipe;
    RenderTargets targets;targets.known=targets.dynamic=targets.has_depth=true;targets.layers=1;targets.area.extent={64,32};targets.views.resize(6);
    for(size_t i=0;i<2;++i){VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};color.imageView=h<VkImageView>(9710+i);
        color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        targets.dynamic_colors.push_back(color);auto& view=targets.views[2*i];view.known=true;view.handle=color.imageView;view.view.image=h<VkImage>(9720+i);
        view.image.id=uint32_t(9720+i);view.image.samples=VK_SAMPLE_COUNT_1_BIT;view.view.format=VK_FORMAT_R8G8B8A8_UNORM;
    }
    targets.depth.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;targets.depth.imageView=h<VkImageView>(9730);
    targets.depth.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;targets.depth.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;targets.depth.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    auto& depth=targets.views[4];depth.known=true;depth.handle=targets.depth.imageView;depth.view.image=h<VkImage>(9731);depth.image.id=9731;
    depth.view.format=VK_FORMAT_D32_SFLOAT;depth.image.samples=VK_SAMPLE_COUNT_1_BIT;
    cb.target_contexts={targets};cb.active_targets=1;cb.native_image_state=std::make_shared<NativeImageState>();
    std::shared_ptr<Resource> resources[2];VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    for(size_t i=0;i<2;++i){ImageInfo image;image.id=uint32_t(9740+i);image.type=VK_IMAGE_TYPE_2D;image.samples=VK_SAMPLE_COUNT_1_BIT;
        image.levels=image.layers=1;image.extent={64,32,1};ImageViewInfo view;view.id=uint32_t(9750+i);view.image=h<VkImage>(9740+i);view.image_id=image.id;
        view.format=VK_FORMAT_R8G8B8A8_UNORM;view.type=VK_IMAGE_VIEW_TYPE_2D;view.range=range;
        const auto handle=h<VkImageView>(9750+i);d.images[view.image]=image;d.image_views[handle]=view;
        cb.image_layouts.set(image.id,1,1,range,i?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL);
        auto resource=std::make_shared<Resource>();resource->bound=resource->image_safe=true;resource->keep=std::make_shared<int>(1);
        resource->image=uint64_t(view.image);resource->image_view=uint64_t(handle);resource->image_view_id=view.id;
        resource->identity=(uint64_t(image.id)<<2)|2;resource->usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;resources[i]=resource;
    }
    efmi::assets::FilePlan plan;
    auto add=[&](uint32_t slot,std::shared_ptr<Resource> resource){efmi::assets::FilePlanItem item;item.operation=efmi::assets::FileOperation::NativeBind;item.native_target.kind=Binding::ColorTarget;item.native_target.slot=slot;item.native_resource=resource;plan.items.push_back(item);};
    add(1,resources[0]);add(0,resources[1]);add(1,{});add(1,resources[0]);
    auto prepare=[&]{prepared={};return prepare_native_targets(&d,&cb,draw,plan,prepared,why);};output_commands.clear();
    bool ready=prepare();check(ready && prepared.state->colors.size()==2 && output_commands.empty(),"MRT whole-plan preflight owns independent original slot contracts without commands");if(!ready)return;
    check(prepared.events[0]->slot==1 && prepared.events[0]->targets.dynamic_colors[0].imageView==targets.dynamic_colors[0].imageView &&
          prepared.events[0]->targets.dynamic_colors[0].loadOp==VK_ATTACHMENT_LOAD_OP_LOAD && prepared.events[0]->targets.depth.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD,
          "moving one color preserves all other stored color and depth LOAD contracts");
    check(prepared.events[2]->images->attachments.size()==2 && prepared.events[2]->images->attachments.count(9731) && prepared.events[2]->images->attachments.count(9741),
          "nulling one slot leaves the other color and depth in conflict proof");
    auto saved=plan;plan.items[1].native_resource=resources[0];
    check(!prepare() && output_commands.empty() && cb.active_targets==1 && !cb.native_target_state,"same image at two active color slots refuses the whole plan before its first event");plan=saved;
    plan.items.back().native_target.slot=8;check(!prepare() && output_commands.empty(),"later unsupported slot refuses all earlier valid MRT events atomically");plan=saved;
    cb.target_contexts[0].dynamic_colors[0].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;check(!prepare(),"untouched discarded color cannot be preserved by a LOAD restart");cb.target_contexts[0]=targets;
    recipe->rendering_colors[0]=VK_FORMAT_R16G16B16A16_SFLOAT;check(!prepare(),"every preserved color requires its exact pipeline format");recipe->rendering_colors[0]=VK_FORMAT_R8G8B8A8_UNORM;
    ready=prepare();check(ready,"valid multi-color plan remains available after rejected attempts");if(!ready)return;
    for(size_t i=0;i<plan.items.size();++i)record_native_target(&d,&cb,prepared,i);
    check(mrt_count==2 && mrt_views[0]==VkImageView(resources[1]->image_view) && mrt_views[1]==VkImageView(resources[0]->image_view) && output_depth==targets.depth.imageView,
          "actual Begin retains all independent color destinations and original depth");
    check(cb.native_target_state->colors[0].active==mrt_views[0] && cb.native_target_state->colors[1].active==mrt_views[1],"both introduced destinations remain independently active for host completion");
    native_targets_end(&d,&cb);VkImageLayout first,second;
    check(mrt_barriers==2 && !cb.native_target_state && cb.image_layouts.get(9740,1,1,range,first) && first==VK_IMAGE_LAYOUT_GENERAL &&
          cb.image_layouts.get(9741,1,1,range,second) && second==VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,"host End restores both introduced images to their distinct exact layouts");
    cb.target_contexts={targets};cb.active_targets=1;cb.target_contexts[0].dynamic_colors[1].imageView=VK_NULL_HANDLE;cb.target_contexts[0].views[2]={};
    plan.items.resize(1);plan.items[0]=saved.items[0];
    ready=prepare();check(ready && !prepared.state->colors[1].original,"initially null color slot keeps known pipeline format without inventing an original view");if(!ready)return;
    record_native_target(&d,&cb,prepared,0);native_targets_end(&d,&cb);
    check(cb.image_layouts.get(9740,1,1,range,first) && first==VK_IMAGE_LAYOUT_GENERAL,"binding an initially null slot still restores the owned destination at End");
    cb.target_contexts={targets};cb.active_targets=1;cb.target_contexts[0].has_depth=false;cb.target_contexts[0].views.resize(4);
    for(size_t slot=0;slot<2;++slot){cb.target_contexts[0].dynamic_colors[slot].imageView=VK_NULL_HANDLE;cb.target_contexts[0].views[2*slot]={};}
    recipe->rendering_depth=VK_FORMAT_UNDEFINED;recipe->multisample.rasterizationSamples=VK_SAMPLE_COUNT_4_BIT;output_commands.clear();
    check(!prepare() && output_commands.empty(),"all-null original outputs do not prove a single-sample pipeline for a newly bound image");
    recipe->multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    check(prepare(),"exact single-sample pipeline contract permits the first actual attachment in an all-null context");

    auto with_stencil=targets;with_stencil.has_depth=false;with_stencil.has_stencil=true;with_stencil.stencil=targets.depth;
    with_stencil.views[4].view.format=VK_FORMAT_S8_UINT;recipe->rendering_depth=VK_FORMAT_UNDEFINED;recipe->rendering_stencil=VK_FORMAT_S8_UINT;
    cb.target_contexts={with_stencil};cb.active_targets=1;output_commands.clear();
    ready=prepare();check(ready && prepared.events[0]->targets.stencil.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD && prepared.events[0]->images->attachments.count(9731),
          "MRT color changes retain stencil-only LOAD and its actual conflict identity");if(!ready)return;
    cb.target_contexts[0].stencil.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;check(!prepare(),"discarded stencil cannot be preserved by a private LOAD");cb.target_contexts[0]=with_stencil;
    cb.target_contexts[0].stencil.resolveMode=VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;check(!prepare(),"stencil resolve cannot run at a private intermediate End");cb.target_contexts[0]=with_stencil;
    cb.target_contexts[0].views[4].known=false;check(!prepare(),"unknown stencil view cannot prove preserved continuation semantics");cb.target_contexts[0]=with_stencil;
    recipe->rendering_stencil=VK_FORMAT_UNDEFINED;check(!prepare(),"actual stencil requires its exact pipeline attachment format");recipe->rendering_stencil=VK_FORMAT_S8_UINT;
    cb.target_contexts[0].views[4].view.image=VkImage(resources[0]->image);check(!prepare(),"color destination cannot alias a preserved stencil-only image");cb.target_contexts[0]=with_stencil;
    cb.target_contexts[0].stencil.imageView=VK_NULL_HANDLE;d.dynamic_unused_attachments=false;
    check(!prepare(),"declared absent stencil cannot infer unused-attachments permission");d.dynamic_unused_attachments=true;
    check(prepare() && !prepared.events[0]->images->attachments.count(9731),"enabled declared but null stencil contributes no image conflict");cb.target_contexts[0]=with_stencil;
    ready=prepare();check(ready && output_commands.empty(),"stencil preparation remains atomic after invalid attempts");if(!ready)return;
    record_native_target(&d,&cb,prepared,0);
    check(!output_depth && output_stencil==with_stencil.stencil.imageView && output_stencil_load==VK_ATTACHMENT_LOAD_OP_LOAD,
          "actual private Begin preserves a stencil-only view without inventing depth");native_targets_end(&d,&cb);
    auto combined=targets;combined.has_stencil=true;combined.stencil=targets.depth;combined.views.resize(8);
    combined.views[4].view.format=VK_FORMAT_D32_SFLOAT_S8_UINT;combined.views[6]=combined.views[4];
    recipe->rendering_depth=recipe->rendering_stencil=VK_FORMAT_D32_SFLOAT_S8_UINT;
    cb.target_contexts={combined};cb.active_targets=1;
    cb.target_contexts[0].stencil.imageView=h<VkImageView>(9790);cb.target_contexts[0].views[6].handle=h<VkImageView>(9790);
    check(!prepare(),"nonnull depth and stencil pointers cannot use different actual views");cb.target_contexts[0]=combined;
    cb.target_contexts[0].depth.imageView=VK_NULL_HANDLE;check(!prepare(),"one null view with two depth/stencil pointers violates their shared-view contract");cb.target_contexts[0]=combined;
    ready=prepare();check(ready && prepared.events[0]->images->attachments.size()==3,"combined depth/stencil keeps one shared image identity beside both colors");if(!ready)return;
    record_native_target(&d,&cb,prepared,0);
    check(output_depth==combined.depth.imageView && output_stencil==output_depth && output_depth_load==VK_ATTACHMENT_LOAD_OP_LOAD && output_stencil_load==VK_ATTACHMENT_LOAD_OP_LOAD,
          "combined depth/stencil actually restarts both aspects with LOAD on their shared view");native_targets_end(&d,&cb);

}
