#include "file_draws.h"

namespace efmi::assets {
FileReadiness request_file_draws(const PreparedFilePlan& plan,ResourceCache& cache,FileDraws& output,std::string& reason)
{
    reason.clear();
    for(const auto& item:plan.plan().items)if(item.operation==FileOperation::External){
        reason="Native Core operation must resolve before file draws";return FileReadiness::Failed;
    }
    std::vector<ResourceSnapshot> owners;
    const auto readiness=request_file_plan(plan,cache,owners,reason);
    if(readiness!=FileReadiness::Ready)return readiness;
    FileDraws next;next.skip_original=plan.plan().skip_original;FileDraw state;
    auto refuse=[&](const char* text){reason=text;return FileReadiness::Failed;};
    for(size_t i=0;i<plan.plan().items.size();++i){
        const auto& item=plan.plan().items[i];
        if(item.operation==FileOperation::DrawIndexed){
            if(next.draws.size()>=256)return refuse("File draw count exceeds transaction budget");
            if(state.index.source!=BindingSource::File || !state.index.buffer)
                return refuse("File draw bounds require a resolved index file");
            const auto& index=*state.index.resource;
            if((uint64_t(item.first_index)+item.index_count)*index.stride>state.index.buffer->size())
                return refuse("Draw range exceeds the uploaded index file");
            uint32_t low=0,high=0;
            if(!state.index.buffer->index_extents(index.index_type,low,high))
                return refuse("Uploaded index file has no compatible extrema");
            const int64_t first=int64_t(low)+item.vertex_offset,last=int64_t(high)+item.vertex_offset;
            if(first<0)return refuse("Whole-file index extrema do not prove a nonnegative base vertex");
            for(const auto& vertex:state.vertex)if(vertex.source==BindingSource::File){
                if(!vertex.buffer || !vertex.resource || last>=int64_t(vertex.buffer->size()/vertex.resource->stride))
                    return refuse("Whole-file index extrema exceed an uploaded vertex stream");
            }
            state.index_count=item.index_count;state.first_index=item.first_index;state.vertex_offset=item.vertex_offset;
            state.instance_count=item.instance_count;state.first_instance=item.first_instance;state.layout=item.layout;
            state.palette=item.palette;
            next.draws.push_back(state);continue;
        }
        FileBinding value;value.source=item.resource?BindingSource::File:BindingSource::Null;
        value.resource=item.resource;value.buffer=owners[i].buffer;value.image=owners[i].image;
        switch(item.operation){
        case FileOperation::BindVertex:state.vertex[item.slot]=std::move(value);break;
        case FileOperation::BindIndex:state.index=std::move(value);break;
        case FileOperation::BindPixelResource:state.pixel[item.slot]=std::move(value);break;
        default:return refuse("Unexpected operation in resolved file draws");
        }
    }
    output=std::move(next);return FileReadiness::Ready;
}
}
