// Cancel original keyed lookup, expiry, lazy variables, proxy and metadata changes.
static void prepared_pool_cases(Device& d,const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    write(root/"d3dx.ini",R"ini([Constants]
global $step = 0
global $value = -1
global $alias = -1
global $feedback = -1
[ResourceSeed]
type = StructuredBuffer
stride = 4
data = R32_UINT 7 11 13 17
bind_flags = shader_resource unordered_access
[ResourceOther]
type = StructuredBuffer
stride = 4
data = R32_UINT 31 37 41 43
bind_flags = shader_resource unordered_access
[PoolValues]
pool_size = 2
pool_persist_variables = true
pool_variable_default_value = 5
[PoolFIFO]
pool_size = 2
pool_index_type = fifo
[PoolAlias]
pool_size = 1
[PoolExpire]
pool_size = 1
pool_index_type = fifo
pool_expiration_timeout_frames = 2
pool_expiration_refresh_on_read = true
[PoolLate]
pool_size = 1
pool_persist_variables = true
[Present]
$step = $step + 1
if $step <= 2
 PoolFIFO[100] = copy ResourceSeed
 cs-u0 = ref PoolFIFO[100]
 $PoolValues[0] = 9
 $value = $PoolValues[0]
elif $step == 3
 PoolFIFO[200] = copy ResourceOther
 PoolFIFO[300] = copy ResourceOther
 $PoolValues[0] = 77
 PoolAlias = ref PoolFIFO
 cs-u0 = ref PoolAlias[300]
 $value = $PoolValues[0]
elif $step == 4 || $step == 6
 cs-u0 = ref PoolFIFO[100]
 $value = $PoolValues[0]
 $alias = PoolAlias->size
elif $step == 5
 PoolFIFO = null
 $PoolValues[*] = null
 $value = $PoolValues[0]
elif $step == 7
 PoolExpire[7] = ref PoolFIFO[100]
elif $step == 8 || $step == 9
 cs-u0 = ref PoolExpire[7]
elif $step == 10
 PoolAlias = ref PoolFIFO
elif $step == 11
 PoolAlias = null
 PoolFIFO[400] = copy ResourceOther
elif $step == 12 || $step == 14
 cs-u0 = ref PoolAlias[100]
 $alias = PoolAlias->size
elif $step == 13
 PoolAlias = copy_desc PoolValues
 $PoolAlias[0] = 6
elif $step == 15
 $PoolLate[0] = 19
 clear = PoolFIFO[100] 99
 store = $feedback, ResourceSeed, 0
endif
)ini");
    using Site=frontend::GpuResourceRuntime::ExecutionSite;using Target=ResourceCopyTargetType;
    auto gpu=std::make_shared<frontend::GpuShaderRuntime>(d.resource,shader_host(d),L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
    auto& resources=gpu->resources();std::string why;
    frontend::RuntimeConfig config{root,{},root/"settings","prepared-pools"};config.installation=root;
    resources.begin(d.queue,Site::OutsideRenderPass);auto ini=frontend::IniRuntime::load(config,frontend::owned_generation({},gpu),why);
    if(!ini)throw std::runtime_error("Load prepared pool fixture: "+why);++checks;
    {auto ticket=resources.finish();if(ticket)ticket->wait();}
    const auto variables=ini->status().variables.size();
    auto begin=[&](unsigned frame){resources.begin_prepared();check(ini->present({},frame,0,why),"Original pool lists execute inside preparation");};
    auto slot=[&]{return resources.binding(L'c',Target::UNORDERED_ACCESS_VIEW,0);};
    auto cancel=[&]{check(!resources.failed(),"Pool preparation remains admissible before cancellation");auto packet=resources.finish_prepared();resources.discard();};
    auto admit=[&]{auto packet=resources.finish_prepared();resources.accept_prepared();return packet;};
    auto read=[&](const frontend::GpuResourceBinding& binding){if(!binding.buffer)return false;
        const auto bytes=resources.commands().readback(d.queue,binding.buffer,0,16);const uint32_t expected[]={7,11,13,17};
        return bytes.size()==sizeof(expected) && !memcmp(bytes.data(),expected,sizeof(expected));};
    begin(10);check(ini->status().variables.size()==variables+1 && ini->status().variables.at("$value")==9,
        "A lazy persistent pool variable is visible within its prepared scope");cancel();
    check(ini->status().variables.size()==variables && ini->status().variables.at("$value")==9 && !slot(),
        "Cancellation removes lazy registration and keeps ordinary scalar assignments");
    begin(10);auto packet=admit();check(bool(packet),"Original pooled copy seals actual buffer work");
    const auto& vk=d.resource.dispatch;VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer cb=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=d.resource.queue_family;
    vkcheck(vk.CreateCommandPool(d.resource.device,&pi,nullptr,&pool),"Create pool transaction command pool");
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.commandBufferCount=1;
    vkcheck(vk.AllocateCommandBuffers(d.resource.device,&ai,&cb),"Allocate pool transaction primary");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(vk.BeginCommandBuffer(cb,&bi),"Begin pool transaction primary");
    packet->record(cb);vkcheck(vk.EndCommandBuffer(cb),"End pool transaction primary");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};vkcheck(vk.CreateFence(d.resource.device,&fi,nullptr,&fence),"Create pool transaction fence");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cb;
    vkcheck(vk.QueueSubmit(d.queue,1,&si,fence),"Submit pool transaction primary");
    vkcheck(vk.WaitForFences(d.resource.device,1,&fence,VK_TRUE,10000000000ull),"Complete pool transaction primary");
    vk.DestroyCommandPool(d.resource.device,pool,nullptr);vk.DestroyFence(d.resource.device,fence,nullptr);packet.reset();
    auto seed=slot();check(read(seed),"Retry after lazy pool rollback initializes actual GPU bytes");
    begin(11);check(ini->status().variables.at("$value")==77,"Pool variables see speculative changes in order");cancel();
    check(slot().buffer==seed.buffer && read(seed),"Cancelled FIFO eviction and copies preserve admitted resource ownership and bytes");
    begin(11);
    check(slot().buffer==seed.buffer,"Cancelled FIFO eviction restores the original keyed resource");
    check(ini->status().variables.at("$value")==9,"Cancelled pool assignment restores its prior variable value");
    check(ini->status().variables.at("$alias")==1,"Cancelled pool reference restores its prior proxy identity");
    check(!admit(),"Restored pool references need no GPU packet");
    begin(11);check(ini->status().variables.at("$value")==5,"Original pool variable reset takes effect speculatively");cancel();
    begin(11);check(slot().buffer==seed.buffer && ini->status().variables.at("$value")==9,"Cancelled resets restore both index and variable state");admit();
    begin(12);admit();
    begin(100);check(!slot(),"Original expiry removes the old key in the speculative clock sample");cancel();
    begin(13);check(slot().buffer==seed.buffer,"Cancelling the speculative expiry restores its key and refresh clock");admit();
    begin(13);admit();begin(13);cancel();
    begin(13);check(slot().buffer==seed.buffer && ini->status().variables.at("$alias")==2,"Cancelled proxy reset preserves shared pool lookup");admit();
    const auto stable_variables=ini->status().variables.size();begin(13);cancel();
    check(ini->status().variables.size()==stable_variables,"Cancelled pool metadata copy removes its newly registered variables");
    begin(13);check(slot().buffer==seed.buffer && ini->status().variables.at("$alias")==2,"Cancelled metadata copy restores the previous pool proxy and size");admit();
    begin(13);check(resources.failed(),"Unsupported feedback fails a scope after original pool variable creation");
    refuses([&]{resources.finish_prepared();},"A failed pool scope cannot seal partial work");resources.discard();
    check(ini->status().variables.size()==stable_variables && ini->status().variables.at("$feedback")==-1 && read(seed),
        "Failure removes lazy persistent registration and discards earlier GPU writes");
}
