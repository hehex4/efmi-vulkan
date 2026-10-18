// The installed image panel (k/1.ini) end to end in the owned domain: the character mark, its
// keys, the window sample, both PNGs, the shipped shader caches and its CustomShader into the
// host back buffer. The reference is the same draw on native DX11 (WARP) with the shipped
// bytecode, the PNGs through the pinned WIC loader and the owned run's final IniParams.
struct PanelFixture {
    Device& device;gpu::ResourceBudgets budgets;gpu::ShaderHost host;
    std::shared_ptr<frontend::GpuShaderRuntime> backend;std::shared_ptr<frontend::IniRuntime> ini;std::string why;
    PanelFixture(Device& d,const std::filesystem::path& root):device(d),host(shader_host(d)){
        frontend::RuntimeConfig config{root,{},root/"settings","panel",root/"Mods"};
        backend=std::make_shared<frontend::GpuShaderRuntime>(d.resource,host,L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll",budgets);
        backend->enable_cached_bytecode(L"F:/vulkan/vkd3d-2.1-build/efmi-vkd3d-shader.dll",gpu::DxbcFloat32Mode::Software);
        backend->resources().begin(d.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
        ini=frontend::IniRuntime::load(config,backend,why);if(!ini)throw std::runtime_error("Load installed panel: "+why);++checks;finish();
    }
    void finish(){auto ticket=backend->resources().finish();if(ticket)ticket->wait();}
    void begin(){backend->resources().begin(device.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);}
    void release(){ini.reset();backend.reset();check(!budgets.resident->used() && !budgets.staging->used() && !host.objects->used(),"Panel generation retires its budgets");}
};
static std::vector<uint8_t> panel_backbuffer_initial(uint32_t width,uint32_t height){
    std::vector<uint8_t> bytes(size_t(width)*height*4);
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){auto* p=&bytes[(size_t(y)*width+x)*4];
        p[0]=uint8_t(x*255/width);p[1]=uint8_t(y*255/height);p[2]=uint8_t((x+y)&0xff);p[3]=255;}
    return bytes;
}
static std::vector<uint8_t> panel_file(const std::filesystem::path& path){
    std::ifstream in(path,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
    check(!bytes.empty(),"Read installed panel file");return bytes;
}
static std::string panel_native_adapter;
static std::vector<uint8_t> native_panel(const std::filesystem::path& k,const std::vector<float>& parameters,uint32_t width,uint32_t height,bool icon,D3D_DRIVER_TYPE driver){
    using Microsoft::WRL::ComPtr;const auto library=LoadLibraryExW(L"d3d11.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);check(library!=nullptr,"System D3D11 for the panel reference");
    const auto create=reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(library,"D3D11CreateDevice"));const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if(!create || FAILED(create(nullptr,driver,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,nullptr,&context))){FreeLibrary(library);return {};}
    auto* d=device.Get();auto* c=context.Get();
    if(driver==D3D_DRIVER_TYPE_HARDWARE){ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC description{};
        if(SUCCEEDED(device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&description))){
            char name[128]={};WideCharToMultiByte(CP_UTF8,0,description.Description,-1,name,sizeof(name)-1,nullptr,nullptr);panel_native_adapter=name;printf("Image panel native adapter: %s\n",name);}}
    const auto vs_code=panel_file(k/"Draw_2D.vs_5_0.8000.bin"),ps_code=panel_file(k/"Draw_2D.ps_5_0.8000.bin");
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    check(SUCCEEDED(d->CreateVertexShader(vs_code.data(),vs_code.size(),nullptr,&vs)) && SUCCEEDED(d->CreatePixelShader(ps_code.data(),ps_code.size(),nullptr,&ps)),
          "Native device accepts the shipped panel bytecode");
    const HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    auto picture=[&](const wchar_t* name){ComPtr<ID3D11Resource> resource;ComPtr<ID3D11ShaderResourceView> view;
        check(SUCCEEDED(DirectX::CreateWICTextureFromFileEx(d,(k/name).c_str(),0,D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,0,DirectX::WIC_LOADER_DEFAULT,&resource,nullptr)) &&
              SUCCEEDED(d->CreateShaderResourceView(resource.Get(),nullptr,&view)),"Native WIC loader reads the panel picture");return view;};
    auto image=picture(L"k.png");ComPtr<ID3D11ShaderResourceView> icon_view;if(icon)icon_view=picture(L"panel_icon.png");
    const UINT count=UINT(parameters.size()/4);D3D11_TEXTURE1D_DESC pd{count,1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,0};
    D3D11_SUBRESOURCE_DATA pdata{parameters.data(),count*16,0};ComPtr<ID3D11Texture1D> ptexture;ComPtr<ID3D11ShaderResourceView> pview;
    check(SUCCEEDED(d->CreateTexture1D(&pd,&pdata,&ptexture)) && SUCCEEDED(d->CreateShaderResourceView(ptexture.Get(),nullptr,&pview)),"Native panel IniParams");
    const auto initial=panel_backbuffer_initial(width,height);D3D11_SUBRESOURCE_DATA bdata{initial.data(),width*4,0};
    D3D11_TEXTURE2D_DESC bd{width,height,1,1,DXGI_FORMAT_R8G8B8A8_UNORM,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE,0,0};
    ComPtr<ID3D11Texture2D> back,read;ComPtr<ID3D11RenderTargetView> rtv;
    check(SUCCEEDED(d->CreateTexture2D(&bd,&bdata,&back)) && SUCCEEDED(d->CreateRenderTargetView(back.Get(),nullptr,&rtv)),"Native panel back buffer");
    bd.Usage=D3D11_USAGE_STAGING;bd.BindFlags=0;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(SUCCEEDED(d->CreateTexture2D(&bd,nullptr,&read)),"Native panel readback");
    // [CustomShaderDraw]: blend = ADD SRC_ALPHA INV_SRC_ALPHA (color only), cull = none,
    // topology = triangle_strip, o0 = set_viewport bb, Draw = 4,0; no depth target is bound.
    D3D11_BLEND_DESC blend{};blend.RenderTarget[0]={TRUE,D3D11_BLEND_SRC_ALPHA,D3D11_BLEND_INV_SRC_ALPHA,D3D11_BLEND_OP_ADD,D3D11_BLEND_ONE,D3D11_BLEND_ZERO,D3D11_BLEND_OP_ADD,15};
    D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
    ComPtr<ID3D11BlendState> blend_state;ComPtr<ID3D11RasterizerState> raster;
    check(SUCCEEDED(d->CreateBlendState(&blend,&blend_state)) && SUCCEEDED(d->CreateRasterizerState(&rs,&raster)),"Native panel state");
    auto* target=rtv.Get();c->OMSetRenderTargets(1,&target,nullptr);c->OMSetBlendState(blend_state.Get(),nullptr,0xffffffff);c->RSSetState(raster.Get());
    D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};c->RSSetViewports(1,&viewport);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11ShaderResourceView* views[2]={image.Get(),icon_view.Get()};auto* parameter_view=pview.Get();
    c->VSSetShaderResources(120,1,&parameter_view);c->PSSetShaderResources(120,1,&parameter_view);c->PSSetShaderResources(100,2,views);
    c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->Draw(4,0);c->CopyResource(read.Get(),back.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};check(SUCCEEDED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)),"Map native panel");
    std::vector<uint8_t> bytes(size_t(width)*height*4);
    for(uint32_t y=0;y<height;++y)memcpy(bytes.data()+size_t(y)*width*4,static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(width)*4);
    c->Unmap(read.Get(),0);if(SUCCEEDED(apartment))CoUninitialize();
    vs.Reset();ps.Reset();image.Reset();icon_view.Reset();ptexture.Reset();pview.Reset();back.Reset();read.Reset();rtv.Reset();blend_state.Reset();raster.Reset();
    context.Reset();device.Reset();FreeLibrary(library);return bytes;
}
struct PanelDifference {size_t covered=0,differing=0;int largest=0;};
static PanelDifference panel_difference(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b,const std::vector<uint8_t>& initial){
    PanelDifference out;
    for(size_t i=0;i+3<a.size() && i+3<b.size();i+=4){bool changed=false,other=false;
        for(size_t c=0;c<4;++c){changed|=b[i+c]!=initial[i+c];const int delta=std::abs(int(a[i+c])-int(b[i+c]));if(delta){other=true;out.largest=std::max(out.largest,delta);}}
        out.covered+=changed;out.differing+=other;}
    return out;
}
static void original_image_panel(Device& d,const std::filesystem::path& root){
    std::ifstream mark_file(root/"mark.txt");uint32_t hash=0,first=0,count=0;
    if(!(mark_file>>hash>>first>>count)){printf("Image panel: no installed panel with shader caches\n");return;}
    const auto k=root/"Mods/panel/k";constexpr uint32_t width=800,height=450;
    PanelFixture f(d,root);auto& resources=f.backend->resources();
    check(f.backend->cached_programs()==2,"The panel's CustomShader runs the shipped cached bytecode, as the original loads it");
    gpu::ImageDescription description;description.format=VK_FORMAT_R8G8B8A8_UNORM;description.extent={width,height,1};
    description.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    f.begin();auto back=resources.commands().create_image(description);f.finish();
    const auto initial=panel_backbuffer_initial(width,height);
    frontend::RuntimeInput input;input.foreground=true;input.window={true,0,0,int32_t(width),int32_t(height)};
    input.cursor={true,120,200};input.resolution_width=width;input.resolution_height=height;
    // Each frame: the character draw sets the mark, Present runs the panel. Ctrl+K on the first
    // frame opens it; it fades in over twenty frames. The back buffer is re-rendered every frame.
    for(unsigned frame=1;frame<=24;++frame){
        f.begin();VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};
        resources.commands().write(back,copy,initial.data(),initial.size());
        frontend::RuntimeDraw draw;draw.hash=hash;draw.first=first;draw.count=count;draw.frame=frame;draw.seconds=frame/60.0f;
        efmi::assets::FilePlan plan;check(f.ini->draw(draw,plan,f.why),"Character mark draw runs the panel's TextureOverride");
        input.keys.fill(false);if(frame==1){input.keys[VK_CONTROL]=true;input.keys['K']=true;}
        resources.backbuffer(back,DXGI_FORMAT_R8G8B8A8_UNORM);
        if(!f.ini->present(input,frame,frame/60.0f,f.why))throw std::runtime_error("Installed panel Present: "+f.why);++checks;f.finish();
    }
    const auto variables=f.ini->status().variables;
    auto value=[&](const char* name){for(const auto& [key,v]:variables)if(key.size()>strlen(name) && key.compare(key.size()-strlen(name),strlen(name),name)==0 && key.find("panel")!=std::string::npos)return v;
        throw std::runtime_error(std::string("Panel variable missing: ")+name);};
    check(value("\\help")==1 && value("\\fade")==20,"Ctrl+K on the marked character opens the panel and it fades in");
    const auto actual=resources.commands().readback(d.queue,back,image_region(back));
    std::vector<float> parameters;
    {   const auto parameter_image=resources.ini_parameters().image;check(bool(parameter_image),"Owned IniParams image");
        const auto raw=resources.commands().readback(d.queue,parameter_image,image_region(parameter_image));
        parameters.resize(raw.size()/4);memcpy(parameters.data(),raw.data(),parameters.size()*4);}
    const bool icon=value("\\minimized")==1;
    const auto warp=native_panel(k,parameters,width,height,icon,D3D_DRIVER_TYPE_WARP);check(!warp.empty(),"Native WARP panel reference");
    const auto hardware=native_panel(k,parameters,width,height,icon,D3D_DRIVER_TYPE_HARDWARE);
    const auto owned_warp=panel_difference(actual,warp,initial);
    printf("Image panel %s: WARP draws %zu of %zu pixels; owned Vulkan differs from WARP in %zu pixels, largest channel difference %d\n",
           k.parent_path().filename().string().c_str(),owned_warp.covered,actual.size()/4,owned_warp.differing,owned_warp.largest);
    check(!hardware.empty(),"Native hardware D3D11 panel reference");
    printf("Image panel Vulkan adapter: %s\n",d.properties.deviceName);
    check(panel_native_adapter==d.properties.deviceName,"The native panel reference runs on the adapter of the owned Vulkan device");
    const auto hardware_warp=panel_difference(hardware,warp,initial),owned_hardware=panel_difference(actual,hardware,initial);
    printf("Image panel: native hardware D3D11 differs from WARP in %zu pixels (largest %d); owned Vulkan differs from native hardware in %zu pixels (largest %d)\n",
           hardware_warp.differing,hardware_warp.largest,owned_hardware.differing,owned_hardware.largest);
    check(owned_warp.covered>width*height/10,"The native panel covers a substantial part of the back buffer");
    check(actual!=initial,"The owned panel draws into the host back buffer");
    // The shader is dominated by sin, exp, pow and atan2, whose precision both APIs leave to the
    // implementation: WARP and the hardware D3D11 driver themselves differ in tens of thousands of
    // pixels. On the same adapter the owned result must equal the native driver's except for
    // isolated pixels at thresholds: at most 0.01% of the back buffer, each within 16 levels.
    check(owned_hardware.differing<=size_t(width)*height/10000 && owned_hardware.largest<=16,
          "The installed panel in the owned domain matches native DX11 on the same adapter");
    back.reset();f.release();
}
