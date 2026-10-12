// Included after the headless Gpu fixture. All rendering goes through the loaded DLL hooks.
#include "m2_fixture.h"

static std::string self_probe(const char* request,const char* output)
{
    wchar_t name[80];swprintf(name,80,L"\\\\.\\pipe\\efmi-vk-%lu",GetCurrentProcessId());
    HANDLE pipe=INVALID_HANDLE_VALUE;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(pipe==INVALID_HANDLE_VALUE && std::chrono::steady_clock::now()<deadline){
        pipe=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        if(pipe==INVALID_HANDLE_VALUE)Sleep(10);
    }
    if(pipe==INVALID_HANDLE_VALUE)throw std::runtime_error("M2 self probe connect");
    auto transfer=[&](void* bytes,uint32_t size,bool write){
        auto* p=static_cast<uint8_t*>(bytes);
        while(size){DWORD n=0;BOOL ok=write?WriteFile(pipe,p,size,&n,nullptr):ReadFile(pipe,p,size,&n,nullptr);
            if(!ok || !n){CloseHandle(pipe);throw std::runtime_error("M2 self probe transfer");}p+=n;size-=n;}
    };
    uint32_t header[]={static_cast<uint32_t>(strlen(request)),0};transfer(header,8,true);
    transfer(const_cast<char*>(request),header[0],true);transfer(header,8,false);
    if(header[0]>(2u<<20) || header[1]){CloseHandle(pipe);throw std::runtime_error("M2 self probe lengths");}
    std::string result(header[0],'\0');transfer(&result[0],header[0],false);CloseHandle(pipe);
    FILE* file=nullptr;fopen_s(&file,output,"wb");if(!file)throw std::runtime_error("M2 report file");
    fwrite(result.data(),1,result.size(),file);fclose(file);return result;
}

static void run_m2(Gpu& g,bool use_rp2)
{
    auto recipe=m2_fixture::recipe();
    RecipeFixture layout_fixture;auto layout=layout_fixture.layout;
    if(use_rp2){auto pass=std::make_shared<RenderPassRecipe>(*recipe.render_pass);pass->version2=true;recipe.render_pass=pass;}
    VK(recipe.render_pass->create(g.device,vkCreateRenderPass,vkCreateRenderPass2,&g.pass));
    for(const auto& set:layout.sets){
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for(const auto& p:set->bindings)bindings.push_back({p.first,static_cast<VkDescriptorType>(p.second.type),p.second.count,p.second.stages,nullptr});
        VkDescriptorSetLayoutCreateInfo ci={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount=static_cast<uint32_t>(bindings.size());ci.pBindings=bindings.data();
        VkDescriptorSetLayout s;VK(vkCreateDescriptorSetLayout(g.device,&ci,nullptr,&s));g.sets.push_back(s);
    }
    VkPipelineLayoutCreateInfo li={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=3;li.pSetLayouts=g.sets.data();
    VK(vkCreatePipelineLayout(g.device,&li,nullptr,&g.layout));
    std::vector<VkShaderModule> modules;
    for(const auto& stage:recipe.stages){
        VkShaderModuleCreateInfo ci={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.codeSize=stage.shader->code.size()*4;ci.pCode=stage.shader->code.data();
        VkShaderModule s;VK(vkCreateShaderModule(g.device,&ci,nullptr,&s));modules.push_back(s);
    }
    VK(recipe.create(g.device,vkCreateGraphicsPipelines,g.layout,g.pass,modules,&g.pipeline));
    for(auto module:modules)vkDestroyShaderModule(g.device,module,nullptr);
    Image color=g.image(VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
    Image depth=g.image(VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
    VkImageView views[]={color.view,depth.view};
    VkFramebufferCreateInfo fi={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fi.renderPass=g.pass;fi.attachmentCount=2;fi.pAttachments=views;fi.width=fi.height=64;fi.layers=1;
    VK(vkCreateFramebuffer(g.device,&fi,nullptr,&g.framebuffer));
    const float positions[12]={-.22f,-.2f,0,0, .22f,-.2f,0,0, 0,.22f,0,0};
    const float uv[6]={};
    struct Blend{uint16_t weight[4];uint8_t index[4];};
    // Bone zero of the component; the vertex shader reads it one matrix after the offset in the entry.
    const Blend blends[]={{{65535,0,0,0},{0,0,0,0}},{{65535,0,0,0},{0,0,0,0}},{{65535,0,0,0},{0,0,0,0}}};
    const uint16_t indices[]={0,1,2,0,1,2};
    auto pos=g.buffer(sizeof(positions),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);memcpy(pos.mapped,positions,sizeof(positions));
    auto tex=g.buffer(sizeof(uv),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);memcpy(tex.mapped,uv,sizeof(uv));
    auto blend=g.buffer(sizeof(blends),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);memcpy(blend.mapped,blends,sizeof(blends));
    auto index=g.buffer(sizeof(indices),VK_BUFFER_USAGE_INDEX_BUFFER_BIT);memcpy(index.mapped,indices,sizeof(indices));
    auto bones=g.buffer(65536,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    float tint[]={0,1,1,1};memcpy(bones.mapped,tint,16);
    uint32_t dynamic=static_cast<uint32_t>(std::max<VkDeviceSize>(256,g.properties.limits.minUniformBufferOffsetAlignment));
    auto instance=g.buffer(dynamic+65536,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    float color_value[]={1,1,0,.5f};memcpy(static_cast<uint8_t*>(instance.mapped)+dynamic+16,color_value,16);
    uint32_t bone_offsets[]={2000,3000,258,0x3f800000};memcpy(static_cast<uint8_t*>(instance.mapped)+dynamic+80,bone_offsets,16);
    auto readback=g.buffer(64*64*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1}};
    VkDescriptorPoolCreateInfo pi={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=3;pi.poolSizeCount=2;pi.pPoolSizes=sizes;
    VK(vkCreateDescriptorPool(g.device,&pi,nullptr,&g.descriptors));
    VkDescriptorSet sets[3];VkDescriptorSetAllocateInfo si={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    si.descriptorPool=g.descriptors;si.descriptorSetCount=3;si.pSetLayouts=g.sets.data();VK(vkAllocateDescriptorSets(g.device,&si,sets));
    VkDescriptorBufferInfo infos[]={{bones.handle,0,65536},{instance.handle,0,65536}};
    VkWriteDescriptorSet writes[2]={};
    for(uint32_t i=0;i<2;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=sets[i?2:0];writes[i].dstBinding=i?0:7;
        writes[i].descriptorCount=1;writes[i].descriptorType=i?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo=&infos[i];}
    vkUpdateDescriptorSets(g.device,2,writes,0,nullptr);
    VkCommandPoolCreateInfo pc={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pc.queueFamilyIndex=g.family;pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK(vkCreateCommandPool(g.device,&pc,nullptr,&g.pool));
    VkCommandBufferAllocateInfo ca={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=g.pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;
    VkCommandBuffer cmd;VK(vkAllocateCommandBuffers(g.device,&ca,&cmd));
    auto is_mesh=[&](uint32_t x,uint32_t y){auto* p=static_cast<uint8_t*>(readback.mapped)+4*(y*64+x);
        return p[0]==0 && p[1]>=127 && p[1]<=128 && p[2]>=127 && p[2]<=128 && p[3]>=127 && p[3]<=128;};
    auto render=[&](float x,float y,bool write_pose=true){
        float current[]={x,0,0,0},previous[]={0,y,0,0};
        if(write_pose){
            memcpy(static_cast<uint8_t*>(bones.mapped)+2003*16,current,16);
            memcpy(static_cast<uint8_t*>(bones.mapped)+3003*16,previous,16);
        }
        VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK(vkBeginCommandBuffer(cmd,&begin));
        VkClearValue clear[2]={};clear[0].color.float32[2]=clear[0].color.float32[3]=1;clear[1].depthStencil.depth=1;
        VkRenderPassBeginInfo bi={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};bi.renderPass=g.pass;bi.framebuffer=g.framebuffer;
        bi.renderArea.extent={64,64};bi.clearValueCount=2;bi.pClearValues=clear;vkCmdBeginRenderPass(cmd,&bi,VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp={0,0,64,64,0,1};VkRect2D sc={{0,0},{64,64}};vkCmdSetViewport(cmd,0,1,&vp);vkCmdSetScissor(cmd,0,1,&sc);
        VkBuffer streams[]={pos.handle,tex.handle,blend.handle,pos.handle};VkDeviceSize offsets[]={0,0,0,0};
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,g.pipeline);vkCmdBindVertexBuffers(cmd,0,4,streams,offsets);
        vkCmdBindIndexBuffer(cmd,index.handle,0,VK_INDEX_TYPE_UINT16);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,g.layout,0,3,sets,1,&dynamic);
        vkCmdDrawIndexed(cmd,3,1,0,0,0); // Matches the configured package and executes the DLL's M2 transaction.
        vkCmdDrawIndexed(cmd,3,1,3,0,0); // Different region key, with no pipeline/VB/IB/descriptor rebind.
        vkCmdEndRenderPass(cmd);
        VkBufferImageCopy copy={};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={64,64,1};
        vkCmdCopyImageToBuffer(cmd,color.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.handle,1,&copy);
        VkBufferMemoryBarrier barrier={VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer=readback.handle;barrier.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
        VK(vkEndCommandBuffer(cmd));VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        VK(vkQueueSubmit(g.queue,1,&submit,VK_NULL_HANDLE));VK(vkQueueWaitIdle(g.queue));
        return is_mesh(static_cast<uint32_t>((2+x)*32),static_cast<uint32_t>((1+y)*32));
    };
    bool active=false;
    for(uint32_t attempt=0;attempt<300 && !active;++attempt){active=render(-.5f,.35f);if(!active)Sleep(5);}
    check(active,"actual M2 hook replaces the configured mesh after asynchronous preparation");
    self_probe("{\"cmd\":\"status\"}","m2-active.json");
    if(!active)return;
    bool animation=true,restored=true;
    for(uint32_t frame=0;frame<30;++frame){
        float x=frame%2?-.65f:-.5f,y=frame%2?-.35f:.35f;
        animation &= render(x,y);restored &= is_mesh(static_cast<uint32_t>((1+x)*32),static_cast<uint32_t>((1+y)*32));
    }
    check(animation,"30 updates use current and previous poses through command recording reuse");
    check(restored,"every subsequent unmatched draw retains original geometry and fragment data");
    VK(vkResetCommandBuffer(cmd,0));
    check(render(-.5f,.35f),"explicit command-buffer reset recycles completed private snapshots");
    VK(vkResetCommandPool(g.device,g.pool,0));
    check(render(-.5f,.35f),"command-pool reset recycles completed private snapshots");
    self_probe("{\"cmd\":\"m2\",\"enabled\":0}","m2-disabled.json");
    VkSubmitInfo again={VK_STRUCTURE_TYPE_SUBMIT_INFO};again.commandBufferCount=1;again.pCommandBuffers=&cmd;
    VK(vkQueueSubmit(g.queue,1,&again,VK_NULL_HANDLE));VK(vkQueueWaitIdle(g.queue));
    check(is_mesh(48,43),"already recorded replacement remains immutable after disable and resubmit");
    check(!render(-.5f,.35f) && is_mesh(16,43),"runtime disable keeps the original draw");
    self_probe("{\"cmd\":\"m2\",\"enabled\":1}","m2-enabled.json");
    check(render(-.5f,.35f),"runtime enable restores replacement on the next recording");
    vkUnmapMemory(g.device,bones.memory);bones.mapped=nullptr;
    for(auto& buffer:g.buffers)if(buffer.memory==bones.memory)buffer.mapped=nullptr;
    check(!render(-.5f,.35f,false) && is_mesh(16,43),"unmapped palette falls back to the original draw without losing it");
    VK(vkMapMemory(g.device,bones.memory,0,bones.size,0,&bones.mapped));
    for(auto& buffer:g.buffers)if(buffer.memory==bones.memory)buffer.mapped=bones.mapped;
    check(render(-.5f,.35f),"mapped palette recovery resumes replacement");
    self_probe("{\"cmd\":\"status\"}","m2-final.json");
    FILE* file=nullptr;fopen_s(&file,"pixels.rgba","wb");if(file){fwrite(readback.mapped,1,64*64*4,file);fclose(file);}
}
