// BUG-046: a Mod folder named outside ASCII whose CustomShader DXC refuses (a structured buffer's
// Length, as shape-key Mods write it) while the original compiler takes it. DXC faulted on the
// diagnostic and ended the game; now nothing faults, the original compiler's bytecode runs through
// the translation, and a CustomShader that compiles nowhere is skipped with a warning while the rest
// of the Mod runs, as the original engine does.
static void original_reference_fallback(Device& d,const std::filesystem::path& base){
    const auto root=base/L"mod-\u00e9\uff1a\u4e2d";std::filesystem::create_directories(root/"Mods");
    write(root/"Mods/shape.hlsl",R"hlsl(RWStructuredBuffer<float> Output : register(u5);
[numthreads(4,1,1)] void main(uint3 id:SV_DispatchThreadID){uint count=Output.Length;if(id.x>=count)return;Output[id.x]+=count;})hlsl");
    write(root/"Mods/broken.hlsl","RWStructuredBuffer<float> Output : register(u5);\n[numthreads(1,1,1)] void main(){Output[0]=undeclared_name;}\n");
    write(root/"Mods/mod.ini",R"ini(namespace = reference
[Constants]
global $a = 0
global $b = 0
global $c = 0
global $after = 0
[ResourceOutput]
type = StructuredBuffer
stride = 4
data = R32_FLOAT 1 2 3
bind_flags = unordered_access shader_resource
[CustomShaderShape]
cs = shape.hlsl
cs-u5 = ResourceOutput
dispatch = 1, 1, 1
store = $a, cs-u5, 0
store = $b, cs-u5, 1
store = $c, cs-u5, 2
[CustomShaderBroken]
cs = broken.hlsl
cs-u5 = ResourceOutput
dispatch = 1, 1, 1
[Present]
run = CustomShaderBroken
run = CustomShaderShape
$after = 1
)ini");
    {gpu::HlslCompiler compiler(L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");gpu::HlslRequest request;
        request.source=root/"Mods/shape.hlsl";request.working_directory=root;request.stage='c';
        gpu::HlslProgram program;std::string why;std::vector<uint8_t> reference;
        check(!compiler.compile(request,program,why,&reference) && !reference.empty(),"DXC refuses a structured buffer's Length without a fault and hands over the original bytecode");
        check(why.find("Length")!=std::string::npos && why.find(request.source.filename().u8string())!=std::string::npos &&
              why.find("source0.hlsl")==std::string::npos,"The refusal names the file outside ASCII, not DXC's alias");
        request.source=root/"Mods/broken.hlsl";
        check(!compiler.compile(request,program,why,&reference) && reference.empty(),"A source the original compiler refuses has no bytecode");}
    gpu::ResourceBudgets budgets;auto host=shader_host(d);
    {frontend::RuntimeConfig config{root,{},root/"settings","reference",root/"Mods"};
        auto backend=std::make_shared<frontend::GpuShaderRuntime>(d.resource,host,L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll",budgets);
        backend->enable_cached_bytecode(L"F:/vulkan/vkd3d-2.1-build/efmi-vkd3d-shader.dll",gpu::DxbcFloat32Mode::Software);
        backend->resources().begin(d.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
        std::string why;auto ini=frontend::IniRuntime::load(config,backend,why);check(ini!=nullptr,"A Mod with a CustomShader that compiles nowhere still loads");
        if(auto ticket=backend->resources().finish())ticket->wait();
        backend->resources().begin(d.queue,frontend::GpuResourceRuntime::ExecutionSite::OutsideRenderPass);
        check(ini->present({},1,0,why),"Present runs past the skipped CustomShader");
        if(auto ticket=backend->resources().finish())ticket->wait();
        const auto status=ini->status();auto value=[&](const char* name){return status.variables.at(std::string("$\\reference\\")+name);};
        check(backend->reference_programs()==1,"The refused source runs as the original compiler's bytecode translation");
        check(value("a")==4 && value("b")==5 && value("c")==6,"The translated shader reads the buffer's length and skips the thread past it");
        check(value("after")==1 && backend->dispatches()==1,"The CustomShader that failed to compile runs nothing and the list goes on");
        check(std::any_of(status.diagnostic_messages.begin(),status.diagnostic_messages.end(),
                          [](const std::string& m){return m.find("Error compiling custom shader")!=std::string::npos;}),"The skipped CustomShader is an original warning");
        ini.reset();backend.reset();}
    check(!budgets.resident->used() && !budgets.staging->used() && !host.objects->used(),"The generation with a translated shader retires its budgets");
}
