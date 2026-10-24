// Runs an unchanged installed transparency library on explicitly owned test targets.
// Synthetic geometry and pass signals are fixture inputs, not game shader identification.
#define wmain unused_resource_regression_main
#include "resource_gpu.cpp"
#undef wmain

static bool require_cached = false;
static size_t cache_min = SIZE_MAX, cache_max = 0;

static const char* translucency_geometry = R"hlsl(
Texture1D<float4> IniParams : register(t120);
struct V { float4 position : SV_Position; float2 uv : TEXCOORD0; };
#ifdef VERTEX_SHADER
V main(uint vertex : SV_VertexID) {
    uint i = vertex % 3;
    float2 p = float2(i == 2 ? 3 : -1, i == 1 ? 3 : -1);
    V v; v.position = float4(p, vertex < 3 ? IniParams[182].x : IniParams[182].y, 1);
    v.uv = float2((p.x + 1) * .5, (1 - p.y) * .5); return v;
}
#else
float4 main(V v) : SV_Target0 { return float4(IniParams[181].xyz, 1); }
#endif
)hlsl";

static std::string translucency_resources() {
    return R"ini(namespace = fixture
[Constants]
global $captures = 0
global $composites = 0
global $reset = 0
global $remaining = 0
global $resize = 0
x180 = 91
y180 = 92
z180 = 93
w180 = 94
[ResourceScene]
type = Texture2D
width = 8
height = 8
array = 1
mips = 1
msaa = 1
format = R32G32B32A32_FLOAT
bind_flags = render_target shader_resource
[ResourceDepth]
type = Texture2D
width = 8
height = 8
array = 1
mips = 1
msaa = 1
format = R32_TYPELESS
bind_flags = depth_stencil shader_resource
[ResourceCopy]
type = Texture2D
width = 8
height = 8
array = 1
mips = 1
msaa = 1
format = R32_FLOAT
bind_flags = render_target shader_resource
[ResourceMask]
type = Texture2D
width = 8
height = 8
array = 1
mips = 1
msaa = 1
format = R32G32B32A32_FLOAT
bind_flags = render_target shader_resource
[ResourceIndices]
type = Buffer
format = R32_UINT
data = R32_UINT 0 1 2 5 4 3
bind_flags = index_buffer
)ini";
}
struct TransparencyFixture {
    Device& device;
    gpu::ResourceBudgets budgets;
    gpu::ShaderHost host;
    std::shared_ptr<frontend::GpuShaderRuntime> backend;
    std::shared_ptr<frontend::IniRuntime> ini;
    std::string why;
    TransparencyFixture(Device& d,const std::filesystem::path& root,const std::string& body,
                        const char* geometry=translucency_geometry,unsigned mask_mips=1):device(d),host(shader_host(d)) {
        auto declarations=translucency_resources();
        declarations.replace(declarations.find("mips = 1",declarations.find("[ResourceMask]")),8,"mips = "+std::to_string(mask_mips));
        write(root/"Mods/fixture.ini",(declarations+body).c_str());
        write(root/"geometry.hlsl",geometry);
        gpu::HlslCompiler compiler(L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
        gpu::HlslRequest request;request.source=root/"geometry.hlsl";request.working_directory=root;
        std::vector<gpu::HlslProgram> programs(2);
        request.stage='v';check(compiler.compile(request,programs[0],why),why.c_str());
        request.stage='p';check(compiler.compile(request,programs[1],why),why.c_str());
        backend=std::make_shared<frontend::GpuShaderRuntime>(d.resource,host,L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll",budgets,root);
        backend->enable_cached_bytecode(L"F:/vulkan/vkd3d-2.1-build/efmi-vkd3d-shader.dll",gpu::DxbcFloat32Mode::Software);
        backend->set_programs(programs);
        auto state=frontend::source_graphics_defaults();state.topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        state.raster.CullMode=D3D11_CULL_NONE;state.depth.DepthEnable=true;
        state.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;state.depth.DepthFunc=D3D11_COMPARISON_GREATER_EQUAL;
        backend->set_graphics_state(state);
        frontend::RuntimeConfig config{root,{},root/"settings","efmittl",root/"Mods"};
        backend->resources().begin(d.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
        ini=frontend::IniRuntime::load(config,backend,why);
        if(!ini)throw std::runtime_error("Load installed EFMITTL: "+why);
        const auto cached=backend->cached_programs();
        check(require_cached ? cached>0 : cached==0,"The requested cached/source shader path was actually used");
        cache_min=std::min(cache_min,cached);cache_max=std::max(cache_max,cached);
        finish();
    }
    void finish(){auto ticket=backend->resources().finish();if(ticket)ticket->wait();}
    std::vector<float> render(uint64_t frame=1) {
        backend->resources().begin(device.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
        if(!ini->present({},frame,0,why))throw std::runtime_error("Run installed EFMITTL: "+why);
        finish();
        auto image=backend->resources().binding(0,ResourceCopyTargetType::RENDER_TARGET,0).image;
        check(bool(image),"The fixture's output target is bound after the library returns");
        auto bytes=backend->resources().commands().readback(device.queue,image,image_region(image));
        std::vector<float> values(bytes.size()/sizeof(float));memcpy(values.data(),bytes.data(),bytes.size());return values;
    }
    float variable(const char* name) {return ini->status().variables.at(std::string("$\\fixture\\")+name);}
    std::vector<float> depth() {
        const auto image=backend->resources().binding(L'p',ResourceCopyTargetType::SHADER_RESOURCE,90).image;
        check(bool(image),"Shadow depth remains available for verification");
        auto bytes=backend->resources().commands().readback(device.queue,image,image_region(image,0,0,1,VK_IMAGE_ASPECT_DEPTH_BIT));
        std::vector<float> values(bytes.size()/sizeof(float));memcpy(values.data(),bytes.data(),bytes.size());return values;
    }
    void release(){ini.reset();backend.reset();check(!budgets.resident->used() && !budgets.staging->used() && !host.objects->used() && !host.sampler_objects->used(),
        "All transparency generation allocations and Vulkan objects retire");}
};
static void transparency_cases(Device& d,const std::filesystem::path& root) {
    // Each test reads its own unchanged library copy. The runner records all input hashes.
    struct Case {const char* name;unsigned layers;float opacity,scene_depth;int channel;bool invert;};
    const Case cases[]={
        {"transparent",1,0,0.1f,-1,false}, {"opaque",1,1,0.1f,-1,false},
        {"one-layer",1,.5f,.1f,-1,false}, {"default-alpha",1,-1,.1f,-1,false},
        {"four-layers",4,.5f,.1f,-1,false}, {"layer-overflow",5,.5f,.1f,-1,false},
        {"occluded",4,.5f,.95f,-1,false}, {"depth-cut",4,.5f,.5f,-1,false},
        {"mask-red",1,-1,.1f,0,false}, {"mask-green",1,-1,.1f,1,false},
        {"mask-blue",1,.5f,.1f,2,false}, {"mask-alpha",1,-1,.1f,3,false}, {"mask-invert",1,-1,.1f,2,true}
    };
    for(const auto& test:cases) {
        const auto folder=root/test.name;
        std::filesystem::create_directories(folder/"Mods");
        std::filesystem::copy(root/"library",folder/"Mods/EFMITTL",std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);
        std::ostringstream body;
        body<<"[Present]\no0 = set_viewport ref ResourceScene\noD = ref ResourceDepth\nib = ref ResourceIndices\n"
            <<"clear = ResourceScene 0.125 0.25 0.5 0.625\nclear = ResourceDepth "<<test.scene_depth<<"\n"
            <<"clear = ResourceMask 0.25 0.5 0.75 1\n$\\EFMITTL\\vs_kind = 1\n";
        struct Surface {float depth,alpha;std::array<float,3> color;};std::vector<Surface> surfaces;
        for(unsigned i=0;i<test.layers;++i) {
            const std::array<float,3> color{float(i==0 || i==3),float(i==1 || i==3),float(i==2)};
            const float front=.3f+.15f*i,back=front-.05f;
            body<<"x181 = "<<color[0]<<"\ny181 = "<<color[1]<<"\nz181 = "<<color[2]<<"\nx182 = "<<front<<"\ny182 = "<<back<<"\n";
            body<<"$\\EFMITTL\\alpha = "<<test.opacity<<"\n";
            if(test.channel>=0)body<<"Resource\\EFMITTL\\Mask = ref ResourceMask\n$\\EFMITTL\\mask_channel = "<<test.channel
                                   <<"\n$\\EFMITTL\\mask_invert = "<<int(test.invert)<<"\n";
            body<<"$\\EFMITTL\\count = 6\n$\\EFMITTL\\first = 0\nrun = CommandList\\EFMITTL\\Draw\n";
            float alpha=test.opacity;
            if(test.channel>=0){float mask=.25f*(test.channel+1);if(test.invert)mask=1-mask;alpha=(alpha<0?1:alpha)*mask;}
            else if(alpha<0)alpha=.5f;
            // The fourth layer is shared: subsequent nearer surfaces replace its previous values.
            if(alpha>0 && (alpha<1 || test.channel>=0) && (i<3 || i+1==test.layers))
                for(float depth:{front,back})if(depth>=test.scene_depth)surfaces.push_back({depth,alpha,color});
        }
        body<<"$captures = $\\EFMITTL\\captures\n"
            <<"$reset = $\\EFMITTL\\alpha == -1 && $\\EFMITTL\\count == 0 && Resource\\EFMITTL\\Mask === null && x180 == 91 && y180 == 92 && z180 == 93 && w180 == 94\n"
            <<"o0 = ref ResourceCopy\noD = null\n$\\EFMITTL\\copy_after_lit = 1\nrun = CommandList\\EFMITTL\\Composite\n"
            <<"$composites = $\\EFMITTL\\composites\n$remaining = $\\EFMITTL\\layers\no0 = set_viewport ref ResourceScene\n";
        TransparencyFixture f(d,folder,body.str());const auto pixels=f.render();
        std::array<float,4> expected{.125f,.25f,.5f,.625f};
        std::sort(surfaces.begin(),surfaces.end(),[](const auto& a,const auto& b){return a.depth<b.depth;});
        for(const auto& s:surfaces)for(unsigned c=0;c<3;++c)expected[c]=s.alpha*s.color[c]+(1-s.alpha)*expected[c];
        if(test.opacity>=1 && test.channel<0)expected={1,0,0,1};
        for(size_t pixel=0;pixel<pixels.size();pixel+=4)for(unsigned c=0;c<4;++c) {
            if(std::abs(pixels[pixel+c]-expected[c])>0.001f) {
                printf("%s pixel %zu channel %u: got %.7f expected %.7f\n",test.name,pixel/4,c,pixels[pixel+c],expected[c]);
                throw std::runtime_error("Installed transparency output differs from sorted front/back reference");
            }
        }
        check(f.variable("reset")==1,"The public library call restores parameters and clears its per-call inputs");
        check(f.variable("remaining")==0,"The composite consumes the active layer set");
        const bool translucent=test.opacity!=0 && (test.channel>=0 || test.opacity<1);
        check(f.variable("captures")==float(translucent?test.layers:0),"Only translucent parts create capture layers");
        check(f.variable("composites")==float(translucent),"Exactly one composite is issued when captures exist");
        check(f.render(2)==pixels,"Frame reset and repeated captures preserve output exactly");
        f.release();printf("EFMITTL %s: pixels, state reset and ownership passed\n",test.name);
    }
}
static void shadow_cases(Device& d,const std::filesystem::path& root) {
    struct Case {const char* name;unsigned kind;float opacity,fade;bool mask;unsigned instance=0;};
    const Case cases[]={
        {"shadow-plain",2,.5f,1,false}, {"shadow-plain-mask",2,1,1,true},
        {"shadow-hidden",2,0,1,false}, {"shadow-opaque",2,1,1,false},
        {"shadow-fade",3,.75f,.4f,false}, {"shadow-negative-fade",3,.75f,-.25f,false},
        {"shadow-fade-mask",3,1,1,true}, {"shadow-instance",3,.75f,.4f,false,3}
    };
    for(const auto& test:cases) {
        auto folder=root/test.name;std::filesystem::create_directories(folder/"Mods");
        std::filesystem::copy(root/"library",folder/"Mods/EFMITTL",std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);
        std::ostringstream geometry;
        if(test.kind==3)geometry<<"#define INSTANCE_ID "<<test.instance<<"\n";
        geometry<<"Texture1D<float4> IniParams : register(t120);\nstruct V {\n";
        if(test.kind==3)geometry<<"float4 position:SV_Position;\n";
        geometry<<"float2 uv:TEXCOORD0; float3 t1:TEXCOORD1; float3 t2:TEXCOORD2; float3 t3:TEXCOORD3; float3 t4:TEXCOORD4;\n";
        // The plain shader never reads the instance index. Do not give the synthetic
        // producer an unused integer output (a validation performance warning).
        if(test.kind==3)geometry<<"nointerpolation uint instance:TEXCOORD5;\n";
        if(test.kind==2)geometry<<"float4 position:SV_Position;\n";
        geometry<<R"hlsl(};
#ifdef VERTEX_SHADER
V main(uint vertex:SV_VertexID) {
    uint i=vertex%3; float2 p=float2(i==2?3:-1,i==1?3:-1);
    V v=(V)0; v.position=float4(p,.75,1);v.uv=float2((p.x+1)*.5,(1-p.y)*.5);
#ifdef INSTANCE_ID
    v.instance=INSTANCE_ID;
#endif
    return v;
}
#else
void main(V v) {}
#endif
)hlsl";
        std::ostringstream body;
        body<<"[ResourceFade]\ntype = Buffer\nbyte_width = 65360\nbind_flags = constant_buffer\ndata = R32_FLOAT";
        const auto fade_index=(test.instance*16+4)*4;
        for(unsigned i=0;i<4085*4;++i)body<<" "<<(i==fade_index?test.fade:i==fade_index+2?.2f:0);
        body<<"\n[Present]\no0 = set_viewport ref ResourceScene\noD = ref ResourceDepth\n"
            <<"clear = ResourceScene 0 0 0 1\nclear = ResourceDepth 0\nclear = ResourceMask 0.25 0.5 0.75 1\n"
            <<"o0 = null\nib = ref ResourceIndices\nps-cb0 = ref ResourceFade\n"
            <<"$\\EFMITTL\\vs_kind = "<<test.kind<<"\n$\\EFMITTL\\alpha = "<<test.opacity<<"\n";
        if(test.mask)body<<"Resource\\EFMITTL\\Mask = ref ResourceMask\n$\\EFMITTL\\mask_channel = 1\n";
        body<<"$\\EFMITTL\\count = 3\nrun = CommandList\\EFMITTL\\Draw\n"
            <<"$reset = $\\EFMITTL\\alpha == -1 && $\\EFMITTL\\count == 0 && Resource\\EFMITTL\\Mask === null && x180 == 91 && y180 == 92 && z180 == 93 && w180 == 94\n"
            <<"oD = null\nps-t90 = ResourceDepth\no0 = set_viewport ref ResourceScene\n";
        TransparencyFixture f(d,folder,body.str(),geometry.str().c_str());f.render();const auto depths=f.depth();
        check(depths.size()==64,"The depth fixture covers every shadow-map texel");
        const float alpha=test.opacity*(test.mask?.5f:1.f);
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            const float a=x*.067111f+y*.005837f,b=52.982918f*(a-std::floor(a)),n=b-std::floor(b);
            bool visible=alpha>n;
            if(test.kind==3)visible=visible && test.fade-(test.fade>=0?n:-n)>=0 && 1-n-.2f>=0;
            if(depths[y*8+x]!=(visible?.75f:0))throw std::runtime_error(std::string(test.name)+": depth differs from opacity/fade reference at "+std::to_string(x)+","+std::to_string(y));
        }
        check(f.variable("reset")==1,"Shadow call restores parameters and clears public inputs");
        f.render(2);check(f.depth()==depths,"Shadow depth and fade are stable across frame reset");
        f.release();printf("EFMITTL %s: all depth pixels, state reset and ownership passed\n",test.name);
    }
}
static void fallback_case(Device& d,const std::filesystem::path& root) {
    const auto folder=root/"fallback-recovery";std::filesystem::create_directories(folder/"Mods");
    std::filesystem::copy(root/"library",folder/"Mods/EFMITTL",std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);
    const char* body=R"ini(
[Present]
$resize = $resize + 1
o0 = set_viewport ref ResourceScene
oD = ref ResourceDepth
ib = ref ResourceIndices
clear = ResourceScene 0.125 0.25 0.5 0.625
clear = ResourceDepth 0.1
x181 = 1
y181 = 0
z181 = 0
x182 = 0.6
y182 = 0.4
$\EFMITTL\alpha = 0.5
$\EFMITTL\count = 6
$\EFMITTL\vs_kind = 0
if $resize >= 3
    $\EFMITTL\vs_kind = 1
endif
run = CommandList\EFMITTL\Draw
$captures = $\EFMITTL\captures
o0 = ref ResourceCopy
oD = null
if $resize >= 3
    $\EFMITTL\copy_after_lit = 1
    run = CommandList\EFMITTL\Composite
endif
$composites = $\EFMITTL\composites
o0 = set_viewport ref ResourceScene
)ini";
    TransparencyFixture f(d,folder,body);
    for(uint64_t frame=1;frame<=4;++frame) {
        const auto pixels=f.render(frame);
        const std::array<float,4> expected=frame==1?std::array<float,4>{.125f,.25f,.5f,.625f}:
            frame<4?std::array<float,4>{1,0,0,1}:std::array<float,4>{.78125f,.0625f,.125f,.625f};
        for(size_t i=0;i<pixels.size();++i)if(std::abs(pixels[i]-expected[i%4])>.001f)
            throw std::runtime_error("Fallback/recovery output differs at frame "+std::to_string(frame));
        check(f.ini->status().variables.at("$\\efmittl\\fallback")==float(frame<3),
              "Missing pass signals cause next-frame fallback and matching signals recover it");
        check(f.variable("captures")==float(frame==4) && f.variable("composites")==float(frame==4),
              "Opaque fallback never leaves a partially captured layer");
    }
    f.release();printf("EFMITTL fallback-recovery: four frames, pixels and ownership passed\n");
}
static void mask_sampling_case(Device& d,const std::filesystem::path& root) {
    const auto folder=root/"mask-mip-wrap";std::filesystem::create_directories(folder/"Mods");
    std::filesystem::copy(root/"library",folder/"Mods/EFMITTL",std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);
    std::string geometry=translucency_geometry;
    geometry.replace(geometry.find("return v;"),9,"v.uv.x = v.uv.x * 4 - .375; return v;");
    const char* body=R"ini(
[Present]
$resize = $resize + 1
o0 = set_viewport ref ResourceScene
oD = ref ResourceDepth
ib = ref ResourceIndices
clear = ResourceScene 0.125 0.25 0.5 0.625
clear = ResourceDepth 0.1
if $resize == 1
    clear = ResourceMask 0 0 0 0
endif
x181 = 1
y181 = 0
z181 = 0
x182 = 0.6
y182 = 0.4
$\EFMITTL\vs_kind = 1
Resource\EFMITTL\Mask = ref ResourceMask
$\EFMITTL\count = 6
run = CommandList\EFMITTL\Draw
o0 = ref ResourceCopy
oD = null
$\EFMITTL\copy_after_lit = 1
run = CommandList\EFMITTL\Composite
o0 = set_viewport ref ResourceScene
ps-t90 = ResourceMask
)ini";
    TransparencyFixture f(d,folder,body,geometry.c_str(),4);f.render();
    auto mask=f.backend->resources().binding(L'p',ResourceCopyTargetType::SHADER_RESOURCE,90).image;
    check(mask && mask->description().mips==4,"The mask fixture contains four mip levels");
    for(unsigned mip=0;mip<4;++mip) {
        const auto width=8>>mip;std::vector<float> data(width*width*4,0);
        for(unsigned y=0;y<width;++y)for(unsigned x=0;x<width;++x)
            data[(y*width+x)*4]=mip==2?(x?.75f:.25f):mip==1?.125f:mip==3?.875f:0;
        f.backend->resources().commands().write(mask,image_region(mask,mip),data.data(),data.size()*sizeof(float));
    }
    auto ticket=f.backend->resources().commands().submit(d.queue);ticket->wait();ticket.reset();mask.reset();
    const auto pixels=f.render(2);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
        // du/dx=.5 selects mip 2. Its repeating .25/.75 texels interpolate
        // at fractional texel positions .25 and 1.25, including negative UVs.
        const float opacity=x%2?.375f:.625f,t=(1-opacity)*(1-opacity);
        const std::array<float,4> expected{1-t+t*.125f,t*.25f,t*.5f,.625f};
        for(unsigned c=0;c<4;++c)if(std::abs(pixels[(y*8+x)*4+c]-expected[c])>.001f)
            throw std::runtime_error("Mask derivative/mip/wrap/bilinear output differs at "+std::to_string(x)+","+std::to_string(y));
    }
    check(f.render(3)==pixels,"Repeated nonuniform mip sampling preserves every pixel");
    f.release();printf("EFMITTL mask-mip-wrap: derivatives, mip selection, bilinear wrapping and ownership passed\n");
}
static void resize_mask_case(Device& d,const std::filesystem::path& root) {
    const auto folder=root/"resize-neutral-mask";std::filesystem::create_directories(folder/"Mods");
    std::filesystem::copy(root/"library",folder/"Mods/EFMITTL",std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);
    std::string geometry="Texture2D<float4> ScreenMask : register(t4);\n";
    geometry+=translucency_geometry;
    const std::string original="return float4(IniParams[181].xyz, 1);";
    geometry.replace(geometry.find(original),original.size(),
        "float2 mask = ScreenMask.Load(int3(v.position.xy,0)).rg; return float4(IniParams[181].xyz * mask.x * mask.y, 1);");
    std::ostringstream body;
    for(unsigned i=0;i<3;++i) {
        body<<"[ResourceSmall"<<i<<"]\ntype = Texture2D\nwidth = 4\nheight = 2\narray = 1\nmips = 1\nmsaa = 1\nformat = "
            <<(i==0?"R32G32B32A32_FLOAT":i==1?"R32_TYPELESS":"R32_FLOAT")
            <<"\nbind_flags = "<<(i==1?"depth_stencil":"render_target")<<" shader_resource\n";
    }
    body<<R"ini(
[ResourceOutput]
[ResourceZ]
[ResourceDepthCopy]
[Present]
$resize = $resize + 1
if $resize < 3
    ResourceOutput = ref ResourceScene
    ResourceZ = ref ResourceDepth
    ResourceDepthCopy = ref ResourceCopy
else
    ResourceOutput = ref ResourceSmall0
    ResourceZ = ref ResourceSmall1
    ResourceDepthCopy = ref ResourceSmall2
endif
o0 = set_viewport ref ResourceOutput
oD = ref ResourceZ
ib = ref ResourceIndices
clear = ResourceOutput 0 0 0 0.625
clear = ResourceZ 0.1
clear = ResourceMask 0.25 0.5 0.75 1
ps-t4 = ResourceMask
x181 = 0.75
y181 = 0.5
z181 = 0.25
x182 = 0.6
y182 = 0.4
$\EFMITTL\vs_kind = 1
$\EFMITTL\alpha = 0.5
$\EFMITTL\count = 6
run = CommandList\EFMITTL\Draw
$reset = ps-t4 === ResourceMask && o0 === ResourceOutput && oD === ResourceZ
o0 = ref ResourceDepthCopy
oD = null
$\EFMITTL\copy_after_lit = 1
run = CommandList\EFMITTL\Composite
o0 = set_viewport ref ResourceOutput
)ini";
    TransparencyFixture f(d,folder,body.str(),geometry.c_str());
    for(uint64_t frame=1;frame<=4;++frame) {
        const auto pixels=f.render(frame);
        check(pixels.size()==size_t(frame<3?256:32),"Resizing changes the composite output extent");
        const std::array<float,4> expected{.5625f,.375f,.1875f,.625f};
        for(size_t i=0;i<pixels.size();++i)if(std::abs(pixels[i]-expected[i%4])>.001f)
            throw std::runtime_error("Neutral screen mask or resized layers differ at frame "+std::to_string(frame));
        check(f.variable("reset")==1,"Capture restores the screen mask and both original targets");
        check(f.ini->status().variables.at("$\\efmittl\\width")==float(frame<3?8:4),
              "The unchanged library discovers a new target size");
    }
    f.release();printf("EFMITTL resize-neutral-mask: resized layers, neutral fill, target restoration and ownership passed\n");
}
int wmain(int argc,wchar_t** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    try {check(argc==3,"Transparency fixture path and cached/source mode required");
        check(!wcscmp(argv[2],L"cached") || !wcscmp(argv[2],L"source"),"Known shader compilation mode");
        require_cached=!wcscmp(argv[2],L"cached");
        {Device d;d.init(false,false,true,false,false,false,true,true,true);
            const auto root=std::filesystem::absolute(argv[1]);transparency_cases(d,root);shadow_cases(d,root);
            fallback_case(d,root);mask_sampling_case(d,root);resize_mask_case(d,root);}
        check(errors==0 && warnings==0,"Clean transparency Vulkan validation through shutdown");
        printf("EFMITTL cache translations per fixture: min=%zu max=%zu\n",cache_min,cache_max);
        printf("efmittl_gpu: %u checks passed; validation errors=%u warnings=%u\n",checks,unsigned(errors),unsigned(warnings));return 0;
    }catch(const std::exception& e){printf("FAIL after %u checks: %s\n",checks,e.what());return 1;}
}
