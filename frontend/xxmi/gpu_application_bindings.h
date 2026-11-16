#pragma once

namespace efmi::frontend {
void GpuResourceRuntime::begin_prepared(const ApplicationBufferState& application){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    require(application.omitted_slots_unbound,"Application buffer imports require a complete proven source context");
    require(application.bindings.size()<=1024,"Application binding snapshot exceeds its budget");
    begin_prepared();
    try{
        Bindings slots;
        decltype(source_identities_) identities;
        std::map<uint64_t,std::shared_ptr<const SourceResourceIdentity>> source_facts;
        for(const auto& input:application.bindings){
            using T=ResourceCopyTargetType;
            const auto type=input.type;
            require(type==T::VERTEX_BUFFER || type==T::INDEX_BUFFER || type==T::CONSTANT_BUFFER ||
                type==T::SHADER_RESOURCE || type==T::UNORDERED_ACCESS_VIEW,"Application import needs a buffer source slot");
            ResourceCopyTarget target;target.type=type;target.shader_type=input.stage;target.slot=input.slot;
            CommandListState state;const auto key=slot(target,state);
            require(!slots.count(key),"Application snapshot contains a duplicate source slot");
            const auto& r=input.resource;
            GpuResourceBinding value;
            if(r.bound){
                const auto& desc=input.description;
                require(input.source_known && r.dimension==D3D11_RESOURCE_DIMENSION_BUFFER && r.identity &&
                    r.buffer && !r.image && !r.image_view && r.keep && r.memory_linear,
                    "Application buffer import requires retained identity and proven source metadata");
                require(!r.uploaded_readonly && !r.uploaded_mutable && !r.publication_semaphore,
                    "Published file resources require their separate dependency contract");
                require(r.size && r.size<=UINT_MAX && desc.ByteWidth==r.size && r.offset<r.size && r.range &&
                    r.range<=r.size-r.offset,"Application source buffer window is invalid");
                require(bool(input.identity),"Application source tracking state is unknown");
                const auto& identity=*input.identity;
                require(identity.hash_known || !identity.hash,"Unknown source hash has an asserted value");
                require(identity.tracking==SourceResourceIdentity::Tracking::Tracked ||
                    (!identity.hash_known && !identity.region_hash),"Untracked source has asserted hash facts");
                const bool structured=(desc.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)!=0;
                const bool raw=(desc.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)!=0;
                require(!(structured && raw) && (structured?(desc.StructureByteStride &&
                    desc.ByteWidth%desc.StructureByteStride==0):desc.StructureByteStride==0),"Application source structure description is invalid");
                const UINT flag=type==T::VERTEX_BUFFER?D3D11_BIND_VERTEX_BUFFER:type==T::INDEX_BUFFER?D3D11_BIND_INDEX_BUFFER:
                    type==T::CONSTANT_BUFFER?D3D11_BIND_CONSTANT_BUFFER:type==T::SHADER_RESOURCE?D3D11_BIND_SHADER_RESOURCE:D3D11_BIND_UNORDERED_ACCESS;
                require((desc.BindFlags&flag)!=0,"Application source slot lacks its original bind flag");
                auto previous=imported_buffers_.find(r.identity);
                if(previous!=imported_buffers_.end())value.buffer=previous->second.buffer.lock();
                if(value.buffer){
                    const auto& b=*value.buffer;
                    require(b.buffer()==reinterpret_cast<VkBuffer>(r.buffer) && b.size()==r.size && b.usage()==r.usage &&
                        b.memory()==reinterpret_cast<VkDeviceMemory>(r.memory) && b.memory_offset()==r.memory_offset &&
                        b.memory_size()==r.memory_size && !memcmp(&previous->second.description,&desc,sizeof(desc)),
                        "Application resource identity changed while its allocation is retained");
                }else{
                    if(imported_buffers_.size()>=65536){
                        for(auto it=imported_buffers_.begin();it!=imported_buffers_.end();)
                            if(it->second.buffer.expired())it=imported_buffers_.erase(it);else ++it;
                        require(imported_buffers_.size()<65536,"Application import identity budget exceeded");
                    }
                    if(imported_handles_.size()>=65536){
                        for(auto it=imported_handles_.begin();it!=imported_handles_.end();)
                            if(it->second.second.expired())it=imported_handles_.erase(it);else ++it;
                        require(imported_handles_.size()<65536,"Application import handle budget exceeded");
                    }
                    const auto handle=imported_handles_.find(r.buffer);
                    require(handle==imported_handles_.end() || handle->second.second.expired(),
                        "A retained application handle cannot acquire a different resource identity");
                    value.buffer=commands_.import_buffer(reinterpret_cast<VkBuffer>(r.buffer),r.size,r.usage,
                        reinterpret_cast<VkDeviceMemory>(r.memory),r.memory_offset,r.memory_size,r.keep);
                    imported_buffers_[r.identity]={value.buffer,desc};
                    imported_handles_[r.buffer]={r.identity,value.buffer};
                }
                value.description=desc;value.format=input.format;value.stride=input.stride;
                value.offset=UINT(r.offset);value.visible_size=UINT(r.range);
                if(type==T::SHADER_RESOURCE || type==T::UNORDERED_ACCESS_VIEW){
                    const UINT width=structured?desc.StructureByteStride:dxgi_format_size(input.format);
                    require(width && r.offset%width==0 && r.range%width==0 &&
                        (structured?input.format==DXGI_FORMAT_UNKNOWN:input.format!=DXGI_FORMAT_UNKNOWN) &&
                        (input.format!=DXGI_FORMAT_R32_TYPELESS || raw),"Application source buffer view is invalid");
                    value.view=type==T::SHADER_RESOURCE?GpuResourceBinding::View::ShaderResource:GpuResourceBinding::View::UnorderedAccess;
                    value.view_format=input.format;value.view_offset=UINT(r.offset);value.view_size=UINT(r.range);
                }else if(type==T::CONSTANT_BUFFER){
                    require(input.format==DXGI_FORMAT_UNKNOWN && desc.ByteWidth%16==0 && r.offset%16==0 && r.range%16==0,
                        "Application constant buffer window is invalid");
                    value.region=r.offset || r.range!=r.size;
                }else if(type==T::INDEX_BUFFER){
                    require((input.format==DXGI_FORMAT_R16_UINT || input.format==DXGI_FORMAT_R32_UINT) &&
                        r.offset%dxgi_format_size(input.format)==0,"Application index buffer format or offset is invalid");
                }else require(input.format==DXGI_FORMAT_UNKNOWN,"Application vertex buffer has an unexpected view format");
                const std::weak_ptr<void> owner=value.buffer;
                // Aliases have one source identity. Reject contradictory facts
                // within one snapshot instead of choosing whichever slot ran last.
                const auto found=source_facts.find(r.identity);
                if(found!=source_facts.end())require(found->second==input.identity ||
                    (found->second->tracking==identity.tracking && found->second->hash_known==identity.hash_known &&
                     found->second->hash==identity.hash && !found->second->region_hash && !identity.region_hash),
                    "Application aliases have conflicting source identity facts");
                else{source_facts.emplace(r.identity,input.identity);identities.emplace(owner,identity);}
            }
            slots.emplace(key,std::move(value));
        }
        // A source context cannot already have a buffer simultaneously bound as
        // an input and an output, or twice as an output. DX11 would null/reject it.
        for(auto a=slots.begin();a!=slots.end();++a)if(a->second)
            for(auto b=std::next(a);b!=slots.end();++b)if(b->second &&
                (std::get<1>(a->first)==ResourceCopyTargetType::UNORDERED_ACCESS_VIEW ||
                 std::get<1>(b->first)==ResourceCopyTargetType::UNORDERED_ACCESS_VIEW))
                require(!source_bindings_overlap(a->second,b->second),"Application snapshot violates original buffer binding hazards");
        // Prepare every allocation before publication. Cancellation then restores
        // the newly observed application context, not an earlier draw's bindings.
        auto baseline=slots;
        if(source_identities_.size()+identities.size()>65536){
            for(auto it=source_identities_.begin();it!=source_identities_.end();)
                if(it->first.expired())it=source_identities_.erase(it);else ++it;
            require(source_identities_.size()+identities.size()<=65536,"Application source identity budget exceeded");
        }
        for(const auto& identity:identities)source_identities_.erase(identity.first);
        source_identities_.merge(identities);
        prepared_state_->slots=std::move(baseline);slots_=std::move(slots);
    }catch(...){discard();throw;}
}
}
