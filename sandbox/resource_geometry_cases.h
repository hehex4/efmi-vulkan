// Owned original geometry execution, independently compared with native WARP.
static std::vector<gpu::HlslProgram> geometry_programs(const std::filesystem::path& path,const char* stages="vgp"){
    gpu::HlslCompiler compiler(L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");gpu::HlslRequest request;request.source=path;request.working_directory=path.parent_path();
    std::vector<gpu::HlslProgram> programs;for(const char* stage=stages;*stage;++stage){request.stage=*stage;gpu::HlslProgram program;std::string why;
        if(!compiler.compile(request,program,why))throw std::runtime_error("Geometry fixture compilation: "+why);++checks;programs.push_back(std::move(program));}
    return programs;
}
static void native_geometry(ID3D11Device* d,ID3D11DeviceContext* c,const gpu::HlslProgram& program,D3D11_PRIMITIVE_TOPOLOGY topology,bool clamp=false){
    ImageNativeReference::Ptr<ID3D11GeometryShader> gs;check(SUCCEEDED(d->CreateGeometryShader(program.dxbc.data(),program.dxbc.size(),nullptr,&gs)),"Native geometry program");
    c->GSSetShader(gs.Get(),nullptr,0);c->IASetPrimitiveTopology(topology);
    if(clamp){D3D11_RASTERIZER_DESC state{};state.FillMode=D3D11_FILL_SOLID;state.CullMode=D3D11_CULL_NONE;
        ImageNativeReference::Ptr<ID3D11RasterizerState> raster;check(SUCCEEDED(d->CreateRasterizerState(&state,&raster)),"Native no-depth-clip geometry raster");c->RSSetState(raster.Get());}
}
static void original_ini_geometry(Device& unavailable,const std::filesystem::path& root){
    Device d;d.init(false,false,true,true);
    struct Case{const char* topology;const char* input;unsigned vertices;D3D11_PRIMITIVE_TOPOLOGY native;VkPrimitiveTopology vulkan;};
    const Case cases[]={{"point_list","point",1,D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,VK_PRIMITIVE_TOPOLOGY_POINT_LIST},
        {"line_list","line",2,D3D11_PRIMITIVE_TOPOLOGY_LINELIST,VK_PRIMITIVE_TOPOLOGY_LINE_LIST},
        {"triangle_list","triangle",3,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST}};
    for(const auto& test:cases){const auto directory=root/test.topology;std::filesystem::create_directories(directory/"Mods");
        std::string hlsl=R"hlsl(struct V{uint index:TEXCOORD0;};struct G{float4 position:SV_Position;nointerpolation float4 color:COLOR0;};
#ifdef VERTEX_SHADER
V main(uint id:SV_VertexID){V v;v.index=id;return v;}
#endif
#ifdef GEOMETRY_SHADER
StructuredBuffer<float4> Values:register(t7);
[maxvertexcount(3)] void main()hlsl";
        hlsl+=std::string(test.input)+" V input["+std::to_string(test.vertices)+R"hlsl(],inout TriangleStream<G> stream){
for(uint i=0;i<3;++i){G g;g.position=float4(i==2?6:-2,i==1?6:-2,4,2);g.color=Values[0]+float4(input[0].index,0,0,0);stream.Append(g);}}
#endif
#ifdef PIXEL_SHADER
float4 main(G g):SV_Target{return g.color+float4(0,0,g.position.z,g.position.w);}
#endif
)hlsl";
        write(directory/"Mods/geometry.hlsl",hlsl.c_str());const auto programs=geometry_programs(directory/"Mods/geometry.hlsl");
        const std::string ini=std::string(ini_graphics_resources)+R"ini(
[ResourceValues]
type = StructuredBuffer
stride = 16
data = R32_FLOAT 10 20 30 40
[CustomShaderGeometry]
vs = geometry.hlsl
gs = geometry.hlsl
ps = geometry.hlsl
cull = none
depth_enable = false
depth_clip_enable = false
gs-t7 = ResourceValues
)ini"+"topology = "+test.topology+"\ndraw = "+std::to_string(test.vertices)+", 6\n[Present]\nrun = CustomShaderGeometry\n";
        ShaderFixture f(d,directory,ini.c_str());f.present();auto output=f.backend->resources().binding(L'\0',ResourceCopyTargetType::RENDER_TARGET,0).image;
        const auto actual=f.backend->resources().commands().readback(d.queue,output,image_region(output));
        GraphicsReferenceOptions options;options.count=test.vertices;options.first_vertex=6;
        const auto expected=native_linked_graphics(programs[0],programs[2],options,[&](auto* device,auto* context){
            native_geometry(device,context,programs[1],test.native,true);const float values[]={10,20,30,40};
            D3D11_BUFFER_DESC description{16,D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,16};D3D11_SUBRESOURCE_DATA initial{values,0,0};
            ImageNativeReference::Ptr<ID3D11Buffer> buffer;ImageNativeReference::Ptr<ID3D11ShaderResourceView> view;
            check(SUCCEEDED(device->CreateBuffer(&description,&initial,&buffer)) && SUCCEEDED(device->CreateShaderResourceView(buffer.Get(),nullptr,&view)),"Native geometry stage resource");
            auto* handle=view.Get();context->GSSetShaderResources(7,1,&handle);});
        check(actual==expected,"Original point/line/triangle INI geometry output equals native DX11 pixels");
        printf("Geometry %s first pixel %.9g %.9g %.9g %.9g\n",test.topology,value(actual,0),value(actual,4),value(actual,8),value(actual,12));
        const float color[]={10,20,32,42};for(unsigned i=0;i<16;++i)check(!memcmp(actual.data()+i*16,color,16),"Geometry stage reads its source slot and preserves unclipped pixel depth/w");
        f.present();check(f.backend->graphics_pipelines()==1 && f.backend->draws()==2,"Original geometry pipeline is reused across calls");
        output.reset();f.release();
        gpu::GraphicsDescription state;state.topology=test.vulkan;state.colors={VK_FORMAT_R32G32B32A32_SFLOAT};state.blend.resize(1);state.blend[0].colorWriteMask=15;
        gpu::ResourceCommands disabled(unavailable.resource);gpu::ShaderCommands commands(disabled,shader_host(unavailable));
        refuses([&]{commands.compile_graphics(programs,state);},"Geometry use refuses when physical support was not enabled");
        auto limits=d.properties.limits;limits.maxGeometryOutputVertices=2;
        refuses([&]{gpu::validate_geometry(programs[1],state.topology,limits);},"Geometry output vertices obey actual device limit");
        limits=d.properties.limits;limits.maxGeometryShaderInvocations=0;
        refuses([&]{gpu::validate_geometry(programs[1],state.topology,limits);},"Geometry invocations obey actual device limit");
        limits=d.properties.limits;limits.maxGeometryInputComponents=0;
        refuses([&]{gpu::validate_geometry(programs[1],state.topology,limits);},"Geometry input components obey actual device limit");
        limits=d.properties.limits;limits.maxGeometryOutputComponents=7;
        refuses([&]{gpu::validate_geometry(programs[1],state.topology,limits);},"Geometry per-vertex output component limit includes position");
        limits=d.properties.limits;limits.maxGeometryTotalOutputComponents=23;
        refuses([&]{gpu::validate_geometry(programs[1],state.topology,limits);},"Geometry total output component limit includes every vertex");
        refuses([&]{gpu::validate_geometry(programs[1],test.vertices==1?VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:VK_PRIMITIVE_TOPOLOGY_POINT_LIST,d.properties.limits);},"Geometry topology mismatch refuses before pipeline creation");
        state.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        refuses([&]{commands.compile_graphics({programs[0]},state);},"Absent geometry cannot rasterize a vertex stage without position");
    }
}
