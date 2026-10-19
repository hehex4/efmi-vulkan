// The host's back buffer behind bb, r_bb and f_bb: original references, queries and a blended
// triangle-strip CustomShader draw into it, compared with native DX11 on WARP.
static const char* backbuffer_hlsl=R"hlsl(struct V{float4 position:SV_Position;};
#ifdef VERTEX_SHADER
V main(uint id:SV_VertexID){V v;v.position=float4(id&1?0.5:-0.75,id&2?-0.5:0.75,0,1);return v;}
#else
float4 main(V v):SV_Target{return float4(v.position.x/8,v.position.y/4,0.75,0.5);}
#endif
)hlsl";
static std::vector<float> backbuffer_initial(){
    std::vector<float> texels(8*4*4);for(size_t i=0;i<texels.size();++i)texels[i]=float((i*37)%17)/16.0f;return texels;
}
static std::vector<uint8_t> native_backbuffer_draw(const gpu::HlslProgram& vertex,const gpu::HlslProgram& pixel){
    ImageNativeReference native;auto* d=native.device.Get();auto* c=native.context.Get();
    ImageNativeReference::Ptr<ID3D11VertexShader> vs;ImageNativeReference::Ptr<ID3D11PixelShader> ps;
    check(SUCCEEDED(d->CreateVertexShader(vertex.dxbc.data(),vertex.dxbc.size(),nullptr,&vs)) &&
          SUCCEEDED(d->CreatePixelShader(pixel.dxbc.data(),pixel.dxbc.size(),nullptr,&ps)),"Native back buffer shaders");
    const auto initial=backbuffer_initial();D3D11_SUBRESOURCE_DATA data{initial.data(),8*16,0};
    D3D11_TEXTURE2D_DESC desc{8,4,1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE,0,0};
    ImageNativeReference::Ptr<ID3D11Texture2D> image,read;ImageNativeReference::Ptr<ID3D11RenderTargetView> rtv;
    check(SUCCEEDED(d->CreateTexture2D(&desc,&data,&image)) && SUCCEEDED(d->CreateRenderTargetView(image.Get(),nullptr,&rtv)),"Native back buffer");
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&read)),"Native back buffer readback");
    // IniHandler's blend = sets the color factors only; alpha keeps its ONE, ZERO, ADD default.
    D3D11_BLEND_DESC blend{};blend.RenderTarget[0]={TRUE,D3D11_BLEND_SRC_ALPHA,D3D11_BLEND_INV_SRC_ALPHA,D3D11_BLEND_OP_ADD,D3D11_BLEND_ONE,D3D11_BLEND_ZERO,D3D11_BLEND_OP_ADD,15};
    D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
    ImageNativeReference::Ptr<ID3D11BlendState> blend_state;ImageNativeReference::Ptr<ID3D11RasterizerState> raster;
    check(SUCCEEDED(d->CreateBlendState(&blend,&blend_state)) && SUCCEEDED(d->CreateRasterizerState(&rs,&raster)),"Native back buffer state");
    auto* target=rtv.Get();c->OMSetRenderTargets(1,&target,nullptr);c->OMSetBlendState(blend_state.Get(),nullptr,0xffffffff);c->RSSetState(raster.Get());
    D3D11_VIEWPORT viewport{0,0,8,4,0,1};c->RSSetViewports(1,&viewport);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->Draw(4,0);c->CopyResource(read.Get(),image.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};check(SUCCEEDED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)),"Map native back buffer");
    std::vector<uint8_t> bytes(8*4*16);for(unsigned y=0;y<4;++y)memcpy(bytes.data()+y*8*16,static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch,8*16);
    c->Unmap(read.Get(),0);return bytes;
}
static void original_backbuffer(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root/"Mods");write(root/"Mods/backbuffer.hlsl",backbuffer_hlsl);
    gpu::HlslCompiler compiler(L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");gpu::HlslRequest request;
    request.source=root/"Mods/backbuffer.hlsl";request.working_directory=root;gpu::HlslProgram vs,ps;std::string why;
    request.stage='v';check(compiler.compile(request,vs,why),"Compile back buffer vertex shader");
    request.stage='p';check(compiler.compile(request,ps,why),"Compile back buffer pixel shader");
    ShaderFixture f(d,root,R"ini(namespace = compute
[Constants]
global $width = 0
global $height = 0
global $format = 0
global $fake_width = 0
[ResourceOther]
type = Texture2D
width = 2
height = 2
format = R8G8B8A8_UNORM
bind_flags = render_target shader_resource
[CustomShaderPanel]
vs = backbuffer.hlsl
ps = backbuffer.hlsl
blend = ADD SRC_ALPHA INV_SRC_ALPHA
cull = none
topology = triangle_strip
o0 = set_viewport bb
draw = 4, 0
[Present]
$width = bb->width
$height = bb->height
$format = r_bb->format
$fake_width = f_bb->width
run = CustomShaderPanel
)ini");
    auto& resources=f.backend->resources();
    gpu::ImageDescription description;description.format=VK_FORMAT_R32G32B32A32_SFLOAT;description.extent={8,4,1};
    description.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    resources.begin(d.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
    auto image=resources.commands().create_image(description);const auto initial=backbuffer_initial();
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={8,4,1};
    resources.commands().write(image,copy,initial.data(),initial.size()*4);f.finish();
    refuses([&]{resources.backbuffer(image,DXGI_FORMAT_R8G8B8A8_UNORM);},"A host back buffer must match its declared DXGI format");
    resources.backbuffer(image,DXGI_FORMAT_R32G32B32A32_FLOAT);f.present();
    check(f.variable("width")==8 && f.variable("height")==4 && f.variable("format")==float(DXGI_FORMAT_R32G32B32A32_FLOAT) && f.variable("fake_width")==8,
          "bb, r_bb and f_bb are the host back buffer without upscaling");
    const auto actual=resources.commands().readback(d.queue,image,image_region(image));
    const auto expected=native_backbuffer_draw(vs,ps);
    check(actual==expected,"Blended triangle-strip CustomShader into set_viewport bb matches native DX11 bytes");
    check(resources.binding(L'\0',ResourceCopyTargetType::RENDER_TARGET,0).image==nullptr,"CustomShader restores the caller's empty render target after drawing into bb");
    image.reset();f.release();
    {   ShaderFixture missing(d,root/"missing",R"ini(namespace = compute
[Constants]
global $width = 0
global $fake_width = 0
[Present]
$width = bb->width
$fake_width = f_bb->width
)ini");
        missing.backend->resources().backbuffer(nullptr,DXGI_FORMAT_UNKNOWN);missing.present();
        check(missing.variable("width")==-2 && missing.variable("fake_width")==-2,"Without a host back buffer bb reads as the original null resource");missing.release();
    }
    {   // bb, r_bb and f_bb are source-only targets: the original parser warns and drops an
        // assignment to them, so a write never reaches execution.
        ShaderFixture source_only(d,root/"source-only",R"ini(namespace = compute
[ResourceOther]
type = Texture2D
width = 2
height = 2
format = R8G8B8A8_UNORM
bind_flags = render_target shader_resource
[Present]
bb = copy ResourceOther
r_bb = ref ResourceOther
)ini");
        check(source_only.ini->status().diagnostics==2,"Assignments to the back buffer are original parse warnings");
        source_only.present();source_only.release();
    }
}
// A caller-owned image, as a swap chain image is: created, cleared and left in its own layout by
// raw Vulkan, imported as the back buffer, drawn into, handed back and waited on by a semaphore.
static void imported_backbuffer(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root/"Mods");write(root/"Mods/backbuffer.hlsl",backbuffer_hlsl);
    ShaderFixture f(d,root,R"ini(namespace = compute
[CustomShaderPanel]
vs = backbuffer.hlsl
ps = backbuffer.hlsl
blend = ADD SRC_ALPHA INV_SRC_ALPHA
cull = none
topology = triangle_strip
o0 = set_viewport bb
draw = 4, 0
[Present]
run = CustomShaderPanel
)ini");
    const auto& v=d.resource.dispatch;const auto device=d.resource.device;
    const auto create_semaphore=reinterpret_cast<PFN_vkCreateSemaphore>(d.get_device_proc(device,"vkCreateSemaphore"));
    const auto destroy_semaphore=reinterpret_cast<PFN_vkDestroySemaphore>(d.get_device_proc(device,"vkDestroySemaphore"));
    const auto free_buffers=reinterpret_cast<PFN_vkFreeCommandBuffers>(d.get_device_proc(device,"vkFreeCommandBuffers"));
    check(create_semaphore && destroy_semaphore && free_buffers,"Semaphore and command buffer entry points");
    VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R32G32B32A32_SFLOAT;ci.extent={8,4,1};
    ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImage raw=VK_NULL_HANDLE;check(v.CreateImage(device,&ci,nullptr,&raw)==VK_SUCCESS,"Create a caller-owned image");
    VkImageMemoryRequirementsInfo2 request={VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};request.image=raw;
    VkMemoryRequirements2 requirements={VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};v.GetImageMemoryRequirements2(device,&request,&requirements);
    uint32_t type=UINT32_MAX;for(uint32_t i=0;i<d.resource.memory.memoryTypeCount && type==UINT32_MAX;++i)
        if((requirements.memoryRequirements.memoryTypeBits&(1u<<i)) && (d.resource.memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))type=i;
    VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=requirements.memoryRequirements.size;ai.memoryTypeIndex=type;
    VkDeviceMemory memory=VK_NULL_HANDLE;check(type!=UINT32_MAX && v.AllocateMemory(device,&ai,nullptr,&memory)==VK_SUCCESS && v.BindImageMemory(device,raw,memory,0)==VK_SUCCESS,"Bind the caller-owned image");
    VkCommandPoolCreateInfo pi={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=d.resource.queue_family;VkCommandPool pool=VK_NULL_HANDLE;
    check(v.CreateCommandPool(device,&pi,nullptr,&pool)==VK_SUCCESS,"Caller command pool");
    auto run=[&](const std::function<void(VkCommandBuffer)>& record,const VkSemaphore* wait){
        VkCommandBufferAllocateInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};bi.commandPool=pool;bi.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;bi.commandBufferCount=1;
        VkCommandBuffer cb=VK_NULL_HANDLE;check(v.AllocateCommandBuffers(device,&bi,&cb)==VK_SUCCESS,"Caller command buffer");
        VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};v.BeginCommandBuffer(cb,&begin);record(cb);v.EndCommandBuffer(cb);
        VkFenceCreateInfo fi={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence=VK_NULL_HANDLE;v.CreateFence(device,&fi,nullptr,&fence);
        const VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cb;
        if(wait){si.waitSemaphoreCount=1;si.pWaitSemaphores=wait;si.pWaitDstStageMask=&stage;}
        check(v.QueueSubmit(d.queue,1,&si,fence)==VK_SUCCESS && v.WaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX)==VK_SUCCESS,"Caller submission");
        v.DestroyFence(device,fence,nullptr);free_buffers(device,pool,1,&cb);
    };
    auto barrier=[&](VkCommandBuffer cb,VkImageLayout from,VkImageLayout to){VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.oldLayout=from;b.newLayout=to;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=raw;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        v.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);};
    const VkClearColorValue color{{0.25f,0.5f,0.75f,1.0f}};const VkImageSubresourceRange whole{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    run([&](VkCommandBuffer cb){barrier(cb,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        v.CmdClearColorImage(cb,raw,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&whole);
        barrier(cb,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);},nullptr);
    auto& resources=f.backend->resources();gpu::ImageDescription description;description.format=ci.format;description.extent=ci.extent;description.usage=ci.usage;
    auto image=resources.commands().import_image(raw,description,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    resources.backbuffer(image,DXGI_FORMAT_R32G32B32A32_FLOAT);
    VkSemaphoreCreateInfo si={VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VkSemaphore done=VK_NULL_HANDLE;check(create_semaphore(device,&si,nullptr,&done)==VK_SUCCESS,"Create semaphore");
    resources.begin(d.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
    check(f.ini->present({},1,0,f.why),"Present draws into the imported back buffer");
    gpu::ResourceCommands::SubmitSync sync;sync.signals={done};auto ticket=resources.finish(sync);check(bool(ticket),"Imported back buffer submission");
    // The caller waits on the semaphore and uses the image from its own layout again.
    run([&](VkCommandBuffer cb){barrier(cb,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        barrier(cb,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);},&done);
    ticket->wait();ticket.reset();
    resources.begin(d.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
    const auto bytes=resources.commands().readback(d.queue,image,image_region(image));resources.discard();
    const auto* texels=reinterpret_cast<const float*>(bytes.data());size_t kept=0,drawn=0;
    for(size_t i=0;i<32;++i){const bool clear=texels[i*4]==0.25f && texels[i*4+1]==0.5f && texels[i*4+2]==0.75f && texels[i*4+3]==1.0f;kept+=clear;drawn+=!clear;}
    check(kept>0 && drawn>0,"The imported image keeps its contents outside the draw and receives the draw");
    check(kept+drawn==32,"Imported back buffer readback covers the image");
    resources.backbuffer(nullptr,DXGI_FORMAT_UNKNOWN);image.reset();f.release();
    destroy_semaphore(device,done,nullptr);v.DestroyCommandPool(device,pool,nullptr);v.DestroyImage(device,raw,nullptr);v.FreeMemory(device,memory,nullptr);
}
