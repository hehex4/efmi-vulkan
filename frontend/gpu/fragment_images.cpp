#include "fragment_images.h"
#include <map>
#include <set>
#include <deque>

namespace efmi::gpu {
namespace {
using Binding=std::pair<uint32_t,uint32_t>;
struct Analysis {
    struct Decor {size_t set=0,binding=0;};
    struct Variable {uint32_t type,storage;};
    struct Pointer {uint32_t storage,type;};
    struct Image {uint32_t scalar,dim,depth,arrayed,ms,sampled,format;bool basic;};
    std::map<uint32_t,Decor> decors;
    std::map<uint32_t,Variable> variables;
    std::map<uint32_t,Pointer> pointers;
    std::map<uint32_t,Image> images;
    std::map<Binding,uint32_t> descriptors;
    std::set<uint32_t> floats,numeric,opaque,forbidden,depth_reads,merges;
    std::map<uint32_t,std::vector<uint32_t>> edges;
    std::map<uint32_t,size_t> opaque_definitions;
    std::vector<size_t> reads;
    size_t first_function=0;

    bool parse(const std::vector<uint32_t>& code,std::string& reason){
        auto fail=[&](const char* why){reason=why;return false;};
        if(code.size()<5 || code.size()>(4u<<20)/4 || code[0]!=0x07230203 || code[1]>0x00010600 ||
           !code[3] || code[3]>(1u<<22) || code[4])return fail("fragment_spirv_header_or_budget");
        std::vector<size_t> instructions;bool in_function=false;unsigned entries=0;
        for(size_t i=5;i<code.size();){
            const auto n=code[i]>>16,op=code[i]&0xffff;
            if(!n || n>code.size()-i)return fail("fragment_spirv_instruction_bounds");
            if(op>255)return fail("fragment_spirv_instruction_extension");
            if(op==54){in_function=true;if(!first_function)first_function=i;}
            if(in_function)instructions.push_back(i);
            if(op==56)in_function=false;
            if(op==15){
                if(n<4 || code[i+1]!=4)return fail("fragment_only_module_required");++entries;
            }else if(op==71){
                if(n<3 || code[i+1]>=code[3])return fail("fragment_spirv_decoration");
                const auto kind=code[i+2];
                if(kind==33 || kind==34){
                    if(n!=4)return fail("fragment_spirv_descriptor_decoration");
                    auto& d=decors[code[i+1]];auto& word=kind==33?d.binding:d.set;
                    if(word)return fail("fragment_spirv_duplicate_decoration");word=i+3;
                }
            }else if(op==73 || op==74 || op==75){
                return fail("fragment_spirv_indirect_decoration");
            }else if(op==21 || op==22){
                if(n!=(op==21?4u:3u) || code[i+1]>=code[3])return fail("fragment_spirv_scalar");
                if(code[i+2]==32){numeric.insert(code[i+1]);if(op==22)floats.insert(code[i+1]);}
            }else if(op==23){
                if(n!=4 || code[i+1]>=code[3])return fail("fragment_spirv_vector");
                if(numeric.count(code[i+2]) && code[i+3]>=2 && code[i+3]<=4)numeric.insert(code[i+1]);
            }else if(op==25){
                if((n!=9 && n!=10) || code[i+1]>=code[3])return fail("fragment_spirv_image");
                opaque.insert(code[i+1]);
                images[code[i+1]]={code[i+2],code[i+3],code[i+4],code[i+5],code[i+6],code[i+7],code[i+8],n==9};
            }else if(op==27){
                if(n!=3 || code[i+1]>=code[3])return fail("fragment_spirv_sampled_image");opaque.insert(code[i+1]);
            }else if(op==32){
                if(n!=4 || code[i+1]>=code[3] || pointers.count(code[i+1]))return fail("fragment_spirv_pointer");
                pointers[code[i+1]]={code[i+2],code[i+3]};opaque.insert(code[i+1]);
            }else if(op==59){
                if((n!=4 && n!=5) || code[i+2]>=code[3] || variables.count(code[i+2]))return fail("fragment_spirv_variable");
                variables[code[i+2]]={code[i+1],code[i+3]};
            }
            i+=n;
        }
        if(entries!=1 || !first_function)return fail("one_fragment_entry_required");
        // Trace opaque values only. Numeric sample results remain ordinary SSA.
        // Source modules must already pass validation; this is a bounded adapter,
        // not a replacement SPIR-V validator or a general alias analysis.
        for(const auto i:instructions){
            const auto n=code[i]>>16,op=code[i]&0xffff;
            auto edge=[&](size_t operand){if(operand<n)edges[code[i+operand]].push_back(code[i+2]);};
            auto forbid=[&](size_t first){for(size_t j=first;j<n;++j)forbidden.insert(code[i+j]);};
            if((op>=87 && op<=97) || op==103 || op==104 || op==106 || op==107){
                if(n<4)return fail("fragment_image_read_bounds");reads.push_back(i);
                if(op==89 || op==90 || op==93 || op==94 || op==97)depth_reads.insert(code[i+3]);
            }else if(op==98 || op==101 || op==102 || op==105){
                if(n<4)return fail("fragment_image_read_bounds");forbidden.insert(code[i+3]);
            }else if(op==12)forbid(5); // Extension operands may not consume selected opaque values.
            else if(op==57)forbid(4);
            else if(op==62 || op==99 || op==254)forbid(1);
            else if(op==80 || op==82)forbid(3);
            else if(n>=4 && opaque.count(code[i+1])){
                opaque_definitions[code[i+2]]=i;
                if(op==61 || op==65 || op==66 || op==67 || op==83 || op==86 || op==100)edge(3);
                else if(op==169){if(n!=6)return fail("fragment_select_bounds");edge(4);edge(5);merges.insert(code[i+4]);merges.insert(code[i+5]);}
                else if(op==245){for(size_t j=3;j<n;j+=2){edge(j);merges.insert(code[i+j]);}}
                else forbid(3);
            }
        }
        for(const auto& item:decors){
            const auto& d=item.second;
            if(!d.set || !d.binding || !variables.count(item.first))return fail("fragment_incomplete_descriptor");
            if(!descriptors.emplace(Binding{code[d.set],code[d.binding]},item.first).second)return fail("fragment_descriptor_alias");
        }
        return true;
    }
    bool source(Binding binding,bool null_mode,uint32_t& id,std::string& reason) const {
        auto fail=[&](const char* why){reason=why;return false;};
        auto found=descriptors.find(binding);if(found==descriptors.end())return fail("fragment_source_missing");
        id=found->second;const auto& var=variables.at(id);auto ptr=pointers.find(var.type);
        if(var.storage || ptr==pointers.end() || ptr->second.storage)return fail("fragment_source_not_uniform_constant");
        auto image=images.find(ptr->second.type);
        if(image==images.end())return fail("fragment_source_not_float_2d_color_image");
        const auto& t=image->second;
        if(!t.basic || !floats.count(t.scalar) || (t.dim!=1 && !(null_mode && t.dim==2)) ||
           t.depth>2 || (!null_mode && t.depth==1) || t.arrayed || t.ms || t.sampled!=1 || t.format)
            return fail("fragment_source_not_float_2d_color_image");
        return true;
    }
    bool trace(uint32_t id,bool null_mode,std::set<uint32_t>& seen,std::string& reason) const {
        std::deque<uint32_t> pending{id};seen.insert(id);
        while(!pending.empty()){
            const auto value=pending.front();pending.pop_front();
            if(forbidden.count(value) || (!null_mode && depth_reads.count(value)) || (null_mode && merges.count(value))){
                reason="fragment_image_depth_or_opaque_escape";return false;
            }
            auto next=edges.find(value);
            if(next!=edges.end())for(auto target:next->second)if(seen.insert(target).second)pending.push_back(target);
        }
        return true;
    }
};
}

bool repoint_fragment_images(const std::vector<uint32_t>& code,const std::vector<ImageBindingMove>& moves,
                             std::vector<uint32_t>& output,std::string& reason)
{
    reason.clear();auto fail=[&](const char* why){reason=why;return false;};
    if(moves.empty() || moves.size()>32)return fail("fragment_image_move_budget");
    Analysis analysis;if(!analysis.parse(code,reason))return false;
    std::set<Binding> sources,targets;std::vector<std::pair<size_t,uint32_t>> edits;
    for(const auto& move:moves){
        const Binding source{move.source_set,move.source_binding},target{move.target_set,move.target_binding};
        if(source==target || !sources.insert(source).second || !targets.insert(target).second)
            return fail("fragment_image_duplicate_or_identity_move");
        if(analysis.descriptors.count(target))return fail("fragment_target_collision");
        uint32_t id=0;std::set<uint32_t> seen;
        if(!analysis.source(source,false,id,reason) || !analysis.trace(id,false,seen,reason))return false;
        edits.push_back({analysis.decors[id].set,move.target_set});edits.push_back({analysis.decors[id].binding,move.target_binding});
    }
    auto rewritten=code;for(const auto& edit:edits)rewritten[edit.first]=edit.second;
    output.swap(rewritten);return true;
}

bool null_fragment_images(const std::vector<uint32_t>& code,const std::vector<ImageBinding>& bindings,
                          std::vector<uint32_t>& output,std::string& reason)
{
    reason.clear();auto fail=[&](const char* why){reason=why;return false;};
    if(bindings.empty() || bindings.size()>32)return fail("fragment_null_binding_budget");
    Analysis analysis;if(!analysis.parse(code,reason))return false;
    std::set<Binding> sources;std::set<uint32_t> seen,removed_ids;std::set<size_t> removed,reads;
    for(const auto& binding:bindings){
        if(!sources.insert({binding.set,binding.binding}).second)return fail("fragment_null_duplicate_source");
        uint32_t id=0;
        if(!analysis.source({binding.set,binding.binding},true,id,reason) || !analysis.trace(id,true,seen,reason))return false;
    }
    for(const auto id:seen){auto def=analysis.opaque_definitions.find(id);
        if(def!=analysis.opaque_definitions.end()){removed.insert(def->second);removed_ids.insert(id);}}
    uint32_t bound=code[3];std::map<uint32_t,uint32_t> zeros;
    for(const auto i:analysis.reads)if(seen.count(code[i+3])){
        const auto type=code[i+1];if(!analysis.numeric.count(type))return fail("fragment_null_result_type");
        if(!zeros.count(type))zeros[type]=bound++;reads.insert(i);
    }
    std::vector<uint32_t> result(code.begin(),code.begin()+5);result[3]=bound;
    for(size_t i=5;i<code.size();){
        const auto n=code[i]>>16,op=code[i]&0xffff;
        if(i==analysis.first_function)for(const auto& zero:zeros)
            result.insert(result.end(),{(3u<<16)|46,zero.first,zero.second}); // OpConstantNull.
        if(reads.count(i))result.insert(result.end(),{(4u<<16)|83,code[i+1],code[i+2],zeros.at(code[i+1])});
        else if(!removed.count(i) && !((op==5 || op==71) && n>=2 && removed_ids.count(code[i+1])))
            result.insert(result.end(),code.begin()+i,code.begin()+i+n);
        i+=n;
    }
    output.swap(result);return true;
}
}
