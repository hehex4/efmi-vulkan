#include "m2_mesh.h"
#include <cmath>
#include <fstream>

namespace efvk {
namespace {
template<class T> T at(const uint8_t* p) { T v; std::memcpy(&v,p,sizeof(v));return v; }
}
bool M2Mesh::load(const std::string& path,std::string& reason)
{
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),nullptr,0);
    if(count<=0){reason="invalid UTF-8 package path";return false;}
    std::wstring wide(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),&wide[0],count);
    std::ifstream in(wide.c_str(),std::ios::binary|std::ios::ate);
    auto size=in.tellg();
    if(!in || size<56 || size>(16<<20)){reason="package missing or outside size budget";return false;}
    std::vector<uint8_t> data(static_cast<size_t>(size));in.seekg(0);in.read(reinterpret_cast<char*>(data.data()),size);
    if(!in){reason="package read failed";return false;}
    return decode(data,reason);
}
bool M2Mesh::decode(const std::vector<uint8_t>& data,std::string& reason)
{
    reason.clear();
    auto fail=[&](const char* text){reason=text;return false;};
    if(data.size()<56 || std::memcmp(data.data(),"EFMIM201",8))return fail("unsupported M2 package");
    uint32_t words[12];std::memcpy(words,data.data()+8,sizeof(words));
    M2Mesh next;
    next.hash=words[0];next.match_count=words[1];next.match_first=words[2];next.bones=words[3];
    next.component_first=words[4];next.component_count=words[5];next.vertices=words[6];next.indices=words[7];
    if(!next.match_count || next.match_count>196608 || !next.bones || next.bones>4096 || !next.component_count ||
       next.component_count>256 || uint64_t(next.component_first)+next.component_count>next.bones ||
       !next.vertices || next.vertices>65536 || !next.indices || next.indices>196608 || next.indices%3 ||
       words[8]!=16 || words[9]!=8 || words[10]!=16)return fail("unsupported M2 mesh dimensions or streams");
    uint64_t expected=56ull+uint64_t(next.vertices)*40+uint64_t(next.indices)*4;
    if(data.size()!=expected || crc32c(0,data.data()+56,data.size()-56)!=words[11])return fail("M2 payload size or checksum mismatch");
    size_t pos=56;
    auto take=[&](std::vector<uint8_t>& out,size_t n){out.assign(data.begin()+pos,data.begin()+pos+n);pos+=n;};
    take(next.position,size_t(next.vertices)*16);take(next.texcoord,size_t(next.vertices)*8);
    take(next.blend,size_t(next.vertices)*16);take(next.index,size_t(next.indices)*4);
    for(uint32_t i=0;i<next.indices;++i)
        if(at<uint32_t>(next.index.data()+i*4)>=next.vertices)return fail("M2 index exceeds vertex count");
    for(uint32_t v=0;v<next.vertices;++v){
        for(uint32_t c=0;c<3;++c)if(!std::isfinite(at<float>(next.position.data()+v*16+c*4)))return fail("nonfinite M2 position");
        for(uint32_t c=0;c<2;++c)if(!std::isfinite(at<float>(next.texcoord.data()+v*8+c*4)))return fail("nonfinite M2 texcoord");
        uint32_t weights=0;
        for(uint32_t c=0;c<4;++c){
            uint32_t weight=at<uint16_t>(next.blend.data()+v*16+c*2);
            uint32_t bone=at<uint16_t>(next.blend.data()+v*16+8+c*2);weights+=weight;
            if(bone>=next.bones || (weight && (bone<next.component_first || bone>=next.component_first+next.component_count)))
                return fail("M2 mesh depends on another component's palette");
        }
        if(weights<65533 || weights>65537)return fail("M2 weights are not normalized");
    }
    *this=std::move(next);return true;
}
bool M2Mesh::vertex_input(const VertexInputRecipe& source,VertexInputRecipe& output,std::string& reason)const
{
    auto next=source;reason.clear();
    bool bindings[4]={},weights=false,indices_attr=false;
    const uint32_t strides[]={16,8,12,16};
    for(auto& b:next.bindings){
        if(b.binding>=4)continue; // Preserve application default attributes outside the replaced streams.
        if(bindings[b.binding] || b.inputRate!=VK_VERTEX_INPUT_RATE_VERTEX || b.stride!=strides[b.binding]){
            reason="unsupported original vertex stream";return false;
        }
        bindings[b.binding]=true;if(b.binding==2)b.stride=16;
    }
    if(!bindings[0] || !bindings[1] || !bindings[2] || !bindings[3]){reason="required original streams missing";return false;}
    for(auto& a:next.attributes){
        if(a.binding!=2)continue;
        if(a.offset==0 && a.format==VK_FORMAT_R16G16B16A16_UNORM && !weights)weights=true;
        else if(a.offset==8 && a.format==VK_FORMAT_R8G8B8A8_UINT && !indices_attr){a.format=VK_FORMAT_R16G16B16A16_UINT;indices_attr=true;}
        else{reason="unsupported original blend attribute";return false;}
    }
    if(!weights || !indices_attr){reason="required blend attributes missing";return false;}
    output=std::move(next);return true;
}
} // namespace efvk
