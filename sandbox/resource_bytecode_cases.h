#include "../frontend/gpu/dxbc_compiler.h"
#include "../frontend/xxmi/shader_assembly.h"

static std::vector<uint8_t> bytecode_source(char stage,const char* text){
    ImageNativeReference::Ptr<ID3DBlob> code,errors;const auto model=std::string(1,stage)+"s_5_0";
    const auto hr=D3DCompile(text,strlen(text),"bytecode-source.hlsl",nullptr,nullptr,"main",model.c_str(),D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(FAILED(hr))throw std::runtime_error(errors?static_cast<const char*>(errors->GetBufferPointer()):"Native bytecode fixture failed");
    ++checks;const auto* start=static_cast<const uint8_t*>(code->GetBufferPointer());return {start,start+code->GetBufferSize()};
}
static gpu::HlslProgram translated_bytecode(gpu::DxbcCompiler& compiler,const std::vector<uint8_t>& bytes,char stage,gpu::DxbcFloat32Mode mode=gpu::DxbcFloat32Mode::Hardware){
    gpu::HlslProgram program;std::string why;if(!compiler.compile(bytes,stage,program,why,mode))throw std::runtime_error(why);++checks;return program;
}
static std::vector<uint8_t> bytecode_roundtrip(const std::vector<uint8_t>& bytes){
    const auto code=frontend::SourceShaderCode::create(bytes.data(),bytes.size());std::vector<std::string> notices;
    return frontend::source_shader_assembly(frontend::source_shader_disassembly(*code,false,false),notices);
}
static void bytecode_execution(Device& d,const std::filesystem::path& root,gpu::DxbcFloat32Mode mode=gpu::DxbcFloat32Mode::Hardware){
    std::filesystem::create_directories(root);gpu::DxbcCompiler compiler(L"F:/vulkan/vkd3d-2.1-build/efmi-vkd3d-shader.dll");
    const char* shared=R"hlsl(groupshared uint Shared[8]; groupshared uint Total;
RWStructuredBuffer<uint4> Output:register(u0);
[numthreads(8,1,1)] void main(uint3 group:SV_GroupID,uint index:SV_GroupIndex){
if(index==0)Total=0; Shared[index]=index+group.x*10; GroupMemoryBarrierWithGroupSync();
InterlockedAdd(Total,index+1); GroupMemoryBarrierWithGroupSync();
Output[group.x*8+index]=uint4(Shared[7-index],Total,group.x,index);})hlsl";
    const auto original=bytecode_source('c',shared);
    gpu::ResourceBudgets budgets;auto host=shader_host(d);
    for(bool reassembled:{false,true}){auto bytes=reassembled?bytecode_roundtrip(original):original;
        auto program=translated_bytecode(compiler,bytes,'c',mode);const auto expected=native_shared_shader(program);
        check(program.dxbc==bytes && program.threads[0]==8 && program.bindings.size()==1,
            "Bytecode translation retains exact source with instruction-derived descriptors and workgroup size");
        gpu::ResourceCommands resources(d.resource,budgets);gpu::ShaderCommands shaders(resources,host);auto pipeline=shaders.compile(program);
        auto output=resources.create(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);
        {auto disabled=host;disabled.float_controls=false;gpu::ShaderCommands denied(resources,disabled);
            refuses([&]{denied.compile(program);},"Both bytecode policies refuse without enabled float controls");
            disabled=host;disabled.float_properties.shaderSignedZeroInfNanPreserveFloat32=VK_FALSE;gpu::ShaderCommands nan_disabled(resources,disabled);
            refuses([&]{nan_disabled.compile(program);},"Bytecode requires native NaN, infinity and signed-zero semantics");
            disabled=host;disabled.float_properties.shaderRoundingModeRTEFloat32=VK_FALSE;gpu::ShaderCommands rounding_disabled(resources,disabled);
            refuses([&]{rounding_disabled.compile(program);},"Bytecode requires native nearest-even arithmetic rounding");}
        if(mode==gpu::DxbcFloat32Mode::Hardware){auto disabled=host;
            disabled=host;disabled.float_properties.shaderDenormFlushToZeroFloat32=VK_FALSE;gpu::ShaderCommands unsupported(resources,disabled);
            refuses([&]{unsupported.compile(program);},"Translated DXBC refuses when device lacks source denormal behavior");}
        shaders.dispatch(pipeline,{buffer_descriptor('u',0,output)},{2,1,1});
        check(resources.readback(d.queue,output,0,256)==expected,"Original and reassembled DXBC shared memory/atomics match native bytes");
        const auto preserved=program.spirv;std::string why;auto invalid=bytes;invalid[0]='X';
        check(!compiler.compile(invalid,'c',program,why) && program.spirv==preserved,"Rejected bytecode preserves the previous program");
        check(!compiler.compile(bytes,'v',program,why) && program.spirv==preserved,"Bytecode stage mismatch refuses atomically");
    }
    const auto constant=bytecode_source('c',"RWStructuredBuffer<uint4> Output:register(u0);[numthreads(8,1,1)] void main(uint3 id:SV_DispatchThreadID){Output[id.x]=1;}");
    const auto code=frontend::SourceShaderCode::create(constant.data(),constant.size());auto text=frontend::source_shader_disassembly(*code,false,false);
    write(root/"original.asm",text.c_str());
    const std::string old_literal="l(1,1,1,1)";const auto position=text.find(old_literal);check(position!=std::string::npos,"Native assembly exposes the constant patch fixture");
    text.replace(position,old_literal.size(),"l(7,7,7,7)");std::vector<std::string> notices;const auto patched=frontend::source_shader_assembly(text,notices);
    auto changed=translated_bytecode(compiler,patched,'c',mode);const auto expected=native_shared_shader(changed);
    {gpu::ResourceCommands resources(d.resource,budgets);gpu::ShaderCommands shaders(resources,host);auto pipeline=shaders.compile(changed);
        auto output=resources.create(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);shaders.dispatch(pipeline,{buffer_descriptor('u',0,output)},{2,1,1});
        const auto actual=resources.readback(d.queue,output,0,256);check(actual==expected,"Patched DXBC executes exact changed instructions on Vulkan and native DX11");
        for(unsigned i=0;i<64;++i){uint32_t value;memcpy(&value,actual.data()+i*4,4);check(value==7,"Translated patch is not an unchanged HLSL recompile");}}
    check(!budgets.resident->used() && !budgets.staging->used() && !host.objects->used(),"Bytecode compute resources retire fully");
    const auto unsupported=bytecode_source('c',"cbuffer Constants:register(b0){double Input;};RWStructuredBuffer<double> Output:register(u0);[numthreads(1,1,1)] void main(){Output[0]=Input*1.5;}");
    for(unsigned i=0;i<8;++i){std::string why;const auto old=changed.spirv;
        check(!compiler.compile(unsupported,'c',changed,why,mode) && changed.spirv==old && why.find("64-bit")!=std::string::npos,
            "Failed native compilation owns no released output and preserves the published program");}
}
static void bytecode_graphics(Device& d,const std::filesystem::path& root,gpu::DxbcFloat32Mode mode=gpu::DxbcFloat32Mode::Hardware){
    gpu::DxbcCompiler compiler(L"F:/vulkan/vkd3d-2.1-build/efmi-vkd3d-shader.dll");
    const auto vertex=bytecode_source('v',R"hlsl(struct V{float4 p:SV_Position;nointerpolation uint2 ids:DATA0;};
V main(uint id:SV_VertexID,uint instance:SV_InstanceID){V v;uint k=id%3;
v.p=float4(k==2?6:-2,k==1?6:-2,1,2);v.ids=uint2(id,instance);return v;})hlsl");
    const auto pixel=bytecode_source('p',"float4 main(float4 position:SV_Position,nointerpolation uint2 ids:DATA0):SV_Target{return float4(position.zw,ids);}");
    gpu::ResourceBudgets budgets;
    for(bool reassembled:{false,true}){
        auto vs=translated_bytecode(compiler,reassembled?bytecode_roundtrip(vertex):vertex,'v',mode);
        auto ps=translated_bytecode(compiler,reassembled?bytecode_roundtrip(pixel):pixel,'p',mode);
        const auto linked=gpu::link_graphics({vs,ps});gpu::ResourceCommands resources(d.resource,budgets);
        for(unsigned first:{0u,3u})for(unsigned instance:{0u,7u}){
            GraphicsReferenceOptions options;options.first_vertex=first;options.first_instance=instance;
            LinkedGraphicsConsumer draw(d);draw.init(linked,options);
            check(draw.read(resources)==native_linked_graphics(vs,ps,options),"Translated DXBC preserves native clip w and nonzero vertex/instance IDs");}
        for(int base:{-3,0,6}){GraphicsReferenceOptions options;options.indices={77,77,3,4,5};options.first_index=2;options.base_vertex=base;
            options.instances=2;options.first_instance=9;LinkedGraphicsConsumer draw(d);draw.init(linked,options);
            check(draw.read(resources)==native_linked_graphics(vs,ps,options),"Translated DXBC indexed IDs agree with native signed base and instance offset");}
    }
    check(!budgets.resident->used() && !budgets.staging->used(),"Translated graphics allocations retire");
}
static void bytecode_resources(Device& d,const std::filesystem::path& root,gpu::DxbcFloat32Mode mode=gpu::DxbcFloat32Mode::Hardware){
    gpu::DxbcCompiler compiler(L"F:/vulkan/vkd3d-2.1-build/efmi-vkd3d-shader.dll");
    const auto bytes=bytecode_source('c',R"hlsl(cbuffer Parameters:register(b2){uint Add;};
StructuredBuffer<uint4> Input:register(t3); ByteAddressBuffer Raw:register(t4); Buffer<uint> Typed:register(t6);
RWStructuredBuffer<uint4> Output:register(u0);
[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID){Output[id.x]=Input[1-id.x]+uint4(Add,Raw.Load(id.x*4),Typed[id.x],0);})hlsl");
    gpu::ResourceBudgets budgets;auto host=shader_host(d);
    for(bool reassembled:{false,true}){auto program=translated_bytecode(compiler,reassembled?bytecode_roundtrip(bytes):bytes,'c',mode);
        check(program.bindings.size()==5,"Bytecode scan maps CBV, raw, structured, typed and UAV declarations");
        gpu::ResourceCommands resources(d.resource,budgets);gpu::ShaderCommands shaders(resources,host);auto pipeline=shaders.compile(program);
        const uint32_t cb[]={23,0,0,0},input[]={1,2,3,4,5,6,7,8},raw[]={11,13},typed[]={17,19};
        auto params=resources.create(16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,cb,16);
        auto a=resources.create(32,VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,input,32);
        auto b=resources.create(8,VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,raw,8);
        auto c=resources.create(8,VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,typed,8);
        auto output=resources.create(32,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);
        shaders.dispatch(pipeline,{buffer_descriptor('b',2,params),buffer_descriptor('t',3,a,VK_FORMAT_R32_UINT),
            buffer_descriptor('t',4,b,VK_FORMAT_R32_UINT),buffer_descriptor('t',6,c,VK_FORMAT_R32_UINT),buffer_descriptor('u',0,output)},{2,1,1});
        const uint32_t expected[]={28,17,24,8,24,15,22,4};const auto actual=resources.readback(d.queue,output,0,32);
        check(!memcmp(actual.data(),expected,32),"Typed, raw and structured bytecode addresses preserve source elements");
    }
    const auto parameter_bytes=bytecode_source('c',R"hlsl(Texture1D<float4> Parameters:register(t120); RWStructuredBuffer<uint4> Output:register(u3);
[numthreads(32,1,1)] void main(uint3 id:SV_DispatchThreadID){uint count;Parameters.GetDimensions(count);
if(id.x<count)Output[id.x]=asuint(Parameters.Load(int2(id.x,0)));})hlsl");
    auto parameter=translated_bytecode(compiler,bytecode_roundtrip(parameter_bytes),'c',mode);
    {ParameterFixture f(d,root/"parameters","[Constants]\nx = -0\ny = 0.25\nw64 = 90\n");const auto input=f.read();
        const auto expected=native_parameter_shader(parameter,input);auto& resources=f.backend->commands();gpu::ShaderCommands shaders(resources,host);
        auto pipeline=shaders.compile(parameter);auto output=resources.create(input.size(),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);
        gpu::ShaderDescriptor texture;texture.binding=gpu::HlslCompiler::binding('c','t',120);texture.image=f.backend->ini_parameters().image_view;
        shaders.dispatch(pipeline,{texture,buffer_descriptor('u',3,output)},{uint32_t((input.size()/16+31)/32),1,1});
        check(resources.readback(d.queue,output,0,input.size())==expected,"Reassembled image loads and original parameter slots match native DX11 bytes");}
    const auto append=bytecode_source('c',"AppendStructuredBuffer<uint> Output:register(u2);[numthreads(1,1,1)] void main(){Output.Append(7);}");
    {auto program=translated_bytecode(compiler,bytecode_roundtrip(append),'c',mode);gpu::ResourceCommands resources(d.resource,budgets);gpu::ShaderCommands shaders(resources,host);
        auto pipeline=shaders.compile(program);auto output=resources.create(16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);
        auto counter=resources.create(4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);auto binding=buffer_descriptor('u',2,counter);binding.binding=gpu::HlslCompiler::binding('c','u',2,true);
        shaders.dispatch(pipeline,{buffer_descriptor('u',2,output),binding},{2,1,1});const auto actual=resources.readback(d.queue,output,0,16);
        const uint32_t expected[]={7,7,0,0};check(!memcmp(actual.data(),expected,16),"Translated bytecode keeps separate append counter addressing");
        const auto count=resources.readback(d.queue,counter,0,4);uint32_t value;memcpy(&value,count.data(),4);check(value==2,"Translated counter atomics execute twice");}
    check(!budgets.resident->used() && !budgets.staging->used() && !host.objects->used(),"Bytecode resource bindings release all owners");
}
static void bytecode_float32(Device& d,const std::filesystem::path& root,gpu::DxbcFloat32Mode mode=gpu::DxbcFloat32Mode::Software){
    std::filesystem::create_directories(root);gpu::DxbcCompiler compiler(L"F:/vulkan/vkd3d-2.1-build/efmi-vkd3d-shader.dll");
    const std::vector<std::string> expressions={
        "Output[id.x]=uint4(asuint(x),asuint(-x),asuint(abs(x)),x==0?1:0);",
        "precise float a=x+x;precise float b=x*0.5;precise float c=x*2;Output[id.x]=uint4(asuint(a),asuint(b),asuint(c),x<0?1:0);",
        "Output[id.x]=uint4(x==0,x!=0,x<=0,x>=0);",
        "Output[id.x]=uint4(asuint(floor(x)),asuint(ceil(x)),asuint(trunc(x)),asuint(round(x)));",
        "float4 v=asfloat(uint4(values[id.x],values[(id.x+1)%16],values[(id.x+8)%16],values[(id.x+9)%16]));Output[id.x]=asuint(-abs(v));",
        "float4 v=asfloat(uint4(values[id.x],values[(id.x+1)%16],values[(id.x+8)%16],values[(id.x+9)%16]));precise float4 a=v*float4(0.5,2,-2,-0.5);Output[id.x]=asuint(a);",
        "Output[id.x]=uint4(asuint(x*8388608),asuint(x/2),asuint(frac(x)),asuint(saturate(x)));",
        R"hlsl(uint other[16]={0x3f000000,0xbf000000,0x3f800000,0xbf800000,0x3f7fffff,0xbf7fffff,0x3f800001,0xbf800001,
0x33000000,0xb3000000,0x7f800000,0xff800000,0x7fc12345,0xffc54321,0x7f7fffff,0xff7fffff};
float4 a=frac(asfloat(uint4(other[id.x],other[(id.x+1)%16],other[(id.x+8)%16],other[(id.x+9)%16])));
Output[id.x]=uint4(isnan(a.x)?0x7fc00000:asuint(a.x),isnan(a.y)?0x7fc00000:asuint(a.y),
isnan(a.z)?0x7fc00000:asuint(a.z),isnan(a.w)?0x7fc00000:asuint(a.w));)hlsl",
    };
    const std::string prefix=R"hlsl(RWStructuredBuffer<uint4> Output:register(u0);
[numthreads(8,1,1)] void main(uint3 id:SV_DispatchThreadID){
uint values[16]={0,1,0x3fffff,0x7fffff,0x800000,0x800001,0x810000,0x1000000,
0x80000000,0x80000001,0x803fffff,0x807fffff,0x80800000,0x80800001,0x80810000,0x81000000};
float x=asfloat(values[id.x]);)hlsl";
    gpu::ResourceBudgets budgets;auto host=shader_host(d);
    for(size_t i=0;i<expressions.size();++i){const auto text=prefix+expressions[i]+"}";write(root/(std::to_string(i)+".hlsl"),text.c_str());
        const auto bytes=bytecode_source('c',text.c_str());auto program=translated_bytecode(compiler,bytes,'c',mode);
        const auto code=frontend::SourceShaderCode::create(bytes.data(),bytes.size());
        write(root/(std::to_string(i)+".asm"),frontend::source_shader_disassembly(*code,false,false).c_str());
        {std::ofstream file(root/(std::to_string(i)+".spv"),std::ios::binary);file.write(reinterpret_cast<const char*>(program.spirv.data()),program.spirv.size()*4);}
        const auto expected=native_shared_shader(program);gpu::ResourceCommands resources(d.resource,budgets);gpu::ShaderCommands shaders(resources,host);
        auto pipeline=shaders.compile(program);auto output=resources.create(256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,nullptr,0,true);
        shaders.dispatch(pipeline,{buffer_descriptor('u',0,output)},{2,1,1});const auto actual=resources.readback(d.queue,output,0,256);
        if(actual!=expected){for(unsigned k=0;k<64;++k){uint32_t a,b;memcpy(&a,expected.data()+k*4,4);memcpy(&b,actual.data()+k*4,4);
            if(a!=b)printf("Float32 case%zu word%u native=%08x translated=%08x\n",i,k,a,b);}}
        check(actual==expected,"Float32 lowering matches native subnormal arithmetic, fractional range, signed zero and data moves");
    }
    check(!budgets.resident->used() && !budgets.staging->used() && !host.objects->used(),"Float32 lowering tests retire all GPU owners");
}
