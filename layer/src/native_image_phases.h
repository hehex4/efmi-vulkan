#pragma once
#include "native_targets.h"
#include <algorithm>

namespace efvk {
// Prove that each explicit sampled-image binding interval is disjoint from all
// output intervals. The caller has normalized/validated the initial attachments.
// This does not prove layouts or allocation aliases: the existing image and
// memory preflight must still accept them before any command is recorded.
inline std::vector<bool> native_image_read_phases(const RenderTargets& initial,const efmi::assets::FilePlan& plan)
{
    using Operation=efmi::assets::FileOperation;
    using Binding=efmi::frontend::NativeBinding;
    std::vector<bool> disjoint(plan.items.size(),false);
    const auto colors=initial.dynamic_colors.size();
    if(!initial.known || colors>8 || initial.views.size()!=2*(colors+initial.has_depth+initial.has_stencil))return disjoint;
    std::array<uint64_t,9> outputs{};
    for(size_t slot=0;slot<colors;++slot)if(initial.dynamic_colors[slot].imageView)outputs[slot]=uint64_t(initial.views[2*slot].view.image);
    if(initial.has_depth && initial.depth.imageView)outputs[8]=uint64_t(initial.views[2*colors].view.image);
    else if(initial.has_stencil && initial.stencil.imageView)outputs[8]=uint64_t(initial.views[2*(colors+initial.has_depth)].view.image);
    using Key=std::tuple<uint32_t,uint32_t,uint32_t>;
    std::map<Key,size_t> bindings;
    std::unordered_map<uint64_t,std::unordered_set<size_t>> readers;
    for(size_t i=0;i<plan.items.size();++i){
        const auto& item=plan.items[i];if(item.operation!=Operation::NativeBind)continue;
        const auto& target=item.native_target;const auto& resource=item.native_resource;
        if(native_output_binding(target)){
            const auto slot=target.kind==Binding::DepthTarget?(target.slot?9u:8u):target.slot;
            if(slot>=outputs.size())return std::vector<bool>(plan.items.size(),false);
            outputs[slot]=resource && resource->bound?resource->image:0;
            const auto active=readers.find(outputs[slot]);
            if(active!=readers.end()){
                for(const auto index:active->second)disjoint[index]=false;
                readers.erase(active); // Each invalid interval is visited only once.
            }
            continue;
        }
        // All stage aliases for the same Vulkan descriptor replace one binding.
        const Key key{target.set,target.binding,target.element};
        const auto prior=bindings.find(key);
        if(prior!=bindings.end() && disjoint[prior->second]){
            const auto& previous=plan.items[prior->second].native_resource;
            const auto active=readers.find(previous->image);
            if(active!=readers.end()){
                active->second.erase(prior->second);
                if(active->second.empty())readers.erase(active);
            }
        }
        bindings[key]=i;
        if(target.kind!=Binding::Resource || !resource || !resource->bound || !resource->image ||
           resource->uploaded_readonly || resource->uploaded_mutable)continue;
        if(std::find(outputs.begin(),outputs.end(),resource->image)!=outputs.end())continue;
        disjoint[i]=true;readers[resource->image].insert(i);
    }
    return disjoint;
}
}
