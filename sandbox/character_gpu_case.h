// Included after m2_gpu_case.h. A three-component character drawn through the loaded DLL hooks:
// one component replaced on its own bones, one whose replacement also reads the first
// component's bones (the merged skeleton), one the mod hides, and a second LOD with a bone remap.
// The package is written by sandbox/character_test.py; positions below must agree with it.

static void run_character(Gpu& g,bool use_rp2)
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

    // The game's own mesh: one triangle on bone zero of whichever component's palette the entry names.
    const float positions[12]={-.22f,-.2f,0,0, .22f,-.2f,0,0, 0,.22f,0,0};
    const float uv[6]={};
    struct Blend{uint16_t weight[4];uint8_t index[4];};
    const Blend blends[]={{{65535,0,0,0},{0,0,0,0}},{{65535,0,0,0},{0,0,0,0}},{{65535,0,0,0},{0,0,0,0}}};
    // Index regions: component A (3 at 0), B (6 at 3), C (9 at 9), A at LOD 1 (12 at 18), an unmatched draw (3 at 30).
    // Padding triangles are degenerate, so every region draws one triangle.
    uint16_t indices[33]={};
    for(uint32_t first:{0u,3u,9u,18u,30u}){indices[first]=0;indices[first+1]=1;indices[first+2]=2;}
    auto pos=g.buffer(sizeof(positions),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);memcpy(pos.mapped,positions,sizeof(positions));
    auto tex=g.buffer(sizeof(uv),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);memcpy(tex.mapped,uv,sizeof(uv));
    auto blend=g.buffer(sizeof(blends),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);memcpy(blend.mapped,blends,sizeof(blends));
    auto index=g.buffer(sizeof(indices),VK_BUFFER_USAGE_INDEX_BUFFER_BIT);memcpy(index.mapped,indices,sizeof(indices));
    auto bones=g.buffer(65536,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto row=[&](uint32_t r,float x,float y){float v[4]={x,y,0,0};memcpy(static_cast<uint8_t*>(bones.mapped)+r*16,v,16);};
    float tint[]={0,1,1,1};memcpy(bones.mapped,tint,16);
    // Palettes: a component's bone k is the matrix at offset + 3 + 3 * k; only its first row is used.
    enum{A_CUR=2000,A_PREV=2100,B_CUR=2200,B_PREV=2300,C_CUR=2400,C_PREV=2500,L_CUR=2600,L_PREV=2700,U_CUR=2800,U_PREV=2900,
         N_CUR=3000,N_PREV=3100};
    row(A_CUR+3,-.6f,-.6f);row(A_CUR+6,-.6f,.6f);
    row(B_CUR+3,0,-.6f);row(B_CUR+6,0,0);
    row(C_CUR+3,.6f,-.6f);
    row(L_CUR+3,-.2f,.6f);row(L_CUR+6,-.6f,-.2f);
    row(U_CUR+3,.6f,-.1f);
    row(N_CUR+3,0,.6f);
    const uint32_t dynamic=static_cast<uint32_t>(std::max<VkDeviceSize>(256,g.properties.limits.minUniformBufferOffsetAlignment));
    auto instance=g.buffer(6*dynamic+65536,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    auto entry=[&](uint32_t slot,uint32_t current,uint32_t previous,float green,float x=10){
        uint8_t* e=static_cast<uint8_t*>(instance.mapped)+slot*dynamic;
        float shade[]={1,green,0,.5f};memcpy(e+16,shade,16);
        float place[]={x,20,30,1};memcpy(e+48,place,16);
        uint32_t offsets[]={current,previous,258,0x3f800000};memcpy(e+80,offsets,16);
    };
    enum{E_A,E_B,E_C,E_L,E_U,E_N};
    entry(E_A,A_CUR,A_PREV,1);entry(E_B,B_CUR,B_PREV,.5f);entry(E_C,C_CUR,C_PREV,1);entry(E_L,L_CUR,L_PREV,1);entry(E_U,U_CUR,U_PREV,1);
    // Another object, a quarter of a unit away, that happens to use component B's mesh.
    entry(E_N,N_CUR,N_PREV,1,10.25f);
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

    // 0 background, 1 a draw shaded by component A's (or C's, or the unmatched draw's) entry, 2 by B's, 3 anything else.
    auto shade_at=[&](float x,float y){
        const uint32_t px=static_cast<uint32_t>((x+1)*32),py=static_cast<uint32_t>((y+1)*32);
        if(px>=64 || py>=64)return 3;
        const uint8_t* p=static_cast<uint8_t*>(readback.mapped)+4*(py*64+px);
        if(p[0]==0 && p[1]==0 && p[2]==255)return 0;
        if(p[0]==0 && p[1]>=126 && p[1]<=129 && p[2]>=126 && p[2]<=129)return 1;
        if(p[0]==0 && p[1]>=62 && p[1]<=66 && p[2]>=126 && p[2]<=129)return 2;
        return 3;
    };
    auto frame=[&](bool lod1){
        VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK(vkBeginCommandBuffer(cmd,&begin));
        VkClearValue clear[2]={};clear[0].color.float32[2]=clear[0].color.float32[3]=1;clear[1].depthStencil.depth=1;
        VkRenderPassBeginInfo bi={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};bi.renderPass=g.pass;bi.framebuffer=g.framebuffer;
        bi.renderArea.extent={64,64};bi.clearValueCount=2;bi.pClearValues=clear;vkCmdBeginRenderPass(cmd,&bi,VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp={0,0,64,64,0,1};VkRect2D sc={{0,0},{64,64}};vkCmdSetViewport(cmd,0,1,&vp);vkCmdSetScissor(cmd,0,1,&sc);
        VkBuffer streams[]={pos.handle,tex.handle,blend.handle,pos.handle};VkDeviceSize offsets[]={0,0,0,0};
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,g.pipeline);vkCmdBindVertexBuffers(cmd,0,4,streams,offsets);
        vkCmdBindIndexBuffer(cmd,index.handle,0,VK_INDEX_TYPE_UINT16);
        auto bind=[&](uint32_t slot){uint32_t offset=slot*dynamic;
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,g.layout,0,3,sets,1,&offset);};
        if(lod1){bind(E_L);vkCmdDrawIndexed(cmd,12,1,18,0,0);}
        else{bind(E_A);vkCmdDrawIndexed(cmd,3,1,0,0,0);}
        bind(E_B);vkCmdDrawIndexed(cmd,6,1,3,0,0);
        bind(E_C);vkCmdDrawIndexed(cmd,9,1,9,0,0);
        // Not a section of the package; drawn right after replaced draws without rebinding pipeline or buffers.
        bind(E_U);vkCmdDrawIndexed(cmd,3,1,30,0,0);
        // The neighbour draws B's index region with its own entry. It is never the character.
        bind(E_N);vkCmdDrawIndexed(cmd,6,1,3,0,0);
        vkCmdEndRenderPass(cmd);
        VkBufferImageCopy copy={};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={64,64,1};
        vkCmdCopyImageToBuffer(cmd,color.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.handle,1,&copy);
        VkBufferMemoryBarrier barrier={VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer=readback.handle;barrier.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
        VK(vkEndCommandBuffer(cmd));VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        VK(vkQueueSubmit(g.queue,1,&submit,VK_NULL_HANDLE));VK(vkQueueWaitIdle(g.queue));
        // There is no swapchain here: tell the backend that a frame with draws ended.
        self_probe("{\"cmd\":\"m2\",\"advance\":1}","character-advance.json");
    };
    // What the game draws by itself: every component at bone zero of its own palette.
    auto original=[&]{return shade_at(-.6f,-.6f)==1 && shade_at(0,-.6f)==2 && shade_at(.6f,-.6f)==1 && shade_at(.6f,-.1f)==1 && shade_at(0,.6f)==1 &&
                             shade_at(-.6f,.3f)==0 && shade_at(0,0)==0 && shade_at(.6f,.3f)==0;};
    // The package: A's mesh on merged bone 1 moved by (0,-.3); B's on merged bone 3 and, moved by
    // (1.2,.9), on merged bone 0, which is A's; C hidden. `ax` is where A's bone zero currently is.
    auto replaced=[&](float ax){return shade_at(-.6f,.3f)==1 && shade_at(0,0)==2 && shade_at(ax+1.2f,.3f)==2 && shade_at(0,.6f)==1 &&
                                       shade_at(-.6f,-.6f)==0 && shade_at(0,-.6f)==0 && shade_at(.6f,-.6f)==0 && shade_at(.6f,-.1f)==1;};

    frame(false);
    check(original(),"first frame of a new character is drawn by the application");
    bool active=false;
    for(uint32_t attempt=0;attempt<400 && !active;++attempt){frame(false);active=replaced(-.6f);if(!active)Sleep(5);}
    check(active,"whole character replaced: own bones, another component's bones, hidden component, restored next draw, neighbour untouched");
    self_probe("{\"cmd\":\"status\"}","character-active.json");
    if(!active)return;
    bool follows=true;
    for(uint32_t i=0;i<20;++i){
        const float ax=i%2?-.5f:-.7f;
        row(A_CUR+3,ax,-.6f);frame(false);follows&=replaced(ax);
    }
    row(A_CUR+3,-.6f,-.6f);
    check(follows,"20 pose changes of one component move the other component's geometry in the same frame");
    // LOD 1 of component A: its palette is in another order and the package remaps it.
    // Merged bone 0 <- source 1 = (-.6,-.2), merged bone 1 <- source 0 = (-.2,.6).
    bool lod=false;
    for(uint32_t attempt=0;attempt<100 && !lod;++attempt){
        frame(true);
        lod=shade_at(-.2f,.3f)==1 && shade_at(.6f,.7f)==2 && shade_at(0,0)==2 && shade_at(-.2f,.6f)==0 && shade_at(-.6f,-.5f)==0 &&
            shade_at(.6f,-.1f)==1 && shade_at(0,.6f)==1;
        if(!lod)Sleep(5);
    }
    check(lod,"LOD 1 section imports through its remap and drives both components");
    frame(false);frame(false);
    check(replaced(-.6f),"return to LOD 0 restores the unmapped palette");
    self_probe("{\"cmd\":\"m2\",\"enabled\":0}","character-disabled.json");
    frame(false);
    check(original(),"runtime disable returns every component to the application's draws");
    self_probe("{\"cmd\":\"m2\",\"enabled\":1}","character-enabled.json");
    bool again=false;
    for(uint32_t attempt=0;attempt<100 && !again;++attempt){frame(false);again=replaced(-.6f);}
    check(again,"runtime enable identifies the character again and resumes replacement");
    VK(vkResetCommandPool(g.device,g.pool,0));
    frame(false);
    check(replaced(-.6f),"command-pool reset recycles the shared snapshots");
    self_probe("{\"cmd\":\"status\"}","character-final.json");
    FILE* file=nullptr;fopen_s(&file,"pixels.rgba","wb");if(file){fwrite(readback.mapped,1,64*64*4,file);fclose(file);}
}
