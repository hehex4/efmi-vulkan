#include "shader_usage.h"
#define SPV_ENABLE_UTILITY_CODE
#include <include/spirv/unified1/spirv.h>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace efmi::gpu {
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Instruction {
    const uint32_t* words=nullptr;uint32_t count=0;SpvOp op=SpvOpNop;
    uint32_t at(uint32_t i) const {require(words && i<count,"Truncated shader usage instruction");return words[i];}
};
struct Reference {
    uint32_t binding,element,type;
    bool operator<(const Reference& other) const {return std::tie(binding,element,type)<std::tie(other.binding,other.element,other.type);}
};
using References=std::set<Reference>;
class Analysis {
    const HlslProgram& program;
    std::vector<Instruction> definitions,instructions;
    std::map<std::pair<uint32_t,uint32_t>,uint32_t> decorations;
    std::set<uint32_t> member_layout;
    std::map<uint32_t,const ShaderBinding*> bindings;
    std::map<uint32_t,uint64_t> sizes;
    std::set<uint32_t> sizing,tracing;
    std::map<uint32_t,References> origins;
    uint64_t operations=0,origin_entries=0;
    void step(){require(++operations<=1000000,"Shader usage analysis budget exceeded");}
    const Instruction& def(uint32_t id) const {require(id && id<definitions.size() && definitions[id].words,"Unknown shader usage id");return definitions[id];}
    uint32_t decoration(uint32_t id,SpvDecoration kind,uint32_t fallback=UINT32_MAX) const {
        auto found=decorations.find({id,uint32_t(kind)});return found==decorations.end()?fallback:found->second;
    }
    uint64_t add(uint64_t a,uint64_t b){require(a<=UINT32_MAX && b<=UINT32_MAX-a,"Shader storage size overflow");return a+b;}
    uint64_t multiply(uint64_t a,uint64_t b){require(!b || a<=UINT32_MAX/b,"Shader storage size overflow");return a*b;}
    bool constant(uint32_t id,uint32_t& value) const {
        const auto& instruction=def(id);if(instruction.op!=SpvOpConstant)return false;
        const auto& type=def(instruction.at(1));if(type.op!=SpvOpTypeInt || type.at(2)!=32)return false;
        value=instruction.at(3);return true;
    }
    uint64_t size(uint32_t id){
        step();auto cached=sizes.find(id);if(cached!=sizes.end())return cached->second;
        require(sizing.size()<128 && sizing.insert(id).second,"Recursive or excessive shader storage type");
        const auto& type=def(id);uint64_t bytes=0;
        switch(type.op){
        case SpvOpTypeBool:bytes=4;break;
        case SpvOpTypeInt:case SpvOpTypeFloat:
            require(type.at(2) && type.at(2)%8==0,"Unsupported shared scalar width");bytes=type.at(2)/8;break;
        case SpvOpTypeVector:case SpvOpTypeMatrix:bytes=multiply(size(type.at(2)),type.at(3));break;
        case SpvOpTypeArray:{uint32_t length=0;
            require(constant(type.at(3),length) && length,"Shared storage needs a fixed positive array length");
            const auto element=size(type.at(2));const auto stride=decoration(id,SpvDecorationArrayStride,uint32_t(element));
            require(stride>=element,"Shared storage array stride overlaps elements");bytes=add(multiply(stride,length-1),element);break;}
        case SpvOpTypeStruct:
            require(decoration(id,SpvDecorationBlock)==UINT32_MAX && !member_layout.count(id),"Explicit shared block layout requires an enabled adapter");
            for(uint32_t i=2;i<type.count;++i)bytes=add(bytes,size(type.at(i)));break;
        default:throw std::runtime_error("Shader shared storage type requires another adapter");
        }
        sizing.erase(id);sizes.emplace(id,bytes);return bytes;
    }
    uint32_t descriptor_count(uint32_t id,unsigned depth=0){
        step();require(depth<32,"Excessive shader descriptor array depth");const auto& type=def(id);
        if(type.op!=SpvOpTypeArray)return 1;
        uint32_t length=0;require(constant(type.at(3),length) && length,"Runtime descriptor array requires an adapter");
        const auto count=multiply(length,descriptor_count(type.at(2),depth+1));require(count<=2048,"Shader descriptor array budget exceeded");return uint32_t(count);
    }
    void merge(References& out,const References& source){
        step();require(out.size()+source.size()<=4096,"Shader resource provenance budget exceeded");out.insert(source.begin(),source.end());
    }
    References trace(uint32_t id){
        step();auto cached=origins.find(id);if(cached!=origins.end())return cached->second;
        require(tracing.size()<128 && tracing.insert(id).second,"Cyclic or excessive shader resource provenance requires an adapter");
        const auto& instruction=def(id);References result;
        switch(instruction.op){
        case SpvOpVariable:{
            require(instruction.at(3)==SpvStorageClassUniformConstant && decoration(id,SpvDecorationDescriptorSet)==0,"Shader image/sampler is not a reflected descriptor");
            const auto binding=decoration(id,SpvDecorationBinding);const auto found=bindings.find(binding);
            require(found!=bindings.end(),"Shader resource provenance missing from reflection");const auto& type=def(instruction.at(1));
            require(type.op==SpvOpTypePointer && descriptor_count(type.at(3))==found->second->count,"Shader resource array reflection differs");
            result.insert({binding,0,type.at(3)});break;}
        case SpvOpLoad:case SpvOpCopyObject:result=trace(instruction.at(3));break;
        case SpvOpSelect:merge(result,trace(instruction.at(4)));merge(result,trace(instruction.at(5)));break;
        case SpvOpPhi:for(uint32_t i=3;i<instruction.count;i+=2)merge(result,trace(instruction.at(i)));break;
        case SpvOpAccessChain:case SpvOpInBoundsAccessChain:{
            result=trace(instruction.at(3));
            for(uint32_t i=4;i<instruction.count;++i){References next;uint32_t selected=0;const bool fixed=constant(instruction.at(i),selected);
                for(const auto& reference:result){const auto& type=def(reference.type);uint32_t length=0;
                    require(type.op==SpvOpTypeArray && constant(type.at(3),length) && length,"Shader descriptor access-chain type requires an adapter");
                    require(!fixed || selected<length,"Constant descriptor array access is out of bounds");const auto stride=descriptor_count(type.at(2));
                    for(uint32_t index=fixed?selected:0;index<(fixed?selected+1:length);++index){step();
                        require(next.size()<4096,"Shader resource provenance budget exceeded");next.insert({reference.binding,reference.element+index*stride,type.at(2)});}
                }
                result=std::move(next);
            }break;}
        default:throw std::runtime_error("Shader image/sampler provenance requires another adapter");
        }
        require(!result.empty(),"Shader resource provenance is empty");tracing.erase(id);origin_entries+=result.size();
        require(origin_entries<=65536,"Shader provenance cache budget exceeded");origins.emplace(id,result);return result;
    }
public:
    explicit Analysis(const HlslProgram& p):program(p){
        const auto& code=p.spirv;require(code.size()>=5 && code[0]==SpvMagicNumber && code.size()<=16*1024*1024 &&
            code[3] && code[3]<=1024*1024,"Shader usage module budget/header invalid");definitions.resize(code[3]);
        for(size_t at=5;at<code.size();){const auto count=code[at]>>16;require(count && count<=code.size()-at,"Truncated shader usage module");
            Instruction instruction{code.data()+at,count,SpvOp(code[at]&65535)};instructions.push_back(instruction);
            bool result=false,typed=false;SpvHasResultAndType(instruction.op,&result,&typed);
            if(result){const auto id=instruction.at(typed?2:1);require(id && id<definitions.size() && !definitions[id].words,"Invalid shader result identity");definitions[id]=instruction;}
            if(instruction.op==SpvOpDecorate){const auto kind=instruction.at(2);decorations[{instruction.at(1),kind}]=count>3?instruction.at(3):0;}
            if(instruction.op==SpvOpMemberDecorate && (instruction.at(3)==SpvDecorationOffset || instruction.at(3)==SpvDecorationMatrixStride))member_layout.insert(instruction.at(1));
            at+=count;
        }
        for(const auto& binding:p.bindings)require(bindings.emplace(binding.binding,&binding).second,"Repeated shader reflection binding");
    }
    ShaderUsage run(){
        ShaderUsage usage;std::set<std::tuple<uint32_t,uint32_t,uint32_t,uint32_t>> pairs;
        std::map<std::pair<uint32_t,uint32_t>,StorageImageUse> storage;
        for(const auto& instruction:instructions){step();
            const bool reads=instruction.op==SpvOpImageRead,writes=instruction.op==SpvOpImageWrite,atomic=instruction.op==SpvOpImageTexelPointer;
            if(reads || writes || atomic){const auto refs=trace(instruction.at(writes?1:3));
                for(const auto& ref:refs){const auto binding=bindings.at(ref.binding);
                    require(def(ref.type).op==SpvOpTypeImage && (binding->type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
                        binding->type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER),"Storage image operation lost its descriptor provenance");
                    auto& use=storage[{ref.binding,ref.element}];use.binding=ref.binding;use.element=ref.element;
                    use.read|=reads || atomic;use.write|=writes || atomic;use.atomic|=atomic;
                }
            }
            if(instruction.op==SpvOpVariable && instruction.at(3)==SpvStorageClassWorkgroup){
                require(instruction.count==4 && decoration(instruction.at(2),SpvDecorationAliased)==UINT32_MAX,"Initialized/aliased workgroup storage requires an enabled adapter");
                const auto& pointer=def(instruction.at(1));require(pointer.op==SpvOpTypePointer,"Shared storage has no pointer type");
                usage.workgroup_bytes=add(usage.workgroup_bytes,size(pointer.at(3)));
            }
            if(instruction.op==SpvOpSampledImage){const auto images=trace(instruction.at(3)),samplers=trace(instruction.at(4));
                for(const auto& image:images)for(const auto& sampler:samplers){step();
                    require(def(image.type).op==SpvOpTypeImage && def(sampler.type).op==SpvOpTypeSampler &&
                        bindings.at(image.binding)->type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && bindings.at(sampler.binding)->type==VK_DESCRIPTOR_TYPE_SAMPLER,
                        "Shader sampled pair does not resolve to an image and sampler");
                    require(pairs.size()<65536,"Shader sampled-pair budget exceeded");pairs.emplace(image.binding,image.element,sampler.binding,sampler.element);
                }
            }
        }
        for(const auto& pair:pairs)usage.sampled_pairs.push_back({std::get<0>(pair),std::get<1>(pair),std::get<2>(pair),std::get<3>(pair)});
        for(const auto& entry:storage)usage.storage_images.push_back(entry.second);
        return usage;
    }
};
}
ShaderUsage shader_usage(const HlslProgram& program){return Analysis(program).run();}
}
