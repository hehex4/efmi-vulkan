// Prepared transfer/compute work consumes the producer at its insertion point.
static void prepared_resource_cases(Device& d,const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    write(root/"ordered.hlsl",R"hlsl(StructuredBuffer<uint> Input : register(t0);
RWStructuredBuffer<uint> Output : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { Output[id.x] = Input[id.x] * 2; }
)hlsl");
    gpu::HlslCompiler compiler(L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
    gpu::HlslRequest request;request.source=root/"ordered.hlsl";request.working_directory=root;
    gpu::HlslProgram program;std::string why;
    check(compiler.compile(request,program,why),"compile original prepared compute shader");
    uint32_t sampled=UINT32_MAX,storage=UINT32_MAX;
    for(const auto& b:program.bindings){if(b.source_type=='t')sampled=b.binding;if(b.source_type=='u')storage=b.binding;}
    check(sampled!=UINT32_MAX && storage!=UINT32_MAX,"prepared compute uses reflected source registers");
    const VkDeviceSize bytes=256;gpu::ResourceCommands app(d.resource);
    auto source=app.create(bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);
    auto output=app.create(bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);
    auto first=app.create(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT),second=app.create(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    {auto initial=app.submit(d.queue);initial->wait();}
    auto work=std::make_unique<gpu::ResourceCommands>(d.resource);
    auto shaders=std::make_unique<gpu::ShaderCommands>(*work,shader_host(d));auto pipeline=shaders->compile(program);
    {const uint32_t value=7;
        auto input=work->create(4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&value,sizeof(value));
        auto result=work->create(4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        gpu::ShaderDescriptor in;in.binding=sampled;in.buffer=input;in.size=4;
        gpu::ShaderDescriptor out;out.binding=storage;out.buffer=result;out.size=4;
        shaders->dispatch(pipeline,{in,out},{1,1,1});
        const auto data=work->readback(d.queue,result,0,4);uint32_t actual=0;memcpy(&actual,data.data(),4);
        check(actual==14,"ordinary owned compute retains its existing submission behavior");
    }
    auto reject=[&](auto action,const char* name){bool refused=false;try{action();}catch(const std::runtime_error&){refused=true;}check(refused,name);};
    auto borrow=[&](const gpu::Buffer& value){return work->import_buffer(value->buffer(),value->size(),value->usage(),
        value->memory(),value->memory_offset(),value->memory_size(),value);};
    reject([&]{borrow(source);},"application import cannot enter the old deferred submission domain");
    work->begin_prepared();
    reject([&]{work->begin_prepared();},"prepared scopes do not nest");
    auto input=borrow(source),destination=borrow(output),alias=borrow(source);
    reject([&]{work->copy(input,0,alias,0,bytes);},"distinct wrappers cannot copy overlapping physical storage");
    reject([&]{work->copy(input,0,destination,0,bytes+4);},"prepared copy validates complete ranges before emission");
    reject([&]{work->submit(d.queue);},"prepared work never submits before its application producer");
    reject([&]{work->readback(d.queue,destination,0,bytes);},"prepared work cannot synchronously read back");
    reject([&]{work->feedback(destination,0,bytes);},"prepared feedback needs the application's completion owner");
    auto scratch=work->create(bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    work->copy(input,0,scratch,0,bytes);
    gpu::ShaderDescriptor in;in.binding=sampled;in.buffer=scratch;in.size=bytes;
    gpu::ShaderDescriptor out;out.binding=storage;out.buffer=destination;out.size=bytes;
    shaders->dispatch(pipeline,{in,out},{uint32_t(bytes/4),1,1});
    auto prepared=work->finish_prepared();
    work->accept_prepared();
    check(prepared && prepared->changes_compute_state() && prepared->device()==d.resource.device && prepared->queue_family()==d.resource.queue_family,
          "prepared work reports compute restoration and queue-family obligations");
    check(!work->pending() && !work->preparing(),"finish detaches all prepared owners without pending submission");
    const auto& accesses=prepared->buffer_accesses();
    check(accesses.size()==4 && accesses[0].buffer==input && accesses[0].offset==0 && accesses[0].size==bytes &&
        accesses[0].stages==VK_PIPELINE_STAGE_TRANSFER_BIT && accesses[0].access==VK_ACCESS_TRANSFER_READ_BIT &&
        accesses[1].buffer==scratch && accesses[1].access==VK_ACCESS_TRANSFER_WRITE_BIT && accesses[0].command==accesses[1].command,
        "prepared copy exposes both exact transfer spans at the same command position");
    check(accesses[2].buffer==scratch && accesses[3].buffer==destination && accesses[2].size==bytes && accesses[3].size==bytes &&
        accesses[2].stages==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT && (accesses[3].access&VK_ACCESS_SHADER_WRITE_BIT) &&
        accesses[2].command==accesses[3].command && accesses[2].command>accesses[0].command,
        "prepared compute exposes conservative descriptor access after its transfer producer");
    check(accesses[0].overlaps(source->memory(),source->memory_offset()+bytes-1,1) &&
        !accesses[0].overlaps(source->memory(),source->memory_offset()+bytes,1) &&
        !accesses[0].overlaps(source->memory(),UINT64_MAX,2) && !accesses[0].overlaps(source->memory(),0,0) &&
        !accesses[0].overlaps(output->memory(),0,bytes),"physical overlap proof distinguishes adjacency, overflow, empty spans and other allocations");
    reject([&]{work->fill(destination,0,bytes,1);},"saved imports cannot escape through ordinary commands");
    reject([&]{prepared->record(VK_NULL_HANDLE);},"prepared replay requires a real recording target");
    check(app.readback(d.queue,output,0,bytes)==std::vector<uint8_t>(size_t(bytes),0),"preparation emits no commands and cannot change GPU data");
    std::weak_ptr<gpu::BufferResource> source_lifetime=source,scratch_lifetime=scratch;
    in={};out={};input.reset();alias.reset();destination.reset();scratch.reset();pipeline.reset();shaders.reset();work.reset();
    check(!scratch_lifetime.expired(),"prepared commands retain scratch allocation after the preparing engine is destroyed");
    const auto source_handle=source->buffer();source.reset();
    check(!source_lifetime.expired(),"prepared application import retains the caller's complete buffer owner");
    const auto& vk=d.resource.dispatch;
    VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer cb=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool_info.queueFamilyIndex=d.resource.queue_family;
    vkcheck(vk.CreateCommandPool(d.resource.device,&pool_info,nullptr,&pool),"create ordered application pool");
    VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocation.commandPool=pool;allocation.commandBufferCount=1;
    vkcheck(vk.AllocateCommandBuffers(d.resource.device,&allocation,&cb),"allocate ordered application buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkcheck(vk.BeginCommandBuffer(cb,&begin),"begin ordered application buffer");
    VkBufferCopy copy{0,0,bytes};
    vk.CmdFillBuffer(cb,source_handle,0,bytes,11);
    prepared->record(cb);vk.CmdCopyBuffer(cb,output->buffer(),first->buffer(),1,&copy);
    vk.CmdFillBuffer(cb,source_handle,0,bytes,13);
    prepared->record(cb);vk.CmdCopyBuffer(cb,output->buffer(),second->buffer(),1,&copy);
    vkcheck(vk.EndCommandBuffer(cb),"end ordered application buffer");
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkcheck(vk.CreateFence(d.resource.device,&fence_info,nullptr,&fence),"create ordered completion fence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cb;
    vkcheck(vk.QueueSubmit(d.queue,1,&submit,fence),"submit application with in-place prepared commands");
    vkcheck(vk.WaitForFences(d.resource.device,1,&fence,VK_TRUE,10000000000ull),"complete ordered application commands");
    auto words=[&](const gpu::Buffer& value,uint32_t expected){const auto data=app.readback(d.queue,value,0,bytes);
        for(size_t i=0;i<data.size();i+=4){uint32_t word=0;memcpy(&word,data.data()+i,4);if(word!=expected)return false;}return true;};
    check(words(first,22),"first prepared consumer sees its immediately preceding application producer");
    check(words(second,26),"same immutable plan replay sees the later producer instead of stale prepared bytes");
    // A retained prepared object remains necessary while the primary is executable.
    vk.DestroyCommandPool(d.resource.device,pool,nullptr);vk.DestroyFence(d.resource.device,fence,nullptr);
    prepared.reset();
    check(source_lifetime.expired() && scratch_lifetime.expired(),"retiring the recording releases imported and scratch owners exactly once");
    gpu::ResourceCommands cancelled(d.resource);cancelled.begin_prepared();
    auto cancel_output=cancelled.import_buffer(output->buffer(),bytes,output->usage(),output->memory(),0,output->memory_size(),output);
    cancelled.fill(cancel_output,0,bytes,99);cancelled.discard();
    check(!cancelled.pending() && !cancelled.preparing() && words(output,26),"discard emits no partial commands or GPU mutation");
    cancelled.begin_prepared();check(!cancelled.finish_prepared(),"empty prepared scope needs no recording owner");cancelled.accept_prepared();
    cancelled.begin_prepared();
    reject([&]{for(size_t i=0;i<2050;++i)cancelled.fill(cancel_output,0,bytes,99);},"prepared emission has a bounded operation budget");
    cancelled.discard();check(words(output,26),"budget refusal and discard leave application output unchanged");
    cancelled.begin_prepared();cancelled.fill(cancel_output,32,64,99);auto partial=cancelled.finish_prepared();
    const auto& span=partial->buffer_accesses();
    check(span.size()==1 && span[0].offset==32 && span[0].size==64 && span[0].access==VK_ACCESS_TRANSFER_WRITE_BIT &&
        !span[0].overlaps(output->memory(),0,32) && span[0].overlaps(output->memory(),31,2) && !span[0].overlaps(output->memory(),96,4),
        "fill access tracks its subrange rather than the entire buffer");
    partial.reset();cancelled.discard();
    cancelled.begin_prepared();
    reject([&]{for(size_t i=0;i<2050;++i)cancelled.fill(cancel_output,0,bytes,99);},"operation budget reserves publication capacity");
    auto bounded=cancelled.finish_prepared();
    check(bounded && bounded->size()==4095 && bounded->buffer_accesses().size()==2047,
        "a refused next operation leaves a complete bounded packet with its final publication barrier");
}
