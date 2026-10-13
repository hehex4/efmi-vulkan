#include "vertex_plan.h"
#include "../material_fixture.h"
#include <cstdio>
using namespace efmi::frontend;
using namespace efmi::assets;
int HostVertexPlanTests()
{
    int passed=0,failed=0;auto check=[&](bool ok,const char* name){if(ok)++passed;else{++failed;printf("FAILED: %s\n",name);}};
    const auto code=material_fixture::shader(true);VertexProfile profile;std::string reason;
    std::vector<VertexSemantic> names={{0,0,"POSITION"},{4,4,"TEXCOORD"},{5,0,"BLENDWEIGHTS"},{6,0,"BLENDINDICES"},{3,0,"TANGENT"}};
    check(profile.prepare(code,names,reason),"vertex profile owns exact code and semantic names");
    efvk::VertexInputRecipe source;source.bindings={{0,16,VK_VERTEX_INPUT_RATE_VERTEX},{1,20,VK_VERTEX_INPUT_RATE_VERTEX},
        {2,12,VK_VERTEX_INPUT_RATE_VERTEX},{4,0,VK_VERTEX_INPUT_RATE_VERTEX}};
    source.attributes={{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{4,1,VK_FORMAT_R8G8B8A8_SNORM,16},
        {5,2,VK_FORMAT_R16G16B16A16_UNORM,0},{6,2,VK_FORMAT_R8G8B8A8_UINT,8},{3,4,VK_FORMAT_R8G8B8A8_UNORM,12}};
    FileDraws input;input.draws.emplace_back();
    auto file=[](uint32_t stride){FileBinding b;b.source=BindingSource::File;auto r=std::make_shared<FileResource>();
        r->usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;r->stride=stride;b.resource=r;return b;};
    input.draws[0].vertex[0]=file(16);input.draws[0].vertex[1]=file(20);input.draws[0].vertex[2]=file(16);
    LayoutEdit edit;edit.match.semantic_name="BLENDINDICES";edit.match.semantic_index=0;edit.match.input_slot=2;edit.replace.format=12;
    input.draws[0].layout.push_back(edit);VertexPlan output;
    auto run=[&](const FileDraws& draws,const efvk::VertexInputRecipe& vertex){return profile.resolve(draws,code,vertex,output,reason);};
    check(run(input,source) && output.draws[0].bindings[2].stride==16 && output.draws[0].attributes[3].format==VK_FORMAT_R16G16B16A16_UINT &&
          output.draws[0].attributes[3].offset==8,"original ElementFormat edits a 12-byte blend stream into the declared 16-byte file stream");
    check(output.draws[0].bindings[3].stride==0 && output.draws[0].attributes[4].offset==12 && output.draws[0].attributes[4].binding==4,
          "the application stride-zero tangent remains unchanged");
    auto narrow=source;narrow.bindings[2].stride=4;narrow.attributes.erase(narrow.attributes.begin()+2);narrow.attributes[2].offset=0;
    auto compact=input;compact.draws[0].vertex[2]=file(8);
    check(run(compact,narrow) && output.draws[0].bindings[2].stride==8 && output.draws[0].attributes[2].format==VK_FORMAT_R16G16B16A16_UINT &&
          output.draws[0].attributes[2].offset==0,"the same evaluator supports a four-byte index-only blend stream");
    auto reject=[&](const FileDraws& draws,const efvk::VertexInputRecipe& vertex,const char* name){
        check(!run(draws,vertex) && !reason.empty() && output.draws.size()==1 && output.draws[0].bindings[2].stride==8,name);
    };
    auto bad=input;bad.draws.emplace_back();bad.draws.back().vertex[2]=file(1);bad.draws.back().layout.push_back(edit);
    reject(bad,source,"a late undersized stride cannot publish earlier declaration changes");
    bad=input;bad.draws[0].vertex[2]={};reject(bad,source,"editing an inherited application stream is refused");
    bad=input;bad.draws[0].vertex[2].source=BindingSource::Null;reject(bad,source,"null file vertex streams require another adapter");
    auto invalid=source;invalid.bindings[2].inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;reject(input,invalid,"file replacements cannot silently change an instance-rate stream");
    invalid=source;invalid.attributes[3].location=99;reject(input,invalid,"missing semantic correspondence refuses");
    invalid=source;invalid.attributes.push_back(invalid.attributes[0]);reject(input,invalid,"duplicate Vulkan locations refuse before evaluating edits");
    bad=input;bad.draws[0].layout[0].replace.semantic_name="POSITION";reject(bad,source,"semantic renaming cannot reuse a stale shader location");
    bad=input;bad.draws[0].layout[0].replace.format=99999;reject(bad,source,"unknown replacement formats never reach original size arithmetic");
    bad=input;bad.draws[0].layout[0].replace.aligned_byte_offset=2049;reject(bad,source,"unbounded replacement offsets refuse");
    auto changed=code;changed[2]^=1;
    check(!profile.resolve(input,changed,source,output,reason) && output.draws[0].bindings[2].stride==8,
          "same-layout but different exact shader code cannot reuse a profile");
    auto duplicates=names;duplicates.push_back(names[0]);
    check(!profile.prepare(code,duplicates,reason) && run(compact,narrow),"invalid profile replacement preserves the previous profile");
    auto case_sensitive=input;case_sensitive.draws[0].layout[0].match.semantic_name="blendindices";
    check(run(case_sensitive,source) && output.draws[0].attributes[3].format==VK_FORMAT_R8G8B8A8_UINT,
          "original semantic matching remains case-sensitive");
    auto sequential=input;LayoutEdit second;second.match.format=12;second.replace.format=30;sequential.draws[0].layout.push_back(second);
    check(run(sequential,source) && output.draws[0].attributes[3].format==VK_FORMAT_R8G8B8A8_UINT,
          "later original layout overrides match the already modified element");
    auto alias=source;alias.attributes.push_back({7,2,VK_FORMAT_R8G8B8A8_UINT,8});names.push_back({7,1,"BLENDINDICES"});profile.prepare(code,names,reason);
    check(!run(input,alias) && reason.find("Aliased")!=std::string::npos,"different edits to aliased source bytes require a location-aware backend");
    printf("XXMI vertex plans: %d checks passed, %d failed\n",passed,failed);return failed;
}
