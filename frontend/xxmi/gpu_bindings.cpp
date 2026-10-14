#include "gpu_resources.h"
#include <algorithm>

namespace efmi::frontend {
namespace {
using Target=ResourceCopyTargetType;
using Slot=std::tuple<wchar_t,Target,unsigned>;
bool attachment(const Slot& s){return std::get<1>(s)==Target::RENDER_TARGET || std::get<1>(s)==Target::DEPTH_STENCIL_TARGET;}
bool output(const Slot& s){return attachment(s) || std::get<1>(s)==Target::UNORDERED_ACCESS_VIEW;}
bool om(const Slot& s){return attachment(s) || (std::get<1>(s)==Target::UNORDERED_ACCESS_VIEW && std::get<0>(s)==L'p');}
bool interval(UINT a,UINT n,UINT b,UINT m){return uint64_t(a)+n>b && uint64_t(b)+m>a;}
bool attachment_size(const GpuResourceBinding& a,const GpuResourceBinding& b){
    if(!a.image_view || !b.image_view)return false;
    const auto& av=*a.image_view;const auto& bv=*b.image_view;
    const auto as=a.image->extent(av.range().baseMipLevel),bs=b.image->extent(bv.range().baseMipLevel);
    // A DSV may be wider/taller than its color targets (native D3D11 WARP).
    const auto depth=GpuResourceBinding::View::DepthStencil;
    const bool extent=a.view==depth?as.width>=bs.width && as.height>=bs.height:
        b.view==depth?bs.width>=as.width && bs.height>=as.height:as.width==bs.width && as.height==bs.height;
    return av.type()==bv.type() && extent && as.depth==bs.depth &&
        av.range().layerCount==bv.range().layerCount && a.image->description().samples==b.image->description().samples;
}
}
bool source_bindings_overlap(const GpuResourceBinding& a,const GpuResourceBinding& b) noexcept {
    if(a.buffer && a.buffer==b.buffer)return true; // A buffer has only one source subresource.
    if(!a.image || a.image!=b.image)return false;
    const auto ar=a.image_view?a.image_view->range():a.image->full_range();
    const auto br=b.image_view?b.image_view->range():b.image->full_range();
    // Depth/stencil aspects are the same source subresource. The original owned
    // DSV helper creates writable views (Flags=0), including both aspects.
    return interval(ar.baseMipLevel,ar.levelCount,br.baseMipLevel,br.levelCount) &&
        interval(ar.baseArrayLayer,ar.layerCount,br.baseArrayLayer,br.layerCount);
}
void GpuResourceRuntime::assign_binding(Slot key,GpuResourceBinding value){
    if(deferred_bindings_){(*deferred_bindings_)[key]=std::move(value);return;}
    Bindings pending;pending.emplace(key,std::move(value));publish_bindings(std::move(pending));
}
void GpuResourceRuntime::publish_bindings(Bindings pending){
    if(pending.empty())return;
    const auto first=pending.begin()->first;
    const bool targets=attachment(first),writes=output(first);
    // SetResource(RTV/DSV) calls OMSetRenderTargets with the complete eight-slot
    // RTV array. It also clears every pixel UAV, including slots above seven.
    if(targets)for(const auto& item:slots_)if(attachment(item.first))pending.emplace(item);
    if(writes){
        // Duplicate output subresources or incompatible attachments make the
        // native API ignore the entire call. Do not publish a partial range.
        for(auto a=pending.begin();a!=pending.end();++a)if(a->second)
            for(auto b=std::next(a);b!=pending.end();++b)if(b->second){
                if(source_bindings_overlap(a->second,b->second))return;
                if(targets && !attachment_size(a->second,b->second))return;
            }
        if(targets){
            for(auto i=slots_.begin();i!=slots_.end();)if(om(i->first))i=slots_.erase(i);else ++i;
        }else if(std::get<0>(first)==L'p'){
            // Unlike CSSetUnorderedAccessViews, the OM API replaces the whole
            // pixel UAV set even when KEEP_RENDER_TARGETS is specified.
            for(auto& item:slots_)if(std::get<1>(item.first)==Target::UNORDERED_ACCESS_VIEW &&
                std::get<0>(item.first)==L'p')item.second={};
            // KEEP_RENDER_TARGETS still unbinds RTV registers at/after the
            // first UAV register, even when the incoming UAVs are null.
            for(auto& item:slots_)if(std::get<1>(item.first)==Target::RENDER_TARGET &&
                std::get<2>(item.first)>=std::get<2>(first))item.second={};
        }
        for(auto& old:slots_)if(!pending.count(old.first) && old.second)
            for(const auto& value:pending)if(source_bindings_overlap(old.second,value.second)){old.second={};break;}
    }else{
        // Inputs never remove an already-bound writer. The conflicting input
        // element becomes null, independently for every element in the batch.
        for(auto& value:pending)if(value.second)for(const auto& old:slots_)
            if(output(old.first) && source_bindings_overlap(value.second,old.second)){value.second={};break;}
    }
    for(const auto& value:pending)slots_.erase(value.first);
    slots_.merge(pending);
}
GpuResourceRuntime::OutputState GpuResourceRuntime::save_outputs() const {
    active();OutputState state;state.viewport=viewport_;
    // Pinned save_om_state stores only D3D11_PS_CS_UAV_REGISTER_COUNT slots,
    // despite the parser accepting the larger D3D11.1 register range.
    for(const auto& item:slots_)if(attachment(item.first) || (om(item.first) && std::get<2>(item.first)<8))state.bindings.insert(item);
    return state;
}
void GpuResourceRuntime::restore_outputs(OutputState&& state) noexcept {
    // A saved OM configuration is valid already. Restore without allocation:
    // erase the complete current OM set, apply native hazards, transfer nodes.
    for(auto i=slots_.begin();i!=slots_.end();)if(om(i->first))i=slots_.erase(i);else ++i;
    for(auto& old:slots_)if(old.second)for(const auto& saved:state.bindings)
        if(source_bindings_overlap(old.second,saved.second)){old.second={};break;}
    slots_.merge(state.bindings);viewport_=state.viewport;
}
}
