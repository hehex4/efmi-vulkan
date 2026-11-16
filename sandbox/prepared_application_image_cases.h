static void prepared_application_image_cases(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);
    write(root/"ordered.hlsl",R"hlsl(Texture2DArray<uint> Input : register(t0);
RWStructuredBuffer<uint> Output : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { Output[id.x] = Input.Load(int4(id.xy,0,0)) * 2; }
)hlsl");
    write(root/"d3dx.ini",R"ini([Constants]
global $attempt = 0
global $filter = 0
[TextureOverrideSource]
hash = abcdef23
filter_index = 42
[ResourceSaved]
[ResourceCopy]
bind_flags = shader_resource
[ResourceOutput]
type = StructuredBuffer
stride = 4
byte_width = 16
bind_flags = unordered_access
[CustomShaderDouble]
cs = ordered.hlsl
cs-t0 = ref ResourceSaved
cs-u0 = ref ResourceOutput
dispatch = 4,1,1
[Present]
$attempt = $attempt + 1
if $attempt <= 2
 $filter = ps-t0
 ResourceSaved = ref ps-t0
 ResourceCopy = copy ps-t0
 ps-t6 = copy ps-t0
endif
run = CustomShaderDouble
cs-u0 = ref ResourceOutput
ps-t5 = ref ResourceCopy
)ini");
    using Runtime=frontend::GpuResourceRuntime;using Target=ResourceCopyTargetType;
    auto engine=std::make_shared<frontend::GpuShaderRuntime>(d.resource,shader_host(d),L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
    auto& resources=engine->resources();std::string why;
    frontend::RuntimeConfig config{root,{},root/"settings","prepared-application-images"};config.installation=root;
    resources.begin(d.queue,Runtime::ExecutionSite::OutsideRenderPass);
    auto ini=frontend::IniRuntime::load(config,frontend::owned_generation({},engine),why);
    if(!ini)throw std::runtime_error("Load application image registry: "+why);++checks;
    {auto ticket=resources.finish();if(ticket)ticket->wait();}
    gpu::ResourceCommands app(d.resource);gpu::ImageDescription desc;
    desc.format=VK_FORMAT_R32_UINT;desc.extent={4,1,1};desc.layers=2;
    desc.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    auto source=app.create_image(desc);VkClearColorValue zero{};app.clear(source,source->full_range(),zero);
    auto depth_desc=desc;depth_desc.format=VK_FORMAT_D32_SFLOAT;depth_desc.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    auto depth=app.create_image(depth_desc);app.clear(depth,depth->full_range(),VkClearDepthStencilValue{0.5f,0});
    auto constant=app.create(16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,nullptr,0,true);
    auto result=app.create(16,0,nullptr,0,true),copied=app.create(32,0,nullptr,0,true);
    {auto ticket=app.submit(d.queue);ticket->wait();}
    auto facts=[&](const gpu::Image& image,uint64_t id,DXGI_FORMAT format,UINT flags){
        auto out=std::make_shared<Runtime::ApplicationImageSource>();out->resource_id=id;out->source_known=true;
        auto& a=out->allocation;a.image=image->image();a.description=image->description();a.layout=VK_IMAGE_LAYOUT_GENERAL;a.uniform_layout=true;
        a.memory=image->memory();a.memory_offset=image->memory_offset();a.memory_size=image->memory_size();a.owner=image;
        auto& t=out->description.texture;t.Width=4;t.Height=1;t.MipLevels=1;t.ArraySize=2;t.Format=format;t.SampleDesc.Count=1;t.BindFlags=flags;
        auto tracking=std::make_shared<frontend::SourceResourceIdentity>();tracking->tracking=frontend::SourceResourceIdentity::Tracking::Tracked;
        tracking->hash_known=true;tracking->hash=0xabcdef23;out->identity=tracking;return out;
    };
    auto source_facts=facts(source,201,DXGI_FORMAT_R32_UINT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_RENDER_TARGET);
    auto depth_facts=facts(depth,202,DXGI_FORMAT_D32_FLOAT,D3D11_BIND_DEPTH_STENCIL);
    Runtime::ApplicationResourceState initial;initial.omitted_slots_unbound=true;
    Runtime::ApplicationImageBinding input;input.stage=L'p';input.type=Target::SHADER_RESOURCE;input.source=source_facts;
    input.view_known=true;input.format=DXGI_FORMAT_R32_UINT;input.view_type=VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    input.range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,1,1};initial.images.push_back(input);
    input.stage=L'v';input.slot=2;initial.images.push_back(input);
    input.stage=0;input.slot=0;input.type=Target::RENDER_TARGET;input.range.baseArrayLayer=0;initial.images.push_back(input);
    input.type=Target::DEPTH_STENCIL_TARGET;input.source=depth_facts;input.format=DXGI_FORMAT_D32_FLOAT;
    input.range.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;initial.images.push_back(input);input={};
    Runtime::ApplicationBufferBinding buffer;buffer.stage=L'p';buffer.type=Target::CONSTANT_BUFFER;buffer.slot=1;buffer.source_known=true;
    buffer.description={16,D3D11_USAGE_DEFAULT,D3D11_BIND_CONSTANT_BUFFER,0,0,0};
    auto& native=buffer.resource;native.bound=true;native.identity=203;native.dimension=D3D11_RESOURCE_DIMENSION_BUFFER;
    native.buffer=reinterpret_cast<uint64_t>(constant->buffer());native.size=native.range=16;native.usage=constant->usage();
    native.memory=reinterpret_cast<uint64_t>(constant->memory());native.memory_size=constant->memory_size();native.keep=constant;
    buffer.identity=std::make_shared<frontend::SourceResourceIdentity>();initial.buffers.push_back(buffer);buffer={};
    auto reject=[&](auto change,const char* name){auto invalid=initial;change(invalid);refuses([&]{resources.begin_prepared(invalid);},name);
        check(!resources.scope_active() && !resources.pending(),"Rejected image snapshot publishes no partial scope");};
    reject([](auto& s){s.images[0].view_known=false;},"Unknown original image view facts cannot be inferred from Vulkan");
    reject([](auto& s){auto f=std::make_shared<Runtime::ApplicationImageSource>(*s.images[0].source);f->source_known=false;s.images[0].source=f;},
        "Unknown image source description refuses");
    reject([](auto& s){auto f=std::make_shared<Runtime::ApplicationImageSource>(*s.images[0].source);f->allocation.uniform_layout=false;s.images[0].source=f;},
        "View-local image layout is not a complete observation");
    reject([](auto& s){auto f=std::make_shared<Runtime::ApplicationImageSource>(*s.images[0].source);f->allocation.owner.reset();s.images[0].source=f;},
        "Application image lifetime must be retainable");
    reject([](auto& s){s.images[0].range.baseArrayLayer=2;},"Imported source view cannot extend outside the allocation");
    reject([](auto& s){s.images[0].format=DXGI_FORMAT_R32_FLOAT;},"A typed original allocation cannot acquire an unrelated view format");
    reject([](auto& s){auto f=std::make_shared<Runtime::ApplicationImageSource>(*s.images[0].source);f->resource_id=203;s.images[0].source=f;},
        "One source identity cannot describe a buffer and an image in the same snapshot");
    reject([](auto& s){s.images[1]=s.images[0];},"Duplicate image source slots refuse");
    reject([](auto& s){s.images[0].type=Target::CONSTANT_BUFFER;},"Image cannot enter an original buffer-only slot");
    reject([](auto& s){s.images[2].range.baseArrayLayer=1;},"Same image subresource cannot be an input and an output");
    reject([](auto& s){s.images[3].flags=D3D11_DSV_READ_ONLY_DEPTH;},"Read-only DSV flags require their explicit hazard adapter");
    reject([](auto& s){s.images[2].range.layerCount=2;s.images.erase(s.images.begin(),s.images.begin()+2);},
        "Original attachment array sizes must agree");
    reject([](auto& s){s.images[1].type=Target::UNORDERED_ACCESS_VIEW;s.images[1].stage=L'p';s.images[1].slot=0;
        s.images[1].range.baseArrayLayer=1;s.images[0].source.reset();},"Pixel UAV cannot overlap the original RTV register range");
    reject([](auto& s){auto f=std::make_shared<Runtime::ApplicationImageSource>(*s.images[1].source);
        auto id=std::make_shared<frontend::SourceResourceIdentity>(*f->identity);id->hash=1;f->identity=id;s.images[1].source=f;},
        "Application image aliases cannot publish conflicting original hashes");
    reject([](auto& s){auto f=std::make_shared<Runtime::ApplicationImageSource>(*s.images[1].source);
        f->allocation.layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;s.images[1].source=f;},"Application image aliases cannot assert different current layouts");
    resources.begin_prepared(initial);
    auto imported=resources.binding(L'p',Target::SHADER_RESOURCE,0);
    check(imported.image_view==resources.binding(L'v',Target::SHADER_RESOURCE,2).image_view,
        "Identical original views reuse one retained weak-cache view");
    check(imported.image==resources.binding(0,Target::RENDER_TARGET,0).image && resources.binding(L'p',Target::CONSTANT_BUFFER,1).buffer,
        "A complete context accepts buffer bindings and disjoint image subresources together");
    check(!resources.finish_prepared(),"Observing application bindings emits no GPU commands");resources.accept_prepared();
    reject([](auto& s){s.buffers[0].resource.identity=201;},"An existing image source identity cannot be recycled as a buffer");
    reject([](auto& s){for(auto& b:s.images)if(b.source->resource_id==201){auto f=std::make_shared<Runtime::ApplicationImageSource>(*b.source);f->resource_id=204;b.source=f;}},
        "A retained image handle cannot change source identity");
    reject([](auto& s){auto f=std::make_shared<Runtime::ApplicationImageSource>(*s.images[0].source);
        f->description.texture.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;s.images[0].source=f;},"Retained source descriptions cannot change across snapshots");
    auto run=[&](const auto& state){resources.begin_prepared(state);check(ini->present({},10,0,why) && !resources.failed(),
        "Original INI references, image copies and compute execute from observed application sources");};
    run(initial);
    std::weak_ptr<gpu::ImageResource> cancelled_named=resources.binding(L'p',Target::SHADER_RESOURCE,5).image;
    std::weak_ptr<gpu::ImageResource> cancelled_command=resources.binding(L'p',Target::SHADER_RESOURCE,6).image;
    auto cancelled=resources.finish_prepared();resources.discard();cancelled.reset();
    check(cancelled_named.expired(),"Cancellation releases a newly allocated named image copy pool entry");
    check(cancelled_command.expired(),"Cancellation releases a newly allocated instruction image copy pool entry");
    check(resources.binding(L'p',Target::SHADER_RESOURCE,0).image==imported.image && !resources.binding(L'c',Target::UNORDERED_ACCESS_VIEW,0),
        "Cancellation restores the current observed application image context");
    run(initial);check(ini->status().variables.at("$filter")==42,"Original image filters use the supplied source hash");
    auto output=resources.binding(L'c',Target::UNORDERED_ACCESS_VIEW,0).buffer;
    auto image_copy=resources.binding(L'p',Target::SHADER_RESOURCE,5).image;
    auto borrow=[&](const gpu::Buffer& b){return resources.commands().import_buffer(b->buffer(),b->size(),b->usage(),b->memory(),b->memory_offset(),b->memory_size(),b);};
    resources.commands().copy(output,0,borrow(result),0,16);
    VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,2};region.imageExtent={4,1,1};
    resources.commands().copy(image_copy,borrow(copied),region);
    auto packet=resources.finish_prepared();resources.accept_prepared();
    Runtime::ApplicationResourceState observed;observed.omitted_slots_unbound=true;observed.observations.push_back(source_facts);
    run(observed);auto second=resources.finish_prepared();resources.discard();second.reset();
    observed.observations.clear();resources.begin_prepared(observed);
    ini->present({},10,0,why);check(resources.failed(),"Saved original image reference refuses GPU use without a fresh layout observation");
    refuses([&]{resources.finish_prepared();},"Missing saved-image observation cannot seal a partial packet");resources.discard();
    observed.observations.push_back(source_facts);run(observed);auto retry=resources.finish_prepared();resources.discard();retry.reset();
    check(app.readback(d.queue,result,0,16)==std::vector<uint8_t>(16,0),"Original image preparation never executes the pending compute early");
    const auto handle=source->image();const auto range=source->full_range();const auto& vk=d.resource.dispatch;
    auto produce=[&](uint32_t a,uint32_t b){return [&,a,b](VkCommandBuffer cb){
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=handle;barrier.subresourceRange=range;
        barrier.oldLayout=barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.srcAccessMask=barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        vk.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        for(uint32_t layer=0;layer<2;++layer){auto part=range;part.baseArrayLayer=layer;part.layerCount=1;VkClearColorValue value{};value.uint32[0]=layer?b:a;
            vk.CmdClearColorImage(cb,handle,VK_IMAGE_LAYOUT_GENERAL,&value,1,&part);}
    };};
    std::weak_ptr<gpu::ImageResource> lifetime=source;
    imported={};initial={};observed={};source_facts.reset();depth_facts.reset();image_copy.reset();output.reset();ini.reset();engine.reset();source.reset();depth.reset();
    check(!lifetime.expired(),"Original image packet retains application storage beyond registry destruction");
    auto values=[&](const gpu::Buffer& buffer){const auto bytes=app.readback(d.queue,buffer,0,buffer->size());std::vector<uint32_t> out(bytes.size()/4);
        memcpy(out.data(),bytes.data(),bytes.size());return out;};
    record_prepared_packets(d,{packet},produce(5,7));check(values(result)==std::vector<uint32_t>(4,14),"Original reference preserves its selected array view");
    check(values(copied)==std::vector<uint32_t>({5,5,5,5,7,7,7,7}),"Original copy reads the whole allocation rather than the selected view");
    record_prepared_packets(d,{packet},produce(9,11));check(values(result)==std::vector<uint32_t>(4,22),"Original image compute reads each producer at the insertion point");
    check(values(copied)==std::vector<uint32_t>({9,9,9,9,11,11,11,11}),"Replayed original image copy consumes current application pixels");
    packet.reset();check(lifetime.expired(),"Imported image owners retire with the executable original packet");
}
