// Original installed TextPrinter HLSL/font, controlled owned resources and native pixels.
// This does not run the complete Core INI, import a backbuffer, or query a game window.
#include <sstream>
static std::vector<uint8_t> text_file_bytes(const std::filesystem::path& path){
    std::ifstream stream(path,std::ios::binary|std::ios::ate);check(bool(stream) && stream.tellg()>0 && stream.tellg()<1024*1024,"Bounded original text fixture file");
    std::vector<uint8_t> bytes(size_t(stream.tellg()));stream.seekg(0);stream.read(reinterpret_cast<char*>(bytes.data()),bytes.size());check(bool(stream),"Read original text fixture");return bytes;
}
struct TextReferenceCase {
    uint32_t width=128,height=64;float dpi=96;
    // Original TextParameters has scalar stride 72, not a padded float4 array.
    std::array<float,18> parameters{{-.875f,.75f,.875f,-.75f, .25f,.5f,.75f,1, .125f,.25f,.375f,.5f, .03125f,.0625f, 0,0,0,1}};
    std::vector<uint32_t> text;
};
static std::pair<std::vector<uint8_t>,std::vector<uint8_t>> native_text(const std::vector<gpu::HlslProgram>& programs,
    const std::vector<uint8_t>& font,const TextReferenceCase& test){
    std::vector<uint8_t> positions;GraphicsReferenceOptions options;options.count=4096;options.width=test.width;options.height=test.height;
    const auto pixels=native_linked_graphics(programs[0],programs[2],options,[&](auto* d,auto* c){
        native_geometry(d,c,programs[1],D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,true);
        D3D11_TEXTURE2D_DESC image{256,96,1,1,DXGI_FORMAT_B8G8R8A8_UNORM,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,0};
        D3D11_SUBRESOURCE_DATA initial{font.data()+128,1024,0};
        ImageNativeReference::Ptr<ID3D11Texture2D> atlas;ImageNativeReference::Ptr<ID3D11ShaderResourceView> atlas_view;
        check(font.size()==128+256*96*4 && SUCCEEDED(d->CreateTexture2D(&image,&initial,&atlas)) &&
            SUCCEEDED(d->CreateShaderResourceView(atlas.Get(),nullptr,&atlas_view)),"Native original BGRA font atlas");
        const float values[]={float(test.width),float(test.height),0,test.dpi};D3D11_TEXTURE1D_DESC parameter_image{1,1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,0};
        initial={values,16,0};ImageNativeReference::Ptr<ID3D11Texture1D> parameters;ImageNativeReference::Ptr<ID3D11ShaderResourceView> parameter_view;
        check(SUCCEEDED(d->CreateTexture1D(&parameter_image,&initial,&parameters)) && SUCCEEDED(d->CreateShaderResourceView(parameters.Get(),nullptr,&parameter_view)),"Native text IniParams image");
        auto buffer=[&](UINT size,UINT stride,const void* data,UINT flags){D3D11_BUFFER_DESC desc{size,D3D11_USAGE_DEFAULT,flags,0,stride?D3D11_RESOURCE_MISC_BUFFER_STRUCTURED:0,stride};
            D3D11_SUBRESOURCE_DATA input{data,0,0};ImageNativeReference::Ptr<ID3D11Buffer> value;check(SUCCEEDED(d->CreateBuffer(&desc,data?&input:nullptr,&value)),"Native text buffer");return value;};
        auto text=buffer(UINT(test.text.size()*4),0,test.text.data(),D3D11_BIND_SHADER_RESOURCE);
        auto params=buffer(72,72,test.parameters.data(),D3D11_BIND_SHADER_RESOURCE);
        const std::vector<uint8_t> zeros(4096*16,0);auto layout=buffer(UINT(zeros.size()),16,zeros.data(),D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
        D3D11_SHADER_RESOURCE_VIEW_DESC text_desc{};text_desc.Format=DXGI_FORMAT_R32_UINT;text_desc.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;text_desc.Buffer.NumElements=UINT(test.text.size());
        ImageNativeReference::Ptr<ID3D11ShaderResourceView> text_view,params_view,layout_view;ImageNativeReference::Ptr<ID3D11UnorderedAccessView> layout_uav;
        check(SUCCEEDED(d->CreateShaderResourceView(text.Get(),&text_desc,&text_view)) && SUCCEEDED(d->CreateShaderResourceView(params.Get(),nullptr,&params_view)) &&
            SUCCEEDED(d->CreateShaderResourceView(layout.Get(),nullptr,&layout_view)) && SUCCEEDED(d->CreateUnorderedAccessView(layout.Get(),nullptr,&layout_uav)),"Native text structured and typed views");
        auto* atlas_handle=atlas_view.Get();auto* ini_handle=parameter_view.Get();auto* text_handle=text_view.Get();auto* params_handle=params_view.Get();auto* layout_handle=layout_view.Get();auto* uav=layout_uav.Get();
        ImageNativeReference::Ptr<ID3D11ComputeShader> cs;check(SUCCEEDED(d->CreateComputeShader(programs[3].dxbc.data(),programs[3].dxbc.size(),nullptr,&cs)),"Native original text formatter");
        c->CSSetShader(cs.Get(),nullptr,0);c->CSSetShaderResources(100,1,&atlas_handle);c->CSSetShaderResources(113,1,&text_handle);
        c->CSSetShaderResources(114,1,&params_handle);c->CSSetShaderResources(120,1,&ini_handle);c->CSSetUnorderedAccessViews(0,1,&uav,nullptr);c->Dispatch(1,1,1);
        uav=nullptr;c->CSSetUnorderedAccessViews(0,1,&uav,nullptr);c->CSSetShader(nullptr,nullptr,0);
        D3D11_BUFFER_DESC read_desc{UINT(zeros.size()),D3D11_USAGE_STAGING,0,D3D11_CPU_ACCESS_READ,0,0};ImageNativeReference::Ptr<ID3D11Buffer> read;
        check(SUCCEEDED(d->CreateBuffer(&read_desc,nullptr,&read)),"Native text formatter staging");c->CopyResource(read.Get(),layout.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(SUCCEEDED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)),"Native text formatter bytes");positions.resize(zeros.size());memcpy(positions.data(),mapped.pData,positions.size());c->Unmap(read.Get(),0);
        c->GSSetShaderResources(100,1,&atlas_handle);c->GSSetShaderResources(112,1,&layout_handle);c->GSSetShaderResources(113,1,&text_handle);
        c->GSSetShaderResources(114,1,&params_handle);c->GSSetShaderResources(120,1,&ini_handle);c->PSSetShaderResources(100,1,&atlas_handle);c->PSSetShaderResources(114,1,&params_handle);
        D3D11_BLEND_DESC blend{};auto& b=blend.RenderTarget[0];b.BlendEnable=TRUE;b.SrcBlend=b.SrcBlendAlpha=D3D11_BLEND_ONE;
        // Original ParseCustomShaderBlendState defaults alpha to ONE/ZERO;
        // the source `blend` key changes RGB only, unless `alpha` is also set.
        b.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;b.DestBlendAlpha=D3D11_BLEND_ZERO;b.BlendOp=b.BlendOpAlpha=D3D11_BLEND_OP_ADD;b.RenderTargetWriteMask=15;
        ImageNativeReference::Ptr<ID3D11BlendState> state;check(SUCCEEDED(d->CreateBlendState(&blend,&state)),"Native text blending");c->OMSetBlendState(state.Get(),nullptr,UINT_MAX);
    });return {pixels,positions};
}
static void original_core_text(const std::filesystem::path& root){
    const auto source=root/"source";const auto programs=geometry_programs(source/"text.hlsl","vgpc");const auto font=text_file_bytes(source/"font.dds");
    // Font metadata is deliberately decoded independently in the native fixture.
    uint32_t header[32];check(font.size()>=sizeof(header),"Original font header exists");memcpy(header,font.data(),sizeof(header));
    check(header[0]==0x20534444 && header[3]==96 && header[4]==256 && header[7]==1 && header[20]==65 && header[22]==32 &&
        header[23]==0x00ff0000 && header[24]==0x0000ff00 && header[25]==0xff && header[26]==0xff000000,"Original font matches independent BGRA reference contract");
    Device d;d.init(false,true,true,true);
    {gpu::ResourceCommands resources(d.resource);auto host=shader_host(d);host.scalar_block_layout=false;gpu::ShaderCommands strict(resources,host);
        refuses([&]{strict.compile(programs[3]);},"Original text scalar storage layout requires actual host feature enablement");}
    for(unsigned which=0;which<3;++which){TextReferenceCase test;std::string message="ABC 123";
        if(which==1){message="One two three\nVulkan text\n";test.parameters[14]=2;test.parameters[15]=2;test.parameters[16]=1;test.dpi=144;}
        if(which==2){message=std::string(70,'A');test.width=1024;test.height=96;test.parameters[16]=2;test.parameters[14]=3;test.parameters[15]=3;}
        for(unsigned char c:message)test.text.push_back(c);test.text.push_back(0);
        const auto directory=root/std::to_string(which);std::filesystem::create_directories(directory/"Mods");
        std::filesystem::copy_file(source/"text.hlsl",directory/"Mods/text.hlsl",std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(source/"font.dds",directory/"Mods/font.dds",std::filesystem::copy_options::overwrite_existing);
        std::ostringstream ini;ini<<"namespace = compute\n[Constants]\nx = "<<test.width<<"\ny = "<<test.height<<"\nw = "<<test.dpi<<R"ini(
o0 = ref set_viewport ResourceOutput
clear = o0 0 0 0 0
[ResourceOutput]
type = Texture2D
array = 1
mips = 1
msaa = 1
format = R32G32B32A32_FLOAT
bind_flags = render_target shader_resource
)ini"<<"width = "<<test.width<<"\nheight = "<<test.height<<R"ini(
[ResourceFont]
filename = font.dds
[ResourceText]
type = Buffer
format = R32_UINT
data = R32_UINT)ini";for(auto c:test.text)ini<<' '<<c;
        ini<<R"ini(
[ResourceParams]
type = StructuredBuffer
stride = 72
data = R32_FLOAT)ini";for(float value:test.parameters)ini<<' '<<value;
        ini<<R"ini(
[ResourcePositions]
type = StructuredBuffer
stride = 16
array = 4096
bind_flags = unordered_access shader_resource
[CustomShaderFormat]
cs = text.hlsl
vs = null
gs = null
ps = null
cs-t100 = ResourceFont
cs-t113 = ResourceText
cs-t114 = ResourceParams
clear = ResourcePositions
cs-u0 = ResourcePositions
dispatch = 1, 1, 1
cs-u0 = null
cs-t100 = null
cs-t113 = null
cs-t114 = null
[CustomShaderRender]
vs = text.hlsl
gs = text.hlsl
ps = text.hlsl
cs = null
depth_enable = false
depth_clip_enable = false
cull = none
blend = add one inv_src_alpha
topology = point_list
gs-t100 = ResourceFont
ps-t100 = ResourceFont
gs-t112 = ResourcePositions
gs-t113 = ResourceText
gs-t114 = ResourceParams
ps-t114 = ResourceParams
draw = 4096, 0
[Present]
clear = o0 0 0 0 0
run = CustomShaderFormat
run = CustomShaderRender
)ini";
        ShaderFixture f(d,directory,ini.str().c_str());f.present();auto output=f.backend->resources().binding(L'\0',ResourceCopyTargetType::RENDER_TARGET,0).image;
        auto positions=f.backend->resources().binding(L'g',ResourceCopyTargetType::SHADER_RESOURCE,112).buffer;
        const auto actual=f.backend->resources().commands().readback(d.queue,output,image_region(output));
        const auto layout=f.backend->resources().commands().readback(d.queue,positions,0,4096*16);const auto expected=native_text(programs,font,test);
        check(layout.size()==expected.second.size(),"Native and Vulkan text formatter buffers have identical size");
        bool layout_equal=true;float layout_error=0;
        for(size_t i=0;i<layout.size();i+=16){layout_equal=layout_equal && !memcmp(layout.data()+i,expected.second.data()+i,8);
            for(unsigned field:{8u,12u}){float a,b;memcpy(&a,layout.data()+i+field,4);memcpy(&b,expected.second.data()+i+field,4);
                layout_error=std::max(layout_error,std::abs(a-b));layout_equal=layout_equal && std::isfinite(a) && std::abs(a-b)<=1e-6f;}}
        printf("Core text case%u formatter max-position-error=%.9g\n",which,layout_error);
        check(layout_equal,"Original Core formatter matches exact native chunk indices/counts and float positions within arithmetic tolerance");
        check(actual.size()==expected.first.size(),"Native and Vulkan text targets have identical extent");
        size_t differing=0;float max_error=0;for(size_t i=0;i<actual.size();i+=4){float a,b;memcpy(&a,actual.data()+i,4);memcpy(&b,expected.first.data()+i,4);
            const float error=std::abs(a-b);max_error=std::max(max_error,error);differing+=!std::isfinite(a) || error>1e-6f;}
        printf("Core text case%u out-of-tolerance channels=%zu max-error=%.9g\n",which,differing,max_error);
        if(differing){
            std::ofstream a(directory/"vulkan.rgba32f",std::ios::binary),n(directory/"native.rgba32f",std::ios::binary);
            a.write(reinterpret_cast<const char*>(actual.data()),actual.size());n.write(reinterpret_cast<const char*>(expected.first.data()),expected.first.size());}
        check(!differing,"Original Core VS/GS/PS text and font pixels match native DX11 within float blending tolerance");
        size_t visible=0,foreground=0;for(size_t i=0;i<actual.size();i+=16){float alpha;memcpy(&alpha,actual.data()+i+12,4);visible+=alpha>0;foreground+=alpha>.5f;}
        check(visible>50 && foreground>10,"Original font produces visible foreground glyphs and background, not an empty matching image");
        check(f.backend->draws()==1 && f.backend->dispatches()==1 && f.backend->graphics_pipelines()==1,"Original text executes formatter followed by retained geometry graphics");
        f.present();check(f.backend->graphics_pipelines()==1 && f.backend->draws()==2 && f.backend->dispatches()==2,"Original text reuses its geometry pipeline on the next frame");
        output.reset();positions.reset();f.release();
    }
}
