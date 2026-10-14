struct NativeBufferBindings {
    ImageNativeReference native;
    ImageNativeReference::Ptr<ID3D11Buffer> buffer;
    ImageNativeReference::Ptr<ID3D11ShaderResourceView> srv;
    ImageNativeReference::Ptr<ID3D11UnorderedAccessView> uav;
    NativeBufferBindings(D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0):native(level){
        D3D11_BUFFER_DESC desc{256,D3D11_USAGE_DEFAULT,D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_INDEX_BUFFER|
            D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,0,0,0};
        check(SUCCEEDED(native.device->CreateBuffer(&desc,nullptr,&buffer)),"Native binding reference buffer");
        D3D11_SHADER_RESOURCE_VIEW_DESC sr{};sr.Format=DXGI_FORMAT_R32_UINT;sr.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sr.Buffer.NumElements=16;
        check(SUCCEEDED(native.device->CreateShaderResourceView(buffer.Get(),&sr,&srv)),"Native binding reference SRV");
        D3D11_UNORDERED_ACCESS_VIEW_DESC ur{};ur.Format=DXGI_FORMAT_R32_UINT;ur.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ur.Buffer.FirstElement=32;ur.Buffer.NumElements=32;
        check(SUCCEEDED(native.device->CreateUnorderedAccessView(buffer.Get(),&ur,&uav)),"Native binding reference UAV");
    }
    void input(){auto* view=srv.Get();native.context->VSSetShaderResources(0,1,&view);}
    void output(unsigned slot=0){auto* view=uav.Get();native.context->CSSetUnorderedAccessViews(slot,1,&view,nullptr);}
    void pixel(){auto* view=uav.Get();native.context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,1,1,&view,nullptr);}
    void ia(){auto* value=buffer.Get();UINT stride=4,offset=0;native.context->IASetVertexBuffers(0,1,&value,&stride,&offset);native.context->IASetIndexBuffer(value,DXGI_FORMAT_R32_UINT,0);}
    std::array<bool,6> state(){
        ImageNativeReference::Ptr<ID3D11ShaderResourceView> s;
        ImageNativeReference::Ptr<ID3D11UnorderedAccessView> c0,c1,p;
        ImageNativeReference::Ptr<ID3D11Buffer> v,i;UINT stride=0,offset=0;DXGI_FORMAT format;
        native.context->VSGetShaderResources(0,1,&s);native.context->CSGetUnorderedAccessViews(0,1,&c0);native.context->CSGetUnorderedAccessViews(1,1,&c1);
        native.context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,1,1,&p);
        native.context->IAGetVertexBuffers(0,1,&v,&stride,&offset);native.context->IAGetIndexBuffer(&i,&format,&offset);
        return {bool(s),bool(c0),bool(c1),bool(p),bool(v),bool(i)};
    }
};
static void original_buffer_hazards(Device& d,const std::filesystem::path& root){
    NativeBufferBindings native;
    const std::string input="vs-t0 = ref ResourceData->region(0, 64)\n",output="cs-u0 = ref ResourceData->region(128, 256)\n";
    const std::string ia="vb0 = ResourceData\nib = ResourceData\n",pixel="ps-u1 = ResourceData\n";
    using Case=std::pair<std::string,std::function<void()>>;
    const std::vector<Case> cases={
        {output+input,[&]{native.output();native.input();}},
        {input+output,[&]{native.input();native.output();}},
        {ia+output,[&]{native.ia();native.output();}},
        {output+ia,[&]{native.output();native.ia();}},
        {output+"cs-u1 = ResourceData\n",[&]{native.output();native.output(1);}},
        {output+pixel,[&]{native.output();native.pixel();}},
        {pixel+output,[&]{native.pixel();native.output();}},
        {pixel+"o0 = null\n",[&]{native.pixel();ID3D11RenderTargetView* views[8]={};native.native.context->OMSetRenderTargets(8,views,nullptr);}},
    };
    unsigned index=0;for(const auto& item:cases){native.native.context->ClearState();item.second();const auto expected=native.state();
        std::string source="namespace = images\n[ResourceData]\ntype = Buffer\nformat = R32_UINT\nstride = 4\n"
            "bind_flags = vertex_buffer index_buffer shader_resource unordered_access\ndata = R32_UINT";
        for(unsigned i=0;i<64;++i)source+=" 0";source+="\n[Present]\n"+item.first;
        ImageIniFixture f(d,root/std::to_string(index),source);f.present();using T=ResourceCopyTargetType;
        const std::array<bool,6> actual={bool(f.backend->binding(L'v',T::SHADER_RESOURCE,0)),bool(f.backend->binding(L'c',T::UNORDERED_ACCESS_VIEW,0)),
            bool(f.backend->binding(L'c',T::UNORDERED_ACCESS_VIEW,1)),bool(f.backend->binding(L'p',T::UNORDERED_ACCESS_VIEW,1)),
            bool(f.backend->binding(0,T::VERTEX_BUFFER,0)),bool(f.backend->binding(0,T::INDEX_BUFFER,0))};
        if(actual!=expected)throw std::runtime_error("Source buffer binding hazard differs from native DX11, case "+std::to_string(index));++checks;
        f.release();++index;
    }
}
static std::string binding_buffer_source(){
    std::string source="namespace = images\n[ResourceData]\ntype = Buffer\nformat = R32_UINT\nstride = 4\n"
        "bind_flags = vertex_buffer index_buffer shader_resource unordered_access\ndata = R32_UINT";
    for(unsigned i=0;i<64;++i)source+=" 0";
    return source+"\n[ResourceView]\n";
}
static void original_binding_batches(Device& d,const std::filesystem::path& root){
    using T=ResourceCopyTargetType;
    // Invalid native batches run on fresh devices: WARP can otherwise let an
    // invalid call contaminate later hazard observations despite ClearState.
    for(unsigned prior=0;prior<2;++prior){
        NativeBufferBindings native;if(prior)native.output();auto* u=native.uav.Get();ID3D11UnorderedAccessView* values[]={u,u};
        native.native.context->CSSetUnorderedAccessViews(0,2,values,nullptr);const auto expected=native.state();
        std::string source=binding_buffer_source()+"[Present]\ncs-u3 = ResourceData\nResourceView = ref cs-u3\ncs-u3 = null\n";
        if(prior)source+="cs-u0 = ResourceData\n";
        source+="cs-u[0:1] = ref ResourceView\n";
        ImageIniFixture f(d,root/std::to_string(prior),source);f.present();
        check(bool(f.backend->binding(L'c',T::UNORDERED_ACCESS_VIEW,0))==expected[1] &&
            bool(f.backend->binding(L'c',T::UNORDERED_ACCESS_VIEW,1))==expected[2],"Conflicting output range is ignored atomically like native D3D11");
        f.release();
    }
    // All six source shader stages participate in output hazard resolution.
    std::string source=binding_buffer_source()+"[Present]\n";
    for(auto stage:std::string("vhdgpc"))source+=std::string(1,stage)+"s-t0 = ResourceData\n";
    source+="cs-u0 = ResourceData\n";
    ImageIniFixture all(d,root/"all-stages",source);all.present();
    for(auto stage:std::wstring(L"vhdgpc"))check(!all.backend->binding(stage,T::SHADER_RESOURCE,0),"Writer clears every conflicting source shader stage");
    all.release();
    // The original restore helper captures only eight pixel UAV slots. Both
    // OMSetRenderTargets and explicit complete OM restore clear higher slots.
    for(unsigned restore=0;restore<2;++restore){
        NativeBufferBindings native(D3D_FEATURE_LEVEL_11_1);auto* u=native.uav.Get();auto* ctx=native.native.context.Get();
        ctx->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,10,1,&u,nullptr);
        ImageNativeReference::Ptr<ID3D11UnorderedAccessView> high;ctx->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,10,1,&high);
        check(bool(high),"Native feature-level 11.1 accepts a high pixel UAV slot");high.Reset();
        ID3D11RenderTargetView* targets[8]={};ID3D11UnorderedAccessView* empty[8]={};
        if(restore)ctx->OMSetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,8,empty,nullptr);
        else ctx->OMSetRenderTargets(8,targets,nullptr);
        ctx->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,10,1,&high);check(!high,"Native complete OM bind clears high pixel UAV slots");
        // Empty CustomShader calls are removed by the original optimizer.
        auto text=binding_buffer_source()+"[CustomShaderNoop]\ncs-t15 = null\n[Present]\nps-u10 = ResourceData\n";
        text+=restore?"pre run = CustomShaderNoop\n":"o0 = null\n";
        ShaderFixture f(d,root/(restore?"high-restore":"high-target"),text.c_str());f.present();
        check(!f.backend->resources().binding(L'p',T::UNORDERED_ACCESS_VIEW,10),"Original OM assignment and restore discard unsaved high pixel UAVs");f.release();
    }
}
static void original_restore_hazards(Device& d,const std::filesystem::path& root){
    using T=ResourceCopyTargetType;
    for(unsigned kind=0;kind<3;++kind){
        NativeBufferBindings native;
        if(kind==0)native.input();else if(kind==1)native.output();else native.ia();
        native.pixel();const auto expected=native.state();
        auto source=binding_buffer_source()+"[Constants]\nps-u1 = ResourceData\n[CustomShaderNested]\nps-u1 = null\n";
        source+=kind==0?"vs-t0 = ResourceData\n":kind==1?"cs-u0 = ResourceData\n":"vb0 = ResourceData\nib = ResourceData\n";
        source+="[Present]\npre run = CustomShaderNested\n";
        ShaderFixture f(d,root/std::to_string(kind),source.c_str());f.present();auto& r=f.backend->resources();
        const std::array<bool,6> actual={bool(r.binding(L'v',T::SHADER_RESOURCE,0)),bool(r.binding(L'c',T::UNORDERED_ACCESS_VIEW,0)),
            bool(r.binding(L'c',T::UNORDERED_ACCESS_VIEW,1)),bool(r.binding(L'p',T::UNORDERED_ACCESS_VIEW,1)),
            bool(r.binding(0,T::VERTEX_BUFFER,0)),bool(r.binding(0,T::INDEX_BUFFER,0))};
        check(actual==expected,"CustomShader OM restore clears native-conflicting inputs and compute outputs");f.release();
    }
}
static void original_image_binding_ranges(Device& d){
    using B=frontend::GpuResourceBinding;
    ImageNativeReference native;using Ptr=ImageNativeReference::Ptr<ID3D11Texture2D>;
    Ptr image;D3D11_TEXTURE2D_DESC desc{4,4,2,2,DXGI_FORMAT_R8G8B8A8_UNORM,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET,0,0};
    check(SUCCEEDED(native.device->CreateTexture2D(&desc,nullptr,&image)),"Native image hazard texture");
    D3D11_RENDER_TARGET_VIEW_DESC rt{};rt.Format=desc.Format;rt.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;rt.Texture2DArray={0,0,1};
    ImageNativeReference::Ptr<ID3D11RenderTargetView> target;check(SUCCEEDED(native.device->CreateRenderTargetView(image.Get(),&rt,&target)),"Native image hazard RTV");
    auto* raw=target.Get();native.context->OMSetRenderTargets(1,&raw,nullptr);
    gpu::ResourceCommands commands(d.resource);gpu::ImageDescription description;description.format=VK_FORMAT_R8G8B8A8_UNORM;
    description.extent={4,4,1};description.mips=2;description.layers=2;description.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    B output;output.image=commands.create_image(description);output.image_view=commands.view(output.image,VK_IMAGE_VIEW_TYPE_2D_ARRAY,description.format,
        {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    for(unsigned variant=0;variant<4;++variant){
        const unsigned mip=variant==1?1:0,layer=variant==2?1:0,layers=variant==3?2:1;
        D3D11_SHADER_RESOURCE_VIEW_DESC sr{};sr.Format=desc.Format;sr.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;sr.Texture2DArray={mip,1,layer,layers};
        ImageNativeReference::Ptr<ID3D11ShaderResourceView> srv,got;check(SUCCEEDED(native.device->CreateShaderResourceView(image.Get(),&sr,&srv)),"Native image hazard SRV");
        auto* read=srv.Get();native.context->PSSetShaderResources(0,1,&read);native.context->PSGetShaderResources(0,1,&got);
        B input;input.image=output.image;input.image_view=commands.view(input.image,VK_IMAGE_VIEW_TYPE_2D_ARRAY,description.format,
            {VK_IMAGE_ASPECT_COLOR_BIT,mip,1,layer,layers},VK_IMAGE_USAGE_SAMPLED_BIT);
        check(frontend::source_bindings_overlap(output,input)==!got,"Owned mip/layer overlap agrees with native source binding hazards");
    }
}
static void original_attachment_bindings(Device& d,const std::filesystem::path& root){
    using T=ResourceCopyTargetType;
    for(unsigned depth=0;depth<2;++depth)for(unsigned size:{2u,4u,8u}){
        // A fresh device also isolates the deliberately invalid size cases.
        ImageNativeReference native;D3D11_TEXTURE2D_DESC td{4,4,1,1,DXGI_FORMAT_R8G8B8A8_UNORM,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET,0,0};
        ImageNativeReference::Ptr<ID3D11Texture2D> a,b;ImageNativeReference::Ptr<ID3D11RenderTargetView> r,s;
        ImageNativeReference::Ptr<ID3D11DepthStencilView> z;
        check(SUCCEEDED(native.device->CreateTexture2D(&td,nullptr,&a)) && SUCCEEDED(native.device->CreateRenderTargetView(a.Get(),nullptr,&r)),"Native primary attachment");
        td.Width=td.Height=size;if(depth){td.Format=DXGI_FORMAT_D32_FLOAT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;}
        check(SUCCEEDED(native.device->CreateTexture2D(&td,nullptr,&b)),"Native secondary attachment");
        if(depth)check(SUCCEEDED(native.device->CreateDepthStencilView(b.Get(),nullptr,&z)),"Native depth attachment");
        else check(SUCCEEDED(native.device->CreateRenderTargetView(b.Get(),nullptr,&s)),"Native second color attachment");
        ID3D11RenderTargetView* targets[8]={r.Get()};native.context->OMSetRenderTargets(8,targets,nullptr);
        if(!depth)targets[1]=s.Get();native.context->OMSetRenderTargets(8,targets,z.Get());
        ID3D11RenderTargetView* result[8]={};ImageNativeReference::Ptr<ID3D11DepthStencilView> ds;
        native.context->OMGetRenderTargets(8,result,&ds);const bool expected=depth?bool(ds):bool(result[1]);
        for(auto* view:result)if(view)view->Release();
        std::string source="namespace = images\n[ResourceA]\ntype = Texture2D\nwidth = 4\nheight = 4\narray = 1\nmips = 1\nmsaa = 1\nformat = R8G8B8A8_UNORM\nbind_flags = render_target\n";
        source+="[ResourceB]\ntype = Texture2D\nwidth = "+std::to_string(size)+"\nheight = "+std::to_string(size)+"\narray = 1\nmips = 1\nmsaa = 1\n";
        source+=depth?"format = D32_FLOAT\nbind_flags = depth_stencil\n":"format = R8G8B8A8_UNORM\nbind_flags = render_target\n";
        source+="[Present]\no0 = ResourceA\n";source+=depth?"od = ResourceB\n":"o1 = ResourceB\n";
        ImageIniFixture f(d,root/(std::to_string(depth)+"-"+std::to_string(size)),source);f.present();
        check(bool(f.backend->binding(0,depth?T::DEPTH_STENCIL_TARGET:T::RENDER_TARGET,depth?0:1))==expected && f.backend->binding(0,T::RENDER_TARGET,0),
            "Native attachment dimensions reject complete invalid calls and allow larger depth targets");f.release();
    }
}
static void original_pixel_binding_sets(Device& d,const std::filesystem::path& root){
    using T=ResourceCopyTargetType;
    for(auto stage:{L'p',L'c'})for(unsigned empty=0;empty<2;++empty){
        ImageNativeReference native;std::string source="namespace = images\n";
        ImageNativeReference::Ptr<ID3D11Buffer> buffers[3];ImageNativeReference::Ptr<ID3D11UnorderedAccessView> views[3];
        D3D11_BUFFER_DESC desc{4,D3D11_USAGE_DEFAULT,D3D11_BIND_UNORDERED_ACCESS,0,0,0};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=DXGI_FORMAT_R32_UINT;uv.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;uv.Buffer.NumElements=1;
        for(unsigned i=0;i<3;++i){
            check(SUCCEEDED(native.device->CreateBuffer(&desc,nullptr,&buffers[i])) &&
                SUCCEEDED(native.device->CreateUnorderedAccessView(buffers[i].Get(),&uv,&views[i])),"Native independent UAV allocation");
            source+="[Resource"+std::to_string(i)+"]\ntype = Buffer\nformat = R32_UINT\ndata = R32_UINT 0\nbind_flags = unordered_access\n";
        }
        source+="[Present]\n";
        for(unsigned i:{0u,2u,1u}){auto* view=(empty && i==1)?nullptr:views[i].Get();
            if(stage==L'p')native.context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,i,1,&view,nullptr);
            else native.context->CSSetUnorderedAccessViews(i,1,&view,nullptr);
            source+=std::string(1,char(stage))+"s-u"+std::to_string(i)+" = "+((empty && i==1)?"null":"Resource"+std::to_string(i))+"\n";
        }
        ID3D11UnorderedAccessView* result[3]={};
        if(stage==L'p')native.context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,3,result);
        else native.context->CSGetUnorderedAccessViews(0,3,result);
        std::array<bool,3> expected{};for(unsigned i=0;i<3;++i){expected[i]=bool(result[i]);if(result[i])result[i]->Release();}
        ImageIniFixture f(d,root/(std::string(1,char(stage))+std::to_string(empty)),source);f.present();
        for(unsigned i=0;i<3;++i)check(bool(f.backend->binding(stage,T::UNORDERED_ACCESS_VIEW,i))==expected[i],
            "Pixel UAV assignment replaces the entire set; compute assignment preserves other registers");f.release();
    }
    for(unsigned slot:{0u,3u}){
        ImageNativeReference native;D3D11_TEXTURE2D_DESC desc{2,1,1,1,DXGI_FORMAT_R8G8B8A8_UNORM,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET,0,0};
        ImageNativeReference::Ptr<ID3D11Texture2D> image;ImageNativeReference::Ptr<ID3D11RenderTargetView> view;
        check(SUCCEEDED(native.device->CreateTexture2D(&desc,nullptr,&image)) &&
            SUCCEEDED(native.device->CreateRenderTargetView(image.Get(),nullptr,&view)),"Native RTV register reference");
        ID3D11RenderTargetView* targets[8]={};targets[slot]=view.Get();native.context->OMSetRenderTargets(8,targets,nullptr);
        ID3D11UnorderedAccessView* empty=nullptr;
        native.context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,1,1,&empty,nullptr);
        ID3D11RenderTargetView* result[8]={};native.context->OMGetRenderTargets(8,result,nullptr);const bool expected=bool(result[slot]);
        for(auto* value:result)if(value)value->Release();
        const auto source="namespace = images\n"+image_declaration("ResourceImage","R8G8B8A8_UNORM","render_target")+
            "[Present]\no"+std::to_string(slot)+" = ResourceImage\nps-u1 = null\n";
        ImageIniFixture f(d,root/("rtv"+std::to_string(slot)),source);f.present();
        check(bool(f.backend->binding(0,T::RENDER_TARGET,slot))==expected,"Null pixel UAVs preserve lower RTVs and remove overlapping register slots");f.release();
    }
}
