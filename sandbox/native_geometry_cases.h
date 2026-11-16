#include "../layer/src/native_geometry.h"

static void native_geometry_cases()
{
    using Operation=efmi::assets::FileOperation;
    Device device;CmdBuf cb;DrawRec draw;draw.pipeline=h<VkPipeline>(917);draw.kind=DK_DRAW;draw.count=3;draw.instances=1;
    auto recipe=std::make_shared<GraphicsRecipe>();recipe->geometry_complete=true;
    recipe->vertex.bindings.push_back({0,8,VK_VERTEX_INPUT_RATE_VERTEX});
    recipe->vertex.attributes.push_back({0,0,VK_FORMAT_R32G32_SFLOAT,0});
    device.pipes[draw.pipeline].recipe=recipe;
    device.d.CmdBindVertexBuffers=[](VkCommandBuffer,uint32_t,uint32_t,const VkBuffer*,const VkDeviceSize*){};
    device.d.CmdBindIndexBuffer=[](VkCommandBuffer,VkBuffer,VkDeviceSize,VkIndexType){};
    auto vertex=std::make_shared<efmi::frontend::NativeResource>();vertex->bound=true;vertex->buffer=918;vertex->identity=919;
    vertex->size=24;vertex->stride=8;vertex->usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;vertex->uploaded_readonly=true;
    vertex->keep=std::make_shared<int>(1);
    efmi::assets::FilePlan plan;plan.skip_original=true;plan.items.emplace_back();
    plan.items[0].operation=Operation::BindVertex;plan.items[0].native_resource=vertex;
    NativeGeometryPlan prepared;std::string why;
    check(prepare_native_geometry(&device,&cb,draw,plan,prepared,why),"file geometry binding preflights without a character");
    record_native_geometry(&device,&cb,plan,prepared,0);
    check(cb.native_geometry && cb.native_geometry->vertices.count(0),"geometry survives the individual command list");
    const auto first_state=cb.native_geometry;
    std::weak_ptr<const efmi::frontend::NativeResource> duplicate;
    bool ready=true;
    for(unsigned i=0;i<4100;++i){
        auto next=std::make_shared<const efmi::frontend::NativeResource>(*vertex);
        if(i==1)duplicate=next;
        plan.items[0].native_resource=next;
        ready=ready && prepare_native_geometry(&device,&cb,draw,plan,prepared,why);
        if(!ready)break;
        record_native_geometry(&device,&cb,plan,prepared,0);
    }
    check(ready && duplicate.expired(),"repeated views of one upload do not grow recording ownership");
    VkBuffer app=h<VkBuffer>(920);VkDeviceSize offset=0;
    on_bind_vertex_buffers(&cb,1,1,&app,&offset);
    check(cb.native_geometry && cb.native_geometry->vertices.count(0),"application rebind changes only its named vertex slots");
    efmi::assets::FilePlan caller;caller.skip_original=true;caller.items.emplace_back();caller.items[0].operation=Operation::Caller;
    check(prepare_native_geometry(&device,&cb,draw,caller,prepared,why),"a later unmatched direct draw validates persistent geometry");
    draw.count=4;
    check(!prepare_native_geometry(&device,&cb,draw,caller,prepared,why),"a later draw cannot read beyond persistent file vertices");
    recipe->compute=true;draw.kind=DK_DISPATCH;
    check(prepare_native_geometry(&device,&cb,draw,caller,prepared,why),"dispatch ignores persistent IA state");
    check(!prepare_native_geometry(&device,&cb,draw,plan,prepared,why),"dispatch cannot execute an IA assignment");
    recipe->compute=false;draw.kind=DK_DRAW;draw.count=3;
    on_bind_vertex_buffers(&cb,0,1,&app,&offset);
    check(!cb.native_geometry && first_state->vertices.count(0),"application vertex rebind clears current state without mutating retained snapshots");
    check(cb.owners.size()==1 && cb.owners[0]->seal(),"one geometry owner seals all recorded file uses");
    check(!prepare_native_geometry(&device,&cb,draw,plan,prepared,why),"sealed recordings reject further geometry preparation");
    cb.reset_recording();
    check(!cb.native_geometry && !cb.owners[0]->used,"legal recording reset releases geometry ownership");
}
