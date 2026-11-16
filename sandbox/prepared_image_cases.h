// Record only after explicit admission, on this fixture's serialized queue.
static void record_prepared_packets(Device& d,const std::vector<std::shared_ptr<const gpu::PreparedResourceCommands>>& packets,
    const std::function<void(VkCommandBuffer)>& before={},const std::function<void(VkCommandBuffer)>& after={}){
    const auto& vk=d.resource.dispatch;VkCommandPool pool=VK_NULL_HANDLE;VkCommandBuffer cb=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=d.resource.queue_family;
    vkcheck(vk.CreateCommandPool(d.resource.device,&pi,nullptr,&pool),"Create prepared image pool");
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.commandBufferCount=1;
    vkcheck(vk.AllocateCommandBuffers(d.resource.device,&ai,&cb),"Allocate prepared image primary");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkcheck(vk.BeginCommandBuffer(cb,&bi),"Begin prepared image primary");
    if(before)before(cb);
    for(const auto& packet:packets)if(packet)packet->record(cb);
    if(after)after(cb);
    vkcheck(vk.EndCommandBuffer(cb),"End prepared image primary");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};vkcheck(vk.CreateFence(d.resource.device,&fi,nullptr,&fence),"Create prepared image fence");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cb;
    vkcheck(vk.QueueSubmit(d.queue,1,&si,fence),"Submit prepared image primary");
    vkcheck(vk.WaitForFences(d.resource.device,1,&fence,VK_TRUE,10000000000ull),"Wait prepared image primary");
    vk.DestroyCommandPool(d.resource.device,pool,nullptr);vk.DestroyFence(d.resource.device,fence,nullptr);
}
static void prepared_image_cases(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);
    write(root/"images.hlsl",R"hlsl(Texture2D<uint> Input : register(t0);
RWTexture2D<uint> Output : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { Output[id.xy] = Input.Load(int3(id.xy,0)) * 3; }
)hlsl");
    gpu::HlslCompiler compiler(L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
    gpu::HlslRequest request;request.source=root/"images.hlsl";request.working_directory=root;
    gpu::HlslProgram program;std::string why;check(compiler.compile(request,program,why),"Compile prepared image compute");
    uint32_t input_binding=UINT32_MAX,output_binding=UINT32_MAX;
    for(const auto& b:program.bindings){if(b.source_type=='t')input_binding=b.binding;if(b.source_type=='u')output_binding=b.binding;}
    check(input_binding!=UINT32_MAX && output_binding!=UINT32_MAX,"Reflect prepared image source registers");
    gpu::ResourceCommands app(d.resource);auto output=app.create(16,0),depth_output=app.create(16,0),resolve_output=app.create(16,0);
    auto commands=std::make_unique<gpu::ResourceCommands>(d.resource);
    auto shaders=std::make_unique<gpu::ShaderCommands>(*commands,shader_host(d));auto pipeline=shaders->compile(program);
    gpu::ImageDescription description;description.format=VK_FORMAT_R32_UINT;description.extent={4,1,1};description.layers=2;
    description.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT;
    auto input=commands->create_image(description),result=commands->create_image(description);
    VkBufferImageCopy upload{};upload.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,1,1};upload.imageExtent={4,1,1};
    uint32_t values[]={1,2,3,4};
    commands->begin_prepared();commands->write(input,upload,values,sizeof(values));auto cancelled=commands->finish_prepared();
    check(cancelled && cancelled->image_states().size()==1 && cancelled->image_states()[0].initial==VK_IMAGE_LAYOUT_UNDEFINED,
        "Fresh image preparation exposes its undefined initial layout");
    refuses([&]{commands->begin_prepared();},"Sealed image work blocks a later scope before admission");
    commands->discard();refuses([&]{cancelled->record(VK_NULL_HANDLE);},"Cancelled sealed image work cannot record");cancelled.reset();
    auto borrow=[&](const gpu::Buffer& b){return commands->import_buffer(b->buffer(),b->size(),b->usage(),b->memory(),b->memory_offset(),b->memory_size(),b);};
    commands->begin_prepared();commands->write(input,upload,values,sizeof(values));
    auto input_view=commands->view(input,VK_IMAGE_VIEW_TYPE_2D,VK_FORMAT_R32_UINT,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,1,1},VK_IMAGE_USAGE_SAMPLED_BIT);
    auto result_view=commands->view(result,VK_IMAGE_VIEW_TYPE_2D,VK_FORMAT_R32_UINT,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},VK_IMAGE_USAGE_STORAGE_BIT);
    gpu::ShaderDescriptor in;in.binding=input_binding;in.image=input_view;
    gpu::ShaderDescriptor out;out.binding=output_binding;out.image=result_view;
    shaders->dispatch(pipeline,{in,out},{4,1,1});
    VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={4,1,1};
    commands->copy(result,input,copy);auto download=upload;download.imageSubresource.baseArrayLayer=0;
    commands->copy(input,borrow(output),download);
    auto packet=commands->finish_prepared();
    check(packet->image_states().size()==2 && std::all_of(packet->image_states().begin(),packet->image_states().end(),[](const auto& s){
        return s.initial==VK_IMAGE_LAYOUT_UNDEFINED && s.final==VK_IMAGE_LAYOUT_GENERAL && s.image->memory() && s.image->memory_size();}),
        "Cancelled initialization did not publish layout state and retry retains allocation facts");
    const auto& accesses=packet->image_accesses();
    check(std::any_of(accesses.begin(),accesses.end(),[&](const auto& a){return a.image==input && !a.transition &&
        a.stages==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT && a.access==VK_ACCESS_SHADER_READ_BIT && a.range.baseArrayLayer==1 && a.range.layerCount==1;}),
        "Sampled image access preserves the exact descriptor subresource");
    check(std::any_of(accesses.begin(),accesses.end(),[&](const auto& a){return a.image==result && !a.transition &&
        a.stages==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT && (a.access&VK_ACCESS_SHADER_WRITE_BIT) && a.range.baseArrayLayer==0;}),
        "Storage image access conservatively includes shader writes");
    check(std::all_of(accesses.begin(),accesses.end(),[&](const auto& a){return a.command<packet->size() && (!a.transition || a.range.layerCount==2);}),
        "Image transitions expose full-range hazards at their command positions");
    commands->accept_prepared();std::fill(std::begin(values),std::end(values),99);
    commands->begin_prepared();commands->copy(input,borrow(output),download);auto following=commands->finish_prepared();
    check(following->image_states().size()==1 && following->image_states()[0].initial==VK_IMAGE_LAYOUT_GENERAL,
        "Only admission publishes the layout required by the following ordered packet");commands->accept_prepared();
    // Exercise prepared depth clear and multisample resolve through independent bytes.
    auto depth_desc=description;depth_desc.layers=1;depth_desc.format=VK_FORMAT_D32_SFLOAT;depth_desc.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    auto depth=commands->create_image(depth_desc);auto ms_desc=depth_desc;ms_desc.format=VK_FORMAT_R8G8B8A8_UNORM;
    ms_desc.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;ms_desc.samples=VK_SAMPLE_COUNT_4_BIT;
    auto multisample=commands->create_image(ms_desc);ms_desc.samples=VK_SAMPLE_COUNT_1_BIT;auto resolved=commands->create_image(ms_desc);
    commands->begin_prepared();commands->clear(depth,depth->full_range(),VkClearDepthStencilValue{0.5f,0});
    auto depth_region=download;depth_region.imageSubresource.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;commands->copy(depth,borrow(depth_output),depth_region);
    VkClearColorValue color{};color.float32[0]=color.float32[3]=1;commands->clear(multisample,multisample->full_range(),color);
    VkImageResolve resolve{};resolve.srcSubresource=resolve.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};resolve.extent={4,1,1};
    commands->resolve(multisample,resolved,resolve);commands->copy(resolved,borrow(resolve_output),download);
    auto extra=commands->finish_prepared();commands->accept_prepared();
    commands->begin_prepared();
    refuses([&]{for(unsigned i=0;i<2050;++i)commands->clear(input,input->full_range(),color);},"Image command budget refuses before losing final layout capacity");
    auto bounded=commands->finish_prepared();
    check(bounded && bounded->size()<=4096 && bounded->image_states().size()==1 &&
        bounded->image_accesses().back().transition && bounded->image_accesses().back().layout==VK_IMAGE_LAYOUT_GENERAL,
        "A full image packet still seals with its final GENERAL transition");commands->discard();bounded.reset();
    // An old borrowed-image wrapper lacks allocation ownership, even in GENERAL.
    auto external_desc=description;external_desc.layers=1;external_desc.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
    auto borrowed=commands->import_image(input->image(),external_desc,VK_IMAGE_LAYOUT_GENERAL);
    commands->begin_prepared();refuses([&]{commands->copy(borrowed,borrow(output),download);},"Prepared image imports require complete host ownership and layout proof");
    refuses([&]{commands->readback(d.queue,input,download);},"Prepared image readback never waits or submits");commands->discard();borrowed.reset();
    std::weak_ptr<gpu::ImageResource> retained=input;
    in.image.reset();out.image.reset();input_view.reset();result_view.reset();input.reset();result.reset();depth.reset();multisample.reset();resolved.reset();
    pipeline.reset();shaders.reset();commands.reset();check(!retained.expired(),"Prepared image owners survive their preparing engine");
    record_prepared_packets(d,{packet,following,extra});
    auto actual=app.readback(d.queue,output,0,16);const uint32_t expected[]={3,6,9,12};
    check(actual.size()==sizeof(expected) && !memcmp(actual.data(),expected,sizeof(expected)),"Ordered upload, image compute, image copy and later packet preserve frozen CPU upload bytes");
    actual=app.readback(d.queue,depth_output,0,16);const float depths[]={0.5f,0.5f,0.5f,0.5f};
    check(!memcmp(actual.data(),depths,sizeof(depths)),"Prepared depth clear produces independent readback values");
    actual=app.readback(d.queue,resolve_output,0,16);bool red=true;for(size_t i=0;i<4;++i)red&=actual[i*4]==255 && actual[i*4+1]==0 && actual[i*4+2]==0 && actual[i*4+3]==255;
    check(red,"Prepared multisample clear and resolve produce independent pixels");
    packet.reset();following.reset();extra.reset();check(retained.expired(),"Image ownership retires after completed recordings release their packets");
}
