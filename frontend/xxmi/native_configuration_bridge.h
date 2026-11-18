#pragma once
// Included inside efmi::frontend, after the private host transaction definition.
namespace native_configuration {
using Key=std::tuple<wchar_t,ResourceCopyTargetType,unsigned>;
inline ResourceCopyTargetType type(NativeBinding::Kind kind){
    using T=ResourceCopyTargetType;using B=NativeBinding;
    switch(kind){
    case B::Vertex:return T::VERTEX_BUFFER;case B::Index:return T::INDEX_BUFFER;
    case B::Constant:return T::CONSTANT_BUFFER;case B::Resource:return T::SHADER_RESOURCE;
    case B::Access:return T::UNORDERED_ACCESS_VIEW;case B::ColorTarget:return T::RENDER_TARGET;
    case B::DepthTarget:return T::DEPTH_STENCIL_TARGET;
    }throw std::runtime_error("Unknown native source binding kind");
}
inline Key key(const NativeBinding& value){
    const bool shader=value.kind==NativeBinding::Resource || value.kind==NativeBinding::Constant || value.kind==NativeBinding::Access;
    if(shader && value.stage>=6)throw std::runtime_error("Unknown native shader stage");
    return {shader?L"vhdgpc"[value.stage]:wchar_t(0),type(value.kind),value.slot};
}
inline bool equal(const GpuResourceBinding& a,const GpuResourceBinding& b){
    if(!a && !b)return true;
    // Image query metadata (format/stride/size) can be filled by a ref copy
    // without changing the retained typed view passed to the native context.
    if(a.image || b.image)return a.image==b.image && a.image_view==b.image_view && a.view==b.view;
    return a.buffer==b.buffer && a.image==b.image && a.image_view==b.image_view &&
        a.format==b.format && a.stride==b.stride && a.offset==b.offset && a.visible_size==b.visible_size &&
        a.region==b.region && a.view==b.view && a.view_format==b.view_format && a.view_offset==b.view_offset && a.view_size==b.view_size;
}
}
std::unique_ptr<PreparedConfigurationDraw> OwnedRuntime::prepare_native_draw(std::shared_ptr<IniRuntime> registry,
    const RuntimeDraw& draw,const NativeApplicationState& input,bool shader_only,std::string& reason){
    using namespace native_configuration;
    try{
        ApplicationResourceState application;application.allow_unknown_slots=true;application.observe_image=input.observe_image;
        application.preserve_unmapped_ini_slots=true;
        application.independent_initialization=input.independent_initialization;
        std::map<Key,const NativeApplicationBinding*> addresses;
        std::map<uint64_t,std::shared_ptr<const SourceResourceIdentity>> identities;
        std::map<uint64_t,std::shared_ptr<const GpuResourceRuntime::ApplicationImageSource>> images;
        for(const auto& entry:input.bindings){
            const auto k=key(entry.address);const auto& r=entry.resource;
            application.mapped_slots.insert(k);
            if(!addresses.emplace(k,&entry).second)throw std::runtime_error("Native source projection has duplicate registers");
            if(!entry.source_known)continue;
            auto& identity=identities[r.identity];
            if(!identity){auto value=std::make_shared<SourceResourceIdentity>();value->tracking=(r.identity&3)==3?
                SourceResourceIdentity::Tracking::Untracked:SourceResourceIdentity::Tracking::Tracked;
                value->hash_known=r.hash_known;value->hash=r.hash_known?r.hash:0;
                value->cached_data=entry.cached_data;
                if(entry.region_hash)value->region_hash=[read=entry.region_hash](UINT offset,UINT size,uint32_t& hash,std::wstring& why){
                    if(read(offset,size,hash))return true;why=L"Native application buffer region has no completed readable contents";return false;};
                identity=std::move(value);}
            const auto t=std::get<1>(k);
            if(r.image || t==ResourceCopyTargetType::RENDER_TARGET || t==ResourceCopyTargetType::DEPTH_STENCIL_TARGET){
                GpuResourceRuntime::ApplicationImageBinding binding;binding.stage=std::get<0>(k);binding.type=t;binding.slot=entry.address.slot;
                if(r.bound){
                    auto& source=images[r.identity];
                    if(!source){auto value=std::make_shared<GpuResourceRuntime::ApplicationImageSource>();
                        value->resource_id=r.identity;value->source_known=true;value->identity=identity;value->allocation=entry.image;value->retained=r.prepared_source;
                        if(r.prepared_source && r.prepared_source->image){
                            const auto& image=r.prepared_source->image;
                            value->allocation={image->image(),image->description(),image->is_external()?image->external_layout():VK_IMAGE_LAYOUT_GENERAL,true,
                                image->memory(),image->memory_offset(),image->memory_size(),r.keep};
                        }
                        auto& desc=value->description;desc.dimension=D3D11_RESOURCE_DIMENSION(r.dimension);desc.depth=r.depth;
                        desc.texture.Width=r.width;desc.texture.Height=r.height;desc.texture.MipLevels=r.levels;desc.texture.ArraySize=r.layers;
                        desc.texture.Format=DXGI_FORMAT(r.resource_format);desc.texture.SampleDesc.Count=r.samples;desc.texture.BindFlags=r.bind_flags;
                        if(entry.image.description.flags&VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)desc.texture.MiscFlags|=D3D11_RESOURCE_MISC_TEXTURECUBE;
                        if(r.prepared_source)value->description=r.prepared_source->image_description;
                        source=std::move(value);
                    }
                    binding.source=source;binding.view_known=true;binding.volume_full_depth=true;
                    if(r.prepared_source && r.prepared_source->native_view){
                        // Reuse the root provenance, not a growing chain of prior snapshots.
                        binding.native_view=r.prepared_source->native_view;
                    }else if(r.image_view_id && (r.identity&3)==2){
                        auto native=std::make_shared<NativeResource>(r);
                        native->prepared_source.reset();native->prepared_admission.reset();
                        binding.native_view=std::move(native);
                    }
                    binding.format=DXGI_FORMAT(r.format);binding.view_type=VkImageViewType(r.view_type);
                    binding.range={r.aspect,r.base_mip,r.mip_count,r.base_layer,r.layer_count};
                }
                application.images.push_back(std::move(binding));
            }else{
                GpuResourceRuntime::ApplicationBufferBinding binding;binding.stage=std::get<0>(k);binding.type=t;binding.slot=entry.address.slot;
                binding.resource=r;binding.source_known=true;binding.identity=identity;binding.stride=r.stride;binding.retained=r.prepared_source;
                binding.description.ByteWidth=UINT(r.size);binding.description.BindFlags=r.bind_flags;
                binding.description.StructureByteStride=entry.structure_stride;
                if(r.usage&VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)binding.description.MiscFlags|=D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
                if(entry.structure_stride)binding.description.MiscFlags|=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
                if(entry.raw)binding.description.MiscFlags|=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
                if(r.prepared_source)binding.description=r.prepared_source->description;
                binding.format=DXGI_FORMAT(r.format);application.buffers.push_back(std::move(binding));
            }
        }
        const auto& args=input.indirect_arguments;
        if(draw.indirect && args.bound && args.keep && args.size && args.size<=UINT_MAX){
            GpuResourceRuntime::ApplicationBufferBinding binding;
            for(const auto& alias:application.buffers){
                if(alias.resource.identity==args.identity){binding=alias;break;}
            }
            binding.type=ResourceCopyTargetType::THIS_RESOURCE;
            binding.stage=0;binding.slot=0;binding.stride=0;binding.format=DXGI_FORMAT_UNKNOWN;
            binding.resource=args;binding.resource.offset=0;binding.resource.range=args.size;
            binding.source_known=true;binding.description.ByteWidth=UINT(args.size);
            binding.description.BindFlags=args.bind_flags;binding.description.MiscFlags|=D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
            if(!binding.identity){
                auto identity=std::make_shared<SourceResourceIdentity>();
                identity->tracking=SourceResourceIdentity::Tracking::Tracked;
                binding.identity=std::move(identity);
            }
            application.buffers.push_back(std::move(binding));
        }
        auto observed=draw;if(input.source_declaration)observed.source_declaration=input.source_declaration;
        observed.indirect_arguments=input.indirect_arguments;
        auto pending=prepare_draw(std::move(registry),observed,application,shader_only,reason);if(!pending)return {};
        auto& state=*pending->impl_;auto previous=state.initial;
        assets::FilePlan result;result.skip_original=state.plan.skip_original;
        auto emit=[&](const std::shared_ptr<const PreparedDrawState>& snapshot){
            if(!snapshot)throw std::runtime_error("Native source call has no original binding state");
            auto keys=previous;for(const auto& item:snapshot->bindings)keys.emplace(item);
            for(const auto& item:keys){
                const auto found=snapshot->bindings.find(item.first);
                const GpuResourceBinding value=found==snapshot->bindings.end()?GpuResourceBinding{}:found->second;
                const auto old=previous.find(item.first);
                if(old!=previous.end() && equal(old->second,value))continue;
                const auto address=addresses.find(item.first);
                // Original virtual registers survive for later CustomShaders.
                // Only the current application's actual descriptor addresses
                // require a physical bind at this caller.
                if(address==addresses.end())continue;
                assets::FilePlanItem binding;binding.native_target=address->second->address;
                const auto kind=binding.native_target.kind;
                binding.operation=kind==NativeBinding::Vertex?assets::FileOperation::BindVertex:
                    kind==NativeBinding::Index?assets::FileOperation::BindIndex:assets::FileOperation::NativeBind;
                binding.slot=binding.native_target.slot;
                if(value){
                    auto resource=std::make_shared<NativeResource>();
                    // Preserve original image view identities when restoring an imported view.
                    for(const auto& original:state.initial){
                        const auto& baseline=original.second;
                        if((value.buffer && value.buffer==baseline.buffer) || (value.image && value.image==baseline.image)){
                            *resource=addresses.at(original.first)->resource;
                            if(value.image && value.image_view!=baseline.image_view){resource->image_view=0;resource->image_view_id=0;continue;}
                            break;
                        }
                    }
                    if(value.native_view && value.image && value.image_view && value.image->is_external()){
                        const auto& native=*value.native_view;const auto& view=*value.image_view;const auto& range=view.range();
                        if(native.image==uint64_t(value.image->image()) && native.view_type==view.type() && native.view_format==view.format() &&
                           native.aspect==range.aspectMask && native.base_mip==range.baseMipLevel && native.mip_count==range.levelCount &&
                           native.base_layer==range.baseArrayLayer && native.layer_count==range.layerCount)*resource=native;
                    }
                    resource->bound=true;resource->prepared_admission=snapshot->admission();
                    resource->prepared_source=std::make_shared<const GpuResourceBinding>(value);resource->keep=resource->prepared_source;
                    resource->initialization=value.initialization;
                    resource->initial_contents=snapshot->initial_contents.count(value.buffer?static_cast<const void*>(value.buffer.get()):value.image.get())!=0;
                    resource->initial_contents=resource->initial_contents && !(value.bind_flags()&
                        (D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_RENDER_TARGET|D3D11_BIND_DEPTH_STENCIL));
                    resource->bind_flags=value.bind_flags();resource->format=value.format;resource->format_known=true;
                    resource->stride=value.stride;resource->buffer_stride=value.description.StructureByteStride;
                    if(value.buffer){
                        resource->dimension=1;resource->buffer=uint64_t(value.buffer->buffer());resource->size=value.buffer->size();
                        resource->usage=value.buffer->usage();resource->memory=uint64_t(value.buffer->memory());
                        resource->memory_offset=value.buffer->memory_offset();resource->memory_size=value.buffer->memory_size();resource->memory_linear=true;
                        if(!resource->identity)resource->identity=reinterpret_cast<uint64_t>(value.buffer.get())|3;
                        const bool view=value.view!=GpuResourceBinding::View::None;
                        resource->offset=view?value.view_offset:value.offset;
                        resource->range=view?value.view_size:value.visible_size;
                        if(!resource->range && resource->offset<resource->size)resource->range=resource->size-resource->offset;
                        if(kind==NativeBinding::Index){
                            resource->index_size=value.format==DXGI_FORMAT_R16_UINT?2:value.format==DXGI_FORMAT_R32_UINT?4:0;
                            const auto proof=snapshot->indices.find(value.buffer);
                            if(proof!=snapshot->indices.end()){
                                const auto& indices=proof->second;const bool short_index=resource->index_size==2;
                                resource->index_bounds=short_index?indices.valid16:resource->index_size==4 && indices.valid32;
                                resource->index_min=short_index?indices.min16:indices.min32;resource->index_max=short_index?indices.max16:indices.max32;
                            }
                        }
                    }else{
                        if(!value.image_view)throw std::runtime_error("Original image binding has no view");
                        const auto& d=value.image->description();const auto& v=*value.image_view;const auto& range=v.range();
                        resource->dimension=uint32_t(d.type)+2;resource->image=uint64_t(value.image->image());
                        if(!resource->identity)resource->identity=reinterpret_cast<uint64_t>(value.image.get())|3;
                        if(!resource->image_view || value.image->is_external()==false){resource->image_view=uint64_t(v.view());resource->image_view_id=0;}
                        resource->width=d.extent.width;resource->height=d.extent.height;resource->depth=d.extent.depth;
                        resource->levels=d.mips;resource->layers=d.layers;resource->samples=d.samples;resource->usage=d.usage;
                        resource->view_type=v.type();resource->view_format=v.format();resource->image_safe=true;
                        resource->format=value.view_format;resource->resource_format=value.image_description.texture.Format;
                        resource->aspect=range.aspectMask;resource->base_mip=range.baseMipLevel;resource->mip_count=range.levelCount;
                        resource->base_layer=range.baseArrayLayer;resource->layer_count=range.layerCount;
                        resource->image_layout=value.image->is_external()?value.image->external_layout():VK_IMAGE_LAYOUT_GENERAL;
                        resource->memory=uint64_t(value.image->memory());resource->memory_offset=value.image->memory_offset();resource->memory_size=value.image->memory_size();resource->memory_linear=false;
                    }
                    binding.native_resource=std::move(resource);
                }
                result.items.push_back(std::move(binding));
            }
            previous=snapshot->bindings;
        };
        for(size_t i=0;i<state.plan.items.size();++i){
            if(i==state.plan.pre_items)result.pre_items=result.items.size();
            auto item=state.plan.items[i];if(item.prepared_state){emit(item.prepared_state);item.prepared_state.reset();}
            result.items.push_back(std::move(item));
        }
        if(state.plan.pre_items==state.plan.items.size())result.pre_items=result.items.size();
        emit(state.plan.prepared_final_state);result.prepared_final_state=state.plan.prepared_final_state;
        state.plan=std::move(result);return pending;
    }catch(const std::exception& error){reason=error.what();return {};}
}
