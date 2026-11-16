#pragma once
#include "gpu_resources.h"
#include "original_pool_state.h"

namespace efmi::frontend {
// Only entries touched by an operation are copied. Restoring transfers already
// allocated map nodes, so cancelling after an allocation failure needs no allocation.
template<class Map> struct ResourceMapUndo {
    using Key=typename Map::key_type;
    Map previous;
    std::set<Key,typename Map::key_compare> absent;
    void remember(const Map& values,const Key& key){
        if(previous.count(key) || absent.count(key))return;
        const auto found=values.find(key);
        if(found==values.end())absent.insert(key);else previous.insert(*found);
    }
    void restore(Map& values) noexcept {
        for(const auto& key:absent)values.erase(key);
        for(const auto& value:previous)values.erase(value.first);
        values.merge(previous);
    }
};
struct GpuResourceRuntime::PreparedState {
    struct Pools {
        CustomResources* resources=&customResources;
        CommandListVariables* variables=&command_list_globals;
        std::vector<CommandListVariable*>* persistent=&persistent_variables;
        std::set<std::wstring> resource_names,variable_names;
        std::vector<CommandListVariable*> saved_persistent;
        std::map<CustomResourcePool*,OriginalPoolState> states;
        std::map<CustomResource*,OriginalResourceMetadata> metadata;
        std::map<CommandListVariable*,std::pair<float,VariableFlags>> values;
        std::map<PoolCopyOperation*,CustomResourcePool*> copies;
        size_t capacity=0;
        Pools():saved_persistent(*persistent){
            if(resources->size()>65536 || variables->size()>65536)throw std::runtime_error("Prepared pool registration budget exceeded");
            for(const auto& item:*resources)resource_names.insert(item.first);
            for(const auto& item:*variables)variable_names.insert(item.first);
        }
        void restore_metadata() noexcept {
            for(auto& item:metadata)item.second.restore(*item.first);
        }
        void restore() noexcept {
            for(auto& item:states)item.second.restore(*item.first);
            for(const auto& item:values){item.first->fval=item.second.first;item.first->flags=item.second.second;}
            for(const auto& item:copies)OriginalPoolState::failed(*item.first,item.second);
            *persistent=std::move(saved_persistent);
            for(auto it=resources->begin();it!=resources->end();)if(!resource_names.count(it->first))it=resources->erase(it);else ++it;
            for(auto it=variables->begin();it!=variables->end();)if(!variable_names.count(it->first))it=variables->erase(it);else ++it;
        }
    };
    std::unique_ptr<Pools> pools;
    struct Declaration {
        bool is_null,substantiated;
        D3D11_BIND_FLAG bind_flags;
        D3D11_RESOURCE_MISC_FLAG misc_flags;
        UINT stride,offset,buf_size,source_stride,frame_no;
        DXGI_FORMAT format;
        int copies_this_frame;
        void restore(CustomResource& r) const noexcept {
            r.is_null=is_null;r.bind_flags=bind_flags;r.misc_flags=misc_flags;
            r.stride=stride;r.offset=offset;r.buf_size=buf_size;r.source_stride=source_stride;
            r.frame_no=frame_no;r.format=format;r.copies_this_frame=copies_this_frame;
        }
    };
    std::map<CustomResource*,Declaration> declarations;
    ResourceMapUndo<decltype(resources_)> resources;
    ResourceMapUndo<decltype(copies_)> copies;
    ResourceMapUndo<decltype(resource_copy_pools_)> resource_pools;
    ResourceMapUndo<decltype(command_copy_pools_)> command_pools;
    ResourceMapUndo<decltype(range_views_)> range_views;
    Bindings slots;
    D3D11_VIEWPORT viewport;
    bool backbuffer_targeted;
    uint64_t parameter_uploads;
    bool parameter_republish;
    explicit PreparedState(const GpuResourceRuntime& r):slots(r.slots_),viewport(r.viewport_),backbuffer_targeted(r.backbuffer_targeted_),
        parameter_uploads(r.parameter_uploads_),parameter_republish(r.parameter_republish_){}
};
void GpuResourceRuntime::remember(CustomResource& r){
    if(!prepared_state_)return;
    if(r.pool)remember(r.pool);
    auto& undo=*prepared_state_;
    undo.declarations.try_emplace(&r,PreparedState::Declaration{r.is_null,bool(substantiated_.count(&r)),r.bind_flags,r.misc_flags,
        r.stride,r.offset,r.buf_size,r.source_stride,r.frame_no,r.format,r.copies_this_frame});
    undo.resources.remember(resources_,&r);undo.resource_pools.remember(resource_copy_pools_,&r);
}
void GpuResourceRuntime::remember(CustomResourcePool* pool){
    if(!prepared_state_ || !pool)return;
    auto& undo=*prepared_state_;
    if(!undo.pools)undo.pools=std::make_unique<PreparedState::Pools>();
    auto& saved=*undo.pools;
    if(saved.states.count(pool))return;
    const auto capacity=OriginalPoolState::capacity(*pool);
    if(saved.states.size()>=256 || capacity>65536 || saved.capacity+capacity>262144)
        throw std::runtime_error("Prepared pool snapshot budget exceeded");
    saved.states.try_emplace(pool,*pool);saved.capacity+=capacity;
    const auto resource_prefix=pool->name+L"_",variable_prefix=L"$"+pool->name+L"[";
    for(auto& item:*saved.resources){auto& resource=item.second;
        if(resource.pool==pool || &resource==pool->resource_template || item.first.compare(0,resource_prefix.size(),resource_prefix)==0){
            saved.metadata.try_emplace(&resource,resource);remember(resource);
        }
    }
    if(pool->variable_template){auto* value=pool->variable_template.get();saved.values.try_emplace(value,value->fval,value->flags);}
    for(auto& item:*saved.variables)if(item.first.compare(0,variable_prefix.size(),variable_prefix)==0)
        saved.values.try_emplace(&item.second,item.second.fval,item.second.flags);
    remember(OriginalPoolState::source(*pool));
}
void GpuResourceRuntime::remember(PoolCopyOperation& op){
    if(!prepared_state_)return;
    remember(op.dst.custom_resource_pool);remember(op.src.custom_resource_pool);
    if(prepared_state_->pools)prepared_state_->pools->copies.try_emplace(&op,OriginalPoolState::failed(op));
}
void GpuResourceRuntime::remember(ResourceCopyOperation& op){
    if(!prepared_state_)return;
    prepared_state_->copies.remember(copies_,&op);prepared_state_->command_pools.remember(command_copy_pools_,&op);
}
void GpuResourceRuntime::remember(SlotRangeCopyOperation& op){
    if(prepared_state_)prepared_state_->range_views.remember(range_views_,&op);
}
void GpuResourceRuntime::rollback_prepared() noexcept {
    if(!prepared_state_)return;auto& undo=*prepared_state_;
    for(const auto& value:undo.declarations){value.second.restore(*value.first);if(!value.second.substantiated)substantiated_.erase(value.first);}
    if(undo.pools)undo.pools->restore_metadata();
    undo.resources.restore(resources_);undo.copies.restore(copies_);
    undo.resource_pools.restore(resource_copy_pools_);undo.command_pools.restore(command_copy_pools_);undo.range_views.restore(range_views_);
    slots_=std::move(undo.slots);viewport_=undo.viewport;backbuffer_targeted_=undo.backbuffer_targeted;
    parameter_republish_=parameter_republish_ || undo.parameter_republish || parameter_uploads_!=undo.parameter_uploads;
    parameter_uploads_=undo.parameter_uploads;
    if(undo.pools)undo.pools->restore();
    prepared_state_.reset();
}
void GpuResourceRuntime::begin_prepared(){
    if(active_ || prepared_state_)throw std::runtime_error("Prepared registry requires a closed admitted scope");
    auto state=std::make_unique<PreparedState>(*this);
    commands_.begin_prepared();prepared_state_=std::move(state);
    active_=true;failed_=recorded_=backbuffer_targeted_=false;site_=ExecutionSite::Prepared;
}
std::shared_ptr<const gpu::PreparedResourceCommands> GpuResourceRuntime::finish_prepared(){
    active();
    if(!prepared_state_ || failed_)throw std::runtime_error("Cannot seal a failed or ordinary resource scope");
    auto result=commands_.finish_prepared();active_=false;return result;
}
void GpuResourceRuntime::accept_prepared(){
    if(active_ || !prepared_state_ || commands_.preparing() || commands_.pending() || failed_)
        throw std::runtime_error("Prepared registry admission requires a successfully sealed scope");
    commands_.accept_prepared();prepared_state_.reset();
}
}
