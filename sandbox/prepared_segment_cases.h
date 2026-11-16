static bool awaiting_admission(const std::shared_ptr<const gpu::PreparedResourceCommands>& packet){
    try{packet->record(VK_NULL_HANDLE);}catch(const std::exception& e){return std::string(e.what()).find("admission")!=std::string::npos;}
    return false;
}
static void prepared_segment_cases(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);
    write(root/"segments.hlsl",R"hlsl(Texture2D<uint> Saved : register(t0);
Texture2D<uint> Current : register(t1);
RWStructuredBuffer<uint> Output : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { Output[id.x] = Saved.Load(int3(id.xy,0)) + Current.Load(int3(id.xy,0)); }
)hlsl");
    gpu::HlslCompiler compiler(L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
    gpu::HlslRequest request;request.source=root/"segments.hlsl";request.working_directory=root;
    gpu::HlslProgram program;std::string why;check(compiler.compile(request,program,why),"Compile ordered segment consumer");
    uint32_t left=UINT32_MAX,right=UINT32_MAX,storage=UINT32_MAX;
    for(const auto& b:program.bindings){if(b.source_type=='t' && b.slot==0)left=b.binding;
        if(b.source_type=='t' && b.slot==1)right=b.binding;if(b.source_type=='u')storage=b.binding;}
    check(left!=UINT32_MAX && right!=UINT32_MAX && storage!=UINT32_MAX,"Segment consumer has exact source registers");
    gpu::ResourceCommands app(d.resource);gpu::ImageDescription desc;desc.format=VK_FORMAT_R32_UINT;desc.extent={4,1,1};desc.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
    auto source=app.create_image(desc);VkClearColorValue zero{};app.clear(source,source->full_range(),zero);
    auto output=app.create(16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);{auto ticket=app.submit(d.queue);ticket->wait();}
    auto work=std::make_unique<gpu::ResourceCommands>(d.resource);auto shaders=std::make_unique<gpu::ShaderCommands>(*work,shader_host(d));
    auto pipeline=shaders->compile(program);auto saved=work->create_image(desc);
    auto saved_view=work->view(saved,VK_IMAGE_VIEW_TYPE_2D,VK_FORMAT_R32_UINT,saved->full_range(),VK_IMAGE_USAGE_SAMPLED_BIT);
    gpu::ApplicationImage facts;facts.image=source->image();facts.description=source->description();facts.layout=VK_IMAGE_LAYOUT_GENERAL;facts.uniform_layout=true;
    facts.memory=source->memory();facts.memory_offset=source->memory_offset();facts.memory_size=source->memory_size();facts.owner=source;
    auto begin=[&]{work->begin_prepared();return work->import_image(facts);};
    VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={4,1,1};
    auto consume=[&](const gpu::Image& image){
        gpu::ShaderDescriptor a;a.binding=left;a.image=saved_view;
        gpu::ShaderDescriptor b;b.binding=right;b.image=work->view(image,VK_IMAGE_VIEW_TYPE_2D,VK_FORMAT_R32_UINT,image->full_range(),VK_IMAGE_USAGE_SAMPLED_BIT);
        gpu::ShaderDescriptor out;out.binding=storage;out.size=16;
        out.buffer=work->import_buffer(output->buffer(),output->size(),output->usage(),output->memory(),output->memory_offset(),output->memory_size(),output);
        shaders->dispatch(pipeline,{a,b,out},{4,1,1});
    };
    auto imported=begin();check(!work->cut_prepared() && work->preparing(),"Empty cuts keep the whole preparation open");
    work->copy(imported,saved,copy);auto cancelled_a=work->cut_prepared();consume(imported);auto cancelled_b=work->cut_prepared();
    check(cancelled_a && cancelled_b && cancelled_a->sequence()==cancelled_b->sequence() && cancelled_a->segment()==0 && cancelled_b->segment()==1,
        "Segments retain one sequence identity and exact order");
    check(awaiting_admission(cancelled_a) && awaiting_admission(cancelled_b),"Cutting segments never admits their commands early");
    refuses([&]{work->accept_prepared();},"Admission cannot occur while later segments may still be prepared");
    check(!work->finish_prepared(),"Finishing after the last cut needs no extra packet");work->discard();
    check(awaiting_admission(cancelled_a) && awaiting_admission(cancelled_b),"Whole-scope cancellation invalidates every previously cut segment");
    check(app.readback(d.queue,output,0,16)==std::vector<uint8_t>(16,0),"Cutting and cancelling segments leaves application output untouched");
    // Aliases across different segments still need an explicit physical phase proof.
    for(bool buffer_first:{false,true}){
        imported=begin();auto alias=work->import_buffer(output->buffer(),output->size(),output->usage(),source->memory(),source->memory_offset(),source->memory_size(),source);
        if(buffer_first)work->fill(alias,0,16,1);else work->copy(imported,saved,copy);
        auto earlier=work->cut_prepared();
        if(buffer_first)work->copy(imported,saved,copy);else work->fill(alias,0,16,1);
        refuses([&]{work->finish_prepared();},"Image/buffer alias cannot bypass admission by crossing a segment boundary");
        work->discard();check(awaiting_admission(earlier),"A later alias refusal leaves earlier segments unadmitted");
    }
    imported=begin();work->copy(imported,saved,copy);auto a=work->cut_prepared();consume(imported);auto b=work->finish_prepared();
    auto initial=[&](const auto& packet){for(const auto& state:packet->image_states())if(state.image==saved)return state.initial;return VK_IMAGE_LAYOUT_MAX_ENUM;};
    check(initial(a)==VK_IMAGE_LAYOUT_UNDEFINED && initial(b)==VK_IMAGE_LAYOUT_GENERAL,
        "Owned initialization stays private across cuts and cancellation never publishes it");
    check(a->sequence()!=cancelled_a->sequence() && !a->changes_compute_state() && b->changes_compute_state(),
        "New scopes have distinct identities and each segment describes its own compute changes");
    work->accept_prepared();cancelled_a.reset();cancelled_b.reset();
    // One aggregate budget, not a fresh command allowance after every cut.
    work->begin_prepared();auto scratch=work->create(4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);size_t total=0;bool limited=false;
    for(size_t segment=0;segment<30 && !limited;++segment){
        for(size_t i=0;i<100;++i)try{work->fill(scratch,0,4,1);}catch(const std::runtime_error&){limited=true;break;}
        if(auto part=work->cut_prepared())total+=part->size();
    }
    check(limited && total>3800 && total<=4096,"Cutting cannot reset the aggregate command budget");
    work->finish_prepared();work->discard();
    work->begin_prepared();for(size_t i=0;i<256;++i){work->fill(scratch,0,4,1);work->cut_prepared();}
    refuses([&]{work->fill(scratch,0,4,1);},"Nonempty segments have their own bounded count");
    check(!work->finish_prepared(),"A segment-budget refusal preserves complete earlier segments");work->discard();
    const auto handle=source->image();const auto range=source->full_range();const auto& vk=d.resource.dispatch;
    auto produce=[&](VkCommandBuffer cb,uint32_t word){
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=handle;barrier.subresourceRange=range;
        barrier.oldLayout=barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.srcAccessMask=barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        vk.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkClearColorValue clear{};clear.uint32[0]=word;vk.CmdClearColorImage(cb,handle,VK_IMAGE_LAYOUT_GENERAL,&clear,1,&range);
    };
    std::weak_ptr<gpu::ImageResource> lifetime=source,owned_lifetime=saved;
    imported.reset();source.reset();facts.owner.reset();saved.reset();saved_view.reset();scratch.reset();pipeline.reset();shaders.reset();work.reset();
    check(!lifetime.expired() && !owned_lifetime.expired(),"Accepted segments retain application and owned images after engine destruction");
    auto verify=[&](uint32_t expected){const auto bytes=app.readback(d.queue,output,0,16);uint32_t values[4];memcpy(values,bytes.data(),16);
        return std::all_of(std::begin(values),std::end(values),[&](uint32_t x){return x==expected;});};
    record_prepared_packets(d,{a,b},[&](auto cb){produce(cb,7);},{},[&](auto cb,size_t){produce(cb,11);});
    check(verify(18),"Consumer reads a saved pre-call image and the application's new inter-segment producer");
    record_prepared_packets(d,{a,b},[&](auto cb){produce(cb,13);},{},[&](auto cb,size_t){produce(cb,17);});
    check(verify(30),"Replayed segments preserve both sides of the intervening application command");
    a.reset();b.reset();check(lifetime.expired() && owned_lifetime.expired(),"All sequence image owners retire with its executable segments");
}
