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
