#include "../layer/src/native_compute_restore.h"
#include "src/spirv.h"

// Independent application shader: output += dynamic_input + push_constant.
// Its set two is disturbed by the private packet's incompatible set-zero bind.
static std::vector<uint32_t> restore_application_shader(){
    spv::Builder b;
    const auto void_t=b.id(),uint_t=b.id(),fn_t=b.id(),buffer_t=b.id(),buffer_p=b.id(),uint_p=b.id();
    const auto push_t=b.id(),push_p=b.id(),push_uint_p=b.id(),zero=b.id(),input=b.id(),output=b.id(),push=b.id(),main=b.id();
    b.op(spv::OpCapability,{spv::CapabilityShader});b.op(spv::OpMemoryModel,{spv::AddressingLogical,spv::MemoryGLSL450});
    b.entry_point(5,main,"main",{});b.op(spv::OpExecutionMode,{main,17,1,1,1});
    b.op(spv::OpDecorate,{buffer_t,spv::DecorationBufferBlock});b.op(spv::OpMemberDecorate,{buffer_t,0,spv::DecorationOffset,0});
    b.op(spv::OpDecorate,{push_t,spv::DecorationBlock});b.op(spv::OpMemberDecorate,{push_t,0,spv::DecorationOffset,0});
    b.op(spv::OpDecorate,{input,spv::DecorationDescriptorSet,0});b.op(spv::OpDecorate,{input,spv::DecorationBinding,0});
    b.op(spv::OpDecorate,{output,spv::DecorationDescriptorSet,2});b.op(spv::OpDecorate,{output,spv::DecorationBinding,0});
    b.op(spv::OpTypeVoid,{void_t});b.op(spv::OpTypeInt,{uint_t,32,0});b.op(spv::OpTypeFunction,{fn_t,void_t});
    b.op(spv::OpTypeStruct,{buffer_t,uint_t});b.op(spv::OpTypePointer,{buffer_p,spv::StorageUniform,buffer_t});
    b.op(spv::OpTypePointer,{uint_p,spv::StorageUniform,uint_t});
    b.op(spv::OpTypeStruct,{push_t,uint_t});b.op(spv::OpTypePointer,{push_p,9,push_t});b.op(spv::OpTypePointer,{push_uint_p,9,uint_t});
    b.op(spv::OpConstant,{uint_t,zero,0});b.op(spv::OpVariable,{buffer_p,input,spv::StorageUniform});
    b.op(spv::OpVariable,{buffer_p,output,spv::StorageUniform});b.op(spv::OpVariable,{push_p,push,9});
    b.op(spv::OpFunction,{void_t,main,0,fn_t});b.op(spv::OpLabel,{b.id()});
    const auto in_p=b.id(),out_p=b.id(),pc_p=b.id(),in=b.id(),out=b.id(),pc=b.id(),sum=b.id(),result=b.id();
    b.op(spv::OpAccessChain,{uint_p,in_p,input,zero});b.op(spv::OpAccessChain,{uint_p,out_p,output,zero});
    b.op(spv::OpAccessChain,{push_uint_p,pc_p,push,zero});
    b.op(spv::OpLoad,{uint_t,in,in_p});b.op(spv::OpLoad,{uint_t,out,out_p});b.op(spv::OpLoad,{uint_t,pc,pc_p});
    b.op(spv::Op(128),{uint_t,sum,in,pc});b.op(spv::Op(128),{uint_t,result,out,sum});
    b.op(spv::OpStore,{out_p,result});b.op(spv::OpReturn,{});b.op(spv::OpFunctionEnd,{});return b.finish();
}
static void prepared_compute_restore_cases(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root);
    const auto device=d.resource.device;
#define GET_RESTORE(name) const auto name=reinterpret_cast<PFN_vk##name>(d.get_device_proc(device,"vk" #name))
    GET_RESTORE(CreateDescriptorSetLayout);GET_RESTORE(DestroyDescriptorSetLayout);GET_RESTORE(CreatePipelineLayout);GET_RESTORE(DestroyPipelineLayout);
    GET_RESTORE(CreateShaderModule);GET_RESTORE(DestroyShaderModule);GET_RESTORE(CreateComputePipelines);GET_RESTORE(DestroyPipeline);
    GET_RESTORE(CreateDescriptorPool);GET_RESTORE(DestroyDescriptorPool);GET_RESTORE(AllocateDescriptorSets);GET_RESTORE(UpdateDescriptorSets);
    GET_RESTORE(CmdBindPipeline);GET_RESTORE(CmdBindDescriptorSets);GET_RESTORE(CmdPushConstants);GET_RESTORE(CmdDispatch);
#undef GET_RESTORE
    struct Cleanup {std::vector<std::function<void()>> values;~Cleanup(){for(auto it=values.rbegin();it!=values.rend();++it)(*it)();}} cleanup;
    VkDescriptorSetLayout layouts[3]={};
    for(unsigned slot=0;slot<3;++slot){
        VkDescriptorSetLayoutBinding binding{0,slot==0?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};info.bindingCount=slot==1?0:1;info.pBindings=&binding;
        vkcheck(CreateDescriptorSetLayout(device,&info,nullptr,&layouts[slot]),"Create application restore descriptor layout");
        const auto handle=layouts[slot];cleanup.values.push_back([=]{DestroyDescriptorSetLayout(device,handle,nullptr);});
    }
    VkPushConstantRange pushes{VK_SHADER_STAGE_COMPUTE_BIT,0,4};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=3;li.pSetLayouts=layouts;li.pushConstantRangeCount=1;li.pPushConstantRanges=&pushes;
    VkPipelineLayout layout=VK_NULL_HANDLE;vkcheck(CreatePipelineLayout(device,&li,nullptr,&layout),"Create application restore pipeline layout");
    auto original_layout=std::make_shared<VkPipelineLayout>(layout);
    cleanup.values.push_back([=]{if(*original_layout)DestroyPipelineLayout(device,*original_layout,nullptr);});
    const auto code=restore_application_shader();VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=code.size()*4;mi.pCode=code.data();
    VkShaderModule module=VK_NULL_HANDLE;vkcheck(CreateShaderModule(device,&mi,nullptr,&module),"Create independent application restore shader");
    cleanup.values.push_back([=]{DestroyShaderModule(device,module,nullptr);});
    VkComputePipelineCreateInfo pi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pi.layout=layout;
    pi.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.module=module;pi.stage.pName="main";
    VkPipeline pipeline=VK_NULL_HANDLE;vkcheck(CreateComputePipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline),"Create independent application restore pipeline");
    cleanup.values.push_back([=]{DestroyPipeline(device,pipeline,nullptr);});
    const VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool_info.maxSets=2;pool_info.poolSizeCount=2;pool_info.pPoolSizes=sizes;
    VkDescriptorPool pool=VK_NULL_HANDLE;vkcheck(CreateDescriptorPool(device,&pool_info,nullptr,&pool),"Create application restore descriptor pool");
    cleanup.values.push_back([=]{DestroyDescriptorPool(device,pool,nullptr);});
    VkDescriptorSetLayout allocated[]={layouts[0],layouts[2]};VkDescriptorSet sets[2]={};
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=pool;ai.descriptorSetCount=2;ai.pSetLayouts=allocated;
    vkcheck(AllocateDescriptorSets(device,&ai,sets),"Allocate application restore descriptors");
    const auto stride=uint32_t(std::max<VkDeviceSize>(4,d.properties.limits.minStorageBufferOffsetAlignment));
    std::vector<uint32_t> values(stride/4+1,3);values.back()=11;
    gpu::ResourceCommands app(d.resource);auto input=app.create(values.size()*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,values.data(),values.size()*4);
    auto output=app.create(4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);{auto submission=app.submit(d.queue);submission->wait();}
    const VkDescriptorBufferInfo buffers[]={{input->buffer(),0,4},{output->buffer(),0,4}};
    VkWriteDescriptorSet writes[2]={};for(unsigned i=0;i<2;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=sets[i];writes[i].descriptorCount=1;
        writes[i].descriptorType=i==0?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&buffers[i];}
    UpdateDescriptorSets(device,2,writes,0,nullptr);
    write(root/"inserted.hlsl",R"hlsl(RWStructuredBuffer<uint> Output : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) { Output[0] += 100; }
)hlsl");
    gpu::HlslCompiler compiler(L"F:/vulkan/dxc-1.9.2609/bin/x64/dxcompiler.dll");gpu::HlslRequest request;request.source=root/"inserted.hlsl";request.working_directory=root;
    gpu::HlslProgram program;std::string why;check(compiler.compile(request,program,why),"Compile inserted compute state disturbance");
    gpu::ResourceCommands work(d.resource);gpu::ShaderCommands shaders(work,shader_host(d));auto private_pipeline=shaders.compile(program);
    work.begin_prepared();auto imported=work.import_buffer(output->buffer(),output->size(),output->usage(),output->memory(),output->memory_offset(),output->memory_size(),output);
    gpu::ShaderDescriptor descriptor;descriptor.binding=program.bindings.at(0).binding;descriptor.buffer=imported;descriptor.size=4;
    shaders.dispatch(private_pipeline,{descriptor},{1,1,1});auto packet=work.finish_prepared();work.accept_prepared();
    check(packet && packet->changes_compute_state(),"Inserted packet declares its compute-state changes");
    efvk::NativeComputeRestore restore;restore.pipeline=pipeline;restore.bind_pipeline=CmdBindPipeline;restore.bind_sets=CmdBindDescriptorSets;
    restore.sets.push_back({0,layout,sets[0],{stride}});restore.sets.push_back({2,layout,sets[1],{}});
    const auto original=restore;
    VkPipelineLayout replacement=VK_NULL_HANDLE;vkcheck(CreatePipelineLayout(device,&li,nullptr,&replacement),"Recreate compatible application restore layout");
    cleanup.values.push_back([=]{DestroyPipelineLayout(device,replacement,nullptr);});
    for(auto& set:restore.sets)set.layout=replacement;
    record_prepared_packets(d,{packet},[&](auto cb){
        original.record(cb);const uint32_t value=5;CmdPushConstants(cb,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&value);CmdDispatch(cb,1,1,1);
        DestroyPipelineLayout(device,*original_layout,nullptr);*original_layout=VK_NULL_HANDLE;
    },[&](auto cb){restore.record(cb);CmdDispatch(cb,1,1,1);});
    const auto result=app.readback(d.queue,output,0,4);uint32_t actual=0;memcpy(&actual,result.data(),4);
    check(actual==132,"Application continuation uses recreated layout, restored pipeline/high set/dynamic offset and unchanged push constants");
}
