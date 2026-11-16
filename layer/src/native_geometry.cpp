#include "native_geometry.h"
#include "command_owner.h"
#include "native_targets.h"
#include <algorithm>
#include <set>
#include <tuple>

namespace efvk {
namespace {
using Resource = efmi::frontend::NativeResource;
using Operation = efmi::assets::FileOperation;
using ResourceKey = std::tuple<uint64_t,uint64_t,uint64_t,uint64_t>;
ResourceKey resource_key(const Resource& resource) {
    return {resource.identity,resource.buffer,resource.publication_semaphore,resource.publication_value};
}
struct GeometryOwner final : CommandOwner {
    Device* device = nullptr;
    std::vector<std::shared_ptr<const Resource>> resources;
    std::set<ResourceKey> identities;
    std::set<std::shared_ptr<const PipelineVariant>> pipelines;
    std::map<VkSemaphore, uint64_t> publications;
    std::map<VkQueue, std::map<VkSemaphore, uint64_t>> waited;
    std::mutex mutex;
    bool sealed = false;
    void reset_after_completion() override { resources.clear(); identities.clear(); pipelines.clear(); publications.clear(); waited.clear(); used=false; sealed=false; }
    bool seal() override { if(!used || sealed)return false; sealed=true; return true; }
    VkResult before_submit(VkQueue queue) override {
        std::lock_guard<std::mutex> lock(mutex);
        auto& prior=waited[queue]; std::vector<VkSemaphore> semaphores; std::vector<uint64_t> values;
        for(const auto& p:publications)if(prior[p.first]<p.second){semaphores.push_back(p.first);values.push_back(p.second);}
        if(semaphores.empty())return VK_SUCCESS;
        std::vector<VkPipelineStageFlags> stages(values.size(),VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
        VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
        timeline.waitSemaphoreValueCount=uint32_t(values.size());timeline.pWaitSemaphoreValues=values.data();
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.pNext=&timeline;
        submit.waitSemaphoreCount=uint32_t(values.size());submit.pWaitSemaphores=semaphores.data();submit.pWaitDstStageMask=stages.data();
        const auto result=device->d.QueueSubmit(queue,1,&submit,VK_NULL_HANDLE);
        if(result==VK_SUCCESS)for(size_t i=0;i<values.size();++i)prior[semaphores[i]]=values[i];
        return result;
    }
};
const char geometry_owner=0;
std::shared_ptr<GeometryOwner> owner_for_geometry(Device* device,CmdBuf* cb)
{
    std::shared_ptr<GeometryOwner> owner;
    for(const auto& candidate:cb->owners)if(candidate && candidate->backend==&geometry_owner){
        owner=std::static_pointer_cast<GeometryOwner>(candidate);break;
    }
    if(!owner){owner=std::make_shared<GeometryOwner>();owner->backend=&geometry_owner;owner->device=device;cb->owners.push_back(owner);}
    owner->used=true;
    return owner;
}
void retain(Device* device,CmdBuf* cb,const std::shared_ptr<const Resource>& resource)
{
    if(!resource)return;
    const auto owner=owner_for_geometry(device,cb);
    if(owner->identities.insert(resource_key(*resource)).second)owner->resources.push_back(resource);
    if(resource->publication_semaphore){auto& value=owner->publications[VkSemaphore(resource->publication_semaphore)];value=std::max(value,resource->publication_value);}
}
}

bool prepare_native_geometry(Device* device,CmdBuf* cb,const DrawRec& original,const efmi::assets::FilePlan& plan,
                             NativeGeometryPlan& output,std::string& why,NativeGeometryContext* context,
                             const NativeTargetPlan* targets,bool* pending)
{
    auto refuse=[&](const char* reason){why=reason;return false;};
    NativeGeometryPlan next;next.bindings.resize(plan.items.size());next.draws.resize(plan.items.size()+1);
    auto state=cb->native_geometry?std::make_shared<NativeGeometryState>(*cb->native_geometry):std::make_shared<NativeGeometryState>();
    std::set<ResourceKey> retained;
    std::set<std::shared_ptr<const PipelineVariant>> retained_pipelines;
    for(const auto& candidate:cb->owners)if(candidate && candidate->backend==&geometry_owner){
        const auto owner=std::static_pointer_cast<GeometryOwner>(candidate);
        if(owner->sealed)return refuse("Geometry recording owner is already sealed");
        retained=owner->identities;retained_pipelines=owner->pipelines;break;
    }
    std::shared_lock<std::shared_mutex> lock(device->res_mx);
    const auto pipeline=device->pipes.find(original.pipeline);
    if(pipeline==device->pipes.end() || !pipeline->second.recipe)return refuse("Geometry pipeline recipe is unavailable");
    const auto original_recipe=pipeline->second.recipe;
    auto selected=original_recipe;auto selected_layout=pipeline->second.layout;
    if(cb->native_pipeline && cb->native_pipeline->bound==original.bound && cb->native_pipeline->recipe){
        selected=cb->native_pipeline->recipe;selected_layout=cb->native_pipeline->layout;
    }
    const auto& recipe=*selected;
    uint64_t app_sizes[kMaxVb]={};bool app_valid[kMaxVb]={};
    for(uint32_t slot=0;slot<kMaxVb;++slot){const auto found=device->bufs.find(original.vb[slot].buf);
        if(found!=device->bufs.end()){app_valid[slot]=true;app_sizes[slot]=found->second.size;}}
    lock.unlock();
    auto validate=[&](const DrawRec& call,const std::vector<efmi::assets::LayoutEdit>& edits,size_t at,size_t before){
        if(recipe.compute)return true; // IA state does not affect dispatch.
        if((state->empty() && edits.empty()) || !call.count || !call.instances)return true;
        if(call.kind!=DK_DRAW && call.kind!=DK_INDEXED)return refuse("Persistent geometry needs direct draw arguments");
        if(!recipe.geometry_complete || call.vertex_input)return refuse("Geometry needs a complete static vertex declaration");
        uint64_t low=call.first,high=uint64_t(call.first)+call.count-1;
        if(call.kind==DK_INDEXED){
            if(!state->index_overridden)return refuse("Replacement vertices with application indices need a proven index range");
            const auto& index=state->index;
            if(!index || !index->bound)return refuse("Indexed geometry has a null index binding");
            if(!index->index_bounds || !index->index_size || index->offset>index->size ||
               uint64_t(call.first)+call.count>(index->size-index->offset)/index->index_size)
                return refuse("Geometry draw exceeds the uploaded index buffer");
            const int64_t first=int64_t(index->index_min)+call.vertex_offset,last=int64_t(index->index_max)+call.vertex_offset;
            if(first<0 || last<first)return refuse("Geometry base vertex leaves the vertex domain");
            low=uint64_t(first);high=uint64_t(last);
        }
        (void)low;
        const bool dynamic_stride=std::find(recipe.dynamic.begin(),recipe.dynamic.end(),VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE)!=recipe.dynamic.end();
        VertexInputRecipe vertex=recipe.vertex;
        bool changed=false;
        if(!edits.empty()){
            if(!context || !context->inputs)return refuse("Geometry edits need exact shader input semantics");
            std::shared_ptr<const ShaderInfo> shader;
            for(const auto& stage:original_recipe->stages)if(stage.stage==VK_SHADER_STAGE_VERTEX_BIT)shader=stage.shader;
            if(!shader)return refuse("Geometry source vertex module is unavailable");
            std::shared_ptr<const efmi::frontend::VertexProfile> profile;
            {
                std::lock_guard<std::mutex> guard(context->mutex);
                auto found=context->profiles.find(shader);
                if(found==context->profiles.end()){
                    if(context->profiles.size()>=4096)return refuse("Geometry semantic lookup budget exceeded");
                    NativeGeometryContext::Profile entry;auto value=std::make_shared<efmi::frontend::VertexProfile>();
                    if(context->inputs->lookup(shader->code,*value,entry.reason))entry.value=std::move(value);
                    found=context->profiles.emplace(shader,std::move(entry)).first;
                }
                if(!found->second.value){why=found->second.reason;return false;}
                profile=found->second.value;
            }
            efmi::assets::FileDraws input;input.draws.emplace_back();input.draws[0].layout=edits;
            for(const auto& binding:state->vertices){
                if(!binding.second)continue;
                auto file=std::make_shared<efmi::assets::FileResource>();file->usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;file->stride=binding.second->stride;
                auto& stream=input.draws[0].vertex[binding.first];stream.source=efmi::assets::BindingSource::File;stream.resource=std::move(file);
            }
            efmi::frontend::VertexPlan resolved;
            if(!profile->resolve(input,shader->code,vertex,resolved,why))return false;
            vertex=std::move(resolved.draws[0]);changed=true;
        }
        for(auto& binding:vertex.bindings){
            const auto replacement=state->vertices.find(binding.binding);
            if(replacement!=state->vertices.end() && replacement->second && replacement->second->stride!=binding.stride){
                binding.stride=replacement->second->stride;changed=true;
            }
        }
        if(changed && dynamic_stride)return refuse("Geometry layout changes need explicit dynamic stride recording");
        for(const auto& attribute:vertex.attributes){
            const auto binding=std::find_if(vertex.bindings.begin(),vertex.bindings.end(),[&](const auto& b){return b.binding==attribute.binding;});
            if(binding==vertex.bindings.end() || binding->binding>=kMaxVb)return refuse("Geometry vertex binding is outside tracked input slots");
            const auto width=vertex_format_bytes(attribute.format);
            if(!width)return refuse("Geometry attribute format is unsupported");
            const auto& app=call.vb[binding->binding];
            uint32_t stride=binding->stride;
            if(dynamic_stride){if(!app.stride_known)return refuse("Geometry dynamic stride is unknown");stride=app.stride;}
            uint64_t size=0,offset=0;
            const auto replacement=state->vertices.find(binding->binding);
            if(replacement!=state->vertices.end()){
                const auto& resource=replacement->second;
                if(!resource || !resource->bound)return refuse("Geometry uses a null vertex binding");
                if(resource->stride!=stride)return refuse("Geometry file stride needs a matching pipeline variant");
                size=resource->size;offset=resource->offset;
            }else{
                if(!app_valid[binding->binding] || app.off>app_sizes[binding->binding])return refuse("Geometry application vertex buffer is unavailable");
                size=app_sizes[binding->binding];offset=app.off;
                if(app.size!=VK_WHOLE_SIZE)size=offset+std::min(app.size,size-offset);
            }
            const uint64_t element=binding->inputRate==VK_VERTEX_INPUT_RATE_INSTANCE?uint64_t(call.first_instance)+call.instances-1:high;
            const uint64_t tail=uint64_t(attribute.offset)+width;
            if(offset>size || tail>size-offset || (stride && element>(size-offset-tail)/stride))
                return refuse("Geometry vertex fetch exceeds its retained buffer");
        }
        if(changed){
            if(!context || !context->variants || !selected_layout)return refuse("Geometry pipeline variant service is unavailable");
            for(const auto& binding:vertex.bindings)if(binding.stride>device->physical_properties.limits.maxVertexInputBindingStride)
                return refuse("Geometry vertex stride exceeds device limits");
            for(const auto& attribute:vertex.attributes)if(attribute.offset>device->physical_properties.limits.maxVertexInputAttributeOffset)
                return refuse("Geometry vertex attribute offset exceeds device limits");
            uint64_t identity=0;
            {
                static std::atomic<uint64_t> serial{uint64_t(0x42)<<40};
                std::lock_guard<std::mutex> guard(context->mutex);auto found=context->identities.find(selected);
                if(found==context->identities.end()){
                    if(context->identities.size()>=4096)return refuse("Geometry pipeline identity budget exceeded");
                    found=context->identities.emplace(selected,++serial).first;
                }
                identity=found->second;
            }
            FragmentPatch patch;patch.keep_vertex=true;patch.native_only=true;patch.own_geometry=true;
            // The selected recipe already contains the exact active shader fixes and hunting state.
            patch.native_shaders=std::make_shared<const NativeShaderSet>();
            const auto variant=context->variants->request(identity,selected,selected_layout,vertex,patch);
            if(!variant.variant){
                if(pending && (variant.state==VariantState::Queued || variant.state==VariantState::Building))*pending=true;
                why=variant.reason.empty()?"Geometry pipeline variant is pending":variant.reason;return false;
            }
            if(retained_pipelines.insert(variant.variant).second && retained_pipelines.size()>4096)return refuse("Geometry recording pipeline budget exceeded");
            const auto bound=native_target_variant(device,cb,*variant.variant,why,targets,before);
            if(!bound)return false;
            next.draws[at]={variant.variant,bound};
        }
        return true;
    };
    const size_t split=std::min(plan.pre_items,plan.items.size());
    for(size_t i=0;i<=plan.items.size();++i){
        if(i==split && !plan.skip_original && !validate(original,{},plan.items.size(),i))return false;
        if(i==plan.items.size())break;
        const auto& item=plan.items[i];
        if(item.operation==Operation::BindVertex || item.operation==Operation::BindIndex){
            if(recipe.compute)return refuse("Geometry cannot be bound by a compute call");
            if(item.owned)return refuse("Owned geometry needs its producer and publication proof");
            const bool vertex=item.operation==Operation::BindVertex;
            if(vertex && item.slot>=kMaxVb)return refuse("Geometry vertex slot is outside tracked inputs");
            const auto& resource=item.native_resource;
            if(resource && (!resource->bound || !resource->buffer || !resource->keep || !resource->uploaded_readonly ||
                !(resource->usage&(vertex?VK_BUFFER_USAGE_VERTEX_BUFFER_BIT:VK_BUFFER_USAGE_INDEX_BUFFER_BIT))))
                return refuse("Geometry binding needs a retained immutable upload with the correct usage");
            if(resource && retained.insert(resource_key(*resource)).second && retained.size()>4096)
                return refuse("Geometry recording resource budget exceeded");
            if(item.application_slot>=0){
                if(vertex){
                    if(cb->native_geometry && cb->native_geometry->vertices.count(item.slot))return refuse("Original vertex reference needs the persistent binding snapshot");
                    if(!original.vb[item.slot].buf)return refuse("Original vertex binding is null");
                    state->vertices.erase(item.slot);
                }else{
                    if(cb->native_geometry && cb->native_geometry->index_overridden)return refuse("Original index reference needs the persistent binding snapshot");
                    if(!original.ib || (original.index_size!=2 && original.index_size!=4))return refuse("Original index binding or type is unsupported");
                    state->index_overridden=false;state->index.reset();
                }
            }else if(vertex)state->vertices[item.slot]=resource;
            else {state->index_overridden=true;state->index=resource;}
            next.bindings[i]=std::make_shared<const NativeGeometryState>(*state);
        }else if(item.operation==Operation::DrawIndexed){
            DrawRec call=original;call.kind=item.indexed?DK_INDEXED:DK_DRAW;
            call.count=item.index_count;call.first=item.first_index;call.vertex_offset=item.vertex_offset;
            call.instances=item.instance_count;call.first_instance=item.first_instance;
            if(!validate(call,item.layout,i,i))return false;
        }else if(item.operation==Operation::Caller && !validate(original,item.layout,i,i))return false;
    }
    output=std::move(next);return true;
}

void record_native_geometry(Device* device,CmdBuf* cb,const efmi::assets::FilePlan& plan,const NativeGeometryPlan& geometry,size_t index)
{
    const auto& item=plan.items[index];const auto& resource=item.native_resource;
    cb->native_geometry=geometry.bindings[index]->empty()?nullptr:geometry.bindings[index];
    if(item.application_slot>=0){
        if(item.operation==Operation::BindVertex){const auto& b=cb->vb[item.slot];device->d.CmdBindVertexBuffers(cb->handle,item.slot,1,&b.buf,&b.off);}
        else device->d.CmdBindIndexBuffer(cb->handle,cb->ib,cb->ib_off,cb->index_size==2?VK_INDEX_TYPE_UINT16:VK_INDEX_TYPE_UINT32);
    }else if(resource){
        retain(device,cb,resource);
        if(item.operation==Operation::BindVertex){const VkBuffer buffer=VkBuffer(resource->buffer);const VkDeviceSize offset=resource->offset;
            device->d.CmdBindVertexBuffers(cb->handle,item.slot,1,&buffer,&offset);
        }else device->d.CmdBindIndexBuffer(cb->handle,VkBuffer(resource->buffer),resource->offset,resource->index_size==2?VK_INDEX_TYPE_UINT16:VK_INDEX_TYPE_UINT32);
    }
}
void bind_native_geometry_pipeline(Device* device,CmdBuf* cb,const NativeGeometryPlan& geometry,size_t index)
{
    const auto& draw=geometry.draws[index];if(!draw.variant)return;
    owner_for_geometry(device,cb)->pipelines.insert(draw.variant);
    device->d.CmdBindPipeline(cb->handle,VK_PIPELINE_BIND_POINT_GRAPHICS,draw.pipeline);
}
}
