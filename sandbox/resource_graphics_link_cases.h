// Independent headless graphics host for the production semantic linker.
// Explicit test submissions/waits do not implement original INI draw execution.
struct GraphicsReferenceOptions {
    uint32_t count=3,instances=1,first_vertex=0,first_instance=0;
    uint32_t cull=0; // 0 none, 1 front, 2 back; source winding below.
    bool front_ccw=false;
    std::vector<uint32_t> indices;
    uint32_t first_index=0;
    int32_t base_vertex=0;
};
struct LinkedGraphicsConsumer {
    Device& device;
    VkRenderPass pass=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    std::vector<VkShaderModule> modules;
    GraphicsReferenceOptions options;
#define LINK_GRAPHICS_FUNCS(X) X(CreateShaderModule) X(DestroyShaderModule) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
    X(CreateRenderPass) X(DestroyRenderPass) X(CreateGraphicsPipelines) X(DestroyPipeline) X(CreateFramebuffer) X(DestroyFramebuffer) \
    X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdBindPipeline) X(CmdDraw) X(CmdBindIndexBuffer) X(CmdDrawIndexed)
#define X(name) PFN_vk##name name=nullptr;
    LINK_GRAPHICS_FUNCS(X)
#undef X
    explicit LinkedGraphicsConsumer(Device& d):device(d){
#define X(name) name=reinterpret_cast<PFN_vk##name>(d.get_device_proc(d.resource.device,"vk" #name));check(name!=nullptr,"Graphics link reference vk" #name);
        LINK_GRAPHICS_FUNCS(X)
#undef X
    }
    ~LinkedGraphicsConsumer(){const auto dev=device.resource.device;
        if(pipeline)DestroyPipeline(dev,pipeline,nullptr);if(layout)DestroyPipelineLayout(dev,layout,nullptr);
        for(auto module:modules)DestroyShaderModule(dev,module,nullptr);if(pass)DestroyRenderPass(dev,pass,nullptr);}
    void init(const std::vector<gpu::HlslProgram>& programs,GraphicsReferenceOptions requested={}){
        options=requested;
        const auto dev=device.resource.device;
        VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R32G32B32A32_SFLOAT;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_GENERAL;
        VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass{};
        subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;
        VkSubpassDependency dependencies[2]{};dependencies[0]={VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0};
        dependencies[1]={0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,0};
        VkRenderPassCreateInfo rp={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=1;rp.pAttachments=&attachment;
        rp.subpassCount=1;rp.pSubpasses=&subpass;rp.dependencyCount=2;rp.pDependencies=dependencies;
        vkcheck(CreateRenderPass(dev,&rp,nullptr,&pass),"Create linked graphics reference pass");
        VkPipelineLayoutCreateInfo pl={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};vkcheck(CreatePipelineLayout(dev,&pl,nullptr,&layout),"Create linked graphics reference layout");
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        for(const auto& program:programs){VkShaderModuleCreateInfo sm={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=program.spirv.size()*4;sm.pCode=program.spirv.data();
            VkShaderModule module;vkcheck(CreateShaderModule(dev,&sm,nullptr,&module),"Create linked graphics reference module");modules.push_back(module);
            VkPipelineShaderStageCreateInfo stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stage.stage=program.stage;stage.module=module;stage.pName="main";stages.push_back(stage);}
        VkPipelineVertexInputStateCreateInfo input={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,4,4,-4,0,1};VkRect2D scissor{{0,0},{4,4}};
        VkPipelineViewportStateCreateInfo vp={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
        VkPipelineRasterizationStateCreateInfo raster={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;
        raster.cullMode=options.cull==1?VK_CULL_MODE_FRONT_BIT:options.cull==2?VK_CULL_MODE_BACK_BIT:VK_CULL_MODE_NONE;
        raster.frontFace=options.front_ccw?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE;raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo samples={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState target{};target.colorWriteMask=0xf;
        VkPipelineColorBlendStateCreateInfo blend={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&target;
        VkGraphicsPipelineCreateInfo info={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};info.stageCount=uint32_t(stages.size());info.pStages=stages.data();
        info.pVertexInputState=&input;info.pInputAssemblyState=&assembly;info.pViewportState=&vp;info.pRasterizationState=&raster;
        info.pMultisampleState=&samples;info.pColorBlendState=&blend;info.layout=layout;info.renderPass=pass;
        vkcheck(CreateGraphicsPipelines(dev,VK_NULL_HANDLE,1,&info,nullptr,&pipeline),"Create linked graphics reference pipeline");
    }
    std::vector<uint8_t> read(gpu::ResourceCommands& resources){
        const auto dev=device.resource.device;const auto& d=device.resource.dispatch;
        gpu::ImageDescription desc;desc.extent={4,4,1};desc.format=VK_FORMAT_R32G32B32A32_SFLOAT;desc.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        auto image=resources.create_image(desc);resources.clear(image,image->full_range(),VkClearColorValue{});
        gpu::Buffer indices;if(!options.indices.empty())indices=resources.create(options.indices.size()*4,VK_BUFFER_USAGE_INDEX_BUFFER_BIT,options.indices.data(),options.indices.size()*4);
        auto initialized=resources.submit(device.queue);initialized->wait();initialized.reset();
        auto view=resources.view(image,VK_IMAGE_VIEW_TYPE_2D,desc.format,image->full_range(),VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        struct Work {
            LinkedGraphicsConsumer& owner;VkFramebuffer framebuffer=VK_NULL_HANDLE;VkCommandPool pool=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;bool submitted=false;
            ~Work(){const auto dev=owner.device.resource.device;const auto& d=owner.device.resource.dispatch;
                if(submitted)d.WaitForFences(dev,1,&fence,VK_TRUE,UINT64_MAX);if(fence)d.DestroyFence(dev,fence,nullptr);
                if(pool)d.DestroyCommandPool(dev,pool,nullptr);if(framebuffer)owner.DestroyFramebuffer(dev,framebuffer,nullptr);}
        } work{*this};
        const auto handle=view->view();VkFramebufferCreateInfo fb={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=pass;fb.attachmentCount=1;fb.pAttachments=&handle;fb.width=fb.height=4;fb.layers=1;
        vkcheck(CreateFramebuffer(dev,&fb,nullptr,&work.framebuffer),"Create linked graphics reference framebuffer");
        VkCommandPoolCreateInfo cp={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=device.resource.queue_family;
        vkcheck(d.CreateCommandPool(dev,&cp,nullptr,&work.pool),"Create linked graphics reference commands");
        VkCommandBufferAllocateInfo allocation={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocation.commandPool=work.pool;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;
        VkCommandBuffer command;vkcheck(d.AllocateCommandBuffers(dev,&allocation,&command),"Allocate linked graphics reference commands");
        VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkcheck(d.BeginCommandBuffer(command,&begin),"Begin linked graphics reference commands");
        VkRenderPassBeginInfo rp={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};rp.renderPass=pass;rp.framebuffer=work.framebuffer;rp.renderArea.extent={4,4};
        CmdBeginRenderPass(command,&rp,VK_SUBPASS_CONTENTS_INLINE);CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        if(indices){CmdBindIndexBuffer(command,indices->buffer(),0,VK_INDEX_TYPE_UINT32);CmdDrawIndexed(command,options.count,options.instances,options.first_index,options.base_vertex,options.first_instance);}
        else CmdDraw(command,options.count,options.instances,options.first_vertex,options.first_instance);CmdEndRenderPass(command);
        vkcheck(d.EndCommandBuffer(command),"End linked graphics reference commands");
        VkFenceCreateInfo fence={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};vkcheck(d.CreateFence(dev,&fence,nullptr,&work.fence),"Create linked graphics reference fence");
        VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        vkcheck(d.QueueSubmit(device.queue,1,&submit,work.fence),"Submit linked graphics reference");work.submitted=true;
        vkcheck(d.WaitForFences(dev,1,&work.fence,VK_TRUE,UINT64_MAX),"Wait linked graphics reference");work.submitted=false;
        return resources.readback(device.queue,image,image_region(image));
    }
#undef LINK_GRAPHICS_FUNCS
};
static std::vector<uint8_t> native_linked_graphics(const gpu::HlslProgram& vertex,const gpu::HlslProgram& pixel,GraphicsReferenceOptions options={},
    const std::function<void(ID3D11Device*,ID3D11DeviceContext*)>& setup={}){
    ImageNativeReference native;auto* d=native.device.Get();auto* c=native.context.Get();
    ImageNativeReference::Ptr<ID3D11VertexShader> vs;ImageNativeReference::Ptr<ID3D11PixelShader> ps;
    check(SUCCEEDED(d->CreateVertexShader(vertex.dxbc.data(),vertex.dxbc.size(),nullptr,&vs)) &&
        SUCCEEDED(d->CreatePixelShader(pixel.dxbc.data(),pixel.dxbc.size(),nullptr,&ps)),"Native original graphics shaders");
    D3D11_TEXTURE2D_DESC desc{4,4,1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET,0,0};
    ImageNativeReference::Ptr<ID3D11Texture2D> image,read;ImageNativeReference::Ptr<ID3D11RenderTargetView> rtv;ImageNativeReference::Ptr<ID3D11RasterizerState> raster;
    check(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&image)) && SUCCEEDED(d->CreateRenderTargetView(image.Get(),nullptr,&rtv)),"Native graphics output");
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&read)),"Native graphics readback");
    D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=options.cull==1?D3D11_CULL_FRONT:options.cull==2?D3D11_CULL_BACK:D3D11_CULL_NONE;
    rs.FrontCounterClockwise=options.front_ccw;rs.DepthClipEnable=TRUE;
    check(SUCCEEDED(d->CreateRasterizerState(&rs,&raster)),"Native graphics raster state");
    auto* target=rtv.Get();const float zero[4]={};c->ClearRenderTargetView(target,zero);c->OMSetRenderTargets(1,&target,nullptr);
    c->RSSetState(raster.Get());D3D11_VIEWPORT viewport{0,0,4,4,0,1};c->RSSetViewports(1,&viewport);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);
    if(setup)setup(d,c);
    ImageNativeReference::Ptr<ID3D11Buffer> indices;
    if(!options.indices.empty()){D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=UINT(options.indices.size()*4);buffer.BindFlags=D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{options.indices.data(),0,0};check(SUCCEEDED(d->CreateBuffer(&buffer,&initial,&indices)),"Native reference index buffer");
        c->IASetIndexBuffer(indices.Get(),DXGI_FORMAT_R32_UINT,0);c->DrawIndexedInstanced(options.count,options.instances,options.first_index,options.base_vertex,options.first_instance);}
    else c->DrawInstanced(options.count,options.instances,options.first_vertex,options.first_instance);c->CopyResource(read.Get(),image.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};check(SUCCEEDED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)),"Native graphics pixels");
    std::vector<uint8_t> bytes(4*4*16);for(unsigned y=0;y<4;++y)memcpy(bytes.data()+y*64,static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch,64);
    c->Unmap(read.Get(),0);return bytes;
}
static void graphics_link_pixels(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);gpu::HlslCompiler compiler(L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
    gpu::HlslRequest request;request.source=root/"graphics.hlsl";request.working_directory=root;
    const char* vertex=R"hlsl(struct V{float4 position:SV_Position;float2 first:DATA0;float2 second:data1;};
V main(uint id:SV_VertexID){V v;v.position=float4(id==2?3:-1,id==1?3:-1,0,1);v.first=float2(2,3);v.second=float2(7,11);return v;})hlsl";
    const char* pixel="struct V{float4 position:SV_Position;float2 first:data0;float2 second:DATA1;}; float4 main(V v):SV_Target{return float4(v.first+100*v.second,v.second);}";
    gpu::HlslProgram vs,ps;std::string why;request.stage='v';write(request.source,vertex);
    check(compiler.compile(request,vs,why),"Compile source graphics vertex");request.stage='p';write(request.source,pixel);
    check(compiler.compile(request,ps,why),"Compile source graphics pixel");
    const auto expected=native_linked_graphics(vs,ps);const auto linked=gpu::link_graphics({ps,vs});
    gpu::ResourceBudgets budgets;
    {gpu::ResourceCommands resources(d.resource,budgets);
        LinkedGraphicsConsumer raw(d);raw.init({vs,ps});const auto before=raw.read(resources);
        LinkedGraphicsConsumer matched(d);matched.init(linked);const auto after=matched.read(resources);
        if(after!=expected){for(unsigned i=0;i<4;++i)printf("Link pixel channel %u native=%.9g before=%.9g after=%.9g\n",i,value(expected,i*4),value(before,i*4),value(after,i*4));
            for(const auto* program:std::array<const gpu::HlslProgram*,4>{&vs,&ps,&linked[0],&linked[1]}){printf("Link stage %u\n",unsigned(program->stage));
                for(const auto& field:program->inputs)printf("  in %s %u id%u\n",field.semantic.c_str(),field.location,field.spirv_id);
                for(const auto& field:program->outputs)printf("  out %s %u id%u\n",field.semantic.c_str(),field.location,field.spirv_id);}}
        check(before!=expected,"Unlinked case-dependent IO ordering reproduces incorrect real GPU pixels");
        check(after==expected,"Linked Vulkan output matches original DX11 pixels byte for byte");
        const float color[]={702,1103,7,11};for(size_t offset=0;offset<after.size();offset+=sizeof(color))
            check(!memcmp(after.data()+offset,color,sizeof(color)),"Both linked semantics reach every rendered pixel");
    }
    check(!budgets.resident->used() && !budgets.staging->used(),"Graphics link reference releases owned allocations");
}
static void graphics_coordinate_pixels(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);gpu::HlslCompiler compiler(L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
    gpu::HlslRequest request;request.source=root/"coordinates.hlsl";request.working_directory=root;
    auto program=[&](char stage,const char* source){write(request.source,source);request.stage=stage;gpu::HlslProgram result;std::string why;
        if(!compiler.compile(request,result,why))throw std::runtime_error(why);++checks;return result;};
    const auto vs=program('v',R"hlsl(float4 main(uint id:SV_VertexID):SV_Position{
uint vertex=id%3;return float4(vertex==2?6:-2,vertex==1?6:-2,1,2);})hlsl");
    const auto ps=program('p',"float4 main(float4 position:SV_Position):SV_Target{return position;}");
    gpu::ResourceBudgets budgets;{
        gpu::ResourceCommands resources(d.resource,budgets);LinkedGraphicsConsumer pipeline(d);pipeline.init(gpu::link_graphics({vs,ps}));
        const auto native=native_linked_graphics(vs,ps),actual=pipeline.read(resources);
        if(actual!=native)for(unsigned i=0;i<4;++i)printf("Position channel%u native=%.9g Vulkan=%.9g\n",i,value(native,4*i),value(actual,4*i));
        check(actual==native,"Pixel SV_Position xyzw matches native DX11 for non-unit clip w");
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){const auto offset=(y*4+x)*16;
            check(value(actual,offset)==x+.5f && value(actual,offset+4)==y+.5f && value(actual,offset+8)==.5f && value(actual,offset+12)==2,
                "Source pixel coordinates preserve top-left centers, depth and reciprocal-w convention");}
        const auto facing=program('p',"float4 main(bool front:SV_IsFrontFace):SV_Target{return front?float4(1,2,3,4):float4(5,6,7,8);}");
        for(unsigned cull=0;cull<3;++cull)for(bool ccw:{false,true}){
            GraphicsReferenceOptions options;options.cull=cull;options.front_ccw=ccw;LinkedGraphicsConsumer raster(d);raster.init(gpu::link_graphics({vs,facing}),options);
            check(raster.read(resources)==native_linked_graphics(vs,facing,options),"Negative viewport preserves native winding, culling and front-face input");}
        const auto ids=program('v',R"hlsl(struct V{float4 position:SV_Position;nointerpolation uint2 ids:TEXCOORD0;};
V main(uint id:SV_VertexID,uint instance:SV_InstanceID){V v;uint vertex=id%3;
v.position=float4(vertex==2?3:-1,vertex==1?3:-1,0,1);v.ids=uint2(id,instance);return v;})hlsl");
        const auto read_ids=program('p',"struct V{float4 position:SV_Position;nointerpolation uint2 ids:TEXCOORD0;}; float4 main(V v):SV_Target{return float4(v.ids,0,1);}");
        Device id_device;id_device.init(false,false,true);gpu::ResourceCommands id_resources(id_device.resource,budgets);
        for(unsigned first_vertex:{0u,6u})for(unsigned first_instance:{0u,7u}){
            GraphicsReferenceOptions options;options.first_vertex=first_vertex;options.first_instance=first_instance;
            LinkedGraphicsConsumer draw(id_device);draw.init(gpu::link_graphics({ids,read_ids}),options);const auto expected=native_linked_graphics(ids,read_ids,options),output=draw.read(id_resources);
            if(output!=expected)printf("IDs first=%u,%u native=%.9g,%.9g Vulkan=%.9g,%.9g\n",first_vertex,first_instance,value(expected,0),value(expected,4),value(output,0),value(output,4));
            check(output==expected,"Vertex and instance IDs match original nonzero draw offsets");}
        for(int base:{-3,0,6}){
            GraphicsReferenceOptions options;options.indices={77,77,3,4,5};options.first_index=2;options.base_vertex=base;options.instances=2;options.first_instance=9;
            LinkedGraphicsConsumer draw(id_device);draw.init(gpu::link_graphics({ids,read_ids}),options);const auto expected=native_linked_graphics(ids,read_ids,options),output=draw.read(id_resources);
            if(output!=expected)printf("Indexed IDs base=%d native=%.9g,%.9g Vulkan=%.9g,%.9g\n",base,value(expected,0),value(expected,4),value(output,0),value(output,4));
            check(output==expected,"Indexed source IDs preserve signed base vertex, first index and first instance");}
    }
    check(!budgets.resident->used() && !budgets.staging->used(),"Coordinate references release all owned allocations");
}
