#include "../layer/src/native_geometry.h"

static void application_ia_cases()
{
    using B=efmi::frontend::NativeBinding;
    Device d;CmdBuf cb;DrawRec draw;draw.pipeline=h<VkPipeline>(931);draw.kind=DK_DRAW;draw.count=3;draw.instances=1;
    d.d.DestroyBuffer=retained_buffer_destroy;d.d.FreeMemory=retained_memory_free;
    auto recipe=std::make_shared<GraphicsRecipe>();recipe->geometry_complete=true;
    recipe->vertex.bindings.push_back({0,8,VK_VERTEX_INPUT_RATE_VERTEX});
    d.pipes[draw.pipeline].recipe=recipe;
    const auto memory=h<VkDeviceMemory>(932);const auto buffer=h<VkBuffer>(933);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=1024;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=256;
    info.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    on_alloc_memory(&d,memory,&allocation);on_create_buffer(&d,buffer,&info);on_bind_buffer(&d,buffer,memory,0);
    draw.vb[0].buf=buffer;draw.vb[0].off=16;draw.ib=buffer;draw.ib_off=32;draw.index_size=4;
    B address;address.kind=B::Vertex;efmi::frontend::NativeResource vertex,index,missing;std::string why;
    check(native_resource(&d,&cb,draw,address,vertex,why) && vertex.bound && vertex.keep && vertex.size==256 &&
          vertex.offset==16 && vertex.range==240 && vertex.stride==8,"application IA exposes the actual static vertex binding and allocation owner");
    address.slot=1;
    check(native_resource(&d,&cb,draw,address,missing,why) && !missing.bound,"application unbound vertex input is represented explicitly");
    address.slot=kMaxVb;
    check(!native_resource(&d,&cb,draw,address,missing,why),"untracked IA slots cannot claim an application resource");
    address.slot=0;recipe->dynamic.push_back(VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE);
    check(!native_resource(&d,&cb,draw,address,missing,why),"unknown dynamic vertex stride refuses instead of reading the static declaration");
    recipe->dynamic={VK_DYNAMIC_STATE_VERTEX_INPUT_EXT};
    check(!native_resource(&d,&cb,draw,address,missing,why),"unknown dynamic vertex declaration cannot fall back to static IA facts");
    recipe->dynamic={VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE};
    draw.vb[0].stride_known=true;draw.vb[0].stride=12;draw.vb[0].size=48;
    check(native_resource(&d,&cb,draw,address,missing,why) && missing.stride==12 && missing.range==48 && missing.size==256,
          "dynamic vertex properties keep binding windows separate from allocation size");
    auto dynamic=std::make_shared<DynamicVertexInput>();draw.vertex_input=dynamic;
    dynamic->bindings.push_back({0,20,0,1});recipe->dynamic={VK_DYNAMIC_STATE_VERTEX_INPUT_EXT};
    check(!native_resource(&d,&cb,draw,address,missing,why),"Partial dynamic declaration cannot supply a vertex stride");
    dynamic->complete=true;
    check(native_resource(&d,&cb,draw,address,missing,why) && missing.stride==20,"Dynamic declaration supplies its own stride without a pipeline stride override");
    recipe->dynamic.push_back(VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE);
    check(native_resource(&d,&cb,draw,address,missing,why) && missing.stride==12,"An enabled dynamic binding stride overrides the declaration");
    dynamic->bindings.clear();
    check(!native_resource(&d,&cb,draw,address,missing,why),"A binding from an earlier dynamic declaration cannot survive its replacement");
    recipe->dynamic.clear();draw.vb[0].size=VK_WHOLE_SIZE;
    check(native_resource(&d,&cb,draw,address,missing,why) && missing.stride==8,"A static pipeline ignores prior dynamic input and binding stride");
    address.kind=B::Index;
    check(native_resource(&d,&cb,draw,address,index,why) && index.keep && index.offset==32 && index.range==224 && index.format==42 && index.index_size==4,
          "application IA retains the actual index format and binding offset");
    auto persistent=std::make_shared<NativeGeometryState>();persistent->index_overridden=true;
    auto retained=std::make_shared<efmi::frontend::NativeResource>(index);retained->index_size=2;retained->descriptor_type=0;
    persistent->index=retained;cb.native_geometry=persistent;
    check(native_resource(&d,&cb,draw,address,missing,why) && missing.format==57 && missing.descriptor_type==VK_DESCRIPTOR_TYPE_MAX_ENUM,
          "persistent IA facts describe the actual rebound index view");
    cb.native_geometry.reset();persistent.reset();retained.reset();
    efmi::assets::FilePlan plan;plan.skip_original=true;plan.items.emplace_back();
    plan.items[0].operation=efmi::assets::FileOperation::BindIndex;
    plan.items[0].native_resource=std::make_shared<const efmi::frontend::NativeResource>(index);
    NativeGeometryPlan prepared;
    check(prepare_native_geometry(&d,&cb,draw,plan,prepared,why),"retained current original index binding can be restored without importing a new producer");
    auto unrelated=std::make_shared<efmi::frontend::NativeResource>(index);unrelated->identity+=4;plan.items[0].native_resource=unrelated;
    check(!prepare_native_geometry(&d,&cb,draw,plan,prepared,why),"retained foreign or recycled index identity requires an ordered import");
    plan.items.clear();prepared={};unrelated.reset();vertex={};missing={};
    resource_destroy_order.clear();
    check(on_destroy_buffer(&d,buffer) && on_free_memory(&d,memory) && resource_destroy_order.empty(),
          "saved IA reference retains the buffer and allocation after application destruction");
    check(!native_resource(&d,&cb,draw,address,missing,why),"destroyed IA handles are not recaptured from stale draw metadata");
    index={};check(resource_destroy_order==std::vector<int>({2,3}),"last saved IA reference releases buffer before allocation exactly once");
}

static void native_geometry_cases()
{
    application_ia_cases();
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
