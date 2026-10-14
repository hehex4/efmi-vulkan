#include "../frontend/gpu/shader_usage.h"

static std::vector<uint8_t> native_shared_shader(const gpu::HlslProgram& program){
    ImageNativeReference native;ImageNativeReference::Ptr<ID3D11Buffer> output,readback;
    D3D11_BUFFER_DESC desc{256,D3D11_USAGE_DEFAULT,D3D11_BIND_UNORDERED_ACCESS,0,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,16};
    check(SUCCEEDED(native.device->CreateBuffer(&desc,nullptr,&output)),"Native shared-memory output");
    ImageNativeReference::Ptr<ID3D11UnorderedAccessView> uav;
    check(SUCCEEDED(native.device->CreateUnorderedAccessView(output.Get(),nullptr,&uav)),"Native shared-memory UAV");
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(SUCCEEDED(native.device->CreateBuffer(&desc,nullptr,&readback)),"Native shared-memory readback");
    ImageNativeReference::Ptr<ID3D11ComputeShader> shader;
    check(SUCCEEDED(native.device->CreateComputeShader(program.dxbc.data(),program.dxbc.size(),nullptr,&shader)),"Native shared-memory program");
    auto* view=uav.Get();native.context->CSSetShader(shader.Get(),nullptr,0);native.context->CSSetUnorderedAccessViews(0,1,&view,nullptr);
    native.context->Dispatch(2,1,1);native.context->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(native.context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"Native shared-memory results");
    std::vector<uint8_t> bytes(256);memcpy(bytes.data(),mapped.pData,bytes.size());native.context->Unmap(readback.Get(),0);return bytes;
}
static void compute_shared_memory(Device& d,const std::filesystem::path& root){
    auto program=compute_program(root,R"hlsl(struct Shared {float3 value;uint add;};
groupshared Shared SharedData[8];
groupshared uint Total;
RWStructuredBuffer<uint4> Output : register(u0);
[numthreads(8,1,1)] void main(uint3 group : SV_GroupID,uint index : SV_GroupIndex){
    if(index==0)Total=0;
    SharedData[index].value=float3(index,index*2,group.x+1);
    SharedData[index].add=index+1;
    GroupMemoryBarrierWithGroupSync();
    InterlockedAdd(Total,SharedData[index].add);
    GroupMemoryBarrierWithGroupSync();
    Output[group.x*8+index]=uint4(SharedData[7-index].value,Total);
})hlsl");
    const auto usage=gpu::shader_usage(program);check(usage.workgroup_bytes==132 && usage.sampled_pairs.empty(),"Shared storage analysis counts fixed aggregate storage and atomics");
    const auto reference=native_shared_shader(program);gpu::ResourceBudgets budgets;auto host=shader_host(d);
    {gpu::ResourceCommands commands(d.resource,budgets);gpu::ShaderCommands shaders(commands,host);auto pipeline=shaders.compile(program);
        auto output=commands.create(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);
        shaders.dispatch(pipeline,{buffer_descriptor('u',0,output)},{2,1,1});const auto actual=commands.readback(d.queue,output,0,256);
        check(actual==reference,"Vulkan shared structures/barriers/atomics match native DX11 bytes");
        for(unsigned group=0;group<2;++group)for(unsigned i=0;i<8;++i){uint32_t values[4];memcpy(values,actual.data()+(group*8+i)*16,16);
            check(values[0]==7-i && values[1]==(7-i)*2 && values[2]==group+1 && values[3]==36,"Shared memory synchronizes neighbours and isolates workgroups");}
        auto reduced_host=host;reduced_host.properties.limits.maxComputeSharedMemorySize=131;gpu::ShaderCommands limited(commands,reduced_host);
        refuses([&]{limited.compile(program);},"Shared-memory device limit is checked before pipeline creation");
        reduced_host.properties.limits.maxComputeSharedMemorySize=132;gpu::ShaderCommands exact(commands,reduced_host);
        check(bool(exact.compile(program)),"Exact shared-memory budget accepts the shader");
        auto malformed=program;malformed.spirv[3]=UINT32_MAX;
        refuses([&]{gpu::shader_usage(malformed);},"Shared analysis bounds module identity allocations");
    }
    check(!budgets.resident->used() && !budgets.staging->used() && !host.objects->used(),"Shared compute owners and submissions retire cleanly");
}
static void compute_sample_pairs(Device& d,const std::filesystem::path& root){
    auto program=compute_program(root,R"hlsl(Texture2D<uint> Unfiltered : register(t1);
Texture2D<float> Images[2] : register(t4);
SamplerState Filters[2] : register(s1);
RWStructuredBuffer<uint> Output : register(u0);
[numthreads(1,1,1)] void main(){
    Output[0]=Unfiltered.Load(int3(0,0,0));
    Output[1]=(uint)round(Images[0].SampleLevel(Filters[1],float2(.5,.5),0)*100);
    Output[2]=(uint)round(Images[1].SampleLevel(Filters[0],float2(.5,.5),0)*100);
})hlsl");
    const auto usage=gpu::shader_usage(program);
    check(usage.sampled_pairs.size()==2 && !usage.workgroup_bytes,"Sampler analysis excludes unrelated fetch-only images");
    for(const auto& pair:usage.sampled_pairs)check(pair.image_binding==gpu::HlslCompiler::binding('c','t',4) &&
        pair.sampler_binding==gpu::HlslCompiler::binding('c','s',1) && pair.image_element+pair.sampler_element==1,
        "Sampler analysis retains exact constant descriptor-array elements");
    gpu::ResourceBudgets budgets;auto host=shader_host(d);
    {gpu::ResourceCommands commands(d.resource,budgets);gpu::ShaderCommands shaders(commands,host);auto pipeline=shaders.compile(program);
        const uint32_t integer=73;const float first=.25f,second=.75f;
        auto fetch=compute_image(commands,VK_FORMAT_R32_UINT,&integer),a=compute_image(commands,VK_FORMAT_R32_SFLOAT,&first),
            b=compute_image(commands,VK_FORMAT_R32_SFLOAT,&second);
        VkSamplerCreateInfo sampler_info={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler_info.magFilter=sampler_info.minFilter=VK_FILTER_NEAREST;
        sampler_info.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sampler_info.addressModeU=sampler_info.addressModeV=sampler_info.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        auto point=shaders.sampler(sampler_info);sampler_info.magFilter=sampler_info.minFilter=VK_FILTER_LINEAR;auto linear=shaders.sampler(sampler_info);
        auto output=commands.create(12,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);
        std::vector<gpu::ShaderDescriptor> bindings{buffer_descriptor('u',0,output)};
        for(const auto& item:std::vector<std::tuple<unsigned,unsigned,gpu::ImageView>>{{1,0,fetch},{4,0,a},{4,1,b}}){
            gpu::ShaderDescriptor value;value.binding=gpu::HlslCompiler::binding('c','t',std::get<0>(item));value.element=std::get<1>(item);value.image=std::get<2>(item);bindings.push_back(value);}
        for(unsigned i=0;i<2;++i){gpu::ShaderDescriptor value;value.binding=gpu::HlslCompiler::binding('c','s',1);value.element=i;value.sampler=i?linear:point;bindings.push_back(value);}
        shaders.dispatch(pipeline,bindings,{1,1,1});const auto bytes=commands.readback(d.queue,output,0,12);const uint32_t expected[]={73,25,75};
        check(!memcmp(bytes.data(),expected,12),"Unfilterable fetch and separately paired linear/nearest samples execute together");
    }
    check(!budgets.resident->used() && !budgets.staging->used() && !host.objects->used(),"Sample-pair descriptors retain and release all owners");
}
static void compute_indirect(Device& d,const std::filesystem::path& root){
    auto producer=compute_program(root/"producer",R"hlsl(RWStructuredBuffer<uint> Args : register(u0);
[numthreads(1,1,1)] void main(){Args[1]=2;Args[2]=1;Args[3]=1;})hlsl");
    auto consumer=compute_program(root/"consumer",R"hlsl(RWStructuredBuffer<uint> Output : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID){Output[id.x]=100+id.x;})hlsl");
    gpu::ResourceBudgets budgets;auto host=shader_host(d);
    {gpu::ResourceCommands commands(d.resource,budgets);gpu::ShaderCommands shaders(commands,host);
        auto produce=shaders.compile(producer),consume=shaders.compile(consumer);
        auto args=commands.create(16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,nullptr,0,true);
        auto output=commands.create(12,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);const auto bindings=std::vector<gpu::ShaderDescriptor>{buffer_descriptor('u',0,output)};
        shaders.dispatch(produce,{buffer_descriptor('u',0,args)},{1,1,1});shaders.dispatch_indirect(consume,bindings,args,4);
        auto bytes=commands.readback(d.queue,output,0,12);const uint32_t expected[]={100,101,0};
        check(!memcmp(bytes.data(),expected,12),"Indirect dispatch consumes earlier GPU-produced counts without CPU feedback");
        commands.fill(args,4,12,0);commands.fill(output,0,12,0);shaders.dispatch_indirect(consume,bindings,args,4);
        bytes=commands.readback(d.queue,output,0,12);check(std::all_of(bytes.begin(),bytes.end(),[](auto v){return !v;}),"Zero GPU indirect counts produce no work");
        refuses([&]{shaders.dispatch_indirect(consume,bindings,args,1);},"Indirect offset must be aligned");
        refuses([&]{shaders.dispatch_indirect(consume,bindings,args,8);},"Indirect argument structure must fit");
        refuses([&]{shaders.dispatch_indirect(consume,bindings,output,0);},"Indirect argument buffer needs indirect usage");
        gpu::ResourceCommands other(d.resource,budgets);auto foreign=other.create(16,VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        refuses([&]{shaders.dispatch_indirect(consume,bindings,foreign,0);},"Indirect argument buffer belongs to the same domain");
        const uint32_t counts[]={1,1,1};commands.write(args,4,counts,12);shaders.dispatch_indirect(consume,bindings,args,4);
        auto ticket=commands.submit(d.queue);std::weak_ptr<gpu::BufferResource> retained=args;args.reset();
        check(!retained.expired(),"Indirect submission retains its argument buffer");ticket->wait();ticket.reset();
        check(retained.expired(),"Indirect argument owner retires after its fence ticket");
    }
    check(!budgets.resident->used() && !budgets.staging->used() && !host.objects->used(),"Indirect execution releases budgets");
}
static void original_indirect(Device& d,const std::filesystem::path& root){
    std::filesystem::create_directories(root/"Mods");
    write(root/"Mods/produce.hlsl",R"hlsl(RWStructuredBuffer<uint> Args : register(u1);
[numthreads(1,1,1)] void main(){Args[1]=2;Args[2]=1;Args[3]=1;})hlsl");
    write(root/"Mods/consume.hlsl",R"hlsl(RWStructuredBuffer<float> Output : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID){Output[id.x]=100+id.x;})hlsl");
    ShaderFixture f(d,root,R"ini(namespace = compute
[Constants]
global $first = 0
global $second = 0
global $third = 0
[ResourceArgs]
type = StructuredBuffer
stride = 4
data = R32_UINT 0 0 0 0
bind_flags = unordered_access shader_resource
misc_flags = drawindirect_args
[ResourceAlias]
[ResourceOutput]
type = StructuredBuffer
stride = 4
data = R32_FLOAT 0 0 0
bind_flags = unordered_access shader_resource
[ResourceMissing]
[CustomShaderProduce]
cs = produce.hlsl
cs-u1 = ResourceArgs
dispatch = 1, 1, 1
[CustomShaderConsume]
cs = consume.hlsl
cs-u0 = ResourceOutput
ResourceAlias = ref ResourceArgs->region(12, 4)
dispatchindirect = ResourceAlias, ResourceArgs->stride
store = $first, cs-u0, 0
store = $second, cs-u0, 1
store = $third, cs-u0, 2
dispatchindirect = ResourceMissing, 0
[Present]
pre run = CustomShaderProduce
pre run = CustomShaderConsume
)ini");
    f.present();check(f.variable("first")==100 && f.variable("second")==101 && f.variable("third")==0,
        "Original indirect source uses its explicit byte offset, ignores region offset and resolves argument queries");
    check(f.backend->dispatches()==2,"Original null indirect source skips the dispatch");f.release();
}
static void original_sampling(Device& d,const std::filesystem::path& root){
    const auto program=compute_program(root/"Mods",R"hlsl(Texture1D<float4> Parameters : register(t120);
SamplerState Filter : register(s2);
RWStructuredBuffer<float4> Output : register(u3);
[numthreads(1,1,1)] void main(){Output[0]=float4(Parameters.SampleLevel(Filter,.5,0).x,
    Parameters.SampleLevel(Filter,0,0).x,Parameters.SampleLevel(Filter,1,0).x,Parameters.SampleLevel(Filter,.5,-2).x);})hlsl");
    ShaderFixture f(d,root,R"ini(namespace = compute
[Constants]
global $average = 0
x = 2
x1 = 6
[ResourceOutput]
type = StructuredBuffer
stride = 16
data = R32_FLOAT 0 0 0 0
bind_flags = unordered_access shader_resource
[CustomShaderSample]
cs = compute.hlsl
cs-u3 = ResourceOutput
dispatch = 1, 1, 1
store = $average, cs-u3, 0
[Present]
pre run = CustomShaderSample
)ini");
    const float source[]={2,0,0,0,6,0,0,0};std::vector<uint8_t> input(sizeof(source));memcpy(input.data(),source,sizeof(source));
    auto reference=native_parameter_shader(program,input);reference.resize(16);
    f.present();auto output=f.backend->resources().binding(L'c',ResourceCopyTargetType::UNORDERED_ACCESS_VIEW,3).buffer;
    auto actual=f.backend->resources().commands().readback(d.queue,output,0,16);
    const float expected[]={4,2,6,4};check(actual==reference && !memcmp(actual.data(),expected,16),"Original null sampler matches native DX11 interpolation, clamp and negative LOD");
    VkSamplerCreateInfo point={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};point.magFilter=point.minFilter=VK_FILTER_NEAREST;
    point.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;point.addressModeU=point.addressModeV=point.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;point.maxLod=1000;
    f.backend->set_sampler(L'c',2,&point);f.present();check(f.variable("average")==6,"Explicit host sampler state reaches the original compute slot");
    f.backend->set_sampler(L'c',2,nullptr);f.present();check(f.variable("average")==4,"Null sampler restores source default state");
    refuses([&]{f.backend->set_sampler(L'c',16,&point);},"Source sampler slots are bounded");
    output.reset();f.release();
    auto limited_host=shader_host(d);limited_host.sampler_objects=std::make_shared<assets::AllocationBudget>(1);
    {gpu::ResourceCommands resources(d.resource);gpu::ShaderCommands first(resources,limited_host),second(resources,limited_host);
        auto sampler=first.sampler(point);refuses([&]{second.sampler(point);},"Sampler quota is shared by independent shader owners");
        check(limited_host.sampler_objects->used()==1 && limited_host.objects->used()==1,"Failed sampler quota acquisition rolls back object accounting");
        sampler.reset();sampler=second.sampler(point);check(bool(sampler),"Retiring a sampler makes its quota available");
        auto invalid_host=limited_host;invalid_host.sampler_objects=std::make_shared<assets::AllocationBudget>(uint64_t(d.properties.limits.maxSamplerAllocationCount)+1);
        refuses([&]{gpu::ShaderCommands invalid(resources,invalid_host);},"Host cannot reserve more samplers than the physical limit");
    }
    check(!limited_host.sampler_objects->used() && !limited_host.objects->used(),"Sampler and object quotas retire together");
}
