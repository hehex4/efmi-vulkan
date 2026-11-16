static void prepared_registry_segment_cases(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);
    write(root/"d3dx.ini",R"ini([Constants]
global $step = 0
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
[ResourceCopy]
bind_flags = shader_resource unordered_access
[Present]
$step = $step + 1
if $step == 1 || $step == 3
 ResourceCopy = copy ResourceA
else
 ResourceCopy = copy ResourceB
endif
cs-u0 = ref ResourceCopy
)ini");
    using Runtime=frontend::GpuResourceRuntime;using Target=ResourceCopyTargetType;
    gpu::ResourceBudgets budgets;
    auto engine=std::make_shared<frontend::GpuShaderRuntime>(d.resource,shader_host(d),L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll",budgets);
    auto& resources=engine->resources();std::string why;
    frontend::RuntimeConfig config{root,{},root/"settings","prepared-registry-segments"};config.installation=root;
    resources.begin(d.queue,Runtime::ExecutionSite::OutsideRenderPass);
    auto ini=frontend::IniRuntime::load(config,frontend::owned_generation({},engine),why);
    if(!ini)throw std::runtime_error("Load segmented registry: "+why);++checks;
    {auto ticket=resources.finish();if(ticket)ticket->wait();}
    const auto baseline=budgets.resident->used();
    auto run=[&]{check(ini->present({},10,0,why) && !resources.failed(),"Original lists continue across prepared segment boundaries");};
    resources.begin_prepared();run();auto a=resources.cut_prepared();
    check(resources.scope_active(),"Cutting leaves one original registry transaction active");
    refuses([&]{resources.accept_prepared();},"A cut does not admit the original registry before its whole plan is sealed");
    run();std::weak_ptr<gpu::BufferResource> cancelled=resources.binding(L'c',Target::UNORDERED_ACCESS_VIEW,0).buffer;
    auto b=resources.finish_prepared();resources.discard();
    check(!resources.binding(L'c',Target::UNORDERED_ACCESS_VIEW,0) && ini->status().variables.at("$step")==2,
        "Cancelling all segments restores bindings and preserves original scalar assignments");
    check(awaiting_admission(a) && awaiting_admission(b),"Registry cancellation leaves every segment unrecordable");
    a.reset();b.reset();check(cancelled.expired() && budgets.resident->used()==baseline,
        "All speculative registry allocations retire after segmented cancellation");
    resources.begin_prepared();run();a=resources.cut_prepared();auto output=resources.binding(L'c',Target::UNORDERED_ACCESS_VIEW,0).buffer;
    run();check(resources.binding(L'c',Target::UNORDERED_ACCESS_VIEW,0).buffer==output,
        "Later original commands reuse the speculative named allocation in the same transaction");
    b=resources.finish_prepared();resources.accept_prepared();
    gpu::ResourceCommands app(d.resource);auto first=app.create(4,0),second=app.create(4,0);
    const auto handle=output->buffer();std::weak_ptr<gpu::BufferResource> lifetime=output;
    output.reset();ini.reset();engine.reset();check(!lifetime.expired(),"Segmented original commands retain allocations after registry destruction");
    auto capture=[&](VkCommandBuffer cb,const gpu::Buffer& destination){VkBufferCopy copy{0,0,4};
        d.resource.dispatch.CmdCopyBuffer(cb,handle,destination->buffer(),1,&copy);};
    record_prepared_packets(d,{a,b},{},[&](auto cb){capture(cb,second);},[&](auto cb,size_t){capture(cb,first);});
    auto read=[&](const gpu::Buffer& buffer){const auto bytes=app.readback(d.queue,buffer,0,4);uint32_t word=0;memcpy(&word,bytes.data(),4);return word;};
    check(read(first)==7 && read(second)==13,"Application calls between original command segments observe the correct ordered values");
    a.reset();b.reset();check(lifetime.expired(),"Retiring all original segments releases their common resource owner");
}
