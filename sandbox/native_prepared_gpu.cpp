// Real GPU continuation across two ordered buffer packets, without a window.
#include "variant_gpu_fixture.h"
#include "../layer/src/native_targets.h"
#include "../frontend/gpu/shader_commands.h"
#include <cmath>

static efmi::gpu::HlslProgram compile(const std::filesystem::path& root,char stage,const char* source)
{
    const auto path=root/(std::string(1,stage)+".hlsl");std::ofstream(path)<<source;
    efmi::gpu::HlslCompiler compiler(L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");
    efmi::gpu::HlslRequest request;request.source=path;request.working_directory=root;request.stage=stage;
    efmi::gpu::HlslProgram program;std::string why;
    if(!compiler.compile(request,program,why))throw std::runtime_error(why);return program;
}

static void run(bool classic,unsigned mode,const efmi::gpu::HlslProgram& vertex,
                const efmi::gpu::HlslProgram& fragment,const efmi::gpu::HlslProgram& compute)
{
    auto gpu=std::make_shared<Gpu>();auto& g=*gpu;g.init(false,1,0,0,true,true);
    const bool attached=mode!=2,depth=mode==1 || mode==3,stencil=mode==3;
    const auto format=stencil?VK_FORMAT_D32_SFLOAT_S8_UINT:depth?VK_FORMAT_D32_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM;
    const VkImageAspectFlags aspects=depth?VK_IMAGE_ASPECT_DEPTH_BIT|(stencil?VK_IMAGE_ASPECT_STENCIL_BIT:0u):VK_IMAGE_ASPECT_COLOR_BIT;
    const auto attachment_layout=depth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    const VkImageUsageFlags image_usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    Image image{};Buffer pixels{},stencils{};
    if(attached){image=g.image(format,image_usage,aspects);pixels=g.buffer(64*64*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);}
    if(stencil)stencils=g.buffer(64*64,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    constexpr auto buffer_usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const auto output=g.buffer(256,buffer_usage);
    const auto image_output=g.buffer(4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    // All host metadata describes these actual Vulkan objects and their allocation spans.
    Device device;device.handle=g.device;device.physical_properties=g.properties;
    device.application_queue_count=1;device.material_family=g.family;device.material_queues={g.queue};g_cfg.track_resources=true;
#define X(name) device.d.name=reinterpret_cast<PFN_vk##name>(vkGetDeviceProcAddr(g.device,"vk" #name));
    X(CmdPipelineBarrier) X(CmdBeginRendering) X(CmdEndRendering) X(CmdBeginRenderPass) X(CmdEndRenderPass)
    X(CreateRenderPass) X(CreateRenderPass2) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer)
    X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CreatePipelineLayout) X(DestroyPipelineLayout)
#undef X
    TargetView view;
    if(attached){
        view.known=true;view.handle=image.view;view.view.id=1;view.view.image=image.handle;view.view.image_id=view.image.id=1;
        view.view.type=VK_IMAGE_VIEW_TYPE_2D;view.view.format=view.image.format=format;view.view.range={aspects,0,1,0,1};
        view.view.lifetime=std::make_shared<ImageViewLifetime>();view.view.lifetime->dependencies.push_back(gpu);
        view.image.type=VK_IMAGE_TYPE_2D;view.image.extent={64,64,1};view.image.levels=view.image.layers=1;
        view.image.samples=VK_SAMPLE_COUNT_1_BIT;view.image.usage=image_usage;view.image.capture_safe=true;view.image.mem=image.memory;
        VkMemoryRequirements req{};vkGetImageMemoryRequirements(g.device,image.handle,&req);view.image.mem_size=req.size;
        device.image_views[image.view]=view.view;device.images[image.handle]=view.image;
    }
    VkAttachmentDescription desc{};desc.format=format;desc.samples=VK_SAMPLE_COUNT_1_BIT;
    desc.loadOp=desc.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;desc.storeOp=desc.stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;
    desc.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;desc.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ref{0,attachment_layout};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    if(attached){if(depth)sub.pDepthStencilAttachment=&ref;else{sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;}}
    VkSubpassDependency deps[2]{};
    deps[0]={VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
        VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,0};
    deps[1]={0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,0};
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=attached?1:0;rp.pAttachments=&desc;
    rp.subpassCount=1;rp.pSubpasses=&sub;rp.dependencyCount=2;rp.pDependencies=deps;
    std::shared_ptr<RenderPassRecipe> pass;
    if(classic){
        VK(vkCreateRenderPass(g.device,&rp,nullptr,&g.pass));pass=std::make_shared<RenderPassRecipe>(RenderPassRecipe::capture(rp));
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=g.pass;fb.width=fb.height=64;fb.layers=1;
        fb.attachmentCount=attached?1:0;fb.pAttachments=&image.view;VK(vkCreateFramebuffer(g.device,&fb,nullptr,&g.framebuffer));
    }
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};VK(vkCreatePipelineLayout(g.device,&layout,nullptr,&g.layout));
    RecipeFixture fixture;fixture.vi.vertexBindingDescriptionCount=fixture.vi.vertexAttributeDescriptionCount=0;
    fixture.gp.layout=g.layout;fixture.gp.renderPass=g.pass;fixture.ba.blendEnable=VK_FALSE;
    fixture.cb.attachmentCount=attached && !depth?1:0;fixture.ds.depthTestEnable=fixture.ds.depthWriteEnable=depth;
    fixture.ds.depthCompareOp=VK_COMPARE_OP_ALWAYS;fixture.ds.stencilTestEnable=stencil;
    fixture.ds.front={VK_STENCIL_OP_KEEP,VK_STENCIL_OP_REPLACE,VK_STENCIL_OP_KEEP,VK_COMPARE_OP_ALWAYS,255,255,9};fixture.ds.back=fixture.ds.front;
    fixture.shaders.clear();
    for(unsigned i=0;i<2;++i){
        const auto& program=i?fragment:vertex;auto metadata=std::make_shared<ShaderInfo>();metadata->id=i+1;
        metadata->code=program.spirv;metadata->size=uint32_t(program.spirv.size()*4);
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=metadata->size;sm.pCode=metadata->code.data();
        VK(vkCreateShaderModule(g.device,&sm,nullptr,&fixture.stages[i].module));fixture.shaders.push_back({uint32_t(program.stage),"main",metadata});
    }
    // A fragment shader without a color output avoids irrelevant depth-only output warnings.
    fixture.gp.stageCount=attached && !depth?2:1;if(fixture.gp.stageCount==1)fixture.shaders.resize(1);
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount=fixture.cb.attachmentCount;rendering.pColorAttachmentFormats=&format;
    rendering.depthAttachmentFormat=depth?format:VK_FORMAT_UNDEFINED;rendering.stencilAttachmentFormat=stencil?format:VK_FORMAT_UNDEFINED;
    if(!classic)fixture.gp.pNext=&rendering;
    VK(vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&fixture.gp,nullptr,&g.pipeline));
    for(const auto& stage:fixture.stages)vkDestroyShaderModule(g.device,stage.module,nullptr);
    auto recipe=std::make_shared<GraphicsRecipe>(GraphicsRecipe::capture(fixture.gp,fixture.shaders,pass));
    device.pipes[g.pipeline].recipe=recipe;

    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=g.family;
    VK(vkCreateCommandPool(g.device,&pool,nullptr,&g.pool));CmdBuf cb;
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};alloc.commandPool=g.pool;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;alloc.commandBufferCount=1;
    VK(vkAllocateCommandBuffers(g.device,&alloc,&cb.handle));
    RenderTargets targets;targets.known=true;targets.dynamic=!classic;targets.layers=1;targets.area.extent={64,64};
    VkClearValue clear{};if(depth)clear.depthStencil={0.75f,23};else clear.color={{0.25f,0.5f,0.75f,1.f}};
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};attachment.imageView=image.view;attachment.imageLayout=attachment_layout;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.clearValue=clear;
    if(classic){
        auto fb=std::make_shared<FramebufferInfo>();fb->known=true;fb->width=fb->height=64;fb->layers=1;fb->attachment_count=attached?1:0;
        if(attached)fb->views.push_back(view);targets.recipe=cb.pass.recipe=pass;targets.framebuffer=cb.pass.info=fb;
        cb.pass.render_pass=g.pass;cb.pass.framebuffer=g.framebuffer;
    }else if(attached){
        targets.views={view,{}};if(depth){targets.has_depth=true;targets.depth=attachment;
            if(stencil){targets.has_stencil=true;targets.stencil=attachment;targets.views.push_back(view);targets.views.push_back({});}}
        else targets.dynamic_colors.push_back(attachment);
    }
    cb.target_contexts={targets};cb.active_targets=1;cb.in_rp=true;cb.gfx_pipeline=cb.gfx_bound=g.pipeline;
    DrawRec draw;draw.pipeline=draw.bound=g.pipeline;draw.targets=1;

    efmi::gpu::ResourceDevice resource;resource.device=g.device;resource.memory=g.memory;resource.queue_family=g.family;
    resource.physical=g.physical;resource.queue_flags=g.queue_flags;resource.api_version=VK_API_VERSION_1_3;
    resource.GetImageFormatProperties=vkGetPhysicalDeviceImageFormatProperties;resource.GetFormatProperties=vkGetPhysicalDeviceFormatProperties;
#define X(name) resource.dispatch.name=reinterpret_cast<PFN_vk##name>(vkGetDeviceProcAddr(g.device,"vk" #name));
    EFMI_RESOURCE_COMMAND_FUNCS(X)
#undef X
    auto commands=std::make_unique<efmi::gpu::ResourceCommands>(resource);
    efmi::gpu::ShaderHost host;host.get_device_proc=vkGetDeviceProcAddr;host.properties=g.properties;
    host.validator=L"F:/vulkan/Vulkan-ValidationLayers/external/Release/64/SPIRV-Tools/build/source/SPIRV-Tools-shared.dll";
    auto shaders=std::make_unique<efmi::gpu::ShaderCommands>(*commands,host);auto compute_pipeline=shaders->compile(compute);
    commands->begin_prepared();VkMemoryRequirements req{};vkGetBufferMemoryRequirements(g.device,output.handle,&req);
    auto imported=commands->import_buffer(output.handle,output.size,buffer_usage,output.memory,0,req.size,gpu);
    commands->fill(imported,0,4,7);
    efmi::gpu::ImageDescription image_desc;image_desc.format=VK_FORMAT_R32_UINT;
    image_desc.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto private_image=commands->create_image(image_desc);std::weak_ptr<efmi::gpu::ImageResource> image_lifetime=private_image;
    VkClearColorValue image_value{};image_value.uint32[0]=37;commands->clear(private_image,private_image->full_range(),image_value);
    auto first=commands->cut_prepared();
    vkGetBufferMemoryRequirements(g.device,image_output.handle,&req);
    auto image_destination=commands->import_buffer(image_output.handle,image_output.size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        image_output.memory,0,req.size,gpu);
    VkBufferImageCopy image_copy{};image_copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};image_copy.imageExtent={1,1,1};
    commands->copy(private_image,image_destination,image_copy);
    efmi::gpu::ShaderDescriptor binding;binding.binding=compute.bindings.at(0).binding;binding.buffer=imported;binding.size=4;
    shaders->dispatch(compute_pipeline,{binding},{1,1,1});auto second=commands->finish_prepared();
    efmi::assets::FilePlan plan;plan.items.resize(2);
    for(auto& item:plan.items)item.operation=efmi::assets::FileOperation::PreparedWork;
    plan.items[0].prepared_work=first;plan.items[1].prepared_work=second;
    NativeTargetCache cache;NativeTargetPlan prepared;std::string why;
    auto prepare=[&]{prepared={};return prepare_native_targets(&device,&cb,draw,plan,prepared,why,&cache);};
    if(attached){
        auto aliased=targets;
        auto alias=[&](TargetView& v){v.image.mem=private_image->memory();v.image.mem_off=private_image->memory_offset();v.image.mem_size=private_image->memory_size();};
        if(classic){auto fb=std::make_shared<FramebufferInfo>(*targets.framebuffer);alias(fb->views[0]);aliased.framebuffer=fb;}
        else {alias(aliased.views[0]);if(stencil)alias(aliased.views[2]);}
        cb.target_contexts[0]=aliased;
        check(!prepare() && why.find("attachment memory")!=std::string::npos,"private image work refuses physical attachment alias before recording");
        cb.target_contexts[0]=targets;
    }
    if(!prepare())throw std::runtime_error(why);
    commands->accept_prepared();
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK(vkBeginCommandBuffer(cb.handle,&begin));
    if(classic){VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};info.renderPass=g.pass;info.framebuffer=g.framebuffer;
        info.renderArea=targets.area;info.clearValueCount=attached?1:0;info.pClearValues=&clear;vkCmdBeginRenderPass(cb.handle,&info,VK_SUBPASS_CONTENTS_INLINE);
    }else{
        if(attached){VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image.handle;barrier.subresourceRange=view.view.range;
            barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=attachment_layout;
            barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(cb.handle,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,0,0,nullptr,0,nullptr,1,&barrier);}
        VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};info.renderArea=targets.area;info.layerCount=1;
        info.colorAttachmentCount=fixture.cb.attachmentCount;info.pColorAttachments=&attachment;
        info.pDepthAttachment=depth?&attachment:nullptr;info.pStencilAttachment=stencil?&attachment:nullptr;device.d.CmdBeginRendering(cb.handle,&info);
    }
    vkCmdBindPipeline(cb.handle,VK_PIPELINE_BIND_POINT_GRAPHICS,g.pipeline);
    VkViewport viewport{0,0,64,64,0,1};vkCmdSetViewport(cb.handle,0,1,&viewport);
    for(unsigned i=0;i<2;++i){
        record_native_target(&device,&cb,prepared,i);
        VkRect2D rect{{int32_t(8+16*i),int32_t(8+16*i)},{8,8}};vkCmdSetScissor(cb.handle,0,1,&rect);
        vkCmdDraw(cb.handle,3,1,0,0);
    }
    if(classic)vkCmdEndRenderPass(cb.handle);else device.d.CmdEndRendering(cb.handle);
    native_targets_end(&device,&cb);cb.in_rp=false;
    if(attached){
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image.handle;barrier.subresourceRange=view.view.range;
        barrier.oldLayout=classic?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:attachment_layout;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cb.handle,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{};copy.imageSubresource={VkImageAspectFlags(depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,0,1};copy.imageExtent={64,64,1};
        vkCmdCopyImageToBuffer(cb.handle,image.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,pixels.handle,1,&copy);
        if(stencil){copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;vkCmdCopyImageToBuffer(cb.handle,image.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,stencils.handle,1,&copy);}
    }
    VkMemoryBarrier visible{VK_STRUCTURE_TYPE_MEMORY_BARRIER};visible.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;visible.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb.handle,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&visible,0,nullptr,0,nullptr);
    VK(vkEndCommandBuffer(cb.handle));check(prepared.owner->seal(),"GPU recording seals its retained work owner");
    // Destroy preparation before submission; only the executable recording retains its packets.
    prepared={};plan={};first.reset();second.reset();imported.reset();image_destination.reset();private_image.reset();binding={};compute_pipeline.reset();shaders.reset();commands.reset();
    check(!image_lifetime.expired(),"executable recording retains private images after registry preparation retires");
    VK(g.submit(cb.handle));VK(vkQueueWaitIdle(g.queue));
    check(*static_cast<const uint32_t*>(image_output.mapped)==37,"GPU image clear and later copy keep the private GENERAL layout chain");
    check(*static_cast<const uint32_t*>(output.mapped)==13,"GPU buffer result proves ordered fill and compute insertion");
    if(attached){
        bool untouched=true,drawn=true,stencil_ok=true;
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
            const bool changed=(x>=8 && x<16 && y>=8 && y<16)||(x>=24 && x<32 && y>=24 && y<32);
            const auto index=y*64+x;
            if(depth){const auto actual=static_cast<const float*>(pixels.mapped)[index];
                (changed?drawn:untouched)&=std::abs(actual-(changed?0.125f:0.75f))<0.00001f;
            }else{const auto* actual=static_cast<const uint8_t*>(pixels.mapped)+4*index;
                (changed?drawn:untouched)&=changed?(actual[0]==255 && !actual[1] && !actual[2] && actual[3]==255):
                    ((actual[0]==63 || actual[0]==64) && (actual[1]==127 || actual[1]==128) && (actual[2]==191 || actual[2]==192) && actual[3]==255);}
            if(stencil)stencil_ok&=static_cast<const uint8_t*>(stencils.mapped)[index]==(changed?9:23);
        }
        if(!untouched && !depth){const auto* p=static_cast<const uint8_t*>(pixels.mapped);
            printf("untouched pixel 0: %u %u %u %u\n",p[0],p[1],p[2],p[3]);}
        check(untouched,"GPU attachment contents survive both LOAD continuations");
        check(drawn,"GPU draws after both inserted packets retain their graphics state");
        if(stencil)check(stencil_ok,"GPU stencil clear and both replacement regions survive pass splits");
    }
    VK(vkResetCommandPool(g.device,g.pool,0));for(auto& owner:cb.owners)owner->reset_after_completion();
    check(image_lifetime.expired(),"legal recording reset retires private image allocations");
    printf("pass insertion: %s mode=%u complete\n",classic?"classic":"dynamic",mode);
}

int main(int argc,char** argv)
{
    try{
        if(argc!=2)throw std::runtime_error("one fixture directory is required");
        const auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
        const auto vertex=compile(root,'v',"float4 main(uint id:SV_VertexID):SV_Position { float2 p=float2((id<<1)&2,id&2); return float4(p*2-1,0.125,1); }");
        const auto fragment=compile(root,'p',"float4 main():SV_Target { return float4(1,0,0,1); }");
        const auto compute=compile(root,'c',"RWStructuredBuffer<uint> Output:register(u0); [numthreads(1,1,1)] void main(){ Output[0]+=6; }");
        for(bool classic:{false,true})for(unsigned mode=0;mode<4;++mode)run(classic,mode,vertex,fragment,compute);
    }catch(const std::exception& e){printf("EXCEPTION: %s\n",e.what());++failed;}
    check(!errors && !warnings,"zero core and synchronization validation errors or warnings");
    printf("native prepared GPU: %d passed, %d failed; validation errors=%u warnings=%u\n",passed,failed,errors.load(),warnings.load());
    return failed?1:0;
}
