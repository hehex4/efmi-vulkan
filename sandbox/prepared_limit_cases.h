static void prepared_limit_cases(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);
    write(root/"increment.hlsl",R"hlsl(RWStructuredBuffer<uint> Output : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { Output[0] += 1; }
)hlsl");
    write(root/"d3dx.ini",R"ini([Constants]
global $step = 0
global $executed = 0
[ResourceOutput]
type = StructuredBuffer
stride = 4
data = R32_UINT 0
bind_flags = shader_resource unordered_access
[ResourceA]
type = StructuredBuffer
stride = 4
data = R32_UINT 7
bind_flags = shader_resource
[ResourceB]
type = StructuredBuffer
stride = 4
data = R32_UINT 13
bind_flags = shader_resource
[ResourceLimited]
bind_flags = shader_resource unordered_access
max_copies_per_frame = 1
[CustomShaderLimited]
cs = increment.hlsl
max_executions_per_frame = 1
$executed = $executed + 1
cs-u0 = ref ResourceOutput
dispatch = 1,1,1
[Present]
$step = $step + 1
if $step == 3 || $step == 5 || $step == 9
 reset_per_frame_limits = CustomShaderLimited
 reset_per_frame_limits = ResourceLimited
else
 if $step <= 2
  ResourceLimited = copy ResourceA
 else
  ResourceLimited = copy ResourceB
 endif
 run = CustomShaderLimited
endif
cs-u0 = ref ResourceOutput
cs-u1 = ref ResourceLimited
)ini");
    using Runtime=frontend::GpuResourceRuntime;using Target=ResourceCopyTargetType;
    auto engine=std::make_shared<frontend::GpuShaderRuntime>(d.resource,shader_host(d),L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
    auto& resources=engine->resources();std::string why;
    frontend::RuntimeConfig config{root,{},root/"settings","prepared-limits"};config.installation=root;
    resources.begin(d.queue,Runtime::ExecutionSite::OutsideRenderPass);
    auto ini=frontend::IniRuntime::load(config,frontend::owned_generation({},engine),why);
    if(!ini)throw std::runtime_error("Load prepared limits: "+why);++checks;
    {auto ticket=resources.finish();if(ticket)ticket->wait();}
    auto run=[&]{resources.begin_prepared();check(ini->present({},10,0,why) && !resources.failed(),"Original per-frame limits evaluate in one prepared frame");};
    auto finish=[&]{auto packet=resources.finish_prepared();resources.accept_prepared();if(packet)record_prepared_packets(d,{packet});};
    auto read=[&](unsigned index){const auto buffer=resources.binding(L'c',Target::UNORDERED_ACCESS_VIEW,index).buffer;
        const auto bytes=resources.commands().readback(d.queue,buffer,0,4);uint32_t word=0;memcpy(&word,bytes.data(),4);return word;};
    run();auto cancelled=resources.cut_prepared();resources.finish_prepared();resources.discard();cancelled.reset();
    run();finish();check(read(0)==1,"Cancelled shader execution does not consume its same-frame allowance");
    check(read(1)==7 && ini->status().variables.at("$executed")==2,"Resource limits retry while original scalar assignments persist");
    run();auto reset=resources.finish_prepared();check(!reset,"Resetting original limits alone emits no GPU commands");resources.discard();
    run();finish();check(read(0)==1 && ini->status().variables.at("$executed")==2,"A cancelled shader-limit reset cannot authorize another dispatch");
    check(read(1)==7,"A cancelled resource-limit reset cannot authorize another copy");
    run();finish();run();finish();
    check(read(0)==2 && read(1)==13 && ini->status().variables.at("$executed")==3,
        "Admitting an original limit reset permits exactly the next shader execution and resource copy");
    auto ordinary=[&]{resources.begin(d.queue,Runtime::ExecutionSite::OutsideRenderPass);
        check(ini->present({},11,0,why) && !resources.failed(),"Ordinary execution uses unchanged original frame limits");
        auto ticket=resources.finish();if(ticket)ticket->wait();};
    ordinary();ordinary();check(read(0)==3 && ini->status().variables.at("$executed")==4,
        "An ordinary new frame resets allowance and its second execution stays limited");
    ordinary();ordinary();check(read(0)==4 && ini->status().variables.at("$executed")==5,
        "Ordinary explicit limit reset still permits the next execution in that frame");
}
