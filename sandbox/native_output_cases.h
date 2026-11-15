#include "../layer/src/native_targets.h"
#include "../layer/src/native_images.h"
static std::vector<char> output_commands;
static VkImageView output_view;
static VkAttachmentLoadOp output_load,output_depth_load;
static VkImageView output_depth,output_stencil;
static VkAttachmentLoadOp output_stencil_load;
static void VKAPI_CALL output_begin(VkCommandBuffer,const VkRenderingInfo* info){output_commands.push_back('B');output_view=info->colorAttachmentCount?info->pColorAttachments[0].imageView:VK_NULL_HANDLE;output_load=info->colorAttachmentCount?info->pColorAttachments[0].loadOp:VK_ATTACHMENT_LOAD_OP_DONT_CARE;output_depth=info->pDepthAttachment?info->pDepthAttachment->imageView:VK_NULL_HANDLE;output_depth_load=info->pDepthAttachment?info->pDepthAttachment->loadOp:VK_ATTACHMENT_LOAD_OP_DONT_CARE;output_stencil=info->pStencilAttachment?info->pStencilAttachment->imageView:VK_NULL_HANDLE;output_stencil_load=info->pStencilAttachment?info->pStencilAttachment->loadOp:VK_ATTACHMENT_LOAD_OP_DONT_CARE;}
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
    image.mem=h<VkDeviceMemory>(image.id+20000);image.mem_size=8192;d.images[view.image]=image;d.image_views[handle]=view;cb.image_layouts.set(image.id,1,1,view.range,VK_IMAGE_LAYOUT_GENERAL);
    auto resource=std::make_shared<efmi::frontend::NativeResource>();resource->bound=resource->image_safe=true;resource->keep=std::make_shared<int>(1);
    resource->image=uint64_t(view.image);resource->image_view=uint64_t(handle);resource->image_view_id=view.id;resource->identity=(uint64_t(image.id)<<2)|2;resource->usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    efmi::assets::FilePlan plan;plan.items.emplace_back();auto& item=plan.items[0];item.operation=efmi::assets::FileOperation::NativeBind;
    item.native_target.kind=efmi::frontend::NativeBinding::ColorTarget;item.native_resource=resource;
    DrawRec draw;draw.kind=DK_DRAW;draw.targets=1;NativeTargetPlan prepared;std::string why;
    auto prepare=[&]{prepared={};return prepare_native_targets(&d,&cb,draw,plan,prepared,why);};output_commands.clear();
    check(!prepare() && output_commands.empty(),"output preflight requires a complete known pipeline attachment contract");
    auto recipe=std::make_shared<GraphicsRecipe>();recipe->multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;recipe->geometry_complete=true;recipe->dynamic_rendering=true;
    recipe->rendering_colors={VK_FORMAT_R8G8B8A8_UNORM};draw.pipeline=h<VkPipeline>(9508);d.pipes[draw.pipeline].recipe=recipe;
    check(prepare() && prepared.events[0]->change && output_commands.empty() && cb.active_targets==1 && !cb.native_target_state,"output preflight retains a complete destination without emitting commands or changing active context");
    {
        const auto original_image=d.images[view.image];const auto memory=h<VkDeviceMemory>(9900);const auto buffer=h<VkBuffer>(9901);
        d.physical_properties.limits.bufferImageGranularity=256;
        auto& target_image=d.images[view.image];target_image.mem=memory;target_image.mem_off=4096;target_image.mem_size=1024;
        BufInfo input;input.id=9902;input.size=input.mem_size=256;input.mem=memory;input.mem_off=4096;
        input.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;d.bufs[buffer]=input;
        auto descriptors=std::make_shared<DescriptorSetInfo>();
        DescriptorElement value;value.known=true;value.type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        value.buffer=buffer;value.buffer_id=input.id;value.range=256;descriptors->values[{0,0}]=value;
        cb.sets[0].pushed=descriptors;
        check(!prepare() && output_commands.empty() && why.find("memory")!=std::string::npos,
              "output refuses a distinct uniform buffer sharing its memory before recording any command");
        d.bufs[buffer].mem_off=5120;check(prepare(),"same allocation permits disjoint buffer and image granularity pages");
        target_image.mem_size=1000;d.bufs[buffer].mem_off=5100;
        check(!prepare(),"disjoint raw bytes still alias a buffer-image granularity page");
        d.bufs[buffer].mem_off=5120;check(prepare(),"rounding preserves a genuinely disjoint following page");
        d.bufs[buffer].mem_off=UINT64_MAX-4;check(!prepare(),"overflow cannot manufacture nonoverlapping allocation spans");
        d.bufs[buffer].mem_off=5120;target_image.mem_size=0;check(!prepare(),"unknown image requirements cannot prove output independence");target_image.mem_size=1024;
        d.bufs[buffer]=input;
        auto retained=std::make_shared<efmi::frontend::NativeResource>();retained->bound=true;retained->keep=std::make_shared<int>(1);
        retained->buffer=uint64_t(buffer);retained->memory=uint64_t(memory);retained->memory_offset=4096;retained->memory_size=256;
        descriptors->values[{0,0}].native=retained;d.bufs.erase(buffer);
        check(!prepare(),"retained native buffer still conflicts after its application map entry is destroyed");
        retained->memory_offset=8192;check(prepare(),"retained disjoint buffer remains usable after logical destruction");
        descriptors->values[{0,0}].native.reset();descriptors->values[{0,0}].buffer=VK_NULL_HANDLE;
        descriptors->values[{0,0}].native_request=retained;retained->memory_offset=4096;
        check(!prepare(),"suspended native request still protects its future input storage from output writes");
        retained->memory_offset=8192;check(prepare(),"independent suspended request permits output switching");
        descriptors->values[{0,0}].native_request.reset();
        auto fallback=std::make_shared<DescriptorElement>();fallback->known=true;fallback->buffer=buffer;fallback->native=retained;
        descriptors->values[{0,0}].application=fallback;retained->memory_offset=4096;
        check(!prepare(),"saved application fallback retains its storage conflict even behind a null descriptor");
        retained->memory_offset=8192;check(prepare(),"independent saved fallback permits output switching");
        descriptors->values[{0,0}].application.reset();
        cb.sets[0]={};cb.native_images=true;cb.native_image_state=std::make_shared<NativeImageState>();
        auto restoring=std::make_shared<efmi::frontend::NativeResource>(*retained);restoring->image=9908;
        restoring->buffer=0;restoring->memory_offset=4096;
        cb.native_image_state->active.cells[{9908,0,0}]={restoring,0,0,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,false};
        check(!prepare(),"pending layout restoration conflicts even when its descriptor is no longer bound");
        restoring->memory_offset=8192;check(prepare(),"independent persistent images and restoration cells permit output switching");
        cb.native_images=false;cb.native_image_state.reset();
        cb.sets[0]={};retained->memory_offset=4096;
        auto future=plan.items[0];future.native_target.kind=efmi::frontend::NativeBinding::Constant;future.native_resource=retained;
        plan.items.push_back(future);check(!prepare() && output_commands.empty(),"later constant assignment refuses the whole output list on memory overlap");
        std::swap(plan.items[0],plan.items[1]);check(!prepare(),"earlier constant assignment cannot evade whole-list memory proof");
        std::swap(plan.items[0],plan.items[1]);
        auto preserved=target;preserved.dynamic_colors.push_back(color);preserved.dynamic_colors[1].imageView=h<VkImageView>(9910);
        preserved.views.resize(4);preserved.views[2]=target.views[0];preserved.views[2].handle=preserved.dynamic_colors[1].imageView;
        preserved.views[2].view.image=h<VkImage>(9911);preserved.views[2].image=target_image;preserved.views[2].image.id=9912;
        cb.target_contexts[0]=preserved;recipe->rendering_colors.push_back(VK_FORMAT_R8G8B8A8_UNORM);target_image.mem=h<VkDeviceMemory>(9913);
        check(!prepare(),"future buffer cannot alias a preserved output while a different output changes");
        plan.items[0].native_resource.reset();d.dynamic_unused_attachments=true;
        check(!prepare(),"null-only output events still check future input memory against preserved outputs");
        d.dynamic_unused_attachments=false;plan.items[0].native_resource=resource;recipe->rendering_colors.pop_back();
        cb.target_contexts[0]=target;target_image.mem=memory;plan.items.pop_back();
        d.bufs[buffer]=input;draw.vb[0].buf=buffer;
        check(!prepare(),"vertex input memory cannot be overwritten by an introduced output");draw.vb[0]={};
        draw.ib=buffer;check(!prepare(),"index input memory cannot alias an introduced output");draw.ib=VK_NULL_HANDLE;
        draw.indirect=buffer;check(!prepare(),"indirect arguments cannot alias output memory");draw.indirect=VK_NULL_HANDLE;
        draw.indirect_count=buffer;check(!prepare(),"indirect count memory participates in output preflight");draw.indirect_count=VK_NULL_HANDLE;
        const auto sampled=h<VkImage>(9903);const auto sampled_view=h<VkImageView>(9904);
        ImageInfo sample=target_image;sample.id=9905;sample.mem_off=8192;d.images[sampled]=sample;
        ImageViewInfo sv=view;sv.id=9906;sv.image=sampled;sv.image_id=sample.id;d.image_views[sampled_view]=sv;
        DescriptorElement texture;texture.known=true;texture.type=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;texture.image_view=sampled_view;texture.image_view_id=sv.id;
        descriptors->values[{0,0}]=texture;cb.sets[0].pushed=descriptors;
        check(prepare(),"distinct sampled and output images may share disjoint storage in one allocation");
        d.images[sampled].mem_off=4096;check(!prepare(),"different image handles do not hide shared descriptor storage");
        cb.sets[0]={};d.images.erase(sampled);d.image_views.erase(sampled_view);
        cb.target_contexts[0].views[0].image=target_image;
        check(!prepare(),"switching output cannot overwrite a distinct original attachment's backing memory");
        cb.target_contexts[0]=target;check(prepare(),"independent output remains available after all alias refusals");
        cb.native_target_state=prepared.state;NativeTargetState::Saved saved;saved.view.known=true;saved.view.view.image=sampled;saved.view.image=target_image;
        cb.native_target_state->restore[sampled_view]=saved;
        check(!prepare(),"restoration snapshots retain storage conflicts even after an output is no longer active");
        cb.native_target_state.reset();d.bufs.erase(buffer);d.images[view.image]=original_image;
    }
    recipe->geometry_complete=false;check(!prepare(),"unknown pipeline extensions cannot preserve attachment mapping across a split");recipe->geometry_complete=true;
    cb.execution_scopes.known=false;check(!prepare(),"untracked dynamic attachment mapping cannot be reset by a private Begin");cb.execution_scopes.reset();
    cb.execution_scopes.query(1,0,0,true);check(!prepare() && output_commands.empty(),"active query refuses before ending the application pass");cb.execution_scopes.reset();
    cb.execution_scopes.transform(true);check(!prepare(),"active feedback refuses output changes");cb.execution_scopes.reset();
    cb.target_contexts[0].flags=VK_RENDERING_SUSPENDING_BIT;check(!prepare(),"segmented rendering cannot be privately split");cb.target_contexts[0].flags=0;
    cb.target_contexts[0].dynamic_colors[0].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;check(!prepare(),"discarded source cannot become a LOAD continuation");cb.target_contexts[0].dynamic_colors[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    cb.target_contexts[0].dynamic_colors[0].resolveMode=VK_RESOLVE_MODE_AVERAGE_BIT;check(!prepare(),"resolve must not run at an invented intermediate end");cb.target_contexts[0].dynamic_colors[0].resolveMode=VK_RESOLVE_MODE_NONE;
    cb.target_contexts[0].has_depth=true;check(!prepare(),"missing depth view metadata cannot preserve an added depth scope");cb.target_contexts[0].has_depth=false;
    cb.secondary=true;check(!prepare(),"secondary recording cannot replace its parent's output pass");cb.secondary=false;
    d.synchronization2_enabled=true;check(!prepare(),"output dependencies cannot widen stage-limited signal graphs");d.synchronization2_enabled=false;
    d.application_queue_count=2;check(!prepare(),"output movement cannot invent cross-queue ownership");d.application_queue_count=1;
    d.images[view.image].extent.width=32;check(!prepare(),"destination must cover the complete render area");d.images[view.image].extent.width=64;
    cb.image_layouts.reset();check(!prepare(),"destination layout needs recording-local proof");cb.image_layouts.set(image.id,1,1,view.range,VK_IMAGE_LAYOUT_GENERAL);
    auto extra=plan.items[0];extra.native_target.slot=1;plan.items.push_back(extra);
    check(!prepare() && output_commands.empty() && cb.active_targets==1 && !cb.native_target_state,"later invalid output event refuses the whole plan before any earlier event records");plan.items.pop_back();
    check(prepare(),"valid output plan remains available after refused attempts");
    const NativeImagePlan::Key late_key{0xabc2,0,0};
    cb.native_image_state=std::make_shared<NativeImageState>();
    cb.native_image_state->active.layouts[late_key]=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    cb.native_image_state->required[late_key]=VK_IMAGE_LAYOUT_GENERAL;
    record_native_target(&d,&cb,prepared,0);VkImageLayout actual=VK_IMAGE_LAYOUT_UNDEFINED;
    check(cb.native_image_state && cb.native_image_state->active.layouts.count(late_key) &&
          cb.native_image_state->required.count(late_key) && cb.native_image_state->attachments.count(image.id),
          "output recording preserves image requests added after target preflight and updates attachment scope");
    check(output_commands==std::vector<char>({'E','D','B'}) && output_view==handle && output_load==VK_ATTACHMENT_LOAD_OP_LOAD && cb.active_targets==2 &&
          cb.image_layouts.get(image.id,1,1,view.range,actual) && actual==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          "actual output switch ends stored source, orders hazards and begins a LOAD continuation on the new view");
    native_targets_end(&d,&cb);
    check(output_commands.back()=='D' && !cb.native_target_state && cb.image_layouts.get(image.id,1,1,view.range,actual) && actual==VK_IMAGE_LAYOUT_GENERAL,
          "application end restores the introduced destination's exact layout");
    auto with_depth=target;with_depth.has_depth=true;with_depth.views.resize(4);
    with_depth.depth.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;with_depth.depth.imageView=h<VkImageView>(9603);
    with_depth.depth.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    with_depth.depth.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;with_depth.depth.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    auto& depth_view=with_depth.views[2];depth_view.known=true;depth_view.handle=with_depth.depth.imageView;
    depth_view.view.image=h<VkImage>(9602);depth_view.view.format=VK_FORMAT_D32_SFLOAT;
    depth_view.image.id=9601;depth_view.image.samples=VK_SAMPLE_COUNT_1_BIT;
    cb.target_contexts={with_depth};cb.active_targets=1;recipe->rendering_depth=VK_FORMAT_D32_SFLOAT;
    cb.native_image_state=std::make_shared<NativeImageState>();output_commands.clear();
    check(prepare() && prepared.events[0]->targets.depth.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD &&
          prepared.events[0]->images->attachments.count(9601) && prepared.events[0]->images->attachments.count(image.id),
          "color output preflight preserves exact depth LOAD and both actual attachment conflicts");
    cb.target_contexts[0].depth.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;check(!prepare(),"discarded depth cannot become a preserved LOAD continuation");cb.target_contexts[0]=with_depth;
    cb.target_contexts[0].depth.resolveMode=VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;check(!prepare(),"depth resolve cannot run at an invented intermediate boundary");cb.target_contexts[0]=with_depth;
    cb.target_contexts[0].views[2].known=false;check(!prepare(),"unknown depth view cannot be silently copied into a continuation");cb.target_contexts[0]=with_depth;
    recipe->rendering_depth=VK_FORMAT_D16_UNORM;check(!prepare(),"preserved depth requires the exact complete pipeline format contract");recipe->rendering_depth=VK_FORMAT_D32_SFLOAT;
    cb.target_contexts[0].views[2].view.image=view.image;check(!prepare(),"new color target cannot alias the preserved depth image");cb.target_contexts[0]=with_depth;
    cb.target_contexts[0].depth.imageView=VK_NULL_HANDLE;cb.target_contexts[0].depth.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    check(!prepare(),"declared depth with no actual view cannot infer unused-attachments permission");d.dynamic_unused_attachments=true;
    check(prepare() && !prepared.events[0]->images->attachments.count(9601),"enabled unused-attachments feature keeps null depth distinct from the declared format");
    d.dynamic_unused_attachments=false;cb.target_contexts[0]=with_depth;
    auto depth_unbind=plan.items[0];depth_unbind.native_target.kind=efmi::frontend::NativeBinding::DepthTarget;depth_unbind.native_resource.reset();
    plan.items.push_back(depth_unbind);check(!prepare() && output_commands.empty(),"unsupported active depth unbind refuses the whole output plan before its color event");plan.items.pop_back();
    check(prepare(),"preserved depth plan survives prior rejected attempts");record_native_target(&d,&cb,prepared,0);
    check(output_depth==with_depth.depth.imageView && output_depth_load==VK_ATTACHMENT_LOAD_OP_LOAD &&
          cb.native_image_state->attachments.count(9601),"private Begin actually retains the stored depth view and LOAD operation");
    native_targets_end(&d,&cb);cb.target_contexts={with_depth};cb.active_targets=1;d.dynamic_unused_attachments=true;
    plan.items[0].native_resource.reset();check(prepare() && prepared.events[0]->images->attachments.size()==1 && prepared.events[0]->images->attachments.count(9601),
          "null color preserves depth as the only active attachment conflict");
    plan.items[0].native_resource=resource;d.dynamic_unused_attachments=false;recipe->rendering_depth=VK_FORMAT_UNDEFINED;
    cb.native_image_state.reset();

    cb.target_contexts={target};cb.active_targets=1;output_commands.clear();plan.items[0].native_resource.reset();
    check(!prepare() && output_commands.empty(),"null transition cannot infer an enabled unused-attachments feature");
    d.dynamic_unused_attachments=true;check(prepare(),"application-enabled feature permits actual null output preparation");
    record_native_target(&d,&cb,prepared,0);
    check(output_commands==std::vector<char>({'E','D','B'}) && !output_view && cb.native_target_state->colors[0].format==VK_FORMAT_R8G8B8A8_UNORM &&
          !cb.target_contexts.back().dynamic_colors[0].imageView,"unbinding uses an actual null view while retaining pipeline compatibility proof");
    const auto original_plan=plan;plan.items[0].native_resource=resource;plan.items.push_back(original_plan.items[0]);
    check(prepare() && prepared.events[0] && prepared.events[1] && prepared.events[1]->change &&
          !prepared.events[1]->targets.dynamic_colors[0].imageView,"initially null slot keeps a later null after a same-plan nonnull destination assignment");
    plan=original_plan;
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
