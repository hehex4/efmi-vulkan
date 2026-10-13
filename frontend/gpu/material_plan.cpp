#include "material_plan.h"
#include <set>

namespace efmi::gpu {
bool MaterialProfile::prepare(const std::vector<uint32_t>& fragment,const std::vector<MaterialSlot>& rules,
                              std::string& reason)
{
    reason.clear();
    auto refuse=[&](const char* text){reason=text;return false;};
    if(fragment.size()<5 || fragment.size()>(1u<<22) || rules.size()>128)
        return refuse("Material profile exceeds its bytecode or slot budget");
    MaterialProfile next;next.code_=fragment;
    std::set<uint32_t> logical;
    std::set<std::pair<uint32_t,uint32_t>> physical;
    for(const auto& rule:rules){
        if(rule.slot>=128 || !logical.insert(rule.slot).second || rule.kind==MaterialSlotKind::Unknown)
            return refuse("Material profile contains an unknown or duplicate logical slot");
        auto& slot=next.slots_[rule.slot];slot.kind=rule.kind;
        if(rule.kind==MaterialSlotKind::Absent)continue;
        if(rule.kind!=MaterialSlotKind::Image || rule.image.set>=3 ||
           !physical.emplace(rule.image.set,rule.image.binding).second)
            return refuse("Material profile contains an unsupported or aliased image binding");
        slot.image=rule.image;std::vector<uint32_t> changed;std::string detail;
        slot.file_supported=repoint_fragment_images(fragment,{{rule.image.set,rule.image.binding,3,2}},changed,detail);
        slot.null_supported=null_fragment_images(fragment,{rule.image},changed,detail);
        if(!slot.file_supported && !slot.null_supported)
            return refuse("Material profile target is not a supported direct sampled image");
    }
    *this=std::move(next);return true;
}
bool MaterialProfile::select(const std::vector<uint32_t>& fragment,const std::array<assets::BindingSource,128>& bindings,
                             MaterialContract& output,std::string& reason) const
{
    reason.clear();auto refuse=[&](const char* text){reason=text;return false;};
    if(code_.empty() || code_!=fragment)return refuse("Material profile does not match the exact fragment bytecode");
    MaterialContract next;
    for(uint32_t i=0;i<bindings.size();++i){
        const auto binding=bindings[i];const auto& slot=slots_[i];
        if(binding==assets::BindingSource::Application)continue;
        if(binding!=assets::BindingSource::File && binding!=assets::BindingSource::Null)
            return refuse("Invalid logical pixel binding source");
        if(slot.kind==MaterialSlotKind::Unknown)return refuse("Logical pixel slot has no verified material correspondence");
        if(slot.kind==MaterialSlotKind::Absent)continue;
        if(binding==assets::BindingSource::Null){
            if(!slot.null_supported)return refuse("Mapped shader image does not support explicit null");
            next.patch.nulls.push_back(slot.image);
        }else{
            if(!slot.file_supported)return refuse("File image is incompatible with the mapped shader image");
            next.patch.images.push_back(slot.image);next.image_slots.push_back(i);
        }
    }
    if(next.patch.images.size()>32 || next.patch.nulls.size()>32)
        return refuse("Private material descriptor budget exceeded");
    output=std::move(next);return true;
}
bool resolve_material_plan(const assets::FileDraws& source,const std::vector<uint32_t>& fragment,
                           const MaterialProfile* profile,MaterialPlan& output,std::string& reason)
{
    reason.clear();auto refuse=[&](const char* text){reason=text;return false;};
    if(source.draws.size()>256)return refuse("Material draw budget exceeded");
    MaterialPlan next;next.draws.reserve(source.draws.size());
    for(const auto& draw:source.draws){
        std::array<assets::BindingSource,128> bindings{};
        for(size_t i=0;i<bindings.size();++i){
            bindings[i]=draw.pixel[i].source;
            if(!profile && bindings[i]!=assets::BindingSource::Application)
                return refuse("Logical pixel slot has no verified material correspondence");
        }
        MaterialContract contract;
        if(profile && !profile->select(fragment,bindings,contract,reason))return false;
        MaterialDraw material;material.patch=std::move(contract.patch);
        for(const auto slot:contract.image_slots){
            const auto& binding=draw.pixel[slot];
            if(!binding.resource || binding.resource->usage || !binding.image ||
               binding.image->state()!=assets::UploadState::Ready || !binding.image->view())
                return refuse("Mapped material file has no Ready sampled image owner");
            material.images.push_back({binding.image->view(),binding.image});
        }
        next.draws.push_back(std::move(material));
    }
    output=std::move(next);return true;
}
}
