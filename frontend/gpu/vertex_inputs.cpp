#include "vertex_inputs.h"
#define SPV_ENABLE_UTILITY_CODE
#include <spirv_reflect.h>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace efmi::gpu {
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Instruction {
    const uint32_t* words=nullptr;uint32_t count=0;SpvOp op=SpvOpNop;
    uint32_t at(uint32_t i) const {require(words && i<count,"Truncated vertex input instruction");return words[i];}
};
struct Shape {uint32_t scalar=0,count=0;};
struct Pointer {uint32_t original=0,raw=0,count=0,index=0;bool alpha=false;};
void emit(std::vector<uint32_t>& out,SpvOp op,std::initializer_list<uint32_t> operands){
    out.push_back((uint32_t(operands.size()+1)<<16)|uint32_t(op));out.insert(out.end(),operands.begin(),operands.end());
}
class Lowering {
    const HlslProgram& source;
    std::vector<Instruction> instructions,definitions;
    std::map<uint32_t,Shape> shapes;
    std::map<std::pair<uint32_t,uint32_t>,uint32_t> values,constants;
    std::map<uint32_t,uint32_t> input_pointers;
    std::map<uint32_t,Pointer> pointers;
    std::set<uint32_t> moved;
    std::vector<uint32_t> scalar_types,vector_types,pointer_types,constant_words;
    uint32_t next=0,bool_type=0;
    const Instruction& def(uint32_t id) const {require(id && id<definitions.size() && definitions[id].words,"Unknown vertex input identity");return definitions[id];}
    uint32_t id(){require(next<1024*1024,"Vertex input lowering identity budget exceeded");return next++;}
    uint32_t value_type(uint32_t scalar,uint32_t count){
        auto found=values.find({scalar,count});if(found!=values.end())return found->second;
        uint32_t element=0;if(count>1)element=value_type(scalar,1);const auto result=id();
        if(count>1)emit(vector_types,SpvOpTypeVector,{result,element,count});
        else if(scalar==1)emit(scalar_types,SpvOpTypeFloat,{result,32});
        else emit(scalar_types,SpvOpTypeInt,{result,32,scalar==3?1u:0u});
        values.emplace(std::make_pair(scalar,count),result);shapes[result]={scalar,count};return result;
    }
    uint32_t pointer_type(uint32_t value){
        const auto found=input_pointers.find(value);if(found!=input_pointers.end())return found->second;
        const auto result=id();emit(pointer_types,SpvOpTypePointer,{result,SpvStorageClassInput,value});input_pointers[value]=result;return result;
    }
    uint32_t constant(uint32_t type,uint32_t bits){
        const auto found=constants.find({type,bits});if(found!=constants.end())return found->second;
        const auto result=id();emit(constant_words,SpvOpConstant,{type,result,bits});constants[{type,bits}]=result;return result;
    }
    uint32_t boolean(){if(!bool_type){bool_type=id();emit(scalar_types,SpvOpTypeBool,{bool_type});}return bool_type;}
    uint32_t alpha_value(std::vector<uint32_t>& out,const Pointer& p,uint32_t raw){
        if(!p.alpha)return raw;
        const auto shape=shapes.at(p.raw);const auto one=constant(value_type(shape.scalar,1),shape.scalar==1?0x3f800000:1);
        const auto result=id();
        if(p.count==4)emit(out,SpvOpCompositeInsert,{p.raw,result,one,raw,3});
        else {
            require(p.count==1 && p.index,"Vertex alpha override lacks its component index");
            const auto& index=def(p.index);bool has_result=false,typed=false;SpvHasResultAndType(index.op,&has_result,&typed);
            require(has_result && typed,"Vertex component index has no scalar type");const auto type=index.at(1);const auto s=shapes.find(type);
            require(s!=shapes.end() && s->second.count==1 && s->second.scalar!=1,"Vertex component index requires a 32-bit integer");
            const auto test=id();emit(out,SpvOpIEqual,{boolean(),test,p.index,constant(type,3)});
            emit(out,SpvOpSelect,{p.raw,result,test,one,raw});
        }return result;
    }
public:
    explicit Lowering(const HlslProgram& p):source(p){
        const auto& code=p.spirv;require(p.stage==VK_SHADER_STAGE_VERTEX_BIT && code.size()>=5 && code.size()<=16*1024*1024 &&
            code[0]==SpvMagicNumber && code[3] && code[3]<1024*1024,"Vertex input lowering module budget/header invalid");
        next=code[3];definitions.resize(next);
        for(size_t at=5;at<code.size();){const auto count=code[at]>>16;require(count && count<=code.size()-at,"Truncated vertex input module");
            Instruction instruction{code.data()+at,count,SpvOp(code[at]&65535)};instructions.push_back(instruction);
            require(instructions.size()<=1000000,"Vertex input instruction budget exceeded");
            bool result=false,typed=false;SpvHasResultAndType(instruction.op,&result,&typed);
            if(result){const auto value=instruction.at(typed?2:1);require(value && value<definitions.size() && !definitions[value].words,"Duplicate vertex input identity");definitions[value]=instruction;}
            at+=count;
        }
        // Move only standalone numeric types and their Input pointers ahead of
        // globals. Reusing a type declared later must not create forward uses or
        // duplicate non-aggregate types. All other declarations keep their order.
        for(const auto& i:instructions){Shape s;std::vector<uint32_t>* group=nullptr;
            if(i.op==SpvOpTypeBool){require(!bool_type,"Duplicate boolean type");bool_type=i.at(1);group=&scalar_types;}
            else if((i.op==SpvOpTypeFloat || i.op==SpvOpTypeInt) && i.at(2)==32){s={i.op==SpvOpTypeFloat?1u:i.at(3)?3u:2u,1};group=&scalar_types;}
            else if(i.op==SpvOpTypeVector){const auto found=shapes.find(i.at(2));if(found!=shapes.end() && found->second.count==1){
                s={found->second.scalar,i.at(3)};require(s.count>=2 && s.count<=4,"Unsupported vertex numeric vector width");group=&vector_types;}}
            else if(i.op==SpvOpTypePointer && i.at(2)==SpvStorageClassInput && shapes.count(i.at(3))){
                require(input_pointers.emplace(i.at(3),i.at(1)).second,"Duplicate vertex input pointer type");group=&pointer_types;}
            if(s.count){require(values.emplace(std::make_pair(s.scalar,s.count),i.at(1)).second,"Duplicate vertex numeric type");shapes[i.at(1)]=s;}
            if(group){moved.insert(i.at(1));group->insert(group->end(),i.words,i.words+i.count);}
        }
    }
    HlslProgram run(const std::vector<VertexInputConversion>& conversions){
        require(conversions.size()<=32 && source.inputs.size()<=1024,"Vertex conversion count exceeds input budget");auto result=source;
        std::set<uint32_t> locations;
        for(const auto& conversion:conversions){
            require(conversion.scalar>=1 && conversion.scalar<=3 && locations.insert(conversion.location).second,"Invalid or duplicate vertex conversion");
            ShaderInterface* input=nullptr;for(auto& candidate:result.inputs)if(candidate.builtin==UINT32_MAX && candidate.location==conversion.location){
                require(!input,"Ambiguous vertex conversion location");input=&candidate;}
            require(input,"Vertex conversion location has no shader input");const auto& variable=def(input->spirv_id);
            require(variable.op==SpvOpVariable && variable.count==4 && variable.at(3)==SpvStorageClassInput,"Vertex conversion requires an ordinary input variable");
            const auto& type=def(variable.at(1));require(type.op==SpvOpTypePointer && type.at(2)==SpvStorageClassInput,"Vertex input pointer storage differs");
            const auto original=type.at(3);const auto shape=shapes.find(original);
            require(shape!=shapes.end(),"Vertex input conversion requires a 32-bit scalar/vector");const auto count=shape->second.count;
            require(input->format==VkFormat(VK_FORMAT_R32_UINT+(count-1)*3+(shape->second.scalar==1?2:shape->second.scalar==3?1:0)),
                "Vertex input format metadata differs from its executable type");
            const auto raw=value_type(conversion.scalar,count);pointer_type(raw);
            require(pointers.emplace(input->spirv_id,Pointer{original,raw,count,0,conversion.alpha_one && count==4}).second,"Repeated vertex conversion identity");
            input->format=VkFormat(VK_FORMAT_R32_UINT+(count-1)*3+(conversion.scalar==1?2:conversion.scalar==3?1:0));
        }
        std::vector<std::vector<uint32_t>> rewritten;rewritten.reserve(instructions.size());
        for(const auto& i:instructions){std::vector<uint32_t> words(i.words,i.words+i.count);
            if(i.op==SpvOpVariable){const auto found=pointers.find(i.at(2));if(found!=pointers.end())words[1]=pointer_type(found->second.raw);}
            else if(i.op==SpvOpAccessChain || i.op==SpvOpInBoundsAccessChain || i.op==SpvOpCopyObject){
                const auto found=pointers.find(i.at(3));if(found!=pointers.end()){auto p=found->second;
                    if(i.op!=SpvOpCopyObject){require(i.count==5 && p.count>1 && !p.index,"Vertex input access chain needs scalar component lowering");
                        p.original=value_type(shapes.at(p.original).scalar,1);p.raw=value_type(shapes.at(p.raw).scalar,1);p.count=1;p.index=i.at(4);}
                    const auto& original_pointer=def(i.at(1));require(original_pointer.op==SpvOpTypePointer && original_pointer.at(2)==SpvStorageClassInput && original_pointer.at(3)==p.original,
                        "Vertex input alias pointee differs");pointers.emplace(i.at(2),p);words[1]=pointer_type(p.raw);
                }
            }else if(i.op==SpvOpLoad){const auto found=pointers.find(i.at(3));if(found!=pointers.end()){const auto& p=found->second;
                    require(i.at(1)==p.original,"Vertex input load type differs");words[1]=p.raw;words[2]=id();
                    const auto raw=alpha_value(words,p,words[2]);emit(words,p.original==p.raw?SpvOpCopyObject:SpvOpBitcast,{p.original,i.at(2),raw});
                }
            }else if(i.op==SpvOpFunctionCall){for(uint32_t n=4;n<i.count;++n)require(!pointers.count(i.at(n)),"Vertex input pointer function arguments need lowering");}
            else if(i.op==SpvOpCopyMemory || i.op==SpvOpCopyMemorySized){require(!pointers.count(i.at(1)) && !pointers.count(i.at(2)),"Vertex input pointer copies need lowering");}
            else if(i.op==SpvOpPtrAccessChain || i.op==SpvOpInBoundsPtrAccessChain){require(!pointers.count(i.at(3)),"Vertex input pointer arithmetic needs lowering");}
            else if(i.op==SpvOpPhi || i.op==SpvOpSelect){const auto& type=def(i.at(1));require(type.op!=SpvOpTypePointer || type.at(2)!=SpvStorageClassInput,"Vertex input pointer selection needs lowering");}
            rewritten.push_back(std::move(words));
        }
        std::vector<uint32_t> code(source.spirv.begin(),source.spirv.begin()+5);bool types=false,constants_done=false;
        for(size_t n=0;n<instructions.size();++n){const auto& i=instructions[n];
            if(!types && i.op>=SpvOpTypeVoid && i.op<=SpvOpTypeForwardPointer){types=true;
                for(const auto* group:{&scalar_types,&vector_types,&pointer_types})code.insert(code.end(),group->begin(),group->end());}
            if(!constants_done && i.op==SpvOpFunction){constants_done=true;code.insert(code.end(),constant_words.begin(),constant_words.end());}
            if(i.op>=SpvOpTypeVoid && i.op<=SpvOpTypeForwardPointer && moved.count(i.at(1)))continue;
            code.insert(code.end(),rewritten[n].begin(),rewritten[n].end());require(code.size()<=16*1024*1024,"Vertex input lowering module budget exceeded");
        }
        require(types && constants_done,"Vertex input module lacks types or a function");code[3]=next;result.spirv=std::move(code);return result;
    }
};
}
HlslProgram lower_vertex_inputs(const HlslProgram& program,const std::vector<VertexInputConversion>& conversions){
    if(conversions.empty())return program;return Lowering(program).run(conversions);
}
}
