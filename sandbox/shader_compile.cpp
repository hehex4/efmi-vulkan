#include "../frontend/gpu/hlsl_compiler.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <map>
using namespace efmi::gpu;
static unsigned checks=0,compiled=0;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);++checks;}
static void write(const std::filesystem::path& path,const std::string& text){std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<text;if(!file)throw std::runtime_error("Write shader fixture");}
static const char* compute=R"hlsl(#ifndef COMPUTE_SHADER
#error Missing original stage macro
#endif
Texture1D<float4> Parameters : register(t120);
RWStructuredBuffer<uint4> Output : register(u3);
[numthreads(32,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint count; Parameters.GetDimensions(count);
    if (id.x < count) Output[id.x] = asuint(Parameters.Load(int2(id.x,0)));
}
)hlsl";
static HlslProgram compile(HlslCompiler& compiler,HlslRequest request,const std::filesystem::path& out){
    HlslProgram program;std::string why;if(!compiler.compile(request,program,why))throw std::runtime_error("flags="+std::to_string(request.flags)+": "+why);++checks;++compiled;
    std::ofstream file(out/(std::to_string(compiled)+(program.vulkan_version==VK_API_VERSION_1_2?"-vk12.spv":".spv")),std::ios::binary);file.write(reinterpret_cast<const char*>(program.spirv.data()),program.spirv.size()*4);
    check(bool(file),"Export shader for independent SPIR-V validation");return program;
}
int wmain(int argc,wchar_t** argv){try{
    check(argc==3,"Shader fixture and compiler locations supplied");const auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
    HlslCompiler compiler(argv[2]);HlslRequest request;request.source=root/"compute.hlsl";request.working_directory=root;
    write(request.source,compute);auto program=compile(compiler,request,root);
    check(program.stage==VK_SHADER_STAGE_COMPUTE_BIT && program.threads[0]==32 && program.threads[1]==1 && program.threads[2]==1,"Compute stage and local dimensions reflected");
    check(program.bindings.size()==2 && !program.dxbc.empty() && program.dependencies.size()==1,"Original reference, dependencies and live bindings retained");
    for(const auto& b:program.bindings){if(b.name=="Parameters")check(b.source_type=='t' && b.slot==120 && b.type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE &&
        b.binding==HlslCompiler::binding('c','t',120),"Parameter image retains original slot");
        else if(b.name=="Output")check(b.source_type=='u' && b.slot==3 && b.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && b.stride==16 &&
        b.binding==HlslCompiler::binding('c','u',3),"Structured UAV retains original slot and stride");else throw std::runtime_error("Unexpected reflected binding");}
    const uint32_t flags[]={0,1u<<0,1u<<1,1u<<2,1u<<3,1u<<4,1u<<5,1u<<6,1u<<7,1u<<8,1u<<9,1u<<10,1u<<11,
        1u<<12,1u<<13,1u<<14,(1u<<14)|(1u<<15),1u<<15,1u<<18,1u<<19,1u<<20,1u<<21};
    for(auto flag:flags){request.flags=flag;request.vulkan_version=flag==(1u<<13)?VK_API_VERSION_1_2:VK_API_VERSION_1_1;
        if(flag==(1u<<6) || flag==(1u<<7) || flag==(1u<<8)){HlslProgram rejected;std::string why;
            check(!compiler.compile(request,rejected,why) && why.find("Invalid flags")!=std::string::npos,"Original shader-model-5 compiler rejects obsolete software/preshader flags");continue;}
        if(flag==(1u<<12)){HlslProgram rejected;std::string why;
            check(!compiler.compile(request,rejected,why) && why.find("-Gec is not supported with -spirv")!=std::string::npos,
                  "Legacy backwards-compatibility flag remains an explicit DXC execution gap");continue;}
        auto result=compile(compiler,request,root);check(result.bindings.size()==2,"Valid original compiler flag has a reflected Vulkan result");}
    request.flags=1u<<15;request.vulkan_version=VK_API_VERSION_1_1;
    const std::map<char,std::string> graphics={
        {'v',R"hlsl(Texture1D<float4> Parameters : register(t120);
float4 main(uint id : SV_VertexID) : SV_Position {return Parameters.Load(int2(id,0));})hlsl"},
        {'p',R"hlsl(Texture2D<float4> Image : register(t0); SamplerState Linear : register(s0);
float4 main(float2 uv : TEXCOORD0) : SV_Target {return Image.Sample(Linear,uv);})hlsl"},
        {'h',R"hlsl(struct V {float4 position : SV_Position;};
struct C {float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor;};
C Patch(InputPatch<V,3> p){C c; c.edge[0]=c.edge[1]=c.edge[2]=c.inside=1; return c;}
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(3)][patchconstantfunc("Patch")]
V main(InputPatch<V,3> p,uint id : SV_OutputControlPointID){return p[id];})hlsl"},
        {'d',R"hlsl(struct V {float4 position : SV_Position;};
struct C {float edge[3] : SV_TessFactor; float inside : SV_InsideTessFactor;};
[domain("tri")] V main(C c,float3 b : SV_DomainLocation,const OutputPatch<V,3> p){V v;v.position=p[0].position*b.x+p[1].position*b.y+p[2].position*b.z;return v;})hlsl"},
        {'g',R"hlsl(struct V {float4 position : SV_Position;};
[maxvertexcount(3)] void main(triangle V p[3],inout TriangleStream<V> output){output.Append(p[0]);output.Append(p[1]);output.Append(p[2]);})hlsl"}
    };
    for(const auto& item:graphics){request.stage=item.first;write(request.source,item.second);auto result=compile(compiler,request,root);
        check(!result.outputs.empty(),"Original graphics stage has reflected outputs");
        if(item.first=='p'){bool semantic=false;for(const auto& v:result.inputs)if(v.semantic=="TEXCOORD0")semantic=true;
            check(semantic,"Original HLSL semantics retained separately from executable annotations");}}
    request.stage='c';
    write(root/"shared.hlsl","#define VALUE 17\n");write(root/"nested/shared.hlsl","#define VALUE 29\n");write(root/"nested/entry.hlsl","#include \"shared.hlsl\"\n");
    write(request.source,"#include \"nested/entry.hlsl\"\nRWStructuredBuffer<uint> Out : register(u0);\n[numthreads(1,1,1)] void main(){Out[0] = VALUE;}\n");
    for(int mode:{0,1,-1}){request.recursive_include=mode;auto result=compile(compiler,request,root);
        check(result.dependencies.size()==3 && result.dependencies.back().path==(mode==1?root/"nested/shared.hlsl":root/"shared.hlsl"),"Original recursive include policy selects the intended source file");}
    request.recursive_include=0;
    write(root/"empty.hlsl","");
    write(request.source,"#include \"empty.hlsl\"\n"+std::string(compute));
    const D3D_SHADER_MACRO macro[]={{"COMPUTE_SHADER",""},{nullptr,nullptr}};
    Microsoft::WRL::ComPtr<ID3DBlob> native_empty,empty_errors;
    check(SUCCEEDED(D3DCompileFromFile(request.source.c_str(),macro,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_0",request.flags,0,&native_empty,&empty_errors)),
          "Original native compilation accepts an empty include");
    program=compile(compiler,request,root);
    check(program.dependencies.size()==2 && program.dependencies.back().bytes.empty(),"Empty include compiles while dependency bytes remain exact");
    request.source=root/"sub/entry.hlsl";
    write(root/"fallback.hlsl","#define VALUE 41\n");write(root/"empty.hlsl","");
    write(request.source,"#include \"empty.hlsl\"\n#include \"fallback.hlsl\"\nRWStructuredBuffer<uint> Out : register(u0);\n[numthreads(1,1,1)] void main(){Out[0] = VALUE;}\n");
    program=compile(compiler,request,root);
    check(program.dependencies.size()==3 && program.dependencies[1].bytes.empty() && program.dependencies.back().path==root/"fallback.hlsl",
          "Nonrecursive includes preserve empty files and explicit working-directory fallback");
    request.recursive_include=1;std::string refusal;
    check(!compiler.compile(request,program,refusal),"Recursive includes do not add legacy working-directory fallback");
    request.recursive_include=0;request.source=root/"compute.hlsl";
    write(request.source,"#include \"compute.hlsl\"\n");
    check(!compiler.compile(request,program,refusal),"Cyclic include graph refuses within the source budget");
    write(request.source,R"hlsl(Texture2D<float4> Unused;
Texture2D<float4> Automatic;
Texture2D<float4> Images[3] : register(t9);
RWStructuredBuffer<float4> Out;
[numthreads(1,1,1)] void main(){Out[0]=Automatic.Load(int3(0,0,0))+Images[2].Load(int3(0,0,0));}
)hlsl");
    program=compile(compiler,request,root);bool automatic=false,sparse=false;
    for(const auto& b:program.bindings){if(b.name=="Automatic")automatic=b.slot==0 && b.source_type=='t';
        if(b.name=="Images")sparse=b.slot==9 && b.count==3;}
    check(automatic && sparse,"Original automatic registers and unused leading resource-array elements retain source slots");
    write(request.source,R"hlsl(ByteAddressBuffer Raw : register(t7);
Buffer<uint> Typed : register(t8);
RWByteAddressBuffer Output : register(u4);
RWBuffer<uint> Writable : register(u5);
[numthreads(1,1,1)] void main(){Output.Store(0,Raw.Load(0)); Writable[0]=Typed[0];}
)hlsl");
    program=compile(compiler,request,root);bool raw=false,typed=false,writable=false;
    for(const auto& b:program.bindings){if(b.name=="Raw")raw=b.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && b.slot==7;
        if(b.name=="Typed")typed=b.type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER && b.slot==8;
        if(b.name=="Writable")writable=b.type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER && b.slot==5;}
    check(raw && typed && writable,"Raw buffers and typed texel buffers preserve distinct descriptor contracts");
    write(request.source,R"hlsl(Texture2D<float4> Images[2] : register(t5);
SamplerState Linear : register(s2);
cbuffer Values : register(b3) {float4 Tint;};
RWStructuredBuffer<float4> Out : register(u1);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID){Out[id.x]=(Images[0].SampleLevel(Linear,float2(.5,.5),0)+Images[1].SampleLevel(Linear,float2(.5,.5),0))*Tint;}
)hlsl");
    program=compile(compiler,request,root);check(program.bindings.size()==4,"Texture array, sampler, constant and storage buffer coexist");
    bool array=false,constant=false;for(const auto& b:program.bindings){if(b.name=="Images")array=b.slot==5 && b.count==2;
        if(b.name=="Values")constant=b.slot==3 && b.block_size==16;}check(array && constant,"Original array register range and constant block size");
    write(request.source,"AppendStructuredBuffer<uint> Out : register(u2);\n[numthreads(1,1,1)] void main(){Out.Append(7);}\n");
    program=compile(compiler,request,root);bool counter=false;for(const auto& b:program.bindings)if(b.counter)counter=b.slot==2 && b.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    check(program.bindings.size()==2 && counter,"Append UAV counter has separate descriptor tied to its source slot");
    write(request.source,"this is not HLSL");std::string why;const auto old=program.spirv;
    check(!compiler.compile(request,program,why) && !why.empty() && program.spirv==old,"Rejected compiler candidate preserves existing program");
    write(request.source,"#include \"missing.hlsl\"\n");check(!compiler.compile(request,program,why) && why.find("preprocessing")!=std::string::npos,"Missing include reports source failure");
    request.source=root/"absent.hlsl";check(!compiler.compile(request,program,why),"Missing root shader refuses");
    request.source=root/"compute.hlsl";write(request.source,compute);request.flags=(1u<<3)|(1u<<4);
    check(!compiler.compile(request,program,why),"Original compiler rejects conflicting matrix packing flags");
    request.flags=1u<<12;
    write(request.source,"RWStructuredBuffer<float> Out; [numthreads(1,1,1)] void main(){float v=float4(1,2,3,4);Out[0]=v;}\n");
    check(!compiler.compile(request,program,why) && why.find("warning")!=std::string::npos && why.find("-Gec is not supported with -spirv")!=std::string::npos,
          "A native warning cannot hide the following SPIR-V error behind a NUL terminator");
    printf("shader_compile: %u checks passed; %u modules compiled\n",checks,compiled);return 0;
}catch(const std::exception& e){printf("FAIL after %u checks, %u modules: %s\n",checks,compiled,e.what());return 1;}}
