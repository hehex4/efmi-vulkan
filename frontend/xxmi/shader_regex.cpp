#include "shader_regex.h"
#include "draw_plan.h"
#include "globals.h"
#include <set>
#include <stdexcept>

namespace efmi::frontend {
namespace {
void require_regex(bool value,const char* text){if(!value)throw std::runtime_error(text);}
constexpr size_t max_shader_identities=4096;
}
void SourceShaderRegex::initialize(DrawPlanAdapter& adapter){
    require_regex(G && !initialized_,"ShaderRegex links require one active generation");
    require_regex(shader_regex_groups.size()<=4096,"Original ShaderRegex group budget exceeded");
    // Capture no-op status before adaptation replaces source commands. Source
    // links preserve their exact shared identity and native dynamic type.
    for(auto& entry:shader_regex_groups){auto& group=entry.second;
        const bool pre=!group.command_list.noop(),post=!group.post_command_list.noop();
        const std::wstring line=L"["+group.command_list.ini_section+L".Match] run = linked command list";
        if(pre)group.link=adapter.linked(group.command_list,line);
        if(post)group.post_link=adapter.linked(group.post_command_list,line);
        for(const auto& model:group.shader_models)for(unsigned i=0;i<6;++i)
            if(model.size()>=3 && model[0]=="vhdgpc"[i] && model[1]=='s' && model[2]=='_')relevant_stages_|=1u<<i;
    }
    initialized_=true;
}
void SourceShaderRegex::preflight(const SourceShaderState& facts,unsigned mask,bool require_known) const{
    require_regex(initialized_ && !(mask&~63u),"Original ShaderRegex adapter is not initialized");
    std::map<uint64_t,std::string> pending;
    for(unsigned i=0;i<6;++i){if(!(mask&relevant_stages_&(1u<<i)))continue;const auto& shader=facts.stages[i];
        require_regex(shader.known || !require_known,"ShaderRegex matching requires a known original stage");
        if(!shader.known || !shader.bound)continue;
        require_regex(shader.hash_known && !shader.model.empty(),"ShaderRegex matching requires proven original hash and model");
        const auto existing=linked_.find(shader.hash);
        require_regex(existing==linked_.end() || existing->second==shader.model,"One source shader hash changed its proven model");
        const auto item=pending.emplace(shader.hash,shader.model);
        require_regex(item.second || item.first->second==shader.model,"One source shader snapshot has conflicting models");
        for(const auto& entry:shader_regex_groups)if(entry.second.shader_models.count(shader.model))
            require_regex(entry.second.patterns.empty(),"Matching ShaderRegex model requires original assembly matching and execution");
    }
    size_t added=0;for(const auto& item:pending)if(!linked_.count(item.first))++added;
    require_regex(added<=max_shader_identities-linked_.size(),"Original ShaderRegex identity budget exceeded");
    // Native disk cache remains disabled in this hosted generation. No original
    // installation-relative shader cache is read, deleted or written.
    require_regex(!G->SHADER_CACHE_PATH[0] && !G->CACHE_SHADERS && !G->EXPORT_FIXED,"Original ShaderRegex disk cache needs an owned path adapter");
}
void SourceShaderRegex::apply(const SourceShaderState& facts,unsigned mask,DrawPlanAdapter& adapter){
    preflight(facts,mask,false);
    for(unsigned i=0;i<6;++i){if(!(mask&relevant_stages_&(1u<<i)))continue;const auto& shader=facts.stages[i];
        if(!shader.known || !shader.bound || linked_.count(shader.hash))continue;
        std::string model=shader.model;const wchar_t stage[]={L"vhdgpc"[i],L's',0};bool needs_assembly=false;
        link_shader_regex_groups_without_patterns(stage,&model,shader.hash,&needs_assembly);
        require_regex(!needs_assembly,"Original ShaderRegex unexpectedly required assembly");
        const auto found=G->mShaderOverrideMap.find(shader.hash);
        if(found!=G->mShaderOverrideMap.end()){
            adapter.adapt(found->second.command_list);adapter.adapt(found->second.post_command_list);
        }
        linked_.emplace(shader.hash,shader.model);
    }
}
}
