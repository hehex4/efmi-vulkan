#include "shader_state.h"
#include "globals.h"
#include "HackerContext.h"
#include <stdexcept>
#include <cstring>

bool get_shader_bindings_from_bytecode(const void*,size_t,ShaderBindings*);
namespace efmi::frontend {
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
bool same(const ShaderBindings& a,const ShaderBindings& b){
    for(size_t i=0;i<a.constant_buffers.size();++i)
        if(a.constant_buffers[i].type!=b.constant_buffers[i].type || a.constant_buffers[i].size!=b.constant_buffers[i].size)return false;
    for(size_t i=0;i<a.resources.size();++i)
        if(a.resources[i].type!=b.resources[i].type || a.resources[i].dimension!=b.resources[i].dimension || a.resources[i].stride!=b.resources[i].stride)return false;
    return true;
}
}
std::shared_ptr<const ShaderBindings> source_shader_declarations(const void* data,size_t size){
    require(data && size>=32 && size<=(64u<<20),"Original shader bytecode exceeds declaration budget");
    auto result=std::make_shared<ShaderBindings>();
    require(get_shader_bindings_from_bytecode(data,size,result.get()),"Original shader declaration reader rejected DXBC");return result;
}
std::string source_shader_model(const void* data,size_t size){
    require(data && size>=32 && size<=(64u<<20),"Original shader model bytecode exceeds budget");
    const auto* bytes=static_cast<const unsigned char*>(data);
    auto word=[&](size_t offset){uint32_t value;memcpy(&value,bytes+offset,4);return value;};
    require(!memcmp(bytes,"DXBC",4),"Original shader model needs a DXBC container");
    const auto count=word(28);require(count && count<=(size-32)/4,"Original DXBC chunk table is invalid");
    // The pinned model reader scans backward and uses 32-bit offset addition.
    // Bound every table entry first, including chunks after a valid shader.
    for(uint32_t i=0;i<count;++i){const auto offset=word(32+size_t(i)*4);
        require(offset<=size && size-offset>=8,"Original DXBC chunk offset is invalid");
        const auto payload=word(size_t(offset)+4);require(payload<=size-offset-8,"Original DXBC chunk payload is invalid");
        if(!memcmp(bytes+offset,"SHEX",4) || !memcmp(bytes+offset,"SHDR",4))
            require(payload>=8 && word(size_t(offset)+12)>=2 && word(size_t(offset)+12)<=payload/4,"Original DXBC shader chunk is invalid");}
    std::string model;require(get_shader_model_from_bytecode(data,size,&model),"Original model reader rejected DXBC");return model;
}
void SourceShaderContext::install(const SourceShaderState& next,HackerContext& context){
    require(G!=nullptr,"Source shader facts require an active generation");
    ShaderMap hashes;std::unordered_map<UINT64,ShaderBindings> declarations;std::unordered_map<UINT64,std::string> models;
    for(size_t i=0;i<6;++i){const auto& s=next.stages[i];
        require(s.known || (!s.bound && !s.hash_known && !s.hash && !s.declarations && s.model.empty()),"Unknown shader stage contains asserted facts");
        require(s.bound || (!s.hash_known && !s.hash && !s.declarations && s.model.empty()),"Unbound shader stage contains asserted facts");
        require(s.hash_known || (!s.hash && !s.declarations && s.model.empty()),"Unknown shader hash contains declaration facts");
        if(!s.model.empty()){
            const auto separator=s.model.find('_',3);
            require(s.model.size()>=6 && s.model.size()<=32 && s.model[0]=="vhdgpc"[i] && s.model[1]=='s' && s.model[2]=='_' &&
                separator>3 && separator<s.model.size()-1 && s.model.find_first_not_of("0123456789",3)!=std::string::npos &&
                s.model.find_first_not_of("0123456789",3)==separator && s.model.find_first_not_of("0123456789",separator+1)==std::string::npos,
                "Original shader model does not match its stage");
            const auto entry=models.emplace(s.hash,s.model);require(entry.second || entry.first->second==s.model,"One source shader hash has conflicting models");
        }
        if(s.hash_known){hashes.emplace(reinterpret_cast<ID3D11DeviceChild*>(&tokens_[i]),s.hash);
            if(s.declarations){const auto entry=declarations.emplace(s.hash,*s.declarations);
                require(entry.second || same(entry.first->second,*s.declarations),"One source shader hash has conflicting declarations");}}
    }
    // Build and validate the complete next snapshot before replacing old facts.
    state_=next;G->mShaders.swap(hashes);G->mShaderBindingsCache.swap(declarations);
    auto handle=[&](size_t i){return state_.stages[i].bound?&tokens_[i]:nullptr;};
    auto hash=[&](size_t i){return state_.stages[i].hash_known?state_.stages[i].hash:uint64_t(0);};
    context.mCurrentVertexShaderHandle=reinterpret_cast<ID3D11VertexShader*>(handle(0));context.mCurrentVertexShader=hash(0);
    context.mCurrentHullShaderHandle=reinterpret_cast<ID3D11HullShader*>(handle(1));context.mCurrentHullShader=hash(1);
    context.mCurrentDomainShaderHandle=reinterpret_cast<ID3D11DomainShader*>(handle(2));context.mCurrentDomainShader=hash(2);
    context.mCurrentGeometryShaderHandle=reinterpret_cast<ID3D11GeometryShader*>(handle(3));context.mCurrentGeometryShader=hash(3);
    context.mCurrentPixelShaderHandle=reinterpret_cast<ID3D11PixelShader*>(handle(4));context.mCurrentPixelShader=hash(4);
    context.mCurrentComputeShaderHandle=reinterpret_cast<ID3D11ComputeShader*>(handle(5));context.mCurrentComputeShader=hash(5);
}
bool SourceShaderContext::query(CommandListOperand& operand,CommandListState& state,float& value,std::wstring& why){
    try{const auto* names=L"vhdgpc";const auto* found=wcschr(names,operand.shader_target.shader_type);
        require(found && operand.shader_target.shader_type,"Invalid source shader query stage");
        const auto& shader=state_.stages[size_t(found-names)];require(shader.known,"Original tracked shader stage is unknown");
        if(shader.bound && shader.hash_known && operand.shader_target.evaluation_mode!=ShaderTargetEvaluationMode::SHADER)
            require(bool(shader.declarations),"Original shader declarations are unknown");
        value=operand.evaluate(&state);return true;
    }catch(const std::exception& e){std::string text=e.what();why.assign(text.begin(),text.end());return false;}
}
}
