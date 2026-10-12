// Included by asset_gpu.cpp. This exercises sampled-image binding ownership on
// an offscreen render pass, not the game's pipeline reconstruction or material.
#include "material_fixture.h"
#include "../frontend/gpu/fragment_images.h"
#include <array>
using efmi::gpu::ImageBindingMove;
using efmi::gpu::repoint_fragment_images;
using efmi::gpu::null_fragment_images;
using efmi::gpu::ImageBinding;

static void fragment_tests()
{
    auto original=material_fixture::shader(false);std::vector<uint32_t> out;std::string reason;
    const std::vector<ImageBindingMove> moves={{1,6,3,2}};
    check(repoint_fragment_images(original,moves,out,reason),"fragment 2D image moves to private descriptor");
    size_t changed=0;for(size_t i=0;i<out.size();++i)changed+=out[i]!=original[i];
    check(changed==2,"fragment rewrite changes only set/binding literals");
    auto retained=out;
    auto reject=[&](std::vector<uint32_t> code,std::vector<ImageBindingMove> requests,const char* label){
        check(!repoint_fragment_images(code,requests,out,reason) && out==retained && !reason.empty(),label);
    };
    reject(material_fixture::shader(true),moves,"vertex stage cannot enter the fragment rewriter");
    reject(original,{{1,6,1,6}},"identity request refuses");
    reject(original,{{1,6,3,2},{1,6,3,3}},"duplicate source refuses atomically");
    reject(original,{{1,6,3,2},{1,7,3,2}},"duplicate target refuses atomically");
    reject(original,{{1,99,3,2}},"missing requested image refuses atomically");
    reject(original,{},"empty image request refuses");
    auto altered=original;altered.back()=(2u<<16)|56;reject(altered,moves,"truncated final instruction refuses");
    for(unsigned field:{4u,5u,6u,7u}){
        altered=original;
        for(size_t i=5;i<altered.size();i+=altered[i]>>16)if((altered[i]&0xffff)==25){altered[i+field]=field==7?2:1;break;}
        reject(altered,moves,"depth/array/multisample/storage image requires another adapter");
    }
    altered=original;
    for(size_t i=5;i<altered.size();i+=altered[i]>>16)if((altered[i]&0xffff)==22){altered[i+2]=64;break;}
    reject(altered,moves,"non-float32 sampled type refuses");
    // Inserting a second descriptor binding decoration must not silently choose one.
    altered=original;
    for(size_t i=5;i<original.size();i+=original[i]>>16)if((original[i]&0xffff)==71 && original[i+2]==33){
        altered.insert(altered.begin()+i,original.begin()+i,original.begin()+i+4);break;
    }
    reject(altered,moves,"duplicate descriptor decoration refuses");
    altered=original;
    for(size_t i=5;i<altered.size();i+=altered[i]>>16)if((altered[i]&0xffff)==25){altered[i+4]=2;break;}
    check(repoint_fragment_images(altered,moves,out,reason),"unknown depth qualifier permits traced noncomparison use");
    retained=out;
    for(size_t i=5;i<altered.size();i+=altered[i]>>16)if((altered[i]&0xffff)==95){altered[i]=(altered[i]&0xffff0000)|90;break;}
    reject(altered,moves,"target image reaching depth comparison refuses");
    check(null_fragment_images(original,{{1,6}},out,reason),"explicit null fetch rewrites");
    auto probe=material_fixture::null_probe();
    check(null_fragment_images(probe,{{1,6}},out,reason),"null sample and typed size/mip queries rewrite together");
    retained=out;
    auto reject_null=[&](const std::vector<uint32_t>& code,std::vector<ImageBinding> bindings,const char* label){
        check(!null_fragment_images(code,bindings,out,reason) && out==retained && !reason.empty(),label);
    };
    reject_null(probe,{},"empty null operation refuses atomically");
    reject_null(probe,{{1,6},{1,6}},"duplicate null binding refuses atomically");
    reject_null(probe,{{1,6},{1,99}},"valid then missing null binding refuses whole operation");
    reject_null(probe,{{1,7}},"sampler cannot be mistaken for an image");
    reject_null(material_fixture::shader(true),{{1,6}},"null fragment operation refuses vertex shader");
    altered=probe;
    for(size_t i=5;i<altered.size();i+=altered[i]>>16)if((altered[i]&0xffff)==103){altered[i]=(altered[i]&0xffff0000)|105;break;}
    reject_null(altered,{{1,6}},"unsupported LOD query refuses instead of inventing its result");
    altered=probe;
    for(size_t i=5;i<altered.size();i+=altered[i]>>16)if((altered[i]&0xffff)==86){
        const auto pointer=altered[i+3],type=altered[i+1],id=altered[3]++;
        altered.insert(altered.begin()+i,{(6u<<16)|169,type,id,1,pointer,pointer});break;
    }
    reject_null(altered,{{1,6}},"opaque selection refuses without mixed-null alias guesses");
    check(null_fragment_images(probe,{{1,6},{1,8}},out,reason),"multiple null resources share one typed rewrite");
    std::ofstream source_file("null-source.spv",std::ios::binary),null_file("null-result.spv",std::ios::binary);
    source_file.write(reinterpret_cast<const char*>(probe.data()),probe.size()*4);
    if(!null_fragment_images(probe,{{1,6}},out,reason))throw std::runtime_error(reason);
    null_file.write(reinterpret_cast<const char*>(out.data()),out.size()*4);
}

static int rewrite_fragment_file(int argc,wchar_t** argv)
{
    try{
        const bool null_mode=!wcscmp(argv[1],L"--null-fragment");const int width=null_mode?2:4;
        if((argc-4)%width || argc>4+32*width)throw std::runtime_error("Invalid fragment move arguments");
        std::ifstream source(std::filesystem::path(argv[2]),std::ios::binary|std::ios::ate);
        if(!source || source.tellg()<20 || source.tellg()>(4<<20) || static_cast<size_t>(source.tellg())%4)
            throw std::runtime_error("Invalid fragment shader file size");
        std::vector<uint32_t> code(static_cast<size_t>(source.tellg())/4);source.seekg(0);
        source.read(reinterpret_cast<char*>(code.data()),code.size()*4);
        if(!source)throw std::runtime_error("Cannot read fragment shader");
        std::vector<ImageBindingMove> moves;std::vector<ImageBinding> nulls;
        for(int i=4;i<argc;i+=width){
            uint32_t values[4]{};
            for(int j=0;j<width;++j){wchar_t* end=nullptr;auto n=wcstoull(argv[i+j],&end,10);
                if(end==argv[i+j] || *end || n>UINT32_MAX)throw std::runtime_error("Invalid binding number");values[j]=static_cast<uint32_t>(n);}
            moves.push_back({values[0],values[1],values[2],values[3]});
            nulls.push_back({values[0],values[1]});
        }
        std::vector<uint32_t> rewritten;std::string reason;
        if(!(null_mode?null_fragment_images(code,nulls,rewritten,reason):repoint_fragment_images(code,moves,rewritten,reason)))
            throw std::runtime_error(reason);
        std::ofstream output(std::filesystem::path(argv[3]),std::ios::binary|std::ios::trunc);
        output.write(reinterpret_cast<const char*>(rewritten.data()),rewritten.size()*4);
        return output?0:2;
    }catch(const std::exception& e){fprintf(stderr,"Fragment rewrite failed: %s\n",e.what());return 1;}
}
struct MaterialObjects {
    VkDevice device;
    std::vector<VkDescriptorSetLayout> sets;
    std::vector<VkPipelineLayout> layouts;
    std::vector<VkPipeline> pipelines;
    std::vector<VkShaderModule> modules;
    VkImageView target_view{};VkRenderPass pass{};VkFramebuffer framebuffer{};VkDescriptorPool descriptors{};VkSampler sampler{};
    ~MaterialObjects(){
        for(auto p:pipelines)pDestroyPipeline(device,p,nullptr);
        for(auto l:layouts)pDestroyPipelineLayout(device,l,nullptr);
        if(descriptors)pDestroyDescriptorPool(device,descriptors,nullptr);
        if(sampler)pDestroySampler(device,sampler,nullptr);
        for(auto s:sets)pDestroyDescriptorSetLayout(device,s,nullptr);
        for(auto m:modules)pDestroyShaderModule(device,m,nullptr);
        if(framebuffer)pDestroyFramebuffer(device,framebuffer,nullptr);
        if(pass)pDestroyRenderPass(device,pass,nullptr);
        if(target_view)pDestroyImageView(device,target_view,nullptr);
    }
};
static void material_test(Gpu& g)
{
    TextureBudgets budget;std::vector<std::shared_ptr<const ImageUpload>> images;
    {
        TextureCache cache(cache_device(g),budget);std::string reason;
        const auto folder=std::filesystem::current_path()/L"cache-fixtures";
        for(unsigned i=0;i<3;++i){
            auto data=fixture(28);data.resize(152);put(data,12,1);put(data,16,1);put(data,28,1);
            data[148]=i==1?255:0;data[149]=0;data[150]=i==0?255:0;data[151]=i==2?0:255;
            const auto file=folder/(L"material-"+std::to_wstring(i)+L".dds");write_fixture(file,data);
            auto entry=cache.declare_image(file,reason);cache.demand(entry);auto ready=await_texture(cache,entry,g,true);
            if(!ready.image)throw std::runtime_error("Material texture unavailable: "+ready.reason);images.push_back(ready.image);
        }
        cache.stop();
    }
    check(budget.resident->used() && !budget.staging->used(),"material retains images after cache retirement and before drawing");
    MaterialObjects own{g.device};
    VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R8G8B8A8_UNORM;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};
    sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&reference;
    VkSubpassDependency dependencies[2]{};
    dependencies[0]={VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0};
    dependencies[1]={0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,0};
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=rp.subpassCount=1;
    rp.pAttachments=&attachment;rp.pSubpasses=&sub;rp.dependencyCount=2;rp.pDependencies=dependencies;
    CHECK_VK(pCreateRenderPass(g.device,&rp,nullptr,&own.pass));
    g.images.emplace_back();auto& target=g.images.back();VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType=VK_IMAGE_TYPE_2D;ci.format=attachment.format;ci.extent={96,64,1};ci.mipLevels=ci.arrayLayers=1;
    ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    CHECK_VK(pCreateImage(g.device,&ci,nullptr,&target.image));VkMemoryRequirements req{};pGetImageMemoryRequirements(g.device,target.image,&req);
    target.memory=g.allocate(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);CHECK_VK(pBindImageMemory(g.device,target.image,target.memory,0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=target.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=attachment.format;
    vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};CHECK_VK(pCreateImageView(g.device,&vi,nullptr,&own.target_view));
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=own.pass;fb.attachmentCount=1;fb.pAttachments=&own.target_view;
    fb.width=96;fb.height=64;fb.layers=1;CHECK_VK(pCreateFramebuffer(g.device,&fb,nullptr,&own.framebuffer));
    for(uint32_t set=0;set<4;++set){
        VkDescriptorSetLayoutBinding bindings[]={
            {set==1?6u:2u,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VkShaderStageFlags(set==1?VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_FRAGMENT_BIT),nullptr},
            {7,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {8,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        if(set==1 || set==3){info.bindingCount=set==1?3:1;info.pBindings=bindings;}
        VkDescriptorSetLayout layout{};CHECK_VK(pCreateDescriptorSetLayout(g.device,&info,nullptr,&layout));own.sets.push_back(layout);
    }
    for(uint32_t n:{3u,4u}){
        VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};info.setLayoutCount=n;info.pSetLayouts=own.sets.data();
        VkPipelineLayout layout{};CHECK_VK(pCreatePipelineLayout(g.device,&info,nullptr,&layout));own.layouts.push_back(layout);
    }
    auto vs=material_fixture::shader(true),fs=material_fixture::shader(false),probe=material_fixture::null_probe();
    std::vector<uint32_t> replaced,nulled;std::string reason;
    if(!repoint_fragment_images(fs,{{1,6,3,2}},replaced,reason))throw std::runtime_error(reason);
    if(!null_fragment_images(probe,{{1,6}},nulled,reason))throw std::runtime_error(reason);
    for(const auto* code:{&vs,&fs,&replaced,&probe,&nulled}){
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize=code->size()*4;info.pCode=code->data();
        VkShaderModule module{};CHECK_VK(pCreateShaderModule(g.device,&info,nullptr,&module));own.modules.push_back(module);
    }
    VkVertexInputBindingDescription binding{0,8,VK_VERTEX_INPUT_RATE_VERTEX};VkVertexInputAttributeDescription attribute{0,0,VK_FORMAT_R32G32_SFLOAT,0};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount=vertex.vertexAttributeDescriptionCount=1;vertex.pVertexBindingDescriptions=&binding;vertex.pVertexAttributeDescriptions=&attribute;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blending.attachmentCount=1;blending.pAttachments=&blend;
    VkDynamicState dynamic[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamics{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamics.dynamicStateCount=2;dynamics.pDynamicStates=dynamic;
    for(size_t i=0;i<4;++i){
        VkPipelineShaderStageCreateInfo stages[2]{};
        for(size_t j=0;j<2;++j){stages[j].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[j].stage=j?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;
            stages[j].module=own.modules[j?i+1:0];stages[j].pName="main";}
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};info.stageCount=2;info.pStages=stages;
        info.pVertexInputState=&vertex;info.pInputAssemblyState=&ia;info.pViewportState=&viewport;info.pRasterizationState=&raster;
        info.pMultisampleState=&ms;info.pColorBlendState=&blending;info.pDynamicState=&dynamics;info.layout=own.layouts[i==1?1:0];info.renderPass=own.pass;
        VkPipeline pipeline{};CHECK_VK(pCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&info,nullptr,&pipeline));own.pipelines.push_back(pipeline);
    }
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.magFilter=sampler.minFilter=VK_FILTER_NEAREST;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;sampler.maxLod=16;
    CHECK_VK(pCreateSampler(g.device,&sampler,nullptr,&own.sampler));
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,6},{VK_DESCRIPTOR_TYPE_SAMPLER,2}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets=4;dp.poolSizeCount=2;dp.pPoolSizes=sizes;CHECK_VK(pCreateDescriptorPool(g.device,&dp,nullptr,&own.descriptors));
    VkDescriptorSetLayout layouts[]={own.sets[1],own.sets[3],own.sets[3],own.sets[1]};VkDescriptorSet sets[4]{};
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=own.descriptors;ai.descriptorSetCount=4;ai.pSetLayouts=layouts;
    CHECK_VK(pAllocateDescriptorSets(g.device,&ai,sets));
    for(uint32_t i=0;i<3;++i){
        VkDescriptorImageInfo image{VK_NULL_HANDLE,images[i]->view(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=sets[i];write.dstBinding=i?2:6;
        write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.pImageInfo=&image;pUpdateDescriptorSets(g.device,1,&write,0,nullptr);
    }
    for(uint32_t index:{0u,3u})for(uint32_t binding:{6u,7u,8u}){
        const bool is_sampler=binding==7;
        VkDescriptorImageInfo image{is_sampler?own.sampler:VK_NULL_HANDLE,
            is_sampler?VK_NULL_HANDLE:images[index==3 && binding==6?2:0]->view(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=sets[index];write.dstBinding=binding;
        write.descriptorCount=1;write.descriptorType=is_sampler?VK_DESCRIPTOR_TYPE_SAMPLER:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo=&image;pUpdateDescriptorSets(g.device,1,&write,0,nullptr);
    }
    const float positions[]={-.8f,-.8f,-.2f,-.8f,-.5f,-.2f};auto vb=g.buffer(sizeof(positions),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    memcpy(vb.mapped,positions,sizeof(positions));auto readback=g.readback(96*64*4);auto cmd=g.begin();
    VkClearValue clear{};clear.color.float32[1]=clear.color.float32[3]=1;
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=own.pass;begin.framebuffer=own.framebuffer;
    begin.renderArea.extent={96,64};begin.clearValueCount=1;begin.pClearValues=&clear;pCmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
    VkDeviceSize offset=0;pCmdBindVertexBuffers(cmd,0,1,&vb.buffer,&offset);VkRect2D scissor{{0,0},{96,64}};pCmdSetScissor(cmd,0,1,&scissor);
    pCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,own.layouts[0],1,1,sets,0,nullptr);
    for(uint32_t i=0;i<6;++i){
        VkViewport vp{float((i%3)*32),float((i/3)*32),64,64,0,1};pCmdSetViewport(cmd,0,1,&vp);
        const auto pipeline=i==1?0:(i<3?1:(i==4?3:2));
        pCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,own.pipelines[pipeline]);
        if(i==0 || i==2)pCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,own.layouts[1],3,1,&sets[i==0?1:2],0,nullptr);
        if(i==5)pCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,own.layouts[0],1,1,&sets[3],0,nullptr);
        pCmdDraw(cmd,3,1,0,0);
    }
    pCmdEndRenderPass(cmd);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={96,64,1};
    pCmdCopyImageToBuffer(cmd,target.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&copy);g.finish(cmd,readback);
    auto pixel=[&](unsigned x,unsigned y,const std::array<uint8_t,4>& rgba,const char* label){
        const auto p=static_cast<uint8_t*>(readback.mapped)+4*(y*96+x);check(!memcmp(p,rgba.data(),4),label);
    };
    pixel(16,14,{255,0,0,255},"private fragment image turns red while vertex reads unchanged original image");
    pixel(48,14,{0,0,255,255},"following original draw keeps original image without rebinding it");
    pixel(80,14,{0,0,0,0},"zero texture fetch returns zero without Vulkan null-descriptor features");
    pixel(16,46,{255,255,255,255},"real image reports white border and nonzero size/mips");
    pixel(48,46,{0,0,0,255},"null rewrite zeros border sampling and queries while retaining the other blue image");
    pixel(80,46,{255,255,255,255},"zero texture alone fails to implement null border and query semantics");
    pixel(95,63,{0,255,0,255},"material draws leave untouched pixels green");
    std::ofstream pixels("material-pixels.rgba",std::ios::binary);pixels.write(static_cast<const char*>(readback.mapped),96*64*4);
}
