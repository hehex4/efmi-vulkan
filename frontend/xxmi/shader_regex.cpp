#include "shader_regex.h"
#include "shader_assembly.h"
#include "draw_plan.h"
#include "globals.h"
#include <set>
#include <stdexcept>

namespace efmi::frontend {
namespace {
void require_regex(bool value,const char* text){if(!value)throw std::runtime_error(text);}
constexpr size_t max_shader_identities=4096;
constexpr size_t max_source_bytes=64u<<20;
// Native matching appends to one shader entry. Use a temporary native map, then
// restore the live nodes unchanged so local/static variable pointers stay valid.
struct OverrideIsolation {
    ShaderOverrideMap saved;
    explicit OverrideIsolation(uint64_t hash){
        saved.swap(G->mShaderOverrideMap);const auto old=saved.find(hash);
        if(old!=saved.end()){
            try{auto& fresh=G->mShaderOverrideMap[hash];fresh.filter_index=old->second.filter_index;
                fresh.backup_filter_index=old->second.backup_filter_index;
                fresh.command_list.ini_section=old->second.command_list.ini_section;
                fresh.post_command_list.ini_section=old->second.post_command_list.ini_section;
                fresh.post_command_list.post=old->second.post_command_list.post;
            }catch(...){saved.swap(G->mShaderOverrideMap);throw;}
        }
    }
    ~OverrideIsolation(){saved.swap(G->mShaderOverrideMap);}
};
}
struct SourceShaderRegex::Prepared::Entry {
    uint64_t hash=0;Identity identity;bool matched=false;
    ShaderOverride links;
};
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
SourceShaderRegex::Preparation SourceShaderRegex::prepare(const SourceShaderState& facts,unsigned mask,bool require_known,bool analyse_patches) const{
    const char* phase="facts";try{
    require_regex(G && initialized_ && !(mask&~63u),"Original ShaderRegex adapter is not initialized");
    require_regex(!G->SHADER_CACHE_PATH[0] && !G->CACHE_SHADERS && !G->EXPORT_FIXED,"Original ShaderRegex disk cache needs an owned path adapter");
    auto result=std::make_unique<Prepared>();result->owner=this;
    std::map<uint64_t,const SourceShaderBinding*> pending;size_t bytes=retained_bytes_;
    for(unsigned i=0;i<6;++i){if(!(mask&relevant_stages_&(1u<<i)))continue;const auto& shader=facts.stages[i];
        require_regex(shader.known || !require_known,"ShaderRegex matching requires a known original stage");
        if(!shader.known || !shader.bound)continue;
        require_regex(shader.hash_known && !shader.model.empty(),"ShaderRegex matching requires proven original hash and model");
        const auto existing=linked_.find(shader.hash);
        require_regex(!shader.code || shader.code->model()==shader.model,"Original regex bytecode conflicts with its source model");
        require_regex(existing==linked_.end() || existing->second.model==shader.model,"One source shader hash changed its proven model");
        if(existing!=linked_.end()){
            require_regex(!existing->second.code || !shader.code || existing->second.code->bytes()==shader.code->bytes(),
                "One source shader hash changed its proven bytecode");continue;
        }
        const auto item=pending.emplace(shader.hash,&shader);
        require_regex(item.second || (item.first->second->model==shader.model &&
            (!item.first->second->code || !shader.code || item.first->second->code->bytes()==shader.code->bytes())),
            "One source shader snapshot has conflicting models or bytecode");
        if(!item.second)continue;
        bool needs_assembly=false;
        for(const auto& entry:shader_regex_groups)if(entry.second.shader_models.count(shader.model) && !entry.second.patterns.empty())needs_assembly=true;
        if(needs_assembly){require_regex(bool(shader.code),"Matching ShaderRegex model requires original assembly bytecode evidence");
            require_regex(shader.code->bytes().size()<=max_source_bytes-bytes,"Original ShaderRegex retained bytecode budget exceeded");bytes+=shader.code->bytes().size();}
    }
    require_regex(pending.size()<=max_shader_identities-linked_.size(),"Original ShaderRegex identity budget exceeded");
    size_t patch_bytes=0;
    for(const auto& item:pending){const auto& shader=*item.second;auto entry=std::make_shared<Prepared::Entry>();
        entry->hash=shader.hash;entry->identity.model=shader.model;
        std::string model=shader.model;const wchar_t stage[]={wchar_t(shader.model[0]),L's',0};
        phase="native link preparation";OverrideIsolation isolated(shader.hash);bool needs_assembly=false;
        link_shader_regex_groups_without_patterns(stage,&model,shader.hash,&needs_assembly);
        if(needs_assembly){SourceShaderPatch patch;patch.hash=shader.hash;
            patch.stage=unsigned(std::string("vhdgpc").find(model[0]));patch.original=shader.code;
            phase="original disassembly";patch.assembly=source_shader_disassembly(*shader.code,G->patch_cb_offsets,G->disassemble_undecipherable_custom_data);
            phase="native pattern application";
            const bool patched=apply_shader_regex_groups(&patch.assembly,stage,&model,shader.hash,&patch.tagline);
            require_regex(patch.assembly.size()<=16u<<20,"Original regex replacement text exceeds budget");
            entry->identity.code=shader.code;
            if(patched){phase="signature-aware assembly";result->requires_translation=true;patch.bytecode=source_shader_assembly(patch.assembly,patch.notices);
                require_regex(source_shader_model(patch.bytecode.data(),patch.bytecode.size())==model,"Original regex patch changed its shader model");
                patch_bytes+=patch.assembly.size()+patch.bytecode.size();require_regex(patch_bytes<=max_source_bytes,"Original regex patch preparation exceeds budget");
                result->patches.push_back(std::move(patch));}
        }
        phase="link result retention";const auto found=G->mShaderOverrideMap.find(shader.hash);entry->matched=found!=G->mShaderOverrideMap.end();
        if(entry->matched)entry->links=std::move(found->second);
        result->entries.push_back(std::move(entry));
    }
    require_regex(analyse_patches || result->patches.empty(),"Original patched DXBC requires executable translation before command linkage");
    return result;
    }catch(const std::exception& e){throw std::runtime_error(std::string("ShaderRegex ")+phase+": "+e.what());}
}
void SourceShaderRegex::apply(Preparation prepared,DrawPlanAdapter& adapter){
    require_regex(prepared && prepared->owner==this && !prepared->requires_translation,"Original regex patch has no executable translation or belongs to another generation");
    for(const auto& entry:prepared->entries)require_regex(!linked_.count(entry->hash),"Original regex preparation was already published");
    for(auto& entry:prepared->entries){
        if(entry->matched){auto& target=G->mShaderOverrideMap[entry->hash];auto& source=entry->links;
            target.command_list.ini_section=std::move(source.command_list.ini_section);
            target.post_command_list.ini_section=std::move(source.post_command_list.ini_section);target.post_command_list.post=source.post_command_list.post;
            target.filter_index=source.filter_index;
            target.command_list.commands.insert(target.command_list.commands.end(),source.command_list.commands.begin(),source.command_list.commands.end());
            target.post_command_list.commands.insert(target.post_command_list.commands.end(),source.post_command_list.commands.begin(),source.post_command_list.commands.end());
            adapter.adapt(target.command_list);adapter.adapt(target.post_command_list);
        }
        if(entry->identity.code)retained_bytes_+=entry->identity.code->bytes().size();
        linked_.emplace(entry->hash,std::move(entry->identity));
    }
}
}
