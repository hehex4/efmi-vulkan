#include "dxbc_bindings.h"
#include <DirectX11/d3d11TokenizedProgramFormat.hpp>
#include <cstring>
#include <stdexcept>

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
}
