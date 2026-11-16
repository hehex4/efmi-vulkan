// Application resources enter the original registry by proven source identity.
static void prepared_application_cases(Device& d,const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    write(root/"ordered.hlsl",R"hlsl(StructuredBuffer<uint> Input : register(t0);
RWStructuredBuffer<uint> Output : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { Output[id.x] = Input[id.x] * 2; }
)hlsl");
    write(root/"d3dx.ini",R"ini([Constants]
global $attempt = 0
global $filter = 0
[TextureOverrideSource]
hash = abcdef12
filter_index = 41
[ResourceSaved]
[ResourceCopy]
bind_flags = shader_resource
[ResourceOutput]
type = StructuredBuffer
stride = 4
byte_width = 256
bind_flags = unordered_access
[CustomShaderDouble]
cs = ordered.hlsl
cs-t0 = ref ResourceCopy
cs-u0 = ref ResourceOutput
dispatch = 64,1,1
[Present]
$attempt = $attempt + 1
if $attempt == 4
 ps-t1 = ref ResourceSaved
else
 $filter = vs-t0
 ResourceSaved = ref vs-t0
 ResourceCopy = copy vs-t0
 run = CustomShaderDouble
 cs-u0 = ref ResourceOutput
endif
)ini");
    using Runtime=frontend::GpuResourceRuntime;using Target=ResourceCopyTargetType;
    auto gpu=std::make_shared<frontend::GpuShaderRuntime>(d.resource,shader_host(d),L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
    auto& resources=gpu->resources();std::string why;
    frontend::RuntimeConfig config{root,{},root/"settings","prepared-application"};config.installation=root;
    resources.begin(d.queue,Runtime::ExecutionSite::OutsideRenderPass);
    auto ini=frontend::IniRuntime::load(config,frontend::owned_generation({},gpu),why);
    if(!ini)throw std::runtime_error("Load application registry: "+why);++checks;
    {auto ticket=resources.finish();if(ticket)ticket->wait();}
    gpu::ResourceCommands app(d.resource);constexpr VkDeviceSize bytes=256;
    auto source=app.create(bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);
    auto next=app.create(bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);
    auto first=app.create(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,nullptr,0,true);
    auto second=app.create(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,nullptr,0,true);
    {auto ticket=app.submit(d.queue);ticket->wait();}
    auto snapshot=[&](const gpu::Buffer& buffer,uint64_t identity){
        Runtime::ApplicationResourceState state;state.omitted_slots_unbound=true;
        Runtime::ApplicationBufferBinding binding;binding.stage=L'v';binding.type=Target::SHADER_RESOURCE;
        binding.source_known=true;binding.description={UINT(bytes),D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,
            0,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,4};
        auto& r=binding.resource;r.bound=true;r.dimension=D3D11_RESOURCE_DIMENSION_BUFFER;r.identity=identity;
        r.buffer=reinterpret_cast<uint64_t>(buffer->buffer());r.size=r.range=bytes;r.usage=buffer->usage();
        r.memory=reinterpret_cast<uint64_t>(buffer->memory());r.memory_offset=buffer->memory_offset();r.memory_size=buffer->memory_size();r.keep=buffer;
        auto facts=std::make_shared<frontend::SourceResourceIdentity>();facts->tracking=frontend::SourceResourceIdentity::Tracking::Tracked;
        facts->hash_known=true;facts->hash=0xabcdef12;
        facts->region_hash=[](UINT,UINT,uint32_t& hash,std::wstring&){hash=0xabcdef12;return true;};binding.identity=facts;
        state.buffers.push_back(binding);binding.stage=L'p';state.buffers.push_back(binding);return state;
    };
    auto initial=snapshot(source,101),later=snapshot(next,102);
    auto reject=[&](auto change,const char* name){auto invalid=initial;change(invalid);
        refuses([&]{resources.begin_prepared(invalid);},name);
        check(!resources.scope_active() && !resources.pending(),"Rejected snapshot leaves no partial GPU scope");};
    reject([](auto& s){s.omitted_slots_unbound=false;},"Unknown omitted application slots cannot become null bindings");
    reject([](auto& s){s.buffers[0].source_known=false;},"Vulkan storage does not imply original source declarations");
    reject([](auto& s){s.buffers[0].resource.keep.reset();},"Unretained application resources cannot enter the registry");
    reject([](auto& s){s.buffers[0].resource.range+=4;},"Application view bounds are checked before publication");
    reject([](auto& s){s.buffers[1]=s.buffers[0];},"Duplicate source slots refuse atomically");
    reject([](auto& s){s.buffers[1].stage=L'c';s.buffers[1].type=Target::UNORDERED_ACCESS_VIEW;},
        "An application snapshot cannot bypass original input/output hazards");
    reject([](auto& s){auto facts=std::make_shared<frontend::SourceResourceIdentity>(*s.buffers[1].identity);
        facts->hash=9;s.buffers[1].identity=std::move(facts);},"Aliases cannot publish contradictory original source hashes");
    auto run=[&](const auto& state){resources.begin_prepared(state);
        check(ini->present({},10,0,why) && !resources.failed(),"Original copy and CustomShader execute on imported application bindings");};
    run(initial);
    auto imported=resources.binding(L'v',Target::SHADER_RESOURCE,0);
    check(imported.buffer==resources.binding(L'p',Target::SHADER_RESOURCE,0).buffer,
        "Separate input views share one retained source allocation identity");
    check(ini->status().variables.at("$filter")==41,"Original TextureOverride filters see proven imported source hash facts");
    auto cancelled=resources.finish_prepared();check(bool(cancelled),"Imported source work seals without submitting");
    resources.discard();cancelled.reset();
    check(resources.binding(L'v',Target::SHADER_RESOURCE,0).buffer==imported.buffer &&
        !resources.binding(L'c',Target::UNORDERED_ACCESS_VIEW,0),"Cancellation restores the observed application slots");
    reject([](auto& s){for(auto& b:s.buffers)b.resource.memory_offset+=4;},"A retained application identity cannot be recycled for another allocation range");
    reject([](auto& s){for(auto& b:s.buffers)b.resource.identity=103;},"A retained application handle cannot bypass source hazards with a new identity");
    run(initial);auto output=resources.binding(L'c',Target::UNORDERED_ACCESS_VIEW,0).buffer;
    auto packet=resources.finish_prepared();resources.accept_prepared();
    check(packet && packet->changes_compute_state() && output,"Original CustomShader prepares compute state restoration obligations");
    refuses([&]{resources.commands().fill(imported.buffer,0,bytes,99);},"Imported aliases cannot escape into ordinary deferred submission");
    run(later);auto abandoned=resources.finish_prepared();resources.discard();abandoned.reset();
    check(resources.binding(L'v',Target::SHADER_RESOURCE,0).buffer->buffer()==next->buffer(),
        "Cancellation restores this draw's application binding instead of the previous draw's binding");
    run(later);check(resources.binding(L'p',Target::SHADER_RESOURCE,1).buffer==imported.buffer,
        "Cancelled assignment restores an admitted named reference to the earlier application resource");
    auto reference=resources.finish_prepared();check(!reference,"Import and reference alone require no GPU commands");resources.accept_prepared();
    check(app.readback(d.queue,first,0,bytes)==std::vector<uint8_t>(bytes,0),"Registry preparation does not execute GPU work early");
    const auto source_handle=source->buffer(),output_handle=output->buffer();
    std::weak_ptr<gpu::BufferResource> source_lifetime=source,output_lifetime=output;
    initial={};later={};imported={};output.reset();ini.reset();gpu.reset();source.reset();next.reset();
    check(!source_lifetime.expired() && !output_lifetime.expired(),"Prepared original commands retain resources beyond the interpreter and caller");
    const auto& vk=d.resource.dispatch;VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer cb=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=d.resource.queue_family;
    vkcheck(vk.CreateCommandPool(d.resource.device,&pi,nullptr,&pool),"Create imported command pool");
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.commandBufferCount=1;
    vkcheck(vk.AllocateCommandBuffers(d.resource.device,&ai,&cb),"Allocate imported primary");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(vk.BeginCommandBuffer(cb,&bi),"Begin imported primary");
    VkBufferCopy copy{0,0,bytes};
    vk.CmdFillBuffer(cb,source_handle,0,bytes,11);packet->record(cb);vk.CmdCopyBuffer(cb,output_handle,first->buffer(),1,&copy);
    vk.CmdFillBuffer(cb,source_handle,0,bytes,13);packet->record(cb);vk.CmdCopyBuffer(cb,output_handle,second->buffer(),1,&copy);
    vkcheck(vk.EndCommandBuffer(cb),"End imported primary");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};vkcheck(vk.CreateFence(d.resource.device,&fi,nullptr,&fence),"Create imported fence");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cb;
    vkcheck(vk.QueueSubmit(d.queue,1,&si,fence),"Submit imported primary");
    vkcheck(vk.WaitForFences(d.resource.device,1,&fence,VK_TRUE,10000000000ull),"Complete imported primary");
    auto words=[&](const gpu::Buffer& buffer,uint32_t expected){const auto data=app.readback(d.queue,buffer,0,bytes);
        for(size_t i=0;i<data.size();i+=4){uint32_t word=0;memcpy(&word,data.data()+i,4);if(word!=expected)return false;}return true;};
    check(words(first,22) && words(second,26),"Original INI copy and compute consume the producer at each actual insertion point");
    vk.DestroyCommandPool(d.resource.device,pool,nullptr);vk.DestroyFence(d.resource.device,fence,nullptr);packet.reset();
    check(source_lifetime.expired() && output_lifetime.expired(),"Retiring the imported recording releases all application and owned resources");
}
