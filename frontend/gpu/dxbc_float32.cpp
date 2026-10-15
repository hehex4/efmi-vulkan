#include "dxbc_float32.h"
#define SPV_ENABLE_UTILITY_CODE
#include <spirv_reflect.h>
#include <spirv/unified1/GLSL.std.450.h>
#include <map>
#include <set>
#include <stdexcept>
#include <cstring>

namespace efmi::gpu {
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Instruction {
    const uint32_t* data;uint32_t size;SpvOp op;
    uint32_t at(unsigned i) const {require(i<size,"Truncated float32 instruction");return data[i];}
};
struct Shape {unsigned kind=0,count=0;}; // 1 float32, 2 uint32, 3 bool.
void emit(std::vector<uint32_t>& words,SpvOp op,std::initializer_list<uint32_t> args){
    words.push_back((uint32_t(args.size()+1)<<16)|uint32_t(op));words.insert(words.end(),args.begin(),args.end());
}
class Lowering {
    const std::vector<uint32_t>& source;
    bool software_flush;
    std::vector<Instruction> instructions;
    std::vector<uint32_t> value_types;
    std::map<uint32_t,Shape> shapes;
    std::map<std::pair<unsigned,unsigned>,uint32_t> types;
    std::map<std::pair<uint32_t,uint32_t>,uint32_t> constants;
    std::map<uint32_t,uint32_t> operation_results;
    std::set<uint32_t> glsl;
    std::vector<uint32_t> declarations,body;
    uint32_t next=0;size_t first_function=0;
    uint32_t id(){require(next<1024*1024,"Float32 lowering identity budget exceeded");return next++;}
    uint32_t type(unsigned kind,unsigned count){
        const auto key=std::make_pair(kind,count);auto found=types.find(key);if(found!=types.end())return found->second;
        const auto scalar=count==1?0:type(kind,1);const auto result=id();
        if(count>1)emit(declarations,SpvOpTypeVector,{result,scalar,count});
        else if(kind==2)emit(declarations,SpvOpTypeInt,{result,32,0});
        else{require(kind==3,"Unexpected float32 helper type");emit(declarations,SpvOpTypeBool,{result});}
        types.emplace(key,result);shapes.emplace(result,Shape{kind,count});return result;
    }
    uint32_t constant(uint32_t t,uint32_t bits){
        const auto key=std::make_pair(t,bits);auto found=constants.find(key);if(found!=constants.end())return found->second;
        const auto shape=shapes.at(t);require(shape.kind==1 || shape.kind==2,"Float32 constant requires numeric bits");
        const auto scalar=shape.count==1?0:constant(type(shape.kind,1),bits);const auto result=id();
        if(shape.count==1)emit(declarations,SpvOpConstant,{t,result,bits});
        else{declarations.push_back(((3+shape.count)<<16)|SpvOpConstantComposite);declarations.push_back(t);declarations.push_back(result);
            for(unsigned i=0;i<shape.count;++i)declarations.push_back(scalar);}
        constants.emplace(key,result);return result;
    }
    uint32_t flush(uint32_t value,uint32_t t,uint32_t result=0){
        const auto shape=shapes.at(t);require(shape.kind==1 && shape.count && shape.count<=4,"Unsupported float32 arithmetic shape");
        const auto u=type(2,shape.count),b=type(3,shape.count);const auto raw=id(),exponent=id(),denormal=id(),sign=id(),selected=id();
        emit(body,SpvOpBitcast,{u,raw,value});
        emit(body,SpvOpBitwiseAnd,{u,exponent,raw,constant(u,0x7f800000)});
        emit(body,SpvOpIEqual,{b,denormal,exponent,constant(u,0)});
        emit(body,SpvOpBitwiseAnd,{u,sign,raw,constant(u,0x80000000)});
        emit(body,SpvOpSelect,{u,selected,denormal,sign,raw});
        if(!result)result=id();emit(body,SpvOpBitcast,{t,result,selected});return result;
    }
    bool floating(uint32_t t) const {const auto found=shapes.find(t);return found!=shapes.end() && found->second.kind==1;}
    uint32_t operand_type(uint32_t value) const {require(value && value<value_types.size(),"Unknown float32 operand identity");return value_types[value];}
    static bool extended(unsigned op){
        switch(op){
        case GLSLstd450Round:case GLSLstd450RoundEven:case GLSLstd450Trunc:case GLSLstd450FAbs:case GLSLstd450FSign:
        case GLSLstd450Floor:case GLSLstd450Ceil:case GLSLstd450Fract:case GLSLstd450Radians:case GLSLstd450Degrees:
        case GLSLstd450Sin:case GLSLstd450Cos:case GLSLstd450Tan:case GLSLstd450Asin:case GLSLstd450Acos:case GLSLstd450Atan:
        case GLSLstd450Sinh:case GLSLstd450Cosh:case GLSLstd450Tanh:case GLSLstd450Asinh:case GLSLstd450Acosh:case GLSLstd450Atanh:
        case GLSLstd450Atan2:case GLSLstd450Pow:case GLSLstd450Exp:case GLSLstd450Log:case GLSLstd450Exp2:case GLSLstd450Log2:
        case GLSLstd450Sqrt:case GLSLstd450InverseSqrt:case GLSLstd450FMin:case GLSLstd450FMax:case GLSLstd450FClamp:
        case GLSLstd450FMix:case GLSLstd450Step:case GLSLstd450SmoothStep:case GLSLstd450Fma:case GLSLstd450Ldexp:
        case GLSLstd450Length:case GLSLstd450Distance:case GLSLstd450Cross:case GLSLstd450Normalize:case GLSLstd450FaceForward:
        case GLSLstd450Reflect:case GLSLstd450Refract:case GLSLstd450NMin:case GLSLstd450NMax:case GLSLstd450NClamp:return true;
        default:return false;}
    }
    void lower(const Instruction& i){
        const bool fract=i.op==SpvOpExtInst && glsl.count(i.at(3)) && i.at(4)==GLSLstd450Fract && floating(i.at(1));
        if(!software_flush && !fract){body.insert(body.end(),i.data,i.data+i.size);return;}
        unsigned operands=0;
        switch(i.op){
        case SpvOpFNegate:case SpvOpFAdd:case SpvOpFSub:case SpvOpFMul:case SpvOpFDiv:case SpvOpFRem:case SpvOpFMod:
        case SpvOpVectorTimesScalar:case SpvOpDot:case SpvOpConvertFToU:case SpvOpConvertFToS:case SpvOpFConvert:
        case SpvOpQuantizeToF16:case SpvOpFOrdEqual:case SpvOpFUnordEqual:case SpvOpFOrdNotEqual:case SpvOpFUnordNotEqual:
        case SpvOpFOrdLessThan:case SpvOpFUnordLessThan:case SpvOpFOrdGreaterThan:case SpvOpFUnordGreaterThan:
        case SpvOpFOrdLessThanEqual:case SpvOpFUnordLessThanEqual:case SpvOpFOrdGreaterThanEqual:case SpvOpFUnordGreaterThanEqual:
        case SpvOpDPdx:case SpvOpDPdy:case SpvOpFwidth:case SpvOpDPdxFine:case SpvOpDPdyFine:case SpvOpFwidthFine:
        case SpvOpDPdxCoarse:case SpvOpDPdyCoarse:case SpvOpFwidthCoarse:operands=3;break;
        case SpvOpMatrixTimesScalar:case SpvOpVectorTimesMatrix:case SpvOpMatrixTimesVector:case SpvOpMatrixTimesMatrix:case SpvOpOuterProduct:
            throw std::runtime_error("Matrix arithmetic requires another float32 lowering adapter");
        case SpvOpExtInst:
            if(glsl.count(i.at(3)) && extended(i.at(4)))operands=5;
            else if(floating(i.at(1)))throw std::runtime_error("Extended float32 result requires another lowering adapter");
            break;
        case SpvOpSpecConstantOp:throw std::runtime_error("Specialized arithmetic requires another float32 lowering adapter");
        default:break;
        }
        if(!operands){body.insert(body.end(),i.data,i.data+i.size);return;}
        std::vector<uint32_t> words(i.data,i.data+i.size);std::map<uint32_t,uint32_t> converted;
        if(software_flush)for(unsigned at=operands;at<i.size;++at){const auto original=words[at],t=operand_type(original);
            if(!floating(t))continue;auto found=converted.find(original);
            if(found==converted.end())found=converted.emplace(original,flush(original,t)).first;words[at]=found->second;}
        const auto t=i.at(1),result=i.at(2);const bool floats=floating(t);
        if(floats){words[2]=id();operation_results.emplace(result,words[2]);}
        body.insert(body.end(),words.begin(),words.end());
        auto value=words[2];
        if(fract){
            // GLSL Fract may round a tiny negative input up to 1. Native frc
            // requires [0,1). Ordered comparison leaves NaN results unchanged.
            const auto b=type(3,shapes.at(t).count),rounded=id(),bounded=software_flush?id():result;
            emit(body,SpvOpFOrdGreaterThanEqual,{b,rounded,value,constant(t,0x3f800000)});
            emit(body,SpvOpSelect,{t,bounded,rounded,constant(t,0x3f7fffff),value});value=bounded;
        }
        if(floats && software_flush)flush(value,t,result);
        require(body.size()+declarations.size()<=(8u<<20),"Float32 lowering executable budget exceeded");
    }
public:
    explicit Lowering(const std::vector<uint32_t>& code,bool flush):source(code),software_flush(flush){
        require(code.size()>=5 && code.size()<=(8u<<20) && code[0]==SpvMagicNumber && code[3] && code[3]<1024*1024,
            "Float32 lowering module header/budget invalid");next=code[3];value_types.resize(next);std::set<uint32_t> defined;
        for(size_t at=5;at<code.size();){const auto count=code[at]>>16;require(count && count<=code.size()-at,"Truncated float32 module");
            Instruction i{code.data()+at,count,SpvOp(code[at]&65535)};instructions.push_back(i);bool result=false,typed=false;SpvHasResultAndType(i.op,&result,&typed);
            if(result){const auto value=i.at(typed?2:1);require(value && value<next && defined.insert(value).second,"Invalid float32 result identity");if(typed)value_types[value]=i.at(1);}
            Shape shape;
            if(i.op==SpvOpTypeFloat && i.at(2)==32)shape={1,1};
            else if(i.op==SpvOpTypeInt && i.at(2)==32 && !i.at(3))shape={2,1};
            else if(i.op==SpvOpTypeBool)shape={3,1};
            else if(i.op==SpvOpTypeVector){const auto found=shapes.find(i.at(2));if(found!=shapes.end())shape={found->second.kind,i.at(3)};}
            if(shape.kind){shapes.emplace(i.at(1),shape);types.emplace(std::make_pair(shape.kind,shape.count),i.at(1));}
            if(i.op==SpvOpExtInstImport){const auto* name=reinterpret_cast<const char*>(i.data+2);
                require(memchr(name,0,(count-2)*4)!=nullptr,"Unterminated float32 extended import");if(!strcmp(name,"GLSL.std.450"))glsl.insert(i.at(1));}
            at+=count;
        }
        first_function=instructions.size();for(size_t n=0;n<instructions.size();++n)if(instructions[n].op==SpvOpFunction){first_function=n;break;}
        require(first_function<instructions.size(),"Float32 module lacks an executable function");
    }
    std::vector<uint32_t> run(){
        for(size_t n=first_function;n<instructions.size();++n)lower(instructions[n]);
        std::vector<uint32_t> result(source.begin(),source.begin()+5);result[3]=next;
        // Default Vulkan floating-point rules allow ignoring NaNs/infinities
        // and the sign of zero, even when an explicit denormal mode is set.
        // Keep these native contracts in both denormal implementation paths.
        emit(result,SpvOpCapability,{SpvCapabilitySignedZeroInfNanPreserve});
        emit(result,SpvOpCapability,{SpvCapabilityRoundingModeRTE});
        bool extension=false,inserted=false;unsigned entries=0;
        const char name[]="SPV_KHR_float_controls";
        for(const auto& i:instructions)if(i.op==SpvOpExtension && i.size*4>=4+sizeof(name) && !memcmp(i.data+1,name,sizeof(name)))extension=true;
        for(size_t n=0;n<first_function;++n){const auto& i=instructions[n];
            if(!inserted && i.op!=SpvOpCapability){inserted=true;if(!extension){
                const size_t at=result.size(),words=(sizeof(name)+3)/4;result.resize(at+1+words);
                result[at]=(uint32_t(1+words)<<16)|SpvOpExtension;memcpy(result.data()+at+1,name,sizeof(name));}}
            if(i.op==SpvOpGroupDecorate || i.op==SpvOpGroupMemberDecorate || i.op==SpvOpSpecConstantOp)
                throw std::runtime_error("Grouped decorations/specialization require another float32 adapter");
            std::vector<uint32_t> words(i.data,i.data+i.size);
            if(i.op==SpvOpDecorate && (i.at(2)==SpvDecorationNoContraction || i.at(2)==SpvDecorationRelaxedPrecision)){
                const auto found=operation_results.find(i.at(1));if(found!=operation_results.end()){
                    if(i.at(2)==SpvDecorationRelaxedPrecision)result.insert(result.end(),words.begin(),words.end());
                    words[1]=found->second;}}
            result.insert(result.end(),words.begin(),words.end());
            if(i.op==SpvOpEntryPoint){++entries;
                emit(result,SpvOpExecutionMode,{i.at(2),SpvExecutionModeSignedZeroInfNanPreserve,32});
                emit(result,SpvOpExecutionMode,{i.at(2),SpvExecutionModeRoundingModeRTE,32});}
        }
        require(entries==1,"Float32 lowering requires one native entry point");
        result.insert(result.end(),declarations.begin(),declarations.end());result.insert(result.end(),body.begin(),body.end());
        require(result.size()<=(8u<<20),"Float32 lowering result exceeds executable budget");return result;
    }
};
}
void lower_dxbc_float32(std::vector<uint32_t>& code,bool software_flush){code=Lowering(code,software_flush).run();}
}
