// Native Vulkan compute test host. Its queue submission/waits are explicit
// headless test operations, not runtime behavior in an intercepted game draw.
struct ParameterConsumer {
    Device& device;
    VkDescriptorSetLayout set_layout=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkShaderModule shader=VK_NULL_HANDLE;
#define PARAMETER_FUNCS(X) \
    X(CreateShaderModule) X(DestroyShaderModule) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
    X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateComputePipelines) X(DestroyPipeline) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
    X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdDispatch)
#define X(name) PFN_vk##name name=nullptr;
    PARAMETER_FUNCS(X)
#undef X
    ParameterConsumer(Device& d):device(d){
#define X(name) name=reinterpret_cast<PFN_vk##name>(d.get_device_proc(d.resource.device,"vk" #name));check(name!=nullptr,"Parameter consumer entry vk" #name);
        PARAMETER_FUNCS(X)
#undef X
    }
    ~ParameterConsumer(){const auto dev=device.resource.device;
        if(pipeline)DestroyPipeline(dev,pipeline,nullptr);if(layout)DestroyPipelineLayout(dev,layout,nullptr);
        if(set_layout)DestroyDescriptorSetLayout(dev,set_layout,nullptr);if(shader)DestroyShaderModule(dev,shader,nullptr);}
    void init(const std::filesystem::path& path){
        const auto dev=device.resource.device;
        std::ifstream file(path,std::ios::binary|std::ios::ate);check(bool(file) && file.tellg()>0 && size_t(file.tellg())%4==0,"Compiled parameter consumer SPIR-V");
        std::vector<uint32_t> code(size_t(file.tellg())/4);file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),code.size()*4);
        check(bool(file),"Read complete parameter consumer code");
        VkShaderModuleCreateInfo module={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};module.codeSize=code.size()*4;module.pCode=code.data();
        vkcheck(CreateShaderModule(dev,&module,nullptr,&shader),"Create parameter consumer shader");
        const VkDescriptorSetLayoutBinding bindings[]={
            {0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo set={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=2;set.pBindings=bindings;
        vkcheck(CreateDescriptorSetLayout(dev,&set,nullptr,&set_layout),"Create parameter consumer descriptor layout");
        VkPipelineLayoutCreateInfo pi={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pi.setLayoutCount=1;pi.pSetLayouts=&set_layout;
        vkcheck(CreatePipelineLayout(dev,&pi,nullptr,&layout),"Create parameter consumer pipeline layout");
        VkComputePipelineCreateInfo compute={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};compute.layout=layout;
        compute.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};compute.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;compute.stage.module=shader;compute.stage.pName="main";
        vkcheck(CreateComputePipelines(dev,VK_NULL_HANDLE,1,&compute,nullptr,&pipeline),"Create parameter consumer pipeline");
    }
    std::vector<uint8_t> read(gpu::ResourceCommands& commands,const gpu::ImageView& view){
        check(bool(view) && view->type()==VK_IMAGE_VIEW_TYPE_1D,"Shader consumes real parameter Texture1D view");
        const auto dev=device.resource.device;const auto& dispatch=device.resource.dispatch;const auto count=view->owner()->extent().width;
        auto output=commands.create(uint64_t(count)*16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        struct Work {
            ParameterConsumer& owner;VkDescriptorPool descriptors=VK_NULL_HANDLE;VkCommandPool pool=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;bool submitted=false;
            ~Work(){const auto dev=owner.device.resource.device;const auto& dispatch=owner.device.resource.dispatch;
                if(submitted)dispatch.WaitForFences(dev,1,&fence,VK_TRUE,UINT64_MAX);
                if(fence)dispatch.DestroyFence(dev,fence,nullptr);if(pool)dispatch.DestroyCommandPool(dev,pool,nullptr);
                if(descriptors)owner.DestroyDescriptorPool(dev,descriptors,nullptr);}
        } work{*this};
        const VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
        VkDescriptorPoolCreateInfo pool={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=1;pool.poolSizeCount=2;pool.pPoolSizes=sizes;
        vkcheck(CreateDescriptorPool(dev,&pool,nullptr,&work.descriptors),"Allocate parameter consumer descriptor pool");
        VkDescriptorSetAllocateInfo set={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};set.descriptorPool=work.descriptors;set.descriptorSetCount=1;set.pSetLayouts=&set_layout;
        VkDescriptorSet descriptor;vkcheck(AllocateDescriptorSets(dev,&set,&descriptor),"Allocate parameter consumer descriptor set");
        VkDescriptorImageInfo image{VK_NULL_HANDLE,view->view(),VK_IMAGE_LAYOUT_GENERAL};VkDescriptorBufferInfo buffer{output->buffer(),0,output->size()};
        VkWriteDescriptorSet writes[2]{};for(auto& write:writes){write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=descriptor;write.descriptorCount=1;}
        writes[0].dstBinding=0;writes[0].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[0].pImageInfo=&image;
        writes[1].dstBinding=1;writes[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[1].pBufferInfo=&buffer;
        UpdateDescriptorSets(dev,2,writes,0,nullptr);
        VkCommandPoolCreateInfo command_pool={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};command_pool.queueFamilyIndex=device.resource.queue_family;
        vkcheck(dispatch.CreateCommandPool(dev,&command_pool,nullptr,&work.pool),"Create parameter consumer commands");
        VkCommandBufferAllocateInfo allocate={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=work.pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
        VkCommandBuffer command;vkcheck(dispatch.AllocateCommandBuffers(dev,&allocate,&command),"Allocate parameter consumer commands");
        VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkcheck(dispatch.BeginCommandBuffer(command,&begin),"Begin parameter consumer");
        VkMemoryBarrier barrier={VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        dispatch.CmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&descriptor,0,nullptr);
        CmdDispatch(command,(count+31)/32,1,1);
        barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        dispatch.CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        vkcheck(dispatch.EndCommandBuffer(command),"End parameter consumer");
        VkFenceCreateInfo fence={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};vkcheck(dispatch.CreateFence(dev,&fence,nullptr,&work.fence),"Create parameter consumer fence");
        VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        vkcheck(dispatch.QueueSubmit(device.queue,1,&submit,work.fence),"Submit parameter consumer");work.submitted=true;
        vkcheck(dispatch.WaitForFences(dev,1,&work.fence,VK_TRUE,UINT64_MAX),"Wait parameter consumer");work.submitted=false;
        return commands.readback(device.queue,output,0,output->size());
    }
#undef PARAMETER_FUNCS
};
