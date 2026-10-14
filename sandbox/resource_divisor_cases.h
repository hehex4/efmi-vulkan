// Actual enabled KHR instance divisors, with original INI and native WARP bytes.
static void original_layout_divisors(Device& disabled,const std::filesystem::path& root){
    Device d;d.init(false,false,true,false,true);const auto programs=layout_programs(root);
    const uint32_t maximum=std::min(d.max_vertex_divisor,UINT32_MAX);
    for(uint32_t divisor:{0u,2u,3u,maximum})for(unsigned form=0;form<4;++form){
        const auto directory=root/(std::to_string(divisor)+"-"+std::to_string(form));std::filesystem::create_directories(directory/"Mods");
        std::filesystem::copy_file(root/"Mods/layout.hlsl",directory/"Mods/layout.hlsl",std::filesystem::copy_options::overwrite_existing);
        auto elements=layout_elements();for(auto& e:elements){e.classification=D3D11_INPUT_PER_INSTANCE_DATA;e.step=divisor;}
        auto layout=std::make_shared<frontend::SourceInputLayout>(elements,programs[0].input_signature);
        const char* calls[]={"drawinstanced = 3, 4, 0, 1","drawindexedinstanced = 3, 4, 0, 0, 1",
            "drawinstancedindirect = ResourceArguments, 0","drawindexedinstancedindirect = ResourceIndexedArguments, 0"};
        const std::string ini=std::string(layout_resources)+R"ini(
[ResourceIndices]
type = Buffer
format = R32_UINT
data = R32_UINT 0 1 2
bind_flags = index_buffer
[ResourceArguments]
type = Buffer
data = R32_UINT 3 4 0 1
misc_flags = drawindirect_args
[ResourceIndexedArguments]
type = Buffer
data = R32_UINT 3 4 0 0 1
misc_flags = drawindirect_args
[CustomShaderMain]
vs = layout.hlsl
ps = layout.hlsl
cull = none
depth_enable = false
topology = triangle_list
vb0 = ref ResourceVertices
ib = ref ResourceIndices
)ini"+calls[form]+"\n[Present]\nrun = CustomShaderMain\n";
        ShaderFixture f(d,directory,ini.c_str());f.backend->set_input_layout(layout);f.present();
        auto output=f.backend->resources().binding(L'\0',ResourceCopyTargetType::RENDER_TARGET,0).image;
        auto actual=f.backend->resources().commands().readback(d.queue,output,image_region(output));GraphicsReferenceOptions options;
        options.instances=4;options.first_instance=1;if(form&1)options.indices={0,1,2};
        check(actual==native_layout_pixels(programs[0],programs[1],programs[0],*layout,options),"Original direct/indexed/indirect instance divisor matches native DX11 pixels");
        const float multiplier=float(2+(divisor?3/divisor:0));const float color[]={110*multiplier,220*multiplier,330*multiplier,440*multiplier};
        for(unsigned i=0;i<16;++i)check(!memcmp(actual.data()+i*16,color,16),"Instance divisor adds the source base after division");
        if(!divisor && !form){for(auto& e:elements)e.step=2;auto changed=std::make_shared<frontend::SourceInputLayout>(elements,programs[0].input_signature);
            f.backend->set_input_layout(changed);f.present();actual=f.backend->resources().commands().readback(d.queue,output,image_region(output));
            check(actual==native_layout_pixels(programs[0],programs[1],programs[0],*changed,options) && f.backend->graphics_pipelines()==2,"Divisor changes participate in pipeline identity");
            f.backend->set_input_layout(layout);f.present();check(f.backend->graphics_pipelines()==2,"Original divisor state reuses its earlier pipeline");}
        output.reset();f.release();
    }
    gpu::GraphicsDescription state;state.colors={VK_FORMAT_R32G32B32A32_SFLOAT};state.blend.resize(1);state.blend[0].colorWriteMask=15;
    state.streams={{0,32,VK_VERTEX_INPUT_RATE_INSTANCE}};state.divisors={{0,2}};
    for(const auto& input:programs[0].inputs)if(input.builtin==UINT32_MAX)state.attributes.push_back({input.location,0,VK_FORMAT_R32G32B32A32_SFLOAT,input.semantic.find("ALPHA")==0?0u:16u});
    {gpu::ResourceCommands resources(disabled.resource);gpu::ShaderCommands shaders(resources,shader_host(disabled));
        refuses([&]{shaders.compile_graphics(programs,state);},"Physical divisor support is insufficient without logical enablement");}
    {gpu::ResourceCommands resources(d.resource);auto limited=shader_host(d);limited.max_vertex_divisor=1;gpu::ShaderCommands shaders(resources,limited);
        refuses([&]{shaders.compile_graphics(programs,state);},"Instance divisor obeys the advertised device maximum");}
    {gpu::ResourceCommands resources(d.resource);auto limited=shader_host(d);limited.vertex_zero_divisor=false;gpu::ShaderCommands shaders(resources,limited);
        auto zero=state;zero.divisors[0].divisor=0;refuses([&]{shaders.compile_graphics(programs,zero);},"Zero instance divisor requires its enabled feature");}
    {gpu::ResourceCommands resources(d.resource);auto limited=shader_host(d);limited.divisor_first_instance=false;gpu::ShaderCommands shaders(resources,limited);
        const auto pipeline=shaders.compile_graphics(programs,state);gpu::ImageDescription image;image.format=state.colors[0];image.extent={4,4,1};image.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        auto target=resources.create_image(image);auto view=resources.view(target,VK_IMAGE_VIEW_TYPE_2D,image.format,target->full_range());
        const float values[48]={};auto vertex=resources.create(sizeof(values),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,values,sizeof(values));
        gpu::GraphicsDraw draw;draw.colors={view};draw.extent={4,4};draw.viewport={0,4,4,-4,0,1};draw.scissor={{0,0},{4,4}};draw.count=3;draw.instances=4;draw.first_instance=1;draw.vertices={{0,vertex,0}};
        refuses([&]{shaders.draw(pipeline,{},draw);},"Nonzero first instance requires actual divisor property support");
        const VkDrawIndirectCommand args{3,4,0,0};draw.first_instance=0;draw.arguments=resources.create(sizeof(args),VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,&args,sizeof(args));
        refuses([&]{shaders.draw(pipeline,{},draw);},"GPU-produced divisor base requires support without a hidden readback");
        auto duplicate=state;duplicate.divisors.push_back({0,3});refuses([&]{shaders.compile_graphics(programs,duplicate);},"Duplicate divisor bindings refuse before Vulkan creation");
        auto missing=state;missing.divisors[0].binding=1;refuses([&]{shaders.compile_graphics(programs,missing);},"Divisor for an absent stream refuses before Vulkan creation");}
}
