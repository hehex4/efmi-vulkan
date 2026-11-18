#pragma once

namespace efmi::frontend {
void GpuResourceRuntime::begin_prepared(const ApplicationResourceState& application){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    require(application.omitted_slots_unbound || application.allow_unknown_slots,"Application imports require complete proven source bindings");
    require(application.buffers.size()+application.images.size()<=1024 && application.observations.size()<=1024,
        "Application binding snapshot exceeds its budget");
    begin_prepared();
    try{
        caller_resource_={};caller_known_=false;caller_changed_=false;
        Bindings slots;
        decltype(source_identities_) identities;
        std::map<uint64_t,std::shared_ptr<const SourceResourceIdentity>> source_facts;
        auto identify=[&](uint64_t id,const std::shared_ptr<const SourceResourceIdentity>& facts,std::weak_ptr<void> owner){
            require(bool(facts),"Application source tracking state is unknown");const auto& identity=*facts;
            require(identity.hash_known || !identity.hash,"Unknown source hash has an asserted value");
            require(identity.tracking==SourceResourceIdentity::Tracking::Tracked ||
                (!identity.hash_known && !identity.region_hash && !identity.cached_data && !identity.read_cached_float),"Untracked source has asserted hash/cache facts");
            const auto found=source_facts.find(id);
            if(found!=source_facts.end())require(found->second==facts ||
                (found->second->tracking==identity.tracking && found->second->hash_known==identity.hash_known &&
                 found->second->hash==identity.hash && !found->second->region_hash && !identity.region_hash &&
                 !found->second->cached_data && !identity.cached_data &&
                 !found->second->read_cached_float && !identity.read_cached_float),
                "Application aliases have conflicting source identity facts");
            else{source_facts.emplace(id,facts);identities.emplace(std::move(owner),identity);}
        };
        for(const auto& input:application.buffers){
            using T=ResourceCopyTargetType;
            const auto type=input.type;
            require(type==T::THIS_RESOURCE || type==T::VERTEX_BUFFER || type==T::INDEX_BUFFER || type==T::CONSTANT_BUFFER ||
                type==T::SHADER_RESOURCE || type==T::UNORDERED_ACCESS_VIEW,"Application import needs a buffer source slot");
            ResourceCopyTarget target;target.type=type;target.shader_type=input.stage;target.slot=input.slot;
            CommandListState state;const auto key=type==T::THIS_RESOURCE?Slot{0,type,0}:slot(target,state);
            require(!slots.count(key),"Application snapshot contains a duplicate source slot");
            const auto& r=input.resource;
            GpuResourceBinding value;
            if(input.retained && input.retained->buffer && commands_.owns(input.retained->buffer)){
                value=*input.retained;
                require(input.source_known && r.bound && r.buffer==uint64_t(value.buffer->buffer()) &&
                    r.size==value.buffer->size(),"Retained original binding changed allocation identity");
                identify(r.identity,input.identity,value.buffer);slots.emplace(key,std::move(value));continue;
            }
            if(r.bound){
                const auto& desc=input.description;
                require(input.source_known && r.dimension==D3D11_RESOURCE_DIMENSION_BUFFER && r.identity &&
                    r.buffer && !r.image && !r.image_view && r.keep && r.memory_linear,
                    "Application buffer import requires retained identity and proven source metadata");
                require(!r.uploaded_readonly && !r.uploaded_mutable && !r.publication_semaphore,
                    "Published file resources require their separate dependency contract");
                require(r.size && r.size<=UINT_MAX && desc.ByteWidth==r.size && r.offset<r.size && r.range &&
                    r.range<=r.size-r.offset,"Application source buffer window is invalid");
                const auto image=imported_images_.find(r.identity);
                require(image==imported_images_.end() || image->second.image.expired(),"A retained source identity cannot change resource kind");
                const bool structured=(desc.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)!=0;
                const bool raw=(desc.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)!=0;
                require(!(structured && raw) && (structured?(desc.StructureByteStride &&
                    desc.ByteWidth%desc.StructureByteStride==0):desc.StructureByteStride==0),"Application source structure description is invalid");
                const UINT flag=type==T::VERTEX_BUFFER?D3D11_BIND_VERTEX_BUFFER:type==T::INDEX_BUFFER?D3D11_BIND_INDEX_BUFFER:
                    type==T::CONSTANT_BUFFER?D3D11_BIND_CONSTANT_BUFFER:type==T::SHADER_RESOURCE?D3D11_BIND_SHADER_RESOURCE:D3D11_BIND_UNORDERED_ACCESS;
                require(type==T::THIS_RESOURCE || (desc.BindFlags&flag)!=0,"Application source slot lacks its original bind flag");
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
                        reinterpret_cast<VkDeviceMemory>(r.memory),r.memory_offset,r.memory_size,r.keep,input.retained?input.retained->buffer:gpu::Buffer{});
                    imported_buffers_[r.identity]={value.buffer,desc};
                    imported_handles_[r.buffer]={r.identity,value.buffer};
                }
                if(input.retained)value.initialization=input.retained->initialization;
                value.description=desc;value.format=input.format;value.stride=input.stride;
                value.offset=UINT(r.offset);value.visible_size=UINT(r.range);
                if((type==T::INDEX_BUFFER || type==T::VERTEX_BUFFER) && r.range<r.size-r.offset){
                    value.ia_begin=r.offset;value.ia_end=r.offset+r.range;
                }
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
                identify(r.identity,input.identity,value.buffer);
            }
            slots.emplace(key,std::move(value));
        }
        // An indirect argument pointer is local to the call, outside the bound pipeline slots.
        auto root=slots.extract(Slot{0,ResourceCopyTargetType::THIS_RESOURCE,0});
        if(!root.empty()){
            caller_resource_=std::move(root.mapped());
            original_caller_buffer_=caller_resource_.buffer?caller_resource_.buffer->buffer():VK_NULL_HANDLE;
            caller_known_=true;
        }
        std::map<uint64_t,GpuResourceBinding> observed_images;
        auto observe=[&](const std::shared_ptr<const ApplicationImageSource>& source){
            require(bool(source),"An application image observation cannot be null");
            auto value=import_application_image(*source);identify(source->resource_id,source->identity,value.image);
            observed_images[source->resource_id]=value;return value;
        };
        for(const auto& source:application.observations)observe(source);
        for(const auto& input:application.images){
            using T=ResourceCopyTargetType;
            require(input.type==T::SHADER_RESOURCE || input.type==T::UNORDERED_ACCESS_VIEW ||
                input.type==T::RENDER_TARGET || input.type==T::DEPTH_STENCIL_TARGET,"Application image needs a source view slot");
            ResourceCopyTarget target;target.type=input.type;target.shader_type=input.stage;target.slot=input.slot;
            CommandListState state;const auto key=slot(target,state);
            require(!slots.count(key),"Application snapshot contains a duplicate source slot");
            GpuResourceBinding value;
            if(input.source){value=observe(input.source);import_application_view(value,input);}
            slots.emplace(key,std::move(value));
        }
        auto attachment=[](const auto& key){return std::get<1>(key)==ResourceCopyTargetType::RENDER_TARGET ||
            std::get<1>(key)==ResourceCopyTargetType::DEPTH_STENCIL_TARGET;};
        auto output=[&](const auto& key){return attachment(key) || std::get<1>(key)==ResourceCopyTargetType::UNORDERED_ACCESS_VIEW;};
        auto pixel_uav=[](const auto& key){return std::get<0>(key)==L'p' && std::get<1>(key)==ResourceCopyTargetType::UNORDERED_ACCESS_VIEW;};
        auto register_conflict=[&](const auto& a,const auto& b){return pixel_uav(a) &&
            std::get<1>(b)==ResourceCopyTargetType::RENDER_TARGET && std::get<2>(a)<=std::get<2>(b);};
        auto owned_slots=ini_slots_;
        if(application.preserve_unmapped_ini_slots){
            for(auto it=owned_slots.begin();it!=owned_slots.end();){
                if(application.mapped_slots.count(*it)){it=owned_slots.erase(it);continue;}
                const auto saved=slots_.find(*it);slots.emplace(*it,saved==slots_.end()?GpuResourceBinding{}:saved->second);++it;
            }
        }else owned_slots.clear();
        // Snapshot existing original API state; do not silently null conflicting
        // inputs/outputs or accept a partial incompatible attachment set.
        for(auto a=slots.begin();a!=slots.end();++a)if(a->second)
            for(auto b=std::next(a);b!=slots.end();++b)if(b->second){
                if(output(a->first) || output(b->first))require(!source_bindings_overlap(a->second,b->second),
                    "Application snapshot violates original subresource binding hazards");
                if(attachment(a->first) && attachment(b->first))require(source_attachments_compatible(a->second,b->second),
                    "Application snapshot has incompatible original attachments");
                require(!register_conflict(a->first,b->first) && !register_conflict(b->first,a->first),
                    "Application pixel UAV overlaps the original render target register range");
            }
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
        prepared_state_->ini_slots=owned_slots;ini_slots_=std::move(owned_slots);
        prepared_state_->unknown_slots=unknown_application_slots_=!application.omitted_slots_unbound;
        application_image_observer_=application.observe_image;
        commands_.independent_initialization(application.independent_initialization);
    }catch(...){discard();throw;}
}
}
