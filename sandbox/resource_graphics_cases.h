static void owned_graphics(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);
    write(root/"graphics.hlsl",R"hlsl(struct V{float4 position:SV_Position;float2 uv:TEXCOORD0;};
cbuffer Constants:register(b0){float4 Value;};
#ifdef VERTEX_SHADER
V main(uint id:SV_VertexID){V v;uint vertex=id%3;v.position=float4(vertex==2?6:-2,vertex==1?6:-2,Value.z,Value.w);v.uv=float2(.5,.5);return v;}
#else
Texture2D<float4> Image:register(t0);SamplerState Filter:register(s0);
float4 main(V v):SV_Target{return Image.SampleLevel(Filter,v.uv,0)*Value+float4(0,0,0,v.position.w);}
#endif
)hlsl");
    gpu::HlslCompiler compiler(L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");gpu::HlslRequest request;
    request.source=root/"graphics.hlsl";request.working_directory=root;gpu::HlslProgram vs,ps;std::string why;
    request.stage='v';if(!compiler.compile(request,vs,why))throw std::runtime_error("Compile owned graphics vertex: "+why);++checks;
    request.stage='p';if(!compiler.compile(request,ps,why))throw std::runtime_error("Compile owned graphics pixel: "+why);++checks;
    const float vertex[4]={0,0,0,2},pixel[4]={.25f,.5f,.75f,1},texel[4]={4,6,8,10};
    auto native_setup=[&](ID3D11Device* device,ID3D11DeviceContext* context){
        D3D11_BUFFER_DESC cb{16,D3D11_USAGE_DEFAULT,D3D11_BIND_CONSTANT_BUFFER,0,0,0};D3D11_SUBRESOURCE_DATA initial{vertex,0,0};
        ImageNativeReference::Ptr<ID3D11Buffer> v,p;check(SUCCEEDED(device->CreateBuffer(&cb,&initial,&v)),"Native graphics VS constants");
        initial.pSysMem=pixel;check(SUCCEEDED(device->CreateBuffer(&cb,&initial,&p)),"Native graphics PS constants");auto* a=v.Get();auto* b=p.Get();context->VSSetConstantBuffers(0,1,&a);context->PSSetConstantBuffers(0,1,&b);
        D3D11_TEXTURE2D_DESC td{1,1,1,1,DXGI_FORMAT_R32G32B32A32_FLOAT,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,0};initial={texel,16,0};
        ImageNativeReference::Ptr<ID3D11Texture2D> image;ImageNativeReference::Ptr<ID3D11ShaderResourceView> view;
        check(SUCCEEDED(device->CreateTexture2D(&td,&initial,&image)) && SUCCEEDED(device->CreateShaderResourceView(image.Get(),nullptr,&view)),"Native graphics sampled image");
        auto* source=view.Get();context->PSSetShaderResources(0,1,&source);ID3D11SamplerState* sampler=nullptr;context->PSSetSamplers(0,1,&sampler);
    };
    const auto expected=native_linked_graphics(vs,ps,{},native_setup);
    gpu::ResourceBudgets budgets;auto host=shader_host(d);
    {gpu::ResourceCommands resources(d.resource,budgets);gpu::ShaderCommands shaders(resources,host);
        gpu::GraphicsDescription state;state.colors={VK_FORMAT_R32G32B32A32_SFLOAT};state.blend.resize(1);state.blend[0].colorWriteMask=15;
        auto pipeline=shaders.compile_graphics({ps,vs},state);
        auto vertex_buffer=resources.create(16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,vertex,16),pixel_buffer=resources.create(16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,pixel,16);
        gpu::ImageDescription input;input.format=state.colors[0];input.usage=VK_IMAGE_USAGE_SAMPLED_BIT;auto source=resources.create_image(input);resources.write(source,image_region(source),texel,16);
        auto source_view=resources.view(source,VK_IMAGE_VIEW_TYPE_2D,input.format,source->full_range());
        VkSamplerCreateInfo info={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};info.minFilter=info.magFilter=VK_FILTER_LINEAR;info.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU=info.addressModeV=info.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;info.maxAnisotropy=1;info.minLod=-FLT_MAX;info.maxLod=FLT_MAX;
        auto sampler=shaders.sampler(info);
        std::vector<gpu::ShaderDescriptor> bindings(4);
        bindings[0].binding=gpu::HlslCompiler::binding('v','b',0);bindings[0].buffer=vertex_buffer;bindings[0].size=16;
        bindings[1].binding=gpu::HlslCompiler::binding('p','b',0);bindings[1].buffer=pixel_buffer;bindings[1].size=16;
        bindings[2].binding=gpu::HlslCompiler::binding('p','t',0);bindings[2].image=source_view;
        bindings[3].binding=gpu::HlslCompiler::binding('p','s',0);bindings[3].sampler=sampler;
        gpu::ImageDescription output=input;output.extent={4,4,1};output.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        auto first=resources.create_image(output),second=resources.create_image(output);resources.clear(first,first->full_range(),VkClearColorValue{});resources.clear(second,second->full_range(),VkClearColorValue{});
        auto target=[&](const gpu::Image& image){return resources.view(image,VK_IMAGE_VIEW_TYPE_2D,output.format,image->full_range());};
        gpu::GraphicsDraw draw;draw.colors={target(first)};draw.extent={4,4};draw.viewport={0,4,4,-4,0,1};draw.scissor={{0,0},{4,4}};draw.count=3;
        shaders.draw(pipeline,bindings,draw);auto actual=resources.readback(d.queue,first,image_region(first));
        check(actual==expected,"Production graphics descriptors and original native DX11 pixels agree");
        const float color[4]={1,3,6,12};for(unsigned i=0;i<16;++i)check(!memcmp(actual.data()+i*16,color,16),"Production graphics samples all pixels with source w and stage-separated constants");
        const auto baseline=host.objects->used();auto zero=draw;zero.count=0;zero.colors.clear();zero.extent={};shaders.draw(pipeline,{},zero);
        check(host.objects->used()==baseline,"Zero graphics draw allocates no invocation or descriptors");
        const float changed[4]={2,2,2,2};auto next=resources.create(16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,changed,16);
        shaders.draw(pipeline,bindings,draw);auto changed_bindings=bindings;changed_bindings[1].buffer=next;auto changed_draw=draw;changed_draw.colors={target(second)};
        shaders.draw(pipeline,changed_bindings,changed_draw);actual=resources.readback(d.queue,first,image_region(first));check(actual==expected,"Earlier recorded graphics retains its own descriptors and target");
        actual=resources.readback(d.queue,second,image_region(second));const float changed_color[4]={8,12,16,22};
        for(unsigned i=0;i<16;++i)check(!memcmp(actual.data()+i*16,changed_color,16),"Later graphics descriptor update reaches only the second target");
        const uint32_t elements[]={77,77,3,4,5};auto indices=resources.create(sizeof(elements),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,elements,sizeof(elements));
        auto indexed=draw;indexed.indexed=true;indexed.indices=indices;indexed.index_offset=8;indexed.vertex_offset=-3;indexed.first_instance=9;
        shaders.draw(pipeline,bindings,indexed);actual=resources.readback(d.queue,first,image_region(first));check(actual==expected,"Owned indexed drawing preserves buffer byte offset and signed base vertex");
        const VkDrawIndirectCommand direct_args{3,1,6,9};auto indirect=draw;indirect.arguments=resources.create(sizeof(direct_args),VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,&direct_args,sizeof(direct_args));
        shaders.draw(pipeline,bindings,indirect);actual=resources.readback(d.queue,first,image_region(first));check(actual==expected,"Owned indirect drawing consumes GPU-visible start vertex and instance");
        const VkDrawIndexedIndirectCommand indexed_args{3,1,0,-3,9};auto indirect_indexed=indexed;
        indirect_indexed.arguments=resources.create(sizeof(indexed_args),VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,&indexed_args,sizeof(indexed_args));
        shaders.draw(pipeline,bindings,indirect_indexed);actual=resources.readback(d.queue,first,image_region(first));check(actual==expected,"Owned indexed indirect drawing preserves signed GPU arguments");
        write(root/"vertex.hlsl","struct V{float4 position:SV_Position;float2 uv:TEXCOORD0;}; V main(float4 p:POSITION,float2 uv:TEXCOORD0){V v;v.position=p;v.uv=uv;return v;}");
        request.source=root/"vertex.hlsl";request.stage='v';gpu::HlslProgram stream_vertex;if(!compiler.compile(request,stream_vertex,why))throw std::runtime_error(why);++checks;
        struct Vertex {float p[4],uv[2];};const Vertex vertices[]={{{-2,-2,0,2},{.5f,.5f}},{{-2,6,0,2},{.5f,.5f}},{{6,-2,0,2},{.5f,.5f}}};
        auto stream_state=state;stream_state.streams={{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX}};
        for(const auto& input:stream_vertex.inputs)if(input.builtin==UINT32_MAX)stream_state.attributes.push_back({input.location,0,
            input.semantic.find("POSITION")==0?VK_FORMAT_R32G32B32A32_SFLOAT:VK_FORMAT_R32G32_SFLOAT,input.semantic.find("POSITION")==0?0u:16u});
        auto stream_pipeline=shaders.compile_graphics({stream_vertex,ps},stream_state);auto vertex_data=resources.create(sizeof(vertices),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertices,sizeof(vertices));
        auto stream_draw=draw;stream_draw.vertices={{0,vertex_data,0}};auto stream_bindings=bindings;stream_bindings.erase(stream_bindings.begin());
        shaders.draw(stream_pipeline,stream_bindings,stream_draw);actual=resources.readback(d.queue,first,image_region(first));
        const auto native_stream=native_linked_graphics(stream_vertex,ps,{},[&](ID3D11Device* device,ID3D11DeviceContext* context){native_setup(device,context);
            D3D11_BUFFER_DESC desc{sizeof(vertices),D3D11_USAGE_DEFAULT,D3D11_BIND_VERTEX_BUFFER,0,0,0};D3D11_SUBRESOURCE_DATA initial{vertices,0,0};
            ImageNativeReference::Ptr<ID3D11Buffer> buffer;check(SUCCEEDED(device->CreateBuffer(&desc,&initial,&buffer)),"Native vertex stream buffer");
            const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
                {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};ImageNativeReference::Ptr<ID3D11InputLayout> layout;
            check(SUCCEEDED(device->CreateInputLayout(elements,2,stream_vertex.dxbc.data(),stream_vertex.dxbc.size(),&layout)),"Native vertex input layout");
            auto* handle=buffer.Get();UINT stride=sizeof(Vertex),offset=0;context->IASetVertexBuffers(0,1,&handle,&stride,&offset);context->IASetInputLayout(layout.Get());});
        check(actual==native_stream && actual==expected,"Owned vertex attributes agree with original D3D11 input-layout pixels");
        auto excessive=stream_draw;excessive.first=1;refuses([&]{shaders.draw(stream_pipeline,stream_bindings,excessive);},"Direct vertex fetch outside the buffer refuses");
        stream_pipeline.reset();stream_bindings.clear();
        auto blend_state=state;auto& blend=blend_state.blend[0];blend.blendEnable=VK_TRUE;
        blend.srcColorBlendFactor=VK_BLEND_FACTOR_CONSTANT_COLOR;blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_CONSTANT_ALPHA;blend_state.blend_constants={{.5f,.5f,.5f,.5f}};
        {auto blended=shaders.compile_graphics({vs,ps},blend_state);resources.clear(first,first->full_range(),VkClearColorValue{});shaders.draw(blended,bindings,draw);
            actual=resources.readback(d.queue,first,image_region(first));
            const auto native_blend=native_linked_graphics(vs,ps,{},[&](ID3D11Device* device,ID3D11DeviceContext* context){native_setup(device,context);
                D3D11_BLEND_DESC desc{};auto& b=desc.RenderTarget[0];b.BlendEnable=TRUE;b.SrcBlend=b.SrcBlendAlpha=D3D11_BLEND_BLEND_FACTOR;
                b.DestBlend=b.DestBlendAlpha=D3D11_BLEND_ZERO;b.BlendOp=b.BlendOpAlpha=D3D11_BLEND_OP_ADD;b.RenderTargetWriteMask=15;
                ImageNativeReference::Ptr<ID3D11BlendState> state;check(SUCCEEDED(device->CreateBlendState(&desc,&state)),"Native constant blending state");
                const float factors[4]={.5f,.5f,.5f,.5f};context->OMSetBlendState(state.Get(),factors,UINT_MAX);});
            check(actual==native_blend && actual!=expected,"Owned constant blending matches native DX11 pixels");}
        gpu::ImageDescription depth_description;depth_description.format=VK_FORMAT_D32_SFLOAT;depth_description.extent={4,4,1};depth_description.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        auto depth_image=resources.create_image(depth_description);auto depth_view=resources.view(depth_image,VK_IMAGE_VIEW_TYPE_2D,depth_description.format,depth_image->full_range());
        const float depth_values[4]={0,0,1,2};auto depth_constants=resources.create(16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,depth_values,16);
        auto depth_bindings=bindings;depth_bindings[0].buffer=depth_constants;auto depth_draw=draw;depth_draw.depth=depth_view;
        for(bool greater:{false,true}){auto depth_state=state;depth_state.depth_format=depth_description.format;depth_state.depth.depthTestEnable=depth_state.depth.depthWriteEnable=VK_TRUE;
            depth_state.depth.depthCompareOp=greater?VK_COMPARE_OP_GREATER:VK_COMPARE_OP_LESS;
            auto depth_pipeline=shaders.compile_graphics({vs,ps},depth_state);resources.clear(first,first->full_range(),VkClearColorValue{});resources.clear(depth_image,depth_image->full_range(),VkClearDepthStencilValue{.25f,0});
            shaders.draw(depth_pipeline,depth_bindings,depth_draw);actual=resources.readback(d.queue,first,image_region(first));
            const auto native_depth=native_linked_graphics(vs,ps,{},[&](ID3D11Device* device,ID3D11DeviceContext* context){native_setup(device,context);
                D3D11_BUFFER_DESC cb{16,D3D11_USAGE_DEFAULT,D3D11_BIND_CONSTANT_BUFFER,0,0,0};D3D11_SUBRESOURCE_DATA values{depth_values,0,0};ImageNativeReference::Ptr<ID3D11Buffer> buffer;
                check(SUCCEEDED(device->CreateBuffer(&cb,&values,&buffer)),"Native depth-test vertex constants");auto* b=buffer.Get();context->VSSetConstantBuffers(0,1,&b);
                D3D11_TEXTURE2D_DESC td{4,4,1,1,DXGI_FORMAT_D32_FLOAT,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_DEPTH_STENCIL,0,0};ImageNativeReference::Ptr<ID3D11Texture2D> texture;ImageNativeReference::Ptr<ID3D11DepthStencilView> dsv;
                check(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&texture)) && SUCCEEDED(device->CreateDepthStencilView(texture.Get(),nullptr,&dsv)),"Native depth-test target");
                context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,.25f,0);ImageNativeReference::Ptr<ID3D11RenderTargetView> rtv;context->OMGetRenderTargets(1,&rtv,nullptr);auto* color=rtv.Get();context->OMSetRenderTargets(1,&color,dsv.Get());
                D3D11_DEPTH_STENCIL_DESC desc=CD3D11_DEPTH_STENCIL_DESC(D3D11_DEFAULT);desc.DepthFunc=greater?D3D11_COMPARISON_GREATER:D3D11_COMPARISON_LESS;
                ImageNativeReference::Ptr<ID3D11DepthStencilState> state;check(SUCCEEDED(device->CreateDepthStencilState(&desc,&state)),"Native depth compare state");context->OMSetDepthStencilState(state.Get(),0);});
            check(actual==native_depth,"Owned depth acceptance/rejection matches native DX11 pixels");auto depth_bytes=resources.readback(d.queue,depth_image,image_region(depth_image,0,0,1,VK_IMAGE_ASPECT_DEPTH_BIT));
            for(unsigned i=0;i<16;++i)check(value(depth_bytes,i*4)==(greater?.5f:.25f),"Owned depth writes reflect the enabled comparison");}
        depth_bindings.clear();
        auto missing=host;missing.shader_draw_parameters=false;gpu::ShaderCommands unavailable(resources,missing);
        refuses([&]{unavailable.compile_graphics({vs,ps},state);},"Graphics DrawParameters requires actual enablement");
        auto invalid=draw;invalid.viewport.width=0;refuses([&]{shaders.draw(pipeline,bindings,invalid);},"Invalid graphics viewport refuses before recording");
        invalid=draw;invalid.layers=2;refuses([&]{shaders.draw(pipeline,bindings,invalid);},"Framebuffer layers cannot exceed attachment view");
        auto wrong=bindings;wrong.pop_back();refuses([&]{shaders.draw(pipeline,wrong,draw);},"Missing graphics sampler refuses through shared descriptor checks");
        gpu::ResourceCommands foreign(d.resource);auto foreign_image=foreign.create_image(output);invalid=draw;invalid.colors={foreign.view(foreign_image,VK_IMAGE_VIEW_TYPE_2D,output.format,foreign_image->full_range())};
        refuses([&]{shaders.draw(pipeline,bindings,invalid);},"Graphics attachment from another queue domain refuses");
        auto denied=host;denied.objects=std::make_shared<assets::AllocationBudget>(0);gpu::ShaderCommands limited(resources,denied);
        refuses([&]{limited.compile_graphics({vs,ps},state);},"Graphics pipeline allocation budget refuses");check(!denied.objects->used(),"Failed graphics budget acquires no lease");
        shaders.draw(pipeline,bindings,draw);check(host.objects->used()>baseline,"Pending graphics retains descriptor and framebuffer owner");resources.discard();
        check(host.objects->used()==baseline,"Discard releases graphics invocation without submission");
        std::weak_ptr<gpu::ImageViewResource> retained=source_view;shaders.draw(pipeline,bindings,draw);auto ticket=resources.submit(d.queue);
        bindings.clear();changed_bindings.clear();wrong.clear();source_view.reset();source.reset();pipeline.reset();sampler.reset();
        check(!retained.expired(),"Submitted graphics retains descriptor views after generation handles retire");ticket->wait();ticket.reset();
        check(retained.expired(),"Completed graphics releases retired descriptor views");
    }
    check(!budgets.resident->used() && !budgets.staging->used() && !host.objects->used() && !host.sampler_objects->used(),"Owned graphics releases resident, staging and shader quotas");
}
