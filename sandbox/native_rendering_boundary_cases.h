namespace rendering_boundary_fixture {
static unsigned barriers=0;
static VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,
    uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*)
{ ++barriers; }
}
static void native_rendering_boundary_cases()
{
    using namespace rendering_boundary_fixture;
    Device d;d.native_binding_order=true;d.track_image_layouts=true;d.native_barrier=barrier;d.d.CmdPipelineBarrier=barrier;
    CmdBuf cb;VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};info.flags=VK_RENDERING_SUSPENDING_BIT;barriers=0;
    native_images_begin(&d,&cb,&info);const auto state=cb.native_image_state;
    check(barriers==1 && cb.native_graphics_ordered && state,"the first rendering segment establishes the actual pre-pass proof");
    native_images_end(&d,&cb);
    check(barriers==1 && cb.native_graphics_ordered && cb.native_image_state==state,
          "suspending a pass preserves scope and inserts no forbidden closing synchronization");
    info.flags=VK_RENDERING_RESUMING_BIT|VK_RENDERING_SUSPENDING_BIT;native_images_begin(&d,&cb,&info);
    check(barriers==1 && cb.native_graphics_ordered && cb.native_image_state==state,
          "a middle continuation inherits its real proof without a new pre-pass barrier");
    native_images_end(&d,&cb);
    check(barriers==1 && cb.native_image_state==state,"a resumed segment can suspend again without synchronization");
    info.flags=VK_RENDERING_RESUMING_BIT;native_images_begin(&d,&cb,&info);
    check(barriers==1 && cb.native_graphics_ordered && cb.native_image_state==state,"the final continuation retains initial state");
    native_images_end(&d,&cb);
    check(barriers==2 && !cb.native_graphics_ordered && !cb.native_image_state,"the final end alone closes and releases the native scope");
    CmdBuf fresh;native_images_begin(&d,&fresh,&info);
    check(barriers==2 && !fresh.native_graphics_ordered && !fresh.native_image_state,
          "a different recording cannot manufacture the suspended recording's proof or insert a resume barrier");
    info.flags=VK_RENDERING_SUSPENDING_BIT;native_images_begin(&d,&cb,&info);cb.reset_recording();
    check(!cb.native_graphics_ordered && !cb.native_image_state,"reset discards continuation state and dependency proof");
    const unsigned before=barriers;VkRenderPassBeginInfo classic{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};native_images_begin(&d,&cb,&classic);
    native_images_end(&d,&cb);
    check(barriers==before+2 && !cb.native_image_state && !cb.native_graphics_ordered,
          "a following classic pass clears segmented state and records its normal boundaries");
}

static void native_segment_transition_cases()
{
    Device d;d.application_queue_count=1;d.material_queues.push_back(h<VkQueue>(9101));
    const auto pipeline=h<VkPipeline>(9102);d.pipes[pipeline].id=1;
    d.native_image_cache=std::make_shared<NativeImageCache>();auto access=std::make_shared<NativeImageCache::Access>();
    access->images.push_back({{0,2,1,0,true,false,false,false,2,0},false});d.native_image_cache->pipelines[1]=access;
    auto image=std::make_shared<efmi::frontend::NativeResource>();image->identity=6;image->image=9103;image->image_view=9104;
    image->keep=std::make_shared<int>(1);image->image_safe=true;image->view_type=VK_IMAGE_VIEW_TYPE_2D;image->view_format=VK_FORMAT_R32_UINT;
    image->usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image->levels=image->layers=image->mip_count=image->layer_count=1;image->aspect=VK_IMAGE_ASPECT_COLOR_BIT;
    CmdBuf cb;cb.native_rendering_flags=VK_RENDERING_SUSPENDING_BIT;
    cb.image_layouts.set(1,1,1,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    DrawRec draw;draw.kind=DK_DRAW;draw.pipeline=pipeline;efmi::frontend::NativeBinding target;target.binding=2;
    NativeImagePlan plan;VkImageLayout layout;std::string why;
    auto accepted=[&](){plan={};why.clear();return native_image_binding(&d,&cb,draw,target,image,plan,layout,why);};
    check(!accepted() && plan.cells.empty() && why.find("suspended")!=std::string::npos,
          "a suspended segment cannot start a temporary layout that another recording would need to restore");
    cb.native_rendering_flags=VK_RENDERING_RESUMING_BIT;
    check(!accepted() && plan.cells.empty(),"resumed segments cannot begin temporary transitions before rendering either");
    cb.image_layouts.set(1,1,1,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    check(accepted() && plan.cells.empty() && layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          "already readable non-attachment images need no temporary transition across rendering segments");
    cb.native_rendering_flags=0;cb.image_layouts.set(1,1,1,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    check(accepted() && plan.cells.size()==1,"ordinary complete passes retain their bounded pre-pass transition support");
}
