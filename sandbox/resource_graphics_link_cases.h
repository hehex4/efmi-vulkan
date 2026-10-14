// Independent headless graphics host for the production semantic linker.
// Explicit test submissions/waits do not implement original INI draw execution.
struct LinkedGraphicsConsumer {
    Device& device;
    VkRenderPass pass=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    std::vector<VkShaderModule> modules;
#define LINK_GRAPHICS_FUNCS(X) X(CreateShaderModule) X(DestroyShaderModule) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
    X(CreateRenderPass) X(DestroyRenderPass) X(CreateGraphicsPipelines) X(DestroyPipeline) X(CreateFramebuffer) X(DestroyFramebuffer) \
    X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdBindPipeline) X(CmdDraw)
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
    void init(const std::vector<gpu::HlslProgram>& programs){
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
        VkPipelineRasterizationStateCreateInfo raster={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.lineWidth=1;
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
        CmdBeginRenderPass(command,&rp,VK_SUBPASS_CONTENTS_INLINE);CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);CmdDraw(command,3,1,0,0);CmdEndRenderPass(command);
        vkcheck(d.EndCommandBuffer(command),"End linked graphics reference commands");
        VkFenceCreateInfo fence={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};vkcheck(d.CreateFence(dev,&fence,nullptr,&work.fence),"Create linked graphics reference fence");
        VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        vkcheck(d.QueueSubmit(device.queue,1,&submit,work.fence),"Submit linked graphics reference");work.submitted=true;
        vkcheck(d.WaitForFences(dev,1,&work.fence,VK_TRUE,UINT64_MAX),"Wait linked graphics reference");work.submitted=false;
        return resources.readback(device.queue,image,image_region(image));
    }
#undef LINK_GRAPHICS_FUNCS
};
static std::vector<uint8_t> native_linked_graphics(const gpu::HlslProgram& vertex,const gpu::HlslProgram& pixel){
    ImageNativeReference native;auto* d=native.device.Get();auto* c=native.context.Get();
    ImageNativeReference::Ptr<ID3D11VertexShader> vs;ImageNativeReference::Ptr<ID3D11PixelShader> ps;
    check(SUCCEEDED(d->CreateVertexShader(vertex.dxbc.data(),vertex.dxbc.size(),nullptr,&vs)) &&
        SUCCEEDED(d->CreatePixelShader(pixel.dxbc.data(),pixel.dxbc.size(),nullptr,&ps)),"Native original graphics shaders");
    D3D11_TEXTURE2D_DESC desc{4,4,1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET,0,0};
    ImageNativeReference::Ptr<ID3D11Texture2D> image,read;ImageNativeReference::Ptr<ID3D11RenderTargetView> rtv;ImageNativeReference::Ptr<ID3D11RasterizerState> raster;
    check(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&image)) && SUCCEEDED(d->CreateRenderTargetView(image.Get(),nullptr,&rtv)),"Native graphics output");
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&read)),"Native graphics readback");
    D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
    check(SUCCEEDED(d->CreateRasterizerState(&rs,&raster)),"Native graphics raster state");
    auto* target=rtv.Get();const float zero[4]={};c->ClearRenderTargetView(target,zero);c->OMSetRenderTargets(1,&target,nullptr);
    c->RSSetState(raster.Get());D3D11_VIEWPORT viewport{0,0,4,4,0,1};c->RSSetViewports(1,&viewport);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->Draw(3,0);c->CopyResource(read.Get(),image.Get());
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
