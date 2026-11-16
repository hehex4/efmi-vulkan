#include "../frontend/xxmi/shader_input_index.h"
#include "material_fixture.h"
#include <cstdio>
#include <fstream>
#include <set>
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
        if(argc==3 && std::wstring(argv[1])==L"--fixture"){
            const auto code=material_fixture::shader(true);std::ofstream file(argv[2],std::ios::binary);
            file.write(reinterpret_cast<const char*>(code.data()),code.size()*4);return file?0:1;
        }
        int passed=0;auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);++passed;};
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
