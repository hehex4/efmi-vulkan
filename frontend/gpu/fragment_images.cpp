#include "fragment_images.h"
#include <map>
#include <set>
#include <deque>

namespace efmi::gpu {
bool repoint_fragment_images(const std::vector<uint32_t>& code,const std::vector<ImageBindingMove>& moves,
                             std::vector<uint32_t>& output,std::string& reason)
{
    reason.clear();auto fail=[&](const char* why){reason=why;return false;};
    if(code.size()<5 || code.size()>(4u<<20)/4 || code[0]!=0x07230203 || code[1]>0x00010600 || code[4])
        return fail("fragment_spirv_header_or_budget");
    if(moves.empty() || moves.size()>32)return fail("fragment_image_move_budget");
    using Binding=std::pair<uint32_t,uint32_t>;
    std::set<Binding> sources,targets;
    for(const auto& move:moves){
        const Binding source{move.source_set,move.source_binding},target{move.target_set,move.target_binding};
        if(source==target || !sources.insert(source).second || !targets.insert(target).second)
            return fail("fragment_image_duplicate_or_identity_move");
    }
    struct Decor {size_t set=0,binding=0;};
    struct Variable {uint32_t type,storage;};
    struct Pointer {uint32_t storage,type;};
    std::map<uint32_t,Decor> decors;
    std::map<uint32_t,Variable> variables;
    std::map<uint32_t,Pointer> pointers;
    std::map<uint32_t,uint32_t> image_float_types;
    std::set<uint32_t> opaque_types;
    std::set<uint32_t> float32;
    std::vector<size_t> function_instructions;
    bool in_function=false;
    unsigned entries=0;
    for(size_t i=5;i<code.size();){
        const auto n=code[i]>>16,op=code[i]&0xffff;
        if(!n || n>code.size()-i)return fail("fragment_spirv_instruction_bounds");
        if(op>255)return fail("fragment_spirv_instruction_extension");
        if(op==54)in_function=true;
        if(in_function)function_instructions.push_back(i);
        if(op==56)in_function=false;
        if(op==15){
            if(n<4 || code[i+1]!=4)return fail("fragment_only_module_required");
            ++entries;
        }else if(op==71){
            if(n<3 || code[i+1]>=code[3])return fail("fragment_spirv_decoration");
            const auto kind=code[i+2];
            if(kind==33 || kind==34){
                if(n!=4)return fail("fragment_spirv_descriptor_decoration");
                auto& d=decors[code[i+1]];auto& word=kind==33?d.binding:d.set;
                if(word)return fail("fragment_spirv_duplicate_decoration");word=i+3;
            }
        }else if(op==73 || op==74 || op==75 || op==332){
            return fail("fragment_spirv_indirect_decoration");
        }else if(op==22){
            if(n!=3 || code[i+1]>=code[3])return fail("fragment_spirv_float");
            if(code[i+2]==32)float32.insert(code[i+1]);
        }else if(op==25){
            if((n!=9 && n!=10) || code[i+1]>=code[3])return fail("fragment_spirv_image");
            opaque_types.insert(code[i+1]);
            // Dim2D, Depth=0/Unknown, Arrayed=0, MS=0, Sampled=1, Format=Unknown.
            if(n==9 && code[i+3]==1 && (code[i+4]==0 || code[i+4]==2) && !code[i+5] && !code[i+6] && code[i+7]==1 && !code[i+8])
                image_float_types[code[i+1]]=code[i+2];
        }else if(op==27){
            if(n!=3 || code[i+1]>=code[3])return fail("fragment_spirv_sampled_image");
            opaque_types.insert(code[i+1]);
        }else if(op==32){
            if(n!=4 || code[i+1]>=code[3] || pointers.count(code[i+1]))return fail("fragment_spirv_pointer");
            pointers[code[i+1]]={code[i+2],code[i+3]};
            opaque_types.insert(code[i+1]);
        }else if(op==59){
            if((n!=4 && n!=5) || code[i+2]>=code[3] || variables.count(code[i+2]))return fail("fragment_spirv_variable");
            variables[code[i+2]]={code[i+1],code[i+3]};
        }
        i+=n;
    }
    if(entries!=1)return fail("one_fragment_entry_required");
    // Follow only opaque image/pointer values, not sampled numeric colors.
    // Indirect call/store/composite escapes refuse instead of guessing aliases.
    std::map<uint32_t,std::vector<uint32_t>> edges;
    std::set<uint32_t> forbidden;
    for(const auto i:function_instructions){
        const auto n=code[i]>>16,op=code[i]&0xffff;
        const bool opaque=n>=4 && opaque_types.count(code[i+1]);
        auto edge=[&](size_t operand){if(operand<n)edges[code[i+operand]].push_back(code[i+2]);};
        auto forbid=[&](size_t first){for(size_t j=first;j<n;++j)forbidden.insert(code[i+j]);};
        if(op==89 || op==90 || op==93 || op==94 || op==97){
            if(n<4)return fail("fragment_depth_instruction_bounds");forbidden.insert(code[i+3]);
        }else if(op==57)forbid(4); // Function-call arguments.
        else if(op==62 || op==254)forbid(1); // Store or return of an opaque value.
        else if(op==80 || op==82)forbid(3); // Opaque values inside composites.
        else if(opaque){
            if(op==61 || op==65 || op==66 || op==67 || op==83 || op==86 || op==100)edge(3);
            else if(op==169){edge(4);edge(5);} // Select of image or pointer values.
            else if(op==245){for(size_t j=3;j<n;j+=2)edge(j);} // Phi values, not predecessor labels.
            else forbid(3);
        }
    }
    std::map<Binding,uint32_t> descriptors;
    for(const auto& item:decors){
        const auto& d=item.second;
        if(!d.set || !d.binding || !variables.count(item.first))return fail("fragment_incomplete_descriptor");
        const Binding key{code[d.set],code[d.binding]};
        if(!descriptors.emplace(key,item.first).second)return fail("fragment_descriptor_alias");
        if(targets.count(key))return fail("fragment_target_collision");
    }
    std::vector<std::pair<size_t,uint32_t>> edits;
    for(const auto& move:moves){
        auto found=descriptors.find({move.source_set,move.source_binding});
        if(found==descriptors.end())return fail("fragment_source_missing");
        const auto id=found->second;const auto& var=variables.at(id);auto ptr=pointers.find(var.type);
        if(var.storage || ptr==pointers.end() || ptr->second.storage)return fail("fragment_source_not_uniform_constant");
        auto image=image_float_types.find(ptr->second.type);
        if(image==image_float_types.end() || !float32.count(image->second))return fail("fragment_source_not_float_2d_color_image");
        std::set<uint32_t> seen{id};std::deque<uint32_t> pending{id};
        while(!pending.empty()){
            const auto value=pending.front();pending.pop_front();
            if(forbidden.count(value))return fail("fragment_image_depth_or_opaque_escape");
            auto next=edges.find(value);
            if(next!=edges.end())for(auto target:next->second)if(seen.insert(target).second)pending.push_back(target);
        }
        edits.push_back({decors[id].set,move.target_set});edits.push_back({decors[id].binding,move.target_binding});
    }
    auto rewritten=code;for(const auto& edit:edits)rewritten[edit.first]=edit.second;
    output.swap(rewritten);return true;
}
}
