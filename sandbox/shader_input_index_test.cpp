#include "../frontend/xxmi/shader_input_index.h"
#include "../frontend/xxmi/shader_assembly.h"
#include "material_fixture.h"
#include <cstdio>
#include <fstream>
#include <set>
#include <d3dcompiler.h>
using namespace efmi::frontend;
namespace fs=std::filesystem;
static std::vector<uint32_t> code_at(const fs::path& path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);const auto size=file.tellg();
    if(!file || size<20 || size>16*1024*1024 || size%4)throw std::runtime_error("Invalid test shader");
    std::vector<uint32_t> code(size_t(size)/4);file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(code.data()),size))throw std::runtime_error("Cannot read test shader");return code;
}
int wmain(int argc,wchar_t** argv){
    try {
        if(argc==3 && std::wstring(argv[1])==L"--assembly"){
            std::ifstream input(argv[2],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
            const auto code=SourceShaderCode::create(bytes.data(),bytes.size());
            const auto assembly=source_shader_disassembly(*code,false,false);printf("%s",assembly.c_str());return 0;
        }
        if(argc==3 && std::wstring(argv[1])==L"--hashes"){
            std::ifstream input(argv[2],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
            const auto code=SourceShaderCode::create(bytes.data(),bytes.size());
            printf("%016llx %016llx %016llx\n",static_cast<unsigned long long>(code->hash(SourceShaderHash::Fnv)),
                static_cast<unsigned long long>(code->hash(SourceShaderHash::Embedded)),
                static_cast<unsigned long long>(code->hash(SourceShaderHash::Bytecode)));return 0;
        }
        if((argc==4 || argc==5) && std::wstring(argv[1])==L"--dxbc"){
            std::ifstream input(argv[2],std::ios::binary);std::string text((std::istreambuf_iterator<char>(input)),{});
            ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
            const std::wstring wide_model=argc==5?argv[4]:L"cs_5_0";const std::string model(wide_model.begin(),wide_model.end());
            const auto hr=D3DCompile(text.data(),text.size(),nullptr,nullptr,nullptr,"main",model.c_str(),D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
            if(errors){fprintf(stderr,"%s",static_cast<const char*>(errors->GetBufferPointer()));errors->Release();}
            if(FAILED(hr) || !code)throw std::runtime_error("Fixture DXBC compilation failed");
            std::ofstream output(argv[3],std::ios::binary);output.write(static_cast<const char*>(code->GetBufferPointer()),code->GetBufferSize());code->Release();
            return output?0:1;
        }
        if(argc==3 && (std::wstring(argv[1])==L"--fixture" || std::wstring(argv[1])==L"--sandbox-vertex")){
            const auto code=std::wstring(argv[1])==L"--fixture"?material_fixture::shader(true):spv::vertex_shader();
            std::ofstream file(argv[2],std::ios::binary);
            file.write(reinterpret_cast<const char*>(code.data()),code.size()*4);return file?0:1;
        }
        int passed=0;auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);++passed;};
        if(argc==3 && std::wstring(argv[1])==L"--sources"){
            const fs::path root=argv[2];const auto code=code_at(root/L"fixture.spv");std::string reason;
            ShaderSourceIndex index;std::shared_ptr<const SourceShaderCode> source;
            check(index.load(root/L"good.bin",reason) && index.size()==1,"Source archive loads");
            check(index.lookup(code,source,reason) && source->model()=="vs_5_0","Exact original bytecode decodes");
            const auto retained=source;
            auto changed=code;changed[2]^=1;
            check(!index.lookup(changed,source,reason) && !source && !reason.empty(),"Changed executable has no original facts");
            for(const auto& file:fs::directory_iterator(root))if(file.path().filename().wstring().find(L"bad-")==0){
                check(!index.load(file.path(),reason),"Malformed source archive refuses");
                check(index.lookup(code,source,reason) && source->bytes()==retained->bytes(),"Failed archive preserves earlier exact bytes");
            }
            check(index.load(root/L"empty.bin",reason) && !index.size(),"Empty archive replaces the lookup index");
            check(retained->model()=="vs_5_0" && !retained->bytes().empty(),"Original bytecode owner survives index replacement");
            check(index.load(root/L"corrupt.bin",reason) && !index.lookup(code,source,reason) && !source,
                  "Corrupt compressed payload cannot publish shader facts");
            printf("Shader source index: %d checks passed\n",passed);return 0;
        }
        if(argc==3 && std::wstring(argv[1])==L"--models"){
            const fs::path root=argv[2];const auto code=code_at(root/L"fixture.spv");std::string reason,model;
            ShaderModelIndex index;
            check(index.load(root/L"good.bin",reason) && index.size()==1,"Valid source model cache loads");
            check(index.lookup(code,model) && model=="vs_5_0","Exact source model identity resolves");
            auto changed=code;changed[2]^=1;
            check(!index.lookup(changed,model) && model.empty(),"Changed shader clears earlier model facts");
            check(!index.lookup({},model),"Absent source bytes cannot prove a model");
            for(const auto& file:fs::directory_iterator(root))if(file.path().filename().wstring().find(L"bad-")==0){
                check(!index.load(file.path(),reason) && !reason.empty(),"Malformed source model cache refuses");
                check(index.size()==1 && index.lookup(code,model) && model=="vs_5_0","Refusal preserves the previous complete model index");
            }
            printf("Shader model index: %d checks passed\n",passed);return 0;
        }
        if(argc==5 && std::wstring(argv[1])==L"--corpus"){
            ShaderInputIndex index;std::string reason;check(index.load(argv[2],reason),"Corpus cache loads");
            std::set<std::vector<uint32_t>> seen;size_t accepted=0;
            for(const auto& file:fs::recursive_directory_iterator(argv[3]))if(file.path().extension()==L".spv"){
                auto code=code_at(file.path());if(!seen.insert(code).second)continue;
                VertexProfile profile;accepted+=index.lookup(code,profile,reason);
            }
            printf("Input corpus: %zu accepted of %zu unique modules, %zu index entries\n",accepted,seen.size(),index.size());
            check(accepted==std::stoul(argv[4]),"Exact corpus membership count");return 0;
        }
        if(argc!=2)throw std::runtime_error("Expected fixture directory or --corpus cache modules expected");
        const fs::path root=argv[1];const auto code=code_at(root/L"fixture.spv");std::string reason;
        ShaderInputIndex index;VertexProfile profile;
        check(index.load(root/L"good.bin",reason) && index.size()==1,"Valid cache loads");
        check(index.lookup(code,profile,reason),"Exact shader identity resolves");
        auto changed=code;changed[2]^=1;
        check(!index.lookup(changed,profile,reason),"Header-only shader change has no identity");
        check(!index.lookup({},profile,reason),"Missing retained shader code refuses");
        for(const auto& file:fs::directory_iterator(root))if(file.path().filename().wstring().find(L"bad-")==0){
            check(!index.load(file.path(),reason) && !reason.empty(),"Malformed cache refuses");
            check(index.size()==1 && index.lookup(code,profile,reason),"Failure preserves the previous index");
        }
        check(!index.load(root/L"absent.bin",reason) && index.size()==1,"Missing cache preserves the previous index");
        efvk::VertexInputRecipe source;source.bindings={{0,16,VK_VERTEX_INPUT_RATE_VERTEX},{1,24,VK_VERTEX_INPUT_RATE_VERTEX},
            {2,12,VK_VERTEX_INPUT_RATE_VERTEX},{4,0,VK_VERTEX_INPUT_RATE_VERTEX}};
        source.attributes={{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{4,1,VK_FORMAT_R8G8B8A8_SNORM,16},
            {5,2,VK_FORMAT_R16G16B16A16_UNORM,0},{6,2,VK_FORMAT_R8G8B8A8_UINT,8},{3,4,VK_FORMAT_R8G8B8A8_UNORM,12}};
        efmi::assets::FileDraws draws;draws.draws.emplace_back();auto& draw=draws.draws[0];
        auto resource=std::make_shared<efmi::assets::FileResource>();resource->stride=24;resource->usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        draw.vertex[1].source=efmi::assets::BindingSource::File;draw.vertex[1].resource=resource;
        efmi::assets::LayoutEdit edit;edit.match.semantic_name="TEXCOORD";edit.match.semantic_index=4;
        edit.replace.aligned_byte_offset=20;draw.layout.push_back(edit);VertexPlan result;
        check(profile.resolve(draws,code,source,result,reason) && result.draws[0].attributes[1].offset==20 &&
              result.draws[0].attributes[4].offset==12,"Cached semantic edits the sparse location and preserves unrelated input");
        auto dependent=source;dependent.attributes[4].binding=1;dependent.attributes[4].offset=40;
        resource->stride=64;draw.layout[0].replace.aligned_byte_offset=UINT32_MAX;draw.layout[0].replace.format=16;
        check(!profile.resolve(draws,code,dependent,result,reason) && reason.find("declaration order")!=std::string::npos &&
              result.draws[0].attributes[1].offset==20,"Order-dependent realignment refuses without publishing a partial layout");
        printf("Shader input index: %d checks passed\n",passed);return 0;
    }catch(const std::exception& e){fprintf(stderr,"FAILED: %s\n",e.what());return 1;}
}
