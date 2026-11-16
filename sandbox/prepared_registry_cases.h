// The original registry must not retain an initialization whose packet was cancelled.
static void prepared_registry_cases(Device& d,const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    write(root/"d3dx.ini",R"ini([Constants]
global $attempt = 0
global $feedback = -1
[ResourceData]
type = StructuredBuffer
stride = 4
data = R32_UINT 7 11 13 17
bind_flags = shader_resource unordered_access
[ResourceOther]
type = StructuredBuffer
stride = 4
data = R32_UINT 31 37 41 43
bind_flags = shader_resource unordered_access
[ResourceCopy]
bind_flags = shader_resource unordered_access
max_copies_per_frame = 1
[ResourceFresh]
type = StructuredBuffer
stride = 4
data = R32_UINT 47 53 59 61
bind_flags = shader_resource unordered_access
[ResourceFreshCopy]
bind_flags = shader_resource unordered_access
[PoolBuffers]
pool_size = 2
[CommandListReplace]
ResourceCopy = copy ResourceOther
cs-u1 = copy ResourceOther
cs-u0 = ref ResourceCopy
[Present]
$attempt = $attempt + 1
if $attempt <= 2
 ResourceCopy = copy ResourceData
 cs-u0 = ref ResourceCopy
elif $attempt <= 4
 run = CommandListReplace
elif $attempt == 5
 clear = ResourceCopy 99
 store = $feedback, ResourceCopy, 0
elif $attempt == 6
 cs-u0 = ref ResourceCopy
elif $attempt == 7
 PoolBuffers[0] = ref ResourceCopy
elif $attempt == 8
 PoolBuffers[0] = ref ResourceCopy
 cs-u0 = ref PoolBuffers[0]
elif $attempt == 9
 PoolBuffers = null
elif $attempt == 10
 cs-u0 = ref PoolBuffers[0]
elif $attempt <= 12
 $feedback = PoolBuffers[0]->size
elif $attempt == 13
 ResourceCopy = null
 cs-u0 = null
elif $attempt <= 15
 ResourceFreshCopy = copy ResourceFresh
 cs-u0 = ref ResourceFreshCopy
endif
)ini");
    gpu::ResourceBudgets budgets;auto host=shader_host(d);
    auto gpu=std::make_shared<frontend::GpuShaderRuntime>(d.resource,host,L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll",budgets);
    auto& resources=gpu->resources();std::string why;
    using Site=frontend::GpuResourceRuntime::ExecutionSite;using Target=ResourceCopyTargetType;
    frontend::RuntimeConfig config{root,{},root/"settings","prepared-registry"};config.installation=root;
    resources.begin(d.queue,Site::OutsideRenderPass);
    auto ini=frontend::IniRuntime::load(config,frontend::owned_generation({},gpu),why);
    if(!ini)throw std::runtime_error("Load prepared registry: "+why);++checks;
    {auto ticket=resources.finish();if(ticket)ticket->wait();}
    const auto baseline=budgets.resident->used();
    auto slot=[&](unsigned index=0){return resources.binding(L'c',Target::UNORDERED_ACCESS_VIEW,index);};
    auto begin=[&](unsigned frame){resources.begin_prepared();check(ini->present({},frame,0,why),"Original lists evaluate in a prepared registry");};
    auto read=[&](const frontend::GpuResourceBinding& value,const std::vector<uint32_t>& expected){
        const auto bytes=resources.commands().readback(d.queue,value.buffer,0,value.buffer->size());
        return bytes.size()==expected.size()*4 && !memcmp(bytes.data(),expected.data(),bytes.size());};
    auto record=[&](std::shared_ptr<const gpu::PreparedResourceCommands> packet){
        resources.accept_prepared();if(!packet)return;
        const auto& vk=d.resource.dispatch;VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer cb=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=d.resource.queue_family;
        vkcheck(vk.CreateCommandPool(d.resource.device,&pi,nullptr,&pool),"Create registry insertion pool");
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.commandBufferCount=1;
        vkcheck(vk.AllocateCommandBuffers(d.resource.device,&ai,&cb),"Allocate registry insertion primary");
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(vk.BeginCommandBuffer(cb,&bi),"Begin registry insertion");
        packet->record(cb);vkcheck(vk.EndCommandBuffer(cb),"End registry insertion");
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};vkcheck(vk.CreateFence(d.resource.device,&fi,nullptr,&fence),"Create registry insertion fence");
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cb;
        vkcheck(vk.QueueSubmit(d.queue,1,&si,fence),"Submit admitted registry packet");
        vkcheck(vk.WaitForFences(d.resource.device,1,&fence,VK_TRUE,10000000000ull),"Complete registry packet");
        vk.DestroyCommandPool(d.resource.device,pool,nullptr);vk.DestroyFence(d.resource.device,fence,nullptr);
    };
    begin(10);check(bool(slot()),"A prepared assignment is visible to later commands in its scope");
    refuses([&]{resources.accept_prepared();},"An unsealed registry cannot be admitted");
    auto first=resources.finish_prepared();check(first && budgets.resident->used()>baseline,"Lazy initialization is retained by the unadmitted packet");
    refuses([&]{resources.begin(d.queue,Site::OutsideRenderPass);},"A sealed registry cannot escape through an ordinary scope");
    refuses([&]{resources.begin_prepared();},"A sealed registry cannot be overwritten before admission");
    resources.discard();check(!slot() && ini->status().variables.at("$attempt")==1,"Cancellation restores bindings while original CPU variables persist");
    first.reset();check(budgets.resident->used()==baseline,"Cancellation removes uninitialized resource and copy-cache allocations");
    begin(10);auto second=resources.finish_prepared();record(std::move(second));
    auto saved=slot();check(read(saved,{7,11,13,17}),"Retry in the same frame restores copy allowance and actually initializes every source word");
    const auto accepted=budgets.resident->used();
    begin(11);auto third=resources.finish_prepared();check(bool(slot(1)),"Speculative per-instruction copy cache binds its destination");
    resources.discard();third.reset();check(!slot(1) && slot().buffer==saved.buffer && budgets.resident->used()==accepted,
        "Cancellation restores existing aliases and removes new per-instruction copy caches");
    check(read(saved,{7,11,13,17}),"Cancelled writes never alter an already admitted buffer");
    begin(11);record(resources.finish_prepared());
    check(read(slot(),{31,37,41,43}) && read(slot(1),{31,37,41,43}),"Retry reinitializes lazy source and per-instruction destination after cancellation");
    begin(12);check(resources.failed(),"A later unsupported operation fails the entire speculative GPU scope");
    refuses([&]{resources.finish_prepared();},"A failed registry cannot seal partial commands");
    resources.discard();check(ini->status().variables.at("$feedback")==-1 && read(slot(),{31,37,41,43}),
        "Failure rolls back the earlier clear and leaves synchronous feedback untouched");
    begin(12);auto reference=resources.finish_prepared();check(!reference,"A pure reference needs no GPU packet");record(std::move(reference));
    check(read(slot(),{31,37,41,43}),"A subsequent reference-only scope admits the retained original allocation");
    begin(13);check(!resources.failed(),"Pool mutation participates in prepared cancellation");resources.finish_prepared();resources.discard();
    resources.begin(d.queue,Site::OutsideRenderPass);check(ini->present({},13,0,why),"Ordinary original pool execution remains available");
    {auto ticket=resources.finish();if(ticket)ticket->wait();}
    check(read(slot(),{31,37,41,43}),"Ordinary pool references preserve the admitted resource after prepared cancellation");
    begin(14);check(!resources.failed(),"A CPU pool reset participates in prepared cancellation");resources.finish_prepared();resources.discard();
    resources.begin(d.queue,Site::OutsideRenderPass);check(ini->present({},14,0,why),"Ordinary pool lookup follows the cancelled reset");
    {auto ticket=resources.finish();if(ticket)ticket->wait();}
    check(read(slot(),{31,37,41,43}),"Cancelled original CPU pool reset preserves admitted pool elements");
    begin(15);check(!resources.failed() && ini->status().variables.at("$feedback")==16,
        "A prepared pool property query executes through original lookup");resources.finish_prepared();resources.discard();
    resources.begin(d.queue,Site::OutsideRenderPass);check(ini->present({},15,0,why),"Ordinary pool property query remains available");
    {auto ticket=resources.finish();if(ticket)ticket->wait();}
    check(ini->status().variables.at("$feedback")==16,"Ordinary original query reads the preserved pool resource");
    begin(16);check(!slot(),"Null assignment is visible during speculative execution");
    check(!resources.finish_prepared(),"A null assignment alone records no GPU operation");resources.discard();
    check(read(slot(),{31,37,41,43}),"Cancellation restores a resource made null without discarding its old cache");
    const auto usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT|
        VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    auto probe=resources.commands().create(16,usage);const auto allocation=probe->memory_size();probe.reset();
    const auto pressure=budgets.resident->limit()-budgets.resident->used()-allocation;
    check(budgets.resident->acquire(pressure),"Reserve test pressure leaving exactly one buffer allocation");
    begin(17);check(resources.failed() && resources.pending(),"Copy fails after recording its new source initialization when destination allocation is unavailable");
    resources.discard();budgets.resident->release(pressure);
    check(read(slot(),{31,37,41,43}),"Partial copy failure restores the earlier application's logical binding and bytes");
    begin(17);record(resources.finish_prepared());
    check(read(slot(),{47,53,59,61}),"The exact same copy instruction retries successfully after partial preparation failure");
    saved={};ini.reset();gpu.reset();
    check(!budgets.resident->used() && !budgets.staging->used() && !host.objects->used(),"Retiring the registry releases admitted and cancelled owners");
}
