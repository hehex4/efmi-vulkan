static void prepared_imported_image_cases(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);
    write(root/"imported.hlsl",R"hlsl(Texture2D<uint> Input : register(t0);
RWStructuredBuffer<uint> Output : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { Output[id.x] = Input.Load(int3(id.xy,0)) * 2; }
)hlsl");
    gpu::HlslCompiler compiler(L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
    gpu::HlslRequest request;request.source=root/"imported.hlsl";request.working_directory=root;
    gpu::HlslProgram program;std::string why;check(compiler.compile(request,program,why),"Compile retained application image compute");
    uint32_t sampled=UINT32_MAX,storage=UINT32_MAX;
    for(const auto& b:program.bindings){if(b.source_type=='t')sampled=b.binding;if(b.source_type=='u')storage=b.binding;}
    check(sampled!=UINT32_MAX && storage!=UINT32_MAX,"Imported image compute has known source registers");
    gpu::ResourceCommands app(d.resource);gpu::ImageDescription description;
    description.format=VK_FORMAT_R32_UINT;description.extent={4,1,1};
    description.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    auto source=app.create_image(description);VkClearColorValue initial{};initial.uint32[0]=3;
    app.clear(source,source->full_range(),initial);{auto ticket=app.submit(d.queue);ticket->wait();}
    auto output=app.create(16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),copied=app.create(16,0);
    auto commands=std::make_unique<gpu::ResourceCommands>(d.resource);
    auto shaders=std::make_unique<gpu::ShaderCommands>(*commands,shader_host(d));auto pipeline=shaders->compile(program);
    gpu::ApplicationImage facts;facts.image=source->image();facts.description=source->description();facts.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    facts.uniform_layout=true;facts.memory=source->memory();facts.memory_offset=source->memory_offset();facts.memory_size=source->memory_size();facts.owner=source;
    refuses([&]{commands->import_image(facts);},"Application image imports cannot enter deferred execution");
    commands->begin_prepared();
    {
        auto limited=facts;limited.layout=VK_IMAGE_LAYOUT_GENERAL;limited.description.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
        auto sampled_only=commands->import_image(limited);
        refuses([&]{commands->clear(sampled_only,sampled_only->full_range(),initial);},"Image clear requires declared transfer destination usage");
        auto buffer=commands->import_buffer(copied->buffer(),copied->size(),copied->usage(),copied->memory(),copied->memory_offset(),copied->memory_size(),copied);
        refuses([&]{commands->copy(sampled_only,buffer,image_region(source));},"Image read copy requires declared transfer source usage");
    }
    commands->discard();
    commands->begin_prepared();
    auto invalid=facts;invalid.uniform_layout=false;refuses([&]{commands->import_image(invalid);},"A view-local layout cannot prove the whole imported image");
    invalid=facts;invalid.owner.reset();refuses([&]{commands->import_image(invalid);},"Imported image requires a complete lifetime owner");
    invalid=facts;invalid.memory_size=1;refuses([&]{commands->import_image(invalid);},"Imported image span covers actual Vulkan memory requirements");
    invalid=facts;invalid.memory_offset=UINT64_MAX;refuses([&]{commands->import_image(invalid);},"Imported image range cannot overflow");
    invalid=facts;invalid.layout=VK_IMAGE_LAYOUT_UNDEFINED;refuses([&]{commands->import_image(invalid);},"Application image data cannot be imported from an undefined layout");
    auto imported=commands->import_image(facts);check(commands->import_image(facts)==imported,"Aliases share one imported image and layout identity");
    invalid=facts;invalid.layout=VK_IMAGE_LAYOUT_GENERAL;refuses([&]{commands->import_image(invalid);},"Aliases cannot assert two layouts in the same scope");
    invalid=facts;invalid.description.extent.width=2;refuses([&]{commands->import_image(invalid);},"A retained image cannot change its allocation description");
    auto borrow=[&](const gpu::Buffer& value){return commands->import_buffer(value->buffer(),value->size(),value->usage(),value->memory(),value->memory_offset(),value->memory_size(),value);};
    auto view=commands->view(imported,VK_IMAGE_VIEW_TYPE_2D,VK_FORMAT_R32_UINT,imported->full_range(),VK_IMAGE_USAGE_SAMPLED_BIT);
    gpu::ShaderDescriptor in;in.binding=sampled;in.image=view;
    gpu::ShaderDescriptor out;out.binding=storage;out.buffer=borrow(output);out.size=16;
    shaders->dispatch(pipeline,{in,out},{4,1,1});auto cancelled=commands->finish_prepared();commands->discard();cancelled.reset();
    auto bytes=app.readback(d.queue,source,image_region(source));uint32_t words[4];memcpy(words,bytes.data(),16);
    check(std::all_of(std::begin(words),std::end(words),[](uint32_t x){return x==3;}),"Cancelled image import does not change application pixels or layout");
    commands->begin_prepared();
    refuses([&]{shaders->dispatch(pipeline,{in,out},{4,1,1});},"A saved image needs a fresh observation in each prepared scope");
    commands->discard();commands->begin_prepared();commands->import_image(facts);shaders->dispatch(pipeline,{in,out},{4,1,1});
    auto packet=commands->finish_prepared();commands->accept_prepared();
    check(packet->image_states().size()==1 && packet->image_states()[0].initial==facts.layout && packet->image_states()[0].final==facts.layout,
        "Application image packet restores its observed attachment layout");
    check(packet->image_states()[0].image->memory()==facts.memory && packet->image_states()[0].image->memory_size()==facts.memory_size,
        "Image access metadata retains the actual allocation span");
    commands->begin_prepared();auto next=facts;next.layout=VK_IMAGE_LAYOUT_GENERAL;
    check(commands->import_image(next)==imported,"A later scope refreshes layout observation without changing image identity");
    commands->copy(imported,borrow(copied),image_region(source));auto general=commands->finish_prepared();commands->accept_prepared();
    check(general->image_states()[0].initial==VK_IMAGE_LAYOUT_GENERAL && packet->image_states()[0].final==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        "A later observation does not rewrite an already sealed packet's layout contract");
    // Claimed overlapping physical storage must refuse before any packet can record.
    commands->begin_prepared();commands->import_image(next);
    auto alias=commands->import_buffer(output->buffer(),output->size(),output->usage(),source->memory(),source->memory_offset(),source->memory_size(),source);
    commands->copy(imported,alias,image_region(source));
    refuses([&]{commands->finish_prepared();},"Image/buffer allocation alias requires separate phase proof");commands->discard();alias.reset();
    refuses([&]{commands->copy(imported,out.buffer,image_region(source));},"Retained application images cannot leak into ordinary submissions");
    const auto handle=source->image();const auto range=source->full_range();const auto& vk=d.resource.dispatch;
    auto transition=[&](VkCommandBuffer cb,VkImageLayout from,VkImageLayout to){
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=handle;barrier.subresourceRange=range;
        barrier.oldLayout=from;barrier.newLayout=to;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=barrier.srcAccessMask;
        vk.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    };
    auto produce=[&](uint32_t word){return [&,word](VkCommandBuffer cb){
        transition(cb,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL);VkClearColorValue clear{};clear.uint32[0]=word;
        vk.CmdClearColorImage(cb,handle,VK_IMAGE_LAYOUT_GENERAL,&clear,1,&range);
        transition(cb,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    };};
    auto restore=[&](VkCommandBuffer cb){transition(cb,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);};
    std::weak_ptr<gpu::ImageResource> lifetime=source;
    source.reset();facts.owner.reset();next.owner.reset();invalid.owner.reset();imported.reset();view.reset();in.image.reset();out.buffer.reset();
    pipeline.reset();shaders.reset();commands.reset();check(!lifetime.expired(),"Application image owner survives interpreter-side engine destruction");
    auto result=[&](const gpu::Buffer& buffer,uint32_t expected){auto data=app.readback(d.queue,buffer,0,16);uint32_t values[4];memcpy(values,data.data(),16);
        return std::all_of(std::begin(values),std::end(values),[&](uint32_t value){return value==expected;});};
    record_prepared_packets(d,{packet},produce(7),restore);check(result(output,14),"Imported image compute consumes the immediately preceding application producer");
    record_prepared_packets(d,{general});check(result(copied,7),"A following GENERAL packet observes the preserved application data");
    record_prepared_packets(d,{packet},produce(11),restore);check(result(output,22),"Replaying the earlier attachment contract consumes the later producer");
    packet.reset();general.reset();check(lifetime.expired(),"Imported allocation retires only after executable packets are released");
}
