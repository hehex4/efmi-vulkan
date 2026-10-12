// Bounded package decoding and declaration checks for the explicit first M2 component.
#include "../layer/src/m2_mesh.h"
#include <cstdio>
#include <limits>

using namespace efvk;
static int passed=0,failed=0;
static void check(bool ok,const char* text){if(ok)++passed;else{++failed;printf("FAIL: %s\n",text);}}
template<class T> static void put(std::vector<uint8_t>& data,size_t offset,T value)
{std::memcpy(data.data()+offset,&value,sizeof(value));}
static void checksum(std::vector<uint8_t>& data){put(data,52,crc32c(0,data.data()+56,data.size()-56));}
static std::vector<uint8_t> package()
{
    std::vector<uint8_t> data(56+3*40+3*4,0);
    std::memcpy(data.data(),"EFMIM201",8);
    uint32_t header[]={0x12345678,3,0,499,266,12,3,3,16,8,16,0};
    std::memcpy(data.data()+8,header,sizeof(header));
    for(uint32_t i=0;i<3;++i){
        put(data,56+i*16,float(i));
        put(data,56+3*24+i*16,uint16_t(65535));
        put(data,56+3*24+i*16+8,uint16_t(266+i));
        put(data,56+3*40+i*4,i);
    }
    checksum(data);return data;
}

static void stream_contract_tests()
{
    // Three actual families of UV layouts and both packed source bone layouts, expressed
    // entirely as data. Use different binding numbers to detect a hidden component convention.
    for(uint32_t uv_stride: {8u,12u,20u})for(uint32_t bone_stride: {4u,12u}){
        VertexInputRecipe source;
        source.bindings={{5,uv_stride,VK_VERTEX_INPUT_RATE_VERTEX},{7,bone_stride,VK_VERTEX_INPUT_RATE_VERTEX},
                         {9,0,VK_VERTEX_INPUT_RATE_VERTEX}};
        source.attributes={{0,5,VK_FORMAT_R32G32_SFLOAT,0},{1,7,VK_FORMAT_R8G8B8A8_UINT,bone_stride-4},
                           {2,9,VK_FORMAT_R8G8B8A8_UNORM,0}};
        VertexStreamPatch uv={5,uv_stride,uv_stride,{{0,VK_FORMAT_R32G32_SFLOAT,0,VK_FORMAT_R32G32_SFLOAT}}};
        if(uv_stride>8){source.attributes.push_back({3,5,VK_FORMAT_R8G8B8A8_UNORM,8});
            uv.elements.push_back({8,VK_FORMAT_R8G8B8A8_UNORM,8,VK_FORMAT_R8G8B8A8_UNORM});}
        if(uv_stride>12){source.attributes.push_back({4,5,VK_FORMAT_R32G32_SFLOAT,12});
            uv.elements.push_back({12,VK_FORMAT_R32G32_SFLOAT,12,VK_FORMAT_R32G32_SFLOAT});}
        VertexStreamPatch blend={7,bone_stride,16,{{bone_stride-4,VK_FORMAT_R8G8B8A8_UINT,8,VK_FORMAT_R16G16B16A16_UINT}}};
        if(bone_stride==12){source.attributes.push_back({5,7,VK_FORMAT_R16G16B16A16_UNORM,0});
            blend.elements.push_back({0,VK_FORMAT_R16G16B16A16_UNORM,0,VK_FORMAT_R16G16B16A16_UNORM});}
        VertexInputRecipe result;std::string reason;
        check(patch_vertex_streams(source,{uv,blend},result,reason) && result.bindings[0].stride==uv_stride &&
              result.bindings[1].stride==16 && result.attributes[1].offset==8 &&
              result.attributes[1].format==VK_FORMAT_R16G16B16A16_UINT && result.bindings[2].stride==0 &&
              result.attributes[2].binding==9,"declaration families share the same data-driven editor");
    }
    VertexInputRecipe source;
    source.bindings={{6,4,VK_VERTEX_INPUT_RATE_VERTEX}};
    source.attributes={{12,6,VK_FORMAT_R8G8B8A8_UINT,0}};
    VertexStreamPatch valid={6,4,16,{{0,VK_FORMAT_R8G8B8A8_UINT,8,VK_FORMAT_R16G16B16A16_UINT}}};
    VertexInputRecipe result=source;std::string reason;
    auto refuse=[&](const VertexInputRecipe& input,const std::vector<VertexStreamPatch>& patches,const char* text){
        check(!patch_vertex_streams(input,patches,result,reason) && !reason.empty() && result.bindings[0].stride==4 &&
              result.attributes[0].offset==0,text);
    };
    auto wrong=valid;wrong.stride=12;refuse(source,{wrong},"widened element past end rejected atomically");
    wrong=valid;wrong.elements[0].offset=UINT32_MAX;refuse(source,{wrong},"overflowing offset rejected");
    wrong=valid;wrong.elements[0].format=VK_FORMAT_BC1_RGB_UNORM_BLOCK;refuse(source,{wrong},"texture block is not a vertex format");
    wrong=valid;wrong.elements.clear();refuse(source,{wrong},"uncovered source element rejected");
    wrong=valid;wrong.elements.push_back(wrong.elements[0]);refuse(source,{wrong},"ambiguous source element rejected");
    wrong=valid;wrong.elements[0].source_offset=1;refuse(source,{wrong},"incorrect original declaration rejected");
    wrong=valid;wrong.source_stride=12;refuse(source,{wrong},"wrong stream contract rejected");
    wrong=valid;wrong.binding=3;refuse(source,{wrong},"missing stream rejected");
    refuse(source,{valid,valid},"duplicate patch rejected before publication");
    auto bad=source;bad.attributes.push_back(source.attributes[0]);refuse(bad,{valid},"duplicate shader location rejected");
    bad=source;bad.attributes[0].binding=3;refuse(bad,{valid},"unbound attribute rejected");
    bad=source;bad.bindings[0].inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;refuse(bad,{valid},"per-instance stream is not treated as vertex data");
    check(patch_vertex_streams(source,{valid},result,reason) && result.attributes[0].location==12 &&
          result.bindings[0].binding==6 && source.bindings[0].stride==4,"format/offset rewrite preserves shader location and original recipe");
}
int main(int argc,char** argv)
{
    SetEnvironmentVariableW(L"EFMI_VK_LOG",L"mesh-test.log");
    util_init(GetModuleHandleW(nullptr));
    M2Mesh mesh;std::string reason;
    if(argc==3 && !strcmp(argv[1],"--inspect")){
        if(!mesh.load(argv[2],reason)){fprintf(stderr,"package rejected: %s\n",reason.c_str());return 1;}
        printf("{\"hash\":\"%08x\",\"match_count\":%u,\"match_first\":%u,\"bones\":%u,"
               "\"component_first\":%u,\"component_count\":%u,\"vertices\":%u,\"indices\":%u}\n",
               mesh.hash,mesh.match_count,mesh.match_first,mesh.bones,mesh.component_first,mesh.component_count,mesh.vertices,mesh.indices);
        return 0;
    }
    const auto valid=package();
    check(mesh.decode(valid,reason) && mesh.vertices==3 && mesh.blend.size()==48,"valid independently laid out package accepted");
    const auto original_blend=mesh.blend;
    auto reject=[&](const std::vector<uint8_t>& data,const char* text){
        check(!mesh.decode(data,reason) && !reason.empty() && mesh.hash==0x12345678 && mesh.blend==original_blend,text);
    };
    reject({},"empty package rejected atomically");
    auto bad=valid;bad[0]='X';reject(bad,"foreign magic rejected atomically");
    bad=valid;bad.pop_back();reject(bad,"truncated payload rejected atomically");
    bad=valid;bad.push_back(0);reject(bad,"trailing payload rejected atomically");
    bad=valid;bad[56]^=1;reject(bad,"corrupt checksum rejected atomically");
    bad=valid;put(bad,20,uint32_t(4097));reject(bad,"bone budget enforced");
    bad=valid;put(bad,24,uint32_t(0xffffffff));reject(bad,"overflowing component range rejected");
    bad=valid;put(bad,28,uint32_t(257));reject(bad,"component source-window budget enforced");
    bad=valid;put(bad,32,uint32_t(0));reject(bad,"empty vertex stream rejected");
    bad=valid;put(bad,36,uint32_t(2));reject(bad,"non-triangle index count rejected");
    bad=valid;put(bad,48,uint32_t(12));reject(bad,"old blend stride rejected");
    bad=valid;put(bad,56+3*40,uint32_t(3));checksum(bad);reject(bad,"out-of-bounds vertex index rejected");
    bad=valid;put(bad,56,std::numeric_limits<float>::quiet_NaN());checksum(bad);reject(bad,"nonfinite position rejected");
    bad=valid;put(bad,56+3*16,std::numeric_limits<float>::infinity());checksum(bad);reject(bad,"nonfinite UV rejected");
    bad=valid;put(bad,56+3*24+8,uint16_t(265));checksum(bad);reject(bad,"weighted cross-component dependency rejected");
    bad=valid;put(bad,56+3*24+10,uint16_t(499));checksum(bad);reject(bad,"zero-weight out-of-bounds bone rejected");
    bad=valid;put(bad,56+3*24,uint16_t(30000));checksum(bad);reject(bad,"unnormalized weights rejected");
    check(!mesh.load("missing-m2-package.bin",reason) && mesh.blend==original_blend,"missing file preserves loaded package");
    check(!mesh.load(std::string("\xff"),reason) && mesh.blend==original_blend,"invalid UTF-8 path preserves loaded package");

    VertexInputRecipe source;
    source.bindings={{0,16,VK_VERTEX_INPUT_RATE_VERTEX},{1,8,VK_VERTEX_INPUT_RATE_VERTEX},
                     {2,12,VK_VERTEX_INPUT_RATE_VERTEX},{3,16,VK_VERTEX_INPUT_RATE_VERTEX},{4,0,VK_VERTEX_INPUT_RATE_VERTEX}};
    source.attributes={{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,1,VK_FORMAT_R32G32_SFLOAT,0},
                       {2,2,VK_FORMAT_R16G16B16A16_UNORM,0},{3,2,VK_FORMAT_R8G8B8A8_UINT,8},
                       {4,4,VK_FORMAT_R8G8B8A8_UNORM,0}};
    VertexInputRecipe output;
    check(mesh.vertex_input(source,output,reason) && output.bindings[2].stride==16 &&
          output.attributes[3].format==VK_FORMAT_R16G16B16A16_UINT && source.bindings[2].stride==12,
          "blend indices widen without mutating source recipe");
    check(output.bindings.size()==5 && output.bindings[4].stride==0 && output.attributes[4].binding==4,
          "application fifth default stream retained unchanged");
    auto refuse=[&](VertexInputRecipe changed,const char* text){
        check(!mesh.vertex_input(changed,output,reason) && !reason.empty() && output.bindings[2].stride==16,text);
    };
    auto wrong=source;wrong.bindings.erase(wrong.bindings.begin()+3);refuse(wrong,"missing required stream rejected without output mutation");
    wrong=source;wrong.bindings[2].inputRate=VK_VERTEX_INPUT_RATE_INSTANCE;refuse(wrong,"instanced blend stream rejected");
    wrong=source;wrong.bindings[1].stride=12;refuse(wrong,"different texcoord layout rejected");
    wrong=source;wrong.bindings.push_back(source.bindings[2]);refuse(wrong,"duplicate blend binding rejected");
    wrong=source;wrong.attributes[3].format=VK_FORMAT_R8G8B8A8_UNORM;refuse(wrong,"noninteger blend indices rejected");
    wrong=source;wrong.attributes[3].offset=4;refuse(wrong,"different blend offset rejected");
    wrong=source;wrong.attributes.erase(wrong.attributes.begin()+2);refuse(wrong,"missing weight attribute rejected");
    wrong=source;wrong.attributes.push_back({5,2,VK_FORMAT_R32_SFLOAT,0});refuse(wrong,"unknown blend attribute rejected");
    stream_contract_tests();
    printf("M2 mesh package: %d checks passed, %d failed\n",passed,failed);return failed?1:0;
}
