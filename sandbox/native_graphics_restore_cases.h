#include "../layer/src/native_graphics_restore.h"

namespace graphics_restore_test {
static std::vector<int> calls;
static VkPipeline selected;
static uint32_t vertex_slot,index_type;
static VkDeviceSize vertex_offset,index_offset;
static float width,blend_red;
static VkViewport viewport;
static VkRect2D scissor;
static std::array<uint32_t,2> stencil{};
static VKAPI_ATTR void VKAPI_CALL pipeline(VkCommandBuffer,VkPipelineBindPoint point,VkPipeline value){
    check(point==VK_PIPELINE_BIND_POINT_GRAPHICS,"Graphics restore binds the graphics pipeline");selected=value;calls.push_back(100);
}
static VKAPI_ATTR void VKAPI_CALL sets(VkCommandBuffer,VkPipelineBindPoint point,VkPipelineLayout,uint32_t,uint32_t,const VkDescriptorSet*,uint32_t,const uint32_t*){
    check(point==VK_PIPELINE_BIND_POINT_GRAPHICS,"Graphics restore preserves its descriptor bind point");calls.push_back(101);
}
static VKAPI_ATTR void VKAPI_CALL vertices(VkCommandBuffer,uint32_t first,uint32_t,const VkBuffer*,const VkDeviceSize* offsets){vertex_slot=first;vertex_offset=*offsets;calls.push_back(102);}
static VKAPI_ATTR void VKAPI_CALL indices(VkCommandBuffer,VkBuffer,VkDeviceSize offset,VkIndexType type){index_offset=offset;index_type=type;calls.push_back(103);}
static VKAPI_ATTR void VKAPI_CALL viewports(VkCommandBuffer,uint32_t,uint32_t,const VkViewport* value){viewport=*value;calls.push_back(0);}
static VKAPI_ATTR void VKAPI_CALL scissors(VkCommandBuffer,uint32_t,uint32_t,const VkRect2D* value){scissor=*value;calls.push_back(1);}
static VKAPI_ATTR void VKAPI_CALL line(VkCommandBuffer,float value){width=value;calls.push_back(2);}
static VKAPI_ATTR void VKAPI_CALL blend(VkCommandBuffer,const float* values){blend_red=values[0];calls.push_back(4);}
static VKAPI_ATTR void VKAPI_CALL reference(VkCommandBuffer,VkStencilFaceFlags face,uint32_t value){stencil[face==VK_STENCIL_FACE_FRONT_BIT?0:1]=value;calls.push_back(8);}
}
static void native_graphics_restore_cases(){
    namespace t=graphics_restore_test;
    Device d;CmdBuf cb;NativeGraphicsRestore result;std::string why;g_cfg.track_resources=true;
    d.d.CmdBindPipeline=t::pipeline;d.d.CmdBindDescriptorSets=t::sets;d.d.CmdBindVertexBuffers=t::vertices;d.d.CmdBindIndexBuffer=t::indices;
    d.d.CmdSetViewport=t::viewports;d.d.CmdSetScissor=t::scissors;d.d.CmdSetLineWidth=t::line;d.d.CmdSetBlendConstants=t::blend;d.d.CmdSetStencilReference=t::reference;
    cb.gfx_pipeline=h<VkPipeline>(46001);cb.gfx_bound=h<VkPipeline>(46002);
    auto recipe=std::make_shared<GraphicsRecipe>();recipe->geometry_complete=true;recipe->viewport_count=recipe->scissor_count=2;
    recipe->dynamic={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_LINE_WIDTH,VK_DYNAMIC_STATE_BLEND_CONSTANTS,VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    d.pipes[cb.gfx_pipeline].recipe=recipe;
    VkViewport v[]{ {0,0,32,32,0,1},{1,2,30,29,.1f,.9f} };VkRect2D s[]{ {{0,0},{32,32}},{{3,4},{20,21}} };
    cb.graphics_dynamic.set_viewports(1,1,v+1);cb.graphics_dynamic.set_scissors(0,2,s);
    check(!prepare_native_graphics_restore(&d,&cb,result,why),"Graphics restoration refuses a missing earlier viewport");
    cb.graphics_dynamic.set_viewports(0,1,v);cb.graphics_dynamic.line_width=2;cb.graphics_dynamic.blend_constants={.25f,.5f,.75f,1};
    cb.graphics_dynamic.known=(1u<<VK_DYNAMIC_STATE_LINE_WIDTH)|(1u<<VK_DYNAMIC_STATE_BLEND_CONSTANTS);
    cb.graphics_dynamic.set_stencil(2,VK_STENCIL_FACE_FRONT_BIT,7);
    check(!prepare_native_graphics_restore(&d,&cb,result,why),"Graphics restoration refuses a missing back stencil face");
    cb.graphics_dynamic.set_stencil(2,VK_STENCIL_FACE_BACK_BIT,11);
    cb.vb[3].buf=h<VkBuffer>(46003);cb.vb[3].off=24;d.bufs[cb.vb[3].buf].size=128;d.bufs[cb.vb[3].buf].usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    cb.ib=h<VkBuffer>(46004);cb.ib_off=12;cb.index_size=2;d.bufs[cb.ib].size=64;d.bufs[cb.ib].usage=VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    check(prepare_native_graphics_restore(&d,&cb,result,why),"Complete core graphics state prepares without recording");
    check(t::calls.empty(),"Graphics restoration preflight emits no commands");
    auto effective=std::make_shared<NativeGeometryState>();auto vertex=std::make_shared<efmi::frontend::NativeResource>();
    vertex->bound=true;vertex->buffer=46010;vertex->size=128;vertex->offset=40;vertex->usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;vertex->keep=std::make_shared<int>(1);
    effective->vertices[3]=vertex;cb.native_geometry=effective;NativeGraphicsRestore projected;
    check(prepare_native_graphics_restore(&d,&cb,projected,why) && projected.vertices[0].buffer==VkBuffer(46010) && projected.vertices[0].offset==40,
        "Graphics restoration uses the effective retained native vertex binding");
    std::weak_ptr<const void> kept=vertex->keep;cb.native_geometry.reset();effective.reset();vertex.reset();
    check(!kept.expired(),"Prepared graphics restore retains effective native IA owners");projected={};
    check(kept.expired(),"Retired graphics restoration releases its effective IA owners");
    NativeDescriptorRestore::Set set;set.slot=2;set.handle=h<VkDescriptorSet>(46011);set.layout=h<VkPipelineLayout>(46012);result.sets.push_back(set);
    cb.graphics_dynamic.line_width=9;cb.graphics_dynamic.viewports[1].width=1;
    result.record(&d,h<VkCommandBuffer>(46005));
    check(t::selected==cb.gfx_bound && t::width==2 && t::blend_red==.25f,"Graphics restoration uses immutable values and the selected native pipeline");
    check(t::viewport.width==30 && t::scissor.offset.x==3 && t::stencil[0]==7 && t::stencil[1]==11,"Graphics restoration preserves both viewport slots and stencil faces");
    check(t::vertex_slot==3 && t::vertex_offset==24 && t::index_offset==12 && t::index_type==VK_INDEX_TYPE_UINT16,"Graphics restoration preserves IA slots and byte offsets");
    const auto bind=std::find(t::calls.begin(),t::calls.end(),100);
    check(bind!=t::calls.end() && std::find(bind,t::calls.end(),4)!=t::calls.end(),"Required dynamic state is replayed after pipeline rebinding");
    result.record(&d,h<VkCommandBuffer>(46005),h<VkPipeline>(46006));check(t::selected==h<VkPipeline>(46006),"Pass-compatible pipeline selection can replace the restored handle");
    const auto saved=result.pipeline;cb.graphics_dynamic.known=0;
    check(!prepare_native_graphics_restore(&d,&cb,result,why) && result.pipeline==saved,"Failed graphics preflight preserves an existing restoration");
    cb.graphics_dynamic=result.dynamic;recipe->dynamic.push_back(VK_DYNAMIC_STATE_CULL_MODE);
    check(!prepare_native_graphics_restore(&d,&cb,result,why),"Untracked extended dynamic state refuses explicitly");recipe->dynamic.pop_back();
    cb.m2_extended_bind=true;check(!prepare_native_graphics_restore(&d,&cb,result,why),"Unproved extended IA windows refuse restoration");cb.m2_extended_bind=false;
    on_unsupported_descriptor_bind(&cb,VK_PIPELINE_BIND_POINT_GRAPHICS,"test_unknown");
    check(!cb.graphics_sets_complete && !prepare_native_graphics_restore(&d,&cb,result,why),"Untracked graphics descriptors cannot restore a partial snapshot");
    cb.reset_recording();check(cb.graphics_sets_complete && cb.graphics_dynamic.complete && !cb.graphics_dynamic.viewport_mask,"Reset discards old graphics state completeness and values");
    cb.graphics_dynamic.set_scissors(16,1,s);check(!cb.graphics_dynamic.complete,"Viewport tracking bounds never silently drop state");
    t::calls.clear();
}
