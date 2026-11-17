#pragma once
#include "native_resources.h"
#include "../../frontend/xxmi/owned_runtime.h"
#include "../../frontend/gpu/source_registers.h"
namespace efvk {
inline bool native_application_state(Device* device,CmdBuf* cb,const DrawRec& draw,
    efmi::frontend::NativeApplicationState& output,std::string& why,
    const std::vector<efmi::frontend::NativeBinding>& aliases={}){
    using B=efmi::frontend::NativeBinding;using S=efmi::gpu::SourceRegister;
    efmi::frontend::NativeApplicationState next;
    using DeclarationAddress=std::tuple<unsigned,B::Kind,unsigned>;
    std::map<DeclarationAddress,S> declarations;
    std::shared_lock<std::shared_mutex> lock(device->res_mx);
    const auto pipeline=device->pipes.find(draw.pipeline);
    if(pipeline==device->pipes.end()){why="Original context projection needs the current pipeline";return false;}
    auto append=[&](B address,const S* declaration){
        const DeclarationAddress declaration_key{address.stage,address.kind,address.slot};
        declarations.erase(declaration_key);
        if(declaration)declarations.emplace(declaration_key,*declaration);
        efmi::frontend::NativeApplicationBinding entry;entry.address=address;
        std::string unknown;
        entry.source_known=native_resource_locked(device,cb,draw,address,entry.resource,unknown);
        auto& r=entry.resource;
        if(entry.source_known && r.bound){
            entry.source_known=bool(r.keep) && r.format_known;
            if(r.prepared_source && r.prepared_admission && *r.prepared_admission){
                entry.source_known=true;
            }else if(r.image){
                const auto found=device->images.find(VkImage(r.image));
                if(found==device->images.end())entry.source_known=false;
                else{
                    const auto& actual=found->second;auto& image=entry.image;
                    image.image=VkImage(r.image);image.owner=r.keep;image.memory=actual.mem;
                    image.memory_offset=actual.mem_off;image.memory_size=actual.mem_size;
                    image.description={actual.type,actual.format,actual.extent,actual.levels,actual.layers,
                        VkSampleCountFlagBits(actual.samples),actual.usage,actual.flags};
                    // Aspect union includes both depth and stencil in a packed allocation.
                    VkImageAspectFlags aspects=r.aspect;
                    if(actual.format==VK_FORMAT_D32_SFLOAT_S8_UINT || actual.format==VK_FORMAT_D24_UNORM_S8_UINT ||
                       actual.format==VK_FORMAT_D16_UNORM_S8_UINT)aspects=VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;
                    image.uniform_layout=cb->image_layouts.get(actual.id,actual.levels,actual.layers,{aspects,0,actual.levels,0,actual.layers},image.layout);
                    entry.source_known&=actual.capture_safe && r.image_safe && image.uniform_layout;
                }
            }else if(r.buffer){
                entry.source_known&=r.size<=UINT32_MAX;
                if(declaration && (declaration->kind==S::BufferSrv || declaration->kind==S::BufferUav)){
                    // SPIR-V uint words cannot distinguish raw from structured uint.
                    entry.source_known&=!declaration->words && declaration->stride!=0;
                    entry.structure_stride=declaration->stride;r.stride=declaration->stride;r.format=0;
                }
            }
            if(r.buffer && (r.identity&3)==0){
                const auto handle=VkBuffer(r.buffer);const auto id=r.identity;
                entry.cached_data=[device,handle,id]()->std::shared_ptr<const std::vector<uint8_t>>{
                    std::shared_lock<std::shared_mutex> guard(device->res_mx);
                    const auto found=device->bufs.find(handle);
                    if(found==device->bufs.end() || (uint64_t(found->second.id)<<2)!=id ||
                       !found->second.size || found->second.size>(64u<<20))return {};
                    auto bytes=std::make_shared<std::vector<uint8_t>>(size_t(found->second.size));
                    const auto source=read_buffer_locked(device,found->second,0,found->second.size,bytes->data());
                    if(source!=SRC_MAPPED && source!=SRC_SHADOW)return {};
                    return bytes;
                };
                entry.region_hash=[device,handle,id](uint32_t offset,uint32_t size,uint32_t& hash){
                    if(size>(64u<<20))return false;
                    std::shared_lock<std::shared_mutex> guard(device->res_mx);
                    const auto found=device->bufs.find(handle);
                    if(found==device->bufs.end() || (uint64_t(found->second.id)<<2)!=id)return false;
                    std::vector<uint8_t> bytes(size);
                    const auto source=read_buffer_locked(device,found->second,offset,size,bytes.data());
                    if(source!=SRC_MAPPED && source!=SRC_SHADOW)return false;
                    hash=crc32c(0,bytes.data(),bytes.size());return true;
                };
            }
        }
        next.bindings.push_back(std::move(entry));
    };
    for(unsigned slot=0;slot<kMaxVb;++slot){B address;address.kind=B::Vertex;address.slot=slot;append(address,nullptr);}
    {B address;address.kind=B::Index;append(address,nullptr);}
    for(unsigned slot=0;slot<8;++slot){B address;address.kind=B::ColorTarget;address.slot=slot;append(address,nullptr);}
    {B address;address.kind=B::DepthTarget;append(address,nullptr);}
    for(const auto& shader:pipeline->second.shaders){
        unsigned stage=0;while(stage<6 && shader.stage!=(1u<<stage))++stage;
        if(stage==6 || !shader.shader){why="Original context has unknown shader metadata";return false;}
        std::vector<S> registers;if(!efmi::gpu::source_registers(shader.shader->code,registers,why))return false;
        for(const auto& resource:registers){
            if(resource.kind==S::Sampler)continue;
            for(unsigned element=0;element<resource.count;++element){
                if(next.bindings.size()>=1024){why="Original context source register budget exceeded";return false;}
                B address;address.stage=uint8_t(stage);address.hash=shader.shader->hash;address.slot=resource.index+element;
                address.set=resource.set;address.binding=resource.binding;address.element=element;
                address.kind=resource.letter=='b'?B::Constant:resource.letter=='u'?B::Access:B::Resource;
                append(address,&resource);
            }
        }
        for(const auto& address:aliases)if(address.stage==stage && address.hash==shader.shader->hash &&
            (address.kind==B::Resource || address.kind==B::Constant || address.kind==B::Access)){
            const auto source=std::find_if(registers.begin(),registers.end(),[&](const S& r){return r.set==address.set && r.binding==address.binding;});
            next.bindings.erase(std::remove_if(next.bindings.begin(),next.bindings.end(),[&](const auto& r){
                const auto& old=r.address;
                const bool removed=old.stage==address.stage && (old.kind==B::Resource || old.kind==B::Constant || old.kind==B::Access) &&
                    ((old.kind==address.kind && old.slot==address.slot) ||
                     (old.set==address.set && old.binding==address.binding && old.element==address.element));
                if(removed)declarations.erase({old.stage,old.kind,old.slot});return removed;
            }),next.bindings.end());
            append(address,source==registers.end()?nullptr:&*source);
        }
    }
    next.source_declaration=[declarations=std::move(declarations)](wchar_t stage,unsigned slot,
        efmi::frontend::ShaderDeclarationQuery query,float& value,std::string& reason){
        using Q=efmi::frontend::ShaderDeclarationQuery;
        const auto* names=L"vhdgpc";const auto* found=stage?wcschr(names,stage):nullptr;
        if(!found){reason="Unknown source declaration stage";return false;}
        const bool constant=query==Q::ConstantType || query==Q::ConstantSize;
        const auto entry=declarations.find({unsigned(found-names),constant?B::Constant:B::Resource,slot});
        // DX11 may retain declarations that its Vulkan compiler eliminates. A
        // missing variable is not proof of a missing original declaration.
        if(entry==declarations.end()){reason="Source declaration has no verified current shader variable";return false;}
        const auto& source=entry->second;
        if(constant){
            if(source.kind!=S::ConstantBuffer || query==Q::ConstantType || !source.bytes || source.bytes>65536){
                reason="SPIR-V does not prove the original constant declaration property";return false;}
            value=float((source.bytes+15)/16);return true;
        }
        auto mapped=source;mapped.index=slot;mapped.count=1;
        efmi::gpu::SourceSrvDeclaration result;
        if(!efmi::gpu::source_srv_declaration({mapped},slot,result,reason))return false;
        if(result.words){reason="Source uint words cannot distinguish raw from structured declarations";return false;}
        switch(query){
        case Q::ResourceType:value=float(result.type);return true;
        case Q::ResourceStride:value=result.type?float(result.stride):-1.0f;return true;
        case Q::ResourceDimension:value=result.type?float(result.dimension):-1.0f;return true;
        default:reason="Unknown original declaration property";return false;
        }
    };
    // IA and descriptor aliases share one resource description. A vertex slot's
    // binding stride remains independent of that resource's structured stride.
    std::map<uint64_t,uint32_t> strides;
    for(const auto& entry:next.bindings)if(entry.source_known && entry.resource.buffer && entry.structure_stride){
        auto inserted=strides.emplace(entry.resource.identity,entry.structure_stride);
        if(!inserted.second && inserted.first->second!=entry.structure_stride){why="Native resource aliases disagree on structure stride";return false;}
    }
    for(auto& entry:next.bindings)if(entry.resource.buffer && strides.count(entry.resource.identity))entry.structure_stride=strides.at(entry.resource.identity);
    next.observe_image=[device,cb](uint64_t identity,const efmi::gpu::Image& image,efmi::gpu::ApplicationImage& result,std::string& reason){
        if(!image || !image->retained_external()){reason="Saved application image has no retained allocation";return false;}
        const auto& description=image->description();VkImageLayout current;
        std::shared_lock<std::shared_mutex> guard(device->res_mx);
        const auto found=device->images.find(image->image());
        if(found!=device->images.end() && ((uint64_t(found->second.id)<<2)|2)!=identity){reason="Saved application image identity was recycled";return false;}
        if(!cb->image_layouts.get(uint32_t(identity>>2),description.mips,description.layers,image->full_range(),current)){
            reason="Saved application image has no fresh whole-image layout in this recording";return false;
        }
        result={image->image(),description,current,true,image->memory(),image->memory_offset(),image->memory_size(),image};return true;
    };
    next.independent_initialization=device->application_queue_count && device->material_queues.size()==device->application_queue_count &&
        (device->application_queue_count==1 || (device->upload_queue && device->timeline_semaphores));
    output=std::move(next);return true;
}
}
