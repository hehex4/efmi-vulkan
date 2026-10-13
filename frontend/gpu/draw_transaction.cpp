#include "draw_transaction.h"
#include <algorithm>

namespace efmi::gpu {
namespace {
bool geometry_valid(const efvk::DrawGeometry& g){
    const uint32_t bytes=g.index_type==VK_INDEX_TYPE_UINT16?2:g.index_type==VK_INDEX_TYPE_UINT32?4:0;
    return bytes && g.index && g.index_offset%bytes==0 && !g.buffers.empty() && g.buffers.size()==g.offsets.size() &&
           g.first_binding<32 && g.buffers.size()<=32-g.first_binding &&
           std::all_of(g.buffers.begin(),g.buffers.end(),[](auto b){return b!=VK_NULL_HANDLE;});
}
bool ready(const assets::FileBinding& file,VkBufferUsageFlags usage){
    return file.source==assets::BindingSource::File && file.resource && file.resource->usage==usage &&
           file.buffer && file.buffer->state()==assets::UploadState::Ready && file.buffer->buffer() &&
           (file.buffer->usage()&usage)==usage;
}
}
bool prepare_draw_transaction(const assets::FileDraws& files,const std::vector<efvk::VertexInputRecipe>& vertices,
                              const MaterialPlan& materials,const efvk::DrawGeometry& original,DrawTransaction& output,std::string& reason)
{
    reason.clear();auto refuse=[&](const char* text){reason=text;return false;};
    if(!geometry_valid(original) || files.draws.size()>256 || files.draws.size()!=vertices.size() || files.draws.size()!=materials.draws.size())
        return refuse("Draw transaction lacks a complete original or corresponding frontend plans");
    DrawTransaction next;next.skip_original=files.skip_original;next.original=original;next.draws.reserve(files.draws.size());
    for(size_t i=0;i<files.draws.size();++i){
        const auto& source=files.draws[i];TransactionDraw draw;draw.geometry=original;draw.vertex=vertices[i];draw.material=materials.draws[i];
        draw.palette=source.palette;
        if(!source.index_count || source.instance_count!=1 || source.first_instance || !draw.palette || !draw.palette->identity ||
           draw.palette->bones.empty() || draw.palette->bones.size()>(64u<<20))
            return refuse("Draw transaction requires one complete compact palette and direct indexed draw");
        if(draw.material.patch.images.size()!=draw.material.images.size() || draw.material.images.size()>32 ||
           draw.material.patch.nulls.size()>32)return refuse("Draw material ownership differs from the variant contract");
        for(const auto& image:draw.material.images)if(!image.view || !image.owner)return refuse("Draw material image owner is missing");
        if(!ready(source.index,VK_BUFFER_USAGE_INDEX_BUFFER_BIT))return refuse("Draw index file is not Ready");
        const auto type=source.index.resource->index_type;const uint32_t bytes=type==VK_INDEX_TYPE_UINT16?2:type==VK_INDEX_TYPE_UINT32?4:0;
        uint32_t low=0,high=0;
        if(!bytes || source.index.resource->stride!=bytes || !source.index.buffer->index_extents(type,low,high) ||
           (uint64_t(source.first_index)+source.index_count)*bytes>source.index.buffer->size() ||
           int64_t(low)+source.vertex_offset<0)return refuse("Draw index bounds or base vertex are not proved");
        draw.geometry.index=source.index.buffer->buffer();draw.geometry.index_offset=0;draw.geometry.index_type=type;
        draw.buffers.push_back(source.index.buffer);
        // Replacement indices cannot prove bounds for an inherited vertex array.
        // The game's stride-zero default attributes do not advance with indices.
        for(const auto& binding:draw.vertex.bindings){
            if(binding.binding>=source.vertex.size())return refuse("Vertex binding is outside the supported slot range");
            if(source.vertex[binding.binding].source==assets::BindingSource::Application && binding.stride)
                return refuse("Inherited advancing vertex stream has no replacement-index bounds proof");
        }
        for(uint32_t slot=0;slot<source.vertex.size();++slot){
            const auto& file=source.vertex[slot];if(file.source==assets::BindingSource::Application)continue;
            if(slot<original.first_binding || slot-original.first_binding>=original.buffers.size())
                return refuse("Replacement stream is outside the restorable original binding range");
            if(!ready(file,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) || !file.resource->stride ||
               int64_t(high)+source.vertex_offset>=int64_t(file.buffer->size()/file.resource->stride))
                return refuse("Draw vertex file is not Ready or exceeds conservative bounds");
            const auto binding=std::find_if(draw.vertex.bindings.begin(),draw.vertex.bindings.end(),[&](const auto& b){return b.binding==slot;});
            if(binding==draw.vertex.bindings.end() || binding->stride!=file.resource->stride || binding->inputRate!=VK_VERTEX_INPUT_RATE_VERTEX)
                return refuse("Resolved vertex declaration disagrees with its file stride or rate");
            const auto index=slot-original.first_binding;draw.geometry.buffers[index]=file.buffer->buffer();draw.geometry.offsets[index]=0;
            draw.buffers.push_back(file.buffer);
        }
        draw.draw={source.index_count,source.instance_count,source.first_index,source.first_instance,source.vertex_offset};
        next.draws.push_back(std::move(draw));
    }
    output=std::move(next);return true;
}
assets::FileReadiness request_draw_variants(const DrawTransaction& plan,efvk::VariantCache& cache,uint64_t generation,
    std::shared_ptr<const efvk::GraphicsRecipe> recipe,std::shared_ptr<const efvk::PipelineLayoutInfo> layout,
    VariantTransaction& output,std::string& reason)
{
    reason.clear();
    if(!generation || !recipe || !layout || plan.draws.size()>256){reason="Missing original pipeline generation or recipe";return assets::FileReadiness::Failed;}
    VariantTransaction next;next.plan=plan;bool pending=false;
    for(const auto& draw:plan.draws){
        const auto result=cache.request(generation,recipe,layout,draw.vertex,draw.material.patch);
        if(result.state==efvk::VariantState::Queued || result.state==efvk::VariantState::Building){pending=true;continue;}
        if(result.state!=efvk::VariantState::Ready || !result.variant){reason=result.reason.empty()?"Draw variant is unavailable":result.reason;return assets::FileReadiness::Failed;}
        if(result.variant->image_count!=draw.material.images.size()){reason="Ready variant differs from its material image contract";return assets::FileReadiness::Failed;}
        next.variants.push_back(result.variant);
    }
    if(pending)return assets::FileReadiness::Pending;
    output=std::move(next);return assets::FileReadiness::Ready;
}
bool append_draw_snapshots(const VariantTransaction& source,const std::vector<std::shared_ptr<efvk::DrawSnapshots>>& arenas,
                           RecordedTransaction& output,std::string& reason)
{
    reason.clear();const auto count=source.plan.draws.size();
    if(count>256 || count!=source.variants.size() || count!=arenas.size()){reason="Snapshot transaction item counts differ";return false;}
    for(size_t i=0;i<count;++i)if(!arenas[i] || arenas[i]->sealed() || !source.variants[i] || !source.plan.draws[i].palette ||
        source.variants[i]->image_count!=source.plan.draws[i].material.images.size()){
        reason="Snapshot transaction owner is missing, sealed or incompatible";return false;
    }
    RecordedTransaction next;next.source_=source;next.arenas_=arenas;next.slots_.resize(count);
    for(size_t i=0;i<count;++i){const auto& draw=source.plan.draws[i];
        if(!arenas[i]->append(draw.palette->bones,draw.palette->entry,draw.material.images,next.slots_[i])){
            reason=arenas[i]->reason;return false;
        }
    }
    output=std::move(next);return true;
}
bool RecordedTransaction::emit(const efvk::VariantDispatch& dispatch,VkCommandBuffer command,VkPipeline original_pipeline) const
{
    if(source_.plan.draws.size()!=slots_.size() || source_.variants.size()!=slots_.size() || arenas_.size()!=slots_.size())return false;
    std::vector<efvk::VariantDraw> draws;draws.reserve(slots_.size());
    for(size_t i=0;i<slots_.size();++i)draws.push_back({source_.variants[i].get(),slots_[i].set,slots_[i].offset,&source_.plan.draws[i].geometry,source_.plan.draws[i].draw});
    return efvk::emit_variant_draws(dispatch,command,draws.data(),draws.size(),original_pipeline,source_.plan.original);
}
}
