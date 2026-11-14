#include "../layer/src/native_targets.h"
static std::vector<char> output_commands;
static VkImageView output_view;
static VkAttachmentLoadOp output_load;
static void VKAPI_CALL output_begin(VkCommandBuffer,const VkRenderingInfo* info){output_commands.push_back('B');output_view=info->pColorAttachments[0].imageView;output_load=info->pColorAttachments[0].loadOp;}
static void VKAPI_CALL output_end(VkCommandBuffer){output_commands.push_back('E');}
static void VKAPI_CALL output_barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*){output_commands.push_back('D');}
static void native_output_cases()
{
    ExecutionScopes scopes;
    check(scopes.splittable(),"fresh recording has no active execution boundary");
    scopes.query(1,2,0,true);check(!scopes.splittable(),"query prevents output pass splitting");
    scopes.query(1,2,0,false);check(scopes.splittable(),"matching indexed/core query end restores empty scope");
    scopes.query(1,2,3,true);scopes.query(1,2,0,false);check(!scopes.known,"mismatched query index cannot erase a scope");
    scopes.reset();scopes.transform(true);check(!scopes.splittable(),"active transform feedback prevents ending rendering");
    scopes.transform(false);check(scopes.splittable(),"finished transform feedback releases the boundary");
    scopes.query(1,0,0,true);scopes.query(1,0,0,true);check(!scopes.known,"duplicate query poisons the recording proof");
    scopes.reset();for(uint32_t i=0;i<65;++i)scopes.query(1,i,0,true);check(!scopes.known,"query tracking overflow cannot manufacture an empty scope");
    Device d;CmdBuf cb;d.application_queue_count=1;d.material_queues.push_back(h<VkQueue>(9501));cb.in_rp=true;
    d.d.CmdBeginRendering=output_begin;d.d.CmdEndRendering=output_end;d.d.CmdPipelineBarrier=output_barrier;
    RenderTargets target;target.known=target.dynamic=true;target.layers=1;target.area.extent={64,32};target.views.resize(2);
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};color.imageView=h<VkImageView>(9502);
    color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    target.dynamic_colors.push_back(color);target.views[0].known=true;target.views[0].handle=color.imageView;
    target.views[0].view.image=h<VkImage>(9503);target.views[0].view.format=VK_FORMAT_R8G8B8A8_UNORM;target.views[0].image.samples=VK_SAMPLE_COUNT_1_BIT;
    cb.target_contexts.push_back(target);cb.active_targets=1;
    ImageInfo image;image.id=9504;image.type=VK_IMAGE_TYPE_2D;image.samples=VK_SAMPLE_COUNT_1_BIT;image.levels=image.layers=1;image.extent={64,32,1};
    ImageViewInfo view;view.id=9505;view.image=h<VkImage>(9506);view.image_id=image.id;view.format=VK_FORMAT_R8G8B8A8_UNORM;view.type=VK_IMAGE_VIEW_TYPE_2D;
    view.range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};const auto handle=h<VkImageView>(9507);
    d.images[view.image]=image;d.image_views[handle]=view;cb.image_layouts.set(image.id,1,1,view.range,VK_IMAGE_LAYOUT_GENERAL);
    auto resource=std::make_shared<efmi::frontend::NativeResource>();resource->bound=resource->image_safe=true;resource->keep=std::make_shared<int>(1);
    resource->image=uint64_t(view.image);resource->image_view=uint64_t(handle);resource->image_view_id=view.id;resource->identity=(uint64_t(image.id)<<2)|2;resource->usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    efmi::assets::FilePlan plan;plan.items.emplace_back();auto& item=plan.items[0];item.operation=efmi::assets::FileOperation::NativeBind;
    item.native_target.kind=efmi::frontend::NativeBinding::ColorTarget;item.native_resource=resource;
    DrawRec draw;draw.kind=DK_DRAW;draw.targets=1;NativeTargetPlan prepared;std::string why;
    auto prepare=[&]{prepared={};return prepare_native_targets(&d,&cb,draw,plan,prepared,why);};output_commands.clear();
    check(!prepare() && output_commands.empty(),"output preflight requires a complete known pipeline attachment contract");
    auto recipe=std::make_shared<GraphicsRecipe>();recipe->geometry_complete=true;recipe->dynamic_rendering=true;
    recipe->rendering_colors={VK_FORMAT_R8G8B8A8_UNORM};draw.pipeline=h<VkPipeline>(9508);d.pipes[draw.pipeline].recipe=recipe;
    check(prepare() && prepared.events[0]->change && output_commands.empty() && cb.active_targets==1 && !cb.native_target_state,"output preflight retains a complete destination without emitting commands or changing active context");
    recipe->geometry_complete=false;check(!prepare(),"unknown pipeline extensions cannot preserve attachment mapping across a split");recipe->geometry_complete=true;
    cb.execution_scopes.known=false;check(!prepare(),"untracked dynamic attachment mapping cannot be reset by a private Begin");cb.execution_scopes.reset();
    cb.execution_scopes.query(1,0,0,true);check(!prepare() && output_commands.empty(),"active query refuses before ending the application pass");cb.execution_scopes.reset();
    cb.execution_scopes.transform(true);check(!prepare(),"active feedback refuses output changes");cb.execution_scopes.reset();
    cb.target_contexts[0].flags=VK_RENDERING_SUSPENDING_BIT;check(!prepare(),"segmented rendering cannot be privately split");cb.target_contexts[0].flags=0;
    cb.target_contexts[0].dynamic_colors[0].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;check(!prepare(),"discarded source cannot become a LOAD continuation");cb.target_contexts[0].dynamic_colors[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    cb.target_contexts[0].dynamic_colors[0].resolveMode=VK_RESOLVE_MODE_AVERAGE_BIT;check(!prepare(),"resolve must not run at an invented intermediate end");cb.target_contexts[0].dynamic_colors[0].resolveMode=VK_RESOLVE_MODE_NONE;
    cb.target_contexts[0].has_depth=true;check(!prepare(),"depth scope needs its own preservation adapter");cb.target_contexts[0].has_depth=false;
    cb.secondary=true;check(!prepare(),"secondary recording cannot replace its parent's output pass");cb.secondary=false;
    d.synchronization2_enabled=true;check(!prepare(),"output dependencies cannot widen stage-limited signal graphs");d.synchronization2_enabled=false;
    d.application_queue_count=2;check(!prepare(),"output movement cannot invent cross-queue ownership");d.application_queue_count=1;
    d.images[view.image].extent.width=32;check(!prepare(),"destination must cover the complete render area");d.images[view.image].extent.width=64;
    cb.image_layouts.reset();check(!prepare(),"destination layout needs recording-local proof");cb.image_layouts.set(image.id,1,1,view.range,VK_IMAGE_LAYOUT_GENERAL);
    auto extra=plan.items[0];extra.native_target.slot=1;plan.items.push_back(extra);
    check(!prepare() && output_commands.empty() && cb.active_targets==1 && !cb.native_target_state,"later invalid output event refuses the whole plan before any earlier event records");plan.items.pop_back();
    check(prepare(),"valid output plan remains available after refused attempts");
    record_native_target(&d,&cb,prepared,0);VkImageLayout actual=VK_IMAGE_LAYOUT_UNDEFINED;
    check(output_commands==std::vector<char>({'E','D','B'}) && output_view==handle && output_load==VK_ATTACHMENT_LOAD_OP_LOAD && cb.active_targets==2 &&
          cb.image_layouts.get(image.id,1,1,view.range,actual) && actual==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          "actual output switch ends stored source, orders hazards and begins a LOAD continuation on the new view");
    native_targets_end(&d,&cb);
    check(output_commands.back()=='D' && !cb.native_target_state && cb.image_layouts.get(image.id,1,1,view.range,actual) && actual==VK_IMAGE_LAYOUT_GENERAL,
          "application end restores the introduced destination's exact layout");
    cb.target_contexts={target};cb.active_targets=1;output_commands.clear();plan.items[0].native_resource.reset();
    check(!prepare() && output_commands.empty(),"null transition cannot infer an enabled unused-attachments feature");
    d.dynamic_unused_attachments=true;check(prepare(),"application-enabled feature permits actual null output preparation");
    record_native_target(&d,&cb,prepared,0);
    check(output_commands==std::vector<char>({'E','D','B'}) && !output_view && cb.native_target_state->format==VK_FORMAT_R8G8B8A8_UNORM &&
          !cb.target_contexts.back().dynamic_colors[0].imageView,"unbinding uses an actual null view while retaining pipeline compatibility proof");
    plan.items[0].native_resource=resource;check(prepare(),"retained destination can be restored after null without inventing a format");
    record_native_target(&d,&cb,prepared,0);native_targets_end(&d,&cb);
    check(output_view==handle && cb.image_layouts.get(image.id,1,1,view.range,actual) && actual==VK_IMAGE_LAYOUT_GENERAL,
          "null then restore retains the destination layout restoration contract");
    cb.target_contexts={target};cb.active_targets=1;output_commands.clear();plan.items[0].native_resource.reset();
    for(uint32_t slot=1;slot<8;++slot){auto absent=plan.items[0];absent.native_target.slot=slot;plan.items.push_back(absent);}
    auto absent=plan.items[0];absent.native_target.kind=efmi::frontend::NativeBinding::DepthTarget;plan.items.push_back(absent);
    check(prepare(),"original all-output unbind accepts proven absent extra colors and depth");
    for(size_t i=0;i<plan.items.size();++i)record_native_target(&d,&cb,prepared,i);
    check(output_commands==std::vector<char>({'E','D','B'}) && !output_view,"absent built-in entries emit no extra pass breaks");
    native_targets_end(&d,&cb);
    cb.execution_scopes.transform(true);cb.reset_recording();check(cb.execution_scopes.splittable() && !cb.native_target_state,"recording reset drops transient output and execution scopes");
}
