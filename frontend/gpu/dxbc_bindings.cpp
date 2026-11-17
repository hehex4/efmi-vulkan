#include "dxbc_bindings.h"
#include <DirectX11/d3d11TokenizedProgramFormat.hpp>
#include <cstring>
#include <stdexcept>
#define SPV_ENABLE_UTILITY_CODE
#include <spirv_reflect.h>
#include <set>
#include <algorithm>

namespace efmi::gpu {
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
}
DxbcBufferContracts dxbc_buffer_contracts(const std::vector<uint8_t>& code){
    auto word=[&](size_t at){require(at<=code.size() && code.size()-at>=4,"Truncated DXBC buffer contract");
        uint32_t value;memcpy(&value,code.data()+at,4);return value;};
    require(code.size()>=32 && code.size()<=(4u<<20) && !memcmp(code.data(),"DXBC",4),"Invalid DXBC buffer contract container");
    const auto chunks=word(28);require(chunks && chunks<=(code.size()-32)/4,"Invalid DXBC buffer chunk table");
    DxbcBufferContracts result;unsigned programs=0;
    for(unsigned chunk=0;chunk<chunks;++chunk){const size_t at=word(32+size_t(chunk)*4);
        require(at<=code.size() && code.size()-at>=8,"Invalid DXBC buffer chunk offset");
        const auto bytes=word(at+4);require(bytes<=code.size()-at-8,"Invalid DXBC buffer chunk size");
        if(memcmp(code.data()+at,"SHEX",4) && memcmp(code.data()+at,"SHDR",4))continue;
        require(++programs==1 && bytes>=8 && !(bytes%4),"Ambiguous DXBC buffer shader chunk");
        const auto version=word(at+8),major=DECODE_D3D10_SB_TOKENIZED_PROGRAM_MAJOR_VERSION(version),minor=DECODE_D3D10_SB_TOKENIZED_PROGRAM_MINOR_VERSION(version);
        require(major==4 || (major==5 && minor==0),"Buffer declaration adapter requires SM4.x/5.0");
        const auto total=word(at+12);require(total>=2 && total<=bytes/4,"Invalid DXBC buffer instruction count");
        for(size_t index=2;index<total;){
            const size_t base=at+8+index*4;const auto token=word(base);const auto op=DECODE_D3D10_SB_OPCODE_TYPE(token);
            const bool custom=op==D3D10_SB_OPCODE_CUSTOMDATA;
            require(!custom || total-index>=2,"Truncated DXBC custom data");
            const auto count=custom?word(base+4):DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(token);
            require(count>=(custom?2u:1u) && count<=total-index,"Invalid DXBC buffer instruction boundary");
            char kind=0;auto shape=ShaderBufferKind::Unspecified;
            switch(op){
            case D3D10_SB_OPCODE_DCL_RESOURCE:kind='t';shape=ShaderBufferKind::Typed;break;
            case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED:kind='u';shape=ShaderBufferKind::Typed;break;
            case D3D11_SB_OPCODE_DCL_RESOURCE_RAW:kind='t';shape=ShaderBufferKind::Raw;break;
            case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW:kind='u';shape=ShaderBufferKind::Raw;break;
            case D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED:kind='t';shape=ShaderBufferKind::Structured;break;
            case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_STRUCTURED:kind='u';shape=ShaderBufferKind::Structured;break;
            default:break;}
            if(kind && (shape!=ShaderBufferKind::Typed || DECODE_D3D10_SB_RESOURCE_DIMENSION(token)==D3D10_SB_RESOURCE_DIMENSION_BUFFER)){
                require(count==(shape==ShaderBufferKind::Raw?3u:4u) && !DECODE_IS_D3D10_SB_OPCODE_EXTENDED(token),
                    "Extended buffer declaration needs another native adapter");
                const auto operand=word(base+4),slot=word(base+8);
                require(!DECODE_IS_D3D10_SB_OPERAND_EXTENDED(operand) &&
                    DECODE_D3D10_SB_OPERAND_TYPE(operand)==(kind=='t'?D3D10_SB_OPERAND_TYPE_RESOURCE:D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW) &&
                    DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(operand)==D3D10_SB_OPERAND_INDEX_1D &&
                    DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(operand,0)==D3D10_SB_OPERAND_INDEX_IMMEDIATE32 && slot<(kind=='t'?128u:64u),
                    "Buffer declaration lost its original register contract");
                const auto stride=shape==ShaderBufferKind::Structured?word(base+12):0;
                require(shape!=ShaderBufferKind::Structured || (stride && stride<=2048 && !(stride%4)),"Invalid native structured declaration stride");
                require(result.emplace(std::make_pair(kind,slot),DxbcBufferContract{shape,stride}).second,"Duplicated native buffer declaration");
            }
            index+=count;
        }
    }
    require(programs==1,"DXBC buffer contract lacks a shader chunk");return result;
}
void dxbc_storage_reads(HlslProgram& program){
    std::set<uint32_t> bindings;
    for(const auto& b:program.bindings)if(b.source_type=='t' && b.type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER &&
        (b.source_buffer==ShaderBufferKind::Raw || b.source_buffer==ShaderBufferKind::Structured)){
        require(b.count==1,"Storage reads require scalar buffer descriptors");bindings.insert(b.binding);
    }
    if(bindings.empty())return;
    const auto& code=program.spirv;
    require(code.size()>=5 && code.size()<=(8u<<20) && code[0]==SpvMagicNumber && code[1]<=0x10300 && code[3]<1048576,
            "Storage read lowering requires bounded SPIR-V 1.0-1.3");
    struct Inst {size_t at;uint32_t count;SpvOp op;};std::vector<Inst> instructions;
    std::map<uint32_t,Inst> definitions;std::map<uint32_t,uint32_t> sets,locations,value_types;
    uint32_t boolean=0;
    for(size_t at=5;at<code.size();){const uint32_t n=code[at]>>16;const auto op=SpvOp(code[at]&65535);
        require(n && n<=code.size()-at,"Truncated storage read module");Inst i{at,n,op};instructions.push_back(i);
        bool result=false,typed=false;SpvHasResultAndType(op,&result,&typed);
        if(result){require(n>(typed?2u:1u),"Truncated storage read result");const auto id=code[at+(typed?2:1)];
            require(id && id<code[3] && definitions.emplace(id,i).second,"Duplicate storage read identity");
            if(typed)value_types[id]=code[at+1];}
        if(op==SpvOpTypeBool)boolean=code[at+1];
        if(op==SpvOpDecorate && n==4){if(code[at+2]==SpvDecorationDescriptorSet)sets[code[at+1]]=code[at+3];
            if(code[at+2]==SpvDecorationBinding)locations[code[at+1]]=code[at+3];}
        at+=n;
    }
    auto definition=[&](uint32_t id,SpvOp op)->const uint32_t*{const auto found=definitions.find(id);
        require(found!=definitions.end() && found->second.op==op,"Unexpected storage read type");return code.data()+found->second.at;};
    std::set<uint32_t> variables;std::map<uint32_t,uint32_t> images;uint32_t word=0;
    for(const auto& item:locations)if(bindings.count(item.second)){
        require(sets.count(item.first) && !sets.at(item.first),"Storage read descriptor set differs");
        const auto* var=definition(item.first,SpvOpVariable);require((var[0]>>16)==4 && var[3]==SpvStorageClassUniformConstant,"Unexpected buffer image variable");
        const auto* ptr=definition(var[1],SpvOpTypePointer);const auto* image=definition(ptr[3],SpvOpTypeImage);
        require(ptr[2]==SpvStorageClassUniformConstant && (image[0]>>16)==9 && image[3]==SpvDimBuffer &&
                (image[4]==0 || image[4]==2) && !image[5] && !image[6] && image[7]==1,
                "Storage read source is not a read-only buffer image");
        const auto* scalar=definition(image[2],SpvOpTypeInt);require(scalar[2]==32 && !scalar[3] && (!word || word==image[2]),"Storage read source is not uint32 words");
        word=image[2];variables.insert(item.first);
    }
    require(variables.size()==bindings.size(),"Storage read descriptor identity is missing or duplicated");
    for(const auto& i:instructions)if(i.op==SpvOpLoad && i.count>=4 && variables.count(code[i.at+3])){
        require(i.count==4,"Storage read image load has unsupported memory operands");images.emplace(code[i.at+2],code[i.at+3]);}
    uint32_t next=code[3];auto id=[&](){require(next<1048576,"Storage read identity budget exceeded");return next++;};
    const uint32_t array=id(),block=id(),block_ptr=id(),word_ptr=id(),zero=id(),one=id();
    const bool new_boolean=!boolean;if(new_boolean)boolean=id();
    std::vector<uint32_t> result(code.begin(),code.begin()+5);
    auto emit=[&](SpvOp op,std::initializer_list<uint32_t> args){result.push_back((uint32_t(args.size()+1)<<16)|uint32_t(op));result.insert(result.end(),args.begin(),args.end());};
    bool annotations=false,declarations=false,in_function=false;
    for(const auto& i:instructions){const auto* w=code.data()+i.at;
        if(!annotations && i.op>=SpvOpTypeVoid && i.op<=SpvOpTypeForwardPointer){
            emit(SpvOpDecorate,{array,SpvDecorationArrayStride,4});emit(SpvOpDecorate,{block,SpvDecorationBufferBlock});
            emit(SpvOpMemberDecorate,{block,0,SpvDecorationOffset,0});emit(SpvOpMemberDecorate,{block,0,SpvDecorationNonWritable});annotations=true;
        }
        if(i.op==SpvOpVariable && variables.count(w[2])){
            if(!declarations){emit(SpvOpTypeRuntimeArray,{array,word});emit(SpvOpTypeStruct,{block,array});
                emit(SpvOpTypePointer,{block_ptr,SpvStorageClassUniform,block});emit(SpvOpTypePointer,{word_ptr,SpvStorageClassUniform,word});
                if(new_boolean)emit(SpvOpTypeBool,{boolean});emit(SpvOpConstant,{word,zero,0});emit(SpvOpConstant,{word,one,1});declarations=true;}
            emit(SpvOpVariable,{block_ptr,w[2],SpvStorageClassUniform});continue;
        }
        if((i.op==SpvOpName || i.op==SpvOpDecorate) && i.count>=2 && images.count(w[1]))continue;
        if(i.op==SpvOpLoad && i.count>=3 && images.count(w[2]))continue;
        if((i.op==SpvOpImageFetch || i.op==SpvOpImageQuerySize) && i.count>=4 && images.count(w[3])){
            const auto variable=images.at(w[3]);
            if(i.op==SpvOpImageQuerySize){require(i.count==4,"Unexpected buffer size query operands");
                const auto* type=definition(w[1],SpvOpTypeInt);require(type[2]==32,"Unexpected buffer size result type");
                const auto length=w[1]==word?w[2]:id();emit(SpvOpArrayLength,{word,length,variable,0});
                if(length!=w[2])emit(SpvOpBitcast,{w[1],w[2],length});
            }else{
                require(i.count==5,"Buffer fetch has unsupported image operands");const auto* vector=definition(w[1],SpvOpTypeVector);
                require(vector[2]==word && vector[3]==4 && value_types.count(w[4]),"Unexpected buffer fetch result or coordinate");
                const auto index_type=value_types.at(w[4]);const auto* integer=definition(index_type,SpvOpTypeInt);require(integer[2]==32,"Buffer fetch index exceeds 32 bits");
                uint32_t index=w[4];if(index_type!=word){index=id();emit(SpvOpBitcast,{word,index,w[4]});}
                const auto length=id(),valid=id(),bounded=id(),pointer=id(),loaded=id(),value=id(),alpha=id();
                emit(SpvOpArrayLength,{word,length,variable,0});emit(SpvOpULessThan,{boolean,valid,index,length});
                // Every admitted non-null buffer view has at least one word.
                // Clamp before loading; a select after an OOB load is unsafe.
                emit(SpvOpSelect,{word,bounded,valid,index,zero});emit(SpvOpAccessChain,{word_ptr,pointer,variable,zero,bounded});
                emit(SpvOpLoad,{word,loaded,pointer});emit(SpvOpSelect,{word,value,valid,loaded,zero});emit(SpvOpSelect,{word,alpha,valid,one,zero});
                emit(SpvOpCompositeConstruct,{w[1],w[2],value,zero,zero,alpha});
            }
            continue;
        }
        if(i.op==SpvOpFunction)in_function=true;
        if(in_function && i.op!=SpvOpLine && i.op!=SpvOpNoLine){bool has_result=false,typed=false;SpvHasResultAndType(i.op,&has_result,&typed);
            for(uint32_t k=typed?3:has_result?2:1;k<i.count;++k){
                if((i.op==SpvOpExtInst && k==4) || (i.op==SpvOpFunction && k==3) ||
                   (i.op==SpvOpSwitch && k>=3 && (k&1)) || (i.op==SpvOpBranchConditional && k>=4) ||
                   (i.op==SpvOpLoopMerge && k>=3) || (i.op==SpvOpSelectionMerge && k>=2) ||
                   (i.op==SpvOpVectorShuffle && k>=5) || (i.op==SpvOpCompositeExtract && k>=4) ||
                   (i.op==SpvOpCompositeInsert && k>=5) || (i.op==SpvOpArrayLength && k>=4) ||
                   (i.op==SpvOpLoad && k>=4) || ((i.op==SpvOpStore || i.op==SpvOpCopyMemory) && k>=3) ||
                   (i.op==SpvOpCopyMemorySized && k>=4))continue;
                require(!images.count(w[k]) && !variables.count(w[k]),"Unsupported raw/structured buffer image value use");
            }
        }
        result.insert(result.end(),w,w+i.count);if(i.op==SpvOpFunctionEnd)in_function=false;
        require(result.size()<=(8u<<20),"Storage read executable budget exceeded");
    }
    require(result.size()<=(8u<<20),"Storage read executable budget exceeded");
    require(annotations && declarations,"Storage read declarations were not placed");result[3]=next;
    auto updated=program.bindings;
    for(auto& b:updated)if(bindings.count(b.binding)){b.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;b.stride=4;b.block_size=0;
        b.image_dimension=b.image_format=b.image_depth=0;b.image_array=b.image_multisample=false;b.scalar_type=2;b.scalar_width=32;}
    program.bindings=std::move(updated);program.spirv=std::move(result);
}
}
