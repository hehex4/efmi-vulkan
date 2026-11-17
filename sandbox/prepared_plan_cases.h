static void prepared_plan_cases(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);
    write(root/"increment.hlsl",R"hlsl(RWStructuredBuffer<uint> Output : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { Output[0] += 1; }
)hlsl");
    write(root/"d3dx.ini",R"ini([Constants]
global $calls = 0
global $post = 0
global $observed_ib = 0
global $stage_post = 0
global $clear_ib = 0
global $after_failure = 0
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
[CustomShaderIncrement]
cs = increment.hlsl
cs-u0 = ref ResourceCopy
dispatch = 1,1,1
cs-u0 = null
[ShaderOverrideOrdered]
vk_hash = 1234abcd
$calls = $calls + 1
ResourceCopy = copy ResourceA
run = CustomShaderIncrement
ps-t0 = ref ResourceCopy
draw = 3,0
ResourceCopy = copy ResourceB
post run = CustomShaderIncrement
post ps-t0 = null
post draw = 3,1
post ResourceCopy = copy ResourceA
[ShaderOverrideSkip]
vk_hash = 1234abce
handling = skip
draw = from_caller
post dispatch = 2,3,4
[CustomShaderInherited]
dispatch = 1,1,1
[CustomShaderNull]
cs = null
dispatch = 1,1,1
[CustomShaderGraphics]
draw = 3,0
[ShaderOverrideInherited]
vk_hash = 1234abcf
run = CustomShaderInherited
$after_failure = $after_failure + 1
[ShaderOverrideNull]
vk_hash = 1234abd0
run = CustomShaderNull
[ShaderOverrideGraphics]
vk_hash = 1234abd1
run = CustomShaderGraphics
[ShaderOverrideBefore]
vk_hash = 1234abd2
post $post = 9
checktextureoverride = ib
[ShaderOverrideAfter]
vk_hash = 1234abd3
post $stage_post = $post
[ShaderOverrideClear]
vk_hash = 1234abd4
$clear_ib = 1
post $post = 9
checktextureoverride = ib
[ShaderOverrideRematch]
vk_hash = 1234abd5
checktextureoverride = ib
[TextureOverrideOriginal]
hash = abcdef01
if $clear_ib
 ib = null
else
 ib = ref vs-t2
endif
post $post = 1
[TextureOverrideChanged]
hash = abcdef02
post $post = 2
post $observed_ib = ib->size
)ini");
    using Runtime=frontend::GpuResourceRuntime;using Target=ResourceCopyTargetType;using O=assets::FileOperation;
    auto engine=std::make_shared<frontend::GpuShaderRuntime>(d.resource,shader_host(d),L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
    auto& resources=engine->resources();std::string why;
    frontend::RuntimeConfig config{root,{},root/"settings","prepared-plan"};config.installation=root;
    resources.begin(d.queue,Runtime::ExecutionSite::OutsideRenderPass);
    auto ini=frontend::IniRuntime::load(config,frontend::owned_generation({},engine),why);
    if(!ini)throw std::runtime_error("Load prepared plan: "+why);++checks;
    {auto ticket=resources.finish();if(ticket)ticket->wait();}
    Runtime::ApplicationResourceState empty;empty.omitted_slots_unbound=true;
    frontend::RuntimeDraw draw;draw.native_calls=true;draw.indexed=false;draw.count=3;
    draw.stage_hash_mask=1;draw.stage_hashes[0]=0x1234abcd;draw.frame=1;
    assets::FilePlan plan;
    auto run=[&]{resources.begin_prepared_plan(empty);
        if(!ini->shader_draw(draw,plan,why) || resources.failed())throw std::runtime_error("Prepare ordered draw: "+why);++checks;
        check(!resources.finish_prepared(),"Plan captures the final GPU segment before registry sealing");};
    auto state=[&](size_t i)->const frontend::PreparedDrawState&{return *plan.items.at(i).prepared_state;};
    auto buffer=[&](size_t i){return state(i).bindings.at({L'p',Target::SHADER_RESOURCE,0}).buffer;};
    run();
    check(plan.skip_original && plan.pre_items==4 && plan.items.size()==7,"Original pre/caller/post work becomes one ordered plan");
    for(size_t i=0;i<7;++i)check(plan.items[i].operation==(i%2==0?O::PreparedWork:i==3?O::Caller:O::DrawIndexed),
        "GPU segments retain their exact position around source calls");
    check(buffer(1)==buffer(3) && !buffer(5),"Each call retains its current binding snapshot before a later unbind");
    check(!state(1).admitted() && awaiting_admission(plan.items[0].prepared_work),"Neither source calls nor GPU segments are admitted during preparation");
    resources.discard();check(!state(1).admitted(),"Cancellation leaves source calls unadmitted");
    plan={};run();resources.accept_prepared();
    check(state(1).admitted() && state(3).admitted() && state(5).admitted(),"One registry admission covers every call snapshot");
    gpu::ResourceCommands app(d.resource);std::vector<gpu::Buffer> outputs;
    for(unsigned i=0;i<4;++i)outputs.push_back(app.create(4,0));
    const auto shared=buffer(1);std::vector<std::shared_ptr<const gpu::PreparedResourceCommands>> packets;
    for(const auto& item:plan.items)if(item.prepared_work)packets.push_back(item.prepared_work);
    auto capture=[&](VkCommandBuffer cb,size_t i){VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=barrier.srcAccessMask;
        d.resource.dispatch.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        VkBufferCopy copy{0,0,4};d.resource.dispatch.CmdCopyBuffer(cb,shared->buffer(),outputs[i]->buffer(),1,&copy);
        d.resource.dispatch.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);};
    record_prepared_packets(d,packets,{},[&](auto cb){capture(cb,3);},[&](auto cb,size_t i){capture(cb,i);});
    for(size_t i=0;i<4;++i){const auto bytes=app.readback(d.queue,outputs[i],0,4);uint32_t word=0;memcpy(&word,bytes.data(),4);
        check(word==std::array<uint32_t,4>{8,13,14,7}[i],"Host calls consume source copies and compute at their original ordered positions");}
    plan={};packets.clear();
    draw.hunting_skip=true;run();check(plan.skip_original && std::none_of(plan.items.begin(),plan.items.end(),[](const auto& item){return bool(item.prepared_state);}),
        "Hunting suppresses explicit and implicit source calls while resource commands keep their order");resources.discard();plan={};draw.hunting_skip=false;
    draw.stage_hash_mask=0;run();check(plan.items.size()==1 && plan.items[0].operation==O::Caller && !state(0).admitted(),
        "A draw with no matching section still needs whole-plan admission");resources.discard();plan={};
    draw.stage_hash_mask=1;draw.stage_hashes[0]=0x1234abce;run();
    check(plan.items.size()==2 && plan.items[0].operation==O::Caller && plan.items[1].operation==O::Dispatch &&
        plan.items[1].index_count==2 && plan.items[1].instance_count==3 && plan.items[1].first_index==4,
        "Handling skip suppresses only the implicit caller and keeps explicit caller/dispatch arguments");
    resources.accept_prepared();check(state(0).admitted(),"An empty GPU sequence still admits its call snapshots");plan={};
    draw.stage_hashes[0]=0x1234abd0;run();check(plan.items.size()==1,"Explicit null CustomShader compute remains a known no-op");resources.discard();plan={};
    for(uint32_t hash:{0x1234abcfu,0x1234abd1u}){
        draw.stage_hashes[0]=hash;resources.begin_prepared_plan(empty);plan.pre_items=91;
        check(!ini->shader_draw(draw,plan,why) && resources.failed() && plan.pre_items==91,
            "Unproved inherited compute and CustomShader graphics refuse without publishing a partial plan");
        refuses([&]{resources.finish_prepared();},"Failed source-call preparation cannot admit partial resource commands");
        resources.discard();plan={};
    }
    check(ini->status().variables.at("$after_failure")==1,"Original scalar commands continue after a refused source call");
    draw.stage_hash_mask=1;draw.stage_hashes[0]=0x1234abd5;
    // Prove post matching against the new IB, including an override absent from pre.
    auto original=app.create(12,VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto changed=app.create(12,VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto add=[&](const gpu::Buffer& source,uint64_t id,uint32_t hash,Target type,wchar_t stage,unsigned slot){
        Runtime::ApplicationBufferBinding b;b.type=type;b.stage=stage;b.slot=slot;b.source_known=true;b.format=DXGI_FORMAT_R32_UINT;b.stride=4;
        b.description={12,D3D11_USAGE_DEFAULT,D3D11_BIND_INDEX_BUFFER|D3D11_BIND_SHADER_RESOURCE,0,0,0};
        auto& r=b.resource;r.bound=true;r.dimension=D3D11_RESOURCE_DIMENSION_BUFFER;r.identity=id;r.buffer=reinterpret_cast<uint64_t>(source->buffer());
        r.size=r.range=12;r.usage=source->usage();r.memory=reinterpret_cast<uint64_t>(source->memory());
        r.memory_offset=source->memory_offset();r.memory_size=source->memory_size();r.keep=source;
        auto facts=std::make_shared<frontend::SourceResourceIdentity>();facts->tracking=frontend::SourceResourceIdentity::Tracking::Tracked;
        facts->hash_known=true;facts->hash=hash;facts->region_hash=[hash](UINT,UINT,uint32_t& result,std::wstring&){result=hash;return true;};b.identity=facts;
        empty.buffers.push_back(std::move(b));};
    add(original,401,0xabcdef01,Target::INDEX_BUFFER,0,0);add(changed,402,0xabcdef02,Target::SHADER_RESOURCE,L'v',2);
    draw.native_resource=[](const auto&,auto& value,std::string&){value={};return true;};
    draw.indexed=true;draw.hash=0xabcdef01;resources.begin_prepared_plan(empty);
    if(!ini->draw(draw,plan,why) || resources.failed())throw std::runtime_error("Prepare rematched draw: "+why);++checks;
    check(ini->status().variables.at("$post")==2,"AfterDraw rechecks the changed tracked IB and runs its own post section");
    check(ini->status().variables.at("$observed_ib")==12,"Prepared queries read the current GPU registry rather than the earlier application snapshot");
    check(plan.items.size()==1 && state(0).bindings.at({0,Target::INDEX_BUFFER,0}).buffer->buffer()==changed->buffer(),
        "Implicit caller captures the rebound original index resource");
    resources.finish_prepared();resources.discard();plan={};
    draw.stage_hash_mask=17;draw.stage_hashes[0]=0x1234abd2;draw.stage_hashes[4]=0x1234abd3;
    resources.begin_prepared_plan(empty);
    check(ini->draw(draw,plan,why) && ini->status().variables.at("$stage_post")==2,
        "Changed-IB post lists remain between the original vertex and pixel stage post roots");
    resources.finish_prepared();resources.discard();plan={};
    draw.stage_hashes[0]=0x1234abd4;resources.begin_prepared_plan(empty);
    check(ini->draw(draw,plan,why) && ini->status().variables.at("$stage_post")==9,
        "Removing all IB post matches preserves the following shader stage boundary");
    resources.finish_prepared();resources.discard();plan={};draw.stage_hash_mask=0;
    resources.begin_prepared_plan(empty);
    check(ini->shader_draw(draw,plan,why) && ini->status().variables.at("$post")==9,
        "Shader-only entry with no shader match does not add index post lists");
    resources.finish_prepared();auto retained=plan.items.at(0).prepared_state;resources.accept_prepared();
    auto binding_owner=retained->bindings.at({0,Target::INDEX_BUFFER,0}).buffer;
    std::weak_ptr<gpu::BufferResource> observed=binding_owner;binding_owner.reset();
    ini.reset();engine.reset();check(retained->admitted() && !observed.expired(),"Call snapshots retain imported resources after interpreter retirement");
    plan={};retained.reset();check(observed.expired(),"Imported call owners retire with the last snapshot");
}
