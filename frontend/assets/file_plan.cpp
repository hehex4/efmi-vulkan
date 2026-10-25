#include "file_plan.h"

namespace efmi::assets {
namespace {
bool binding(FileOperation op)
{
    return op==FileOperation::BindVertex || op==FileOperation::BindIndex || op==FileOperation::BindPixelResource;
}
bool valid(const FilePlanItem& item)
{
    if(item.layout.size()>256)return false;
    if(item.operation==FileOperation::DrawIndexed)return !item.resource && item.index_count && item.instance_count;
    if(item.operation==FileOperation::Translucent)
        return item.index_count && item.instance_count==1 && item.mask_channel<=3 &&
               (!item.resource || (!item.resource->usage && !item.resource->stride && item.resource->index_type==VK_INDEX_TYPE_MAX_ENUM));
    if(item.operation==FileOperation::External)return !item.resource && !item.external.empty() && item.external.size()<=4096 &&
        item.phase_mask && item.phase_mask<=3;
    if(!binding(item.operation))return false;
    const auto limit=item.operation==FileOperation::BindVertex?32u:item.operation==FileOperation::BindIndex?1u:128u;
    if(item.slot>=limit)return false;
    if(!item.resource)return true;
    const auto& r=*item.resource;
    if(item.operation==FileOperation::BindPixelResource)return !r.usage && !r.stride && r.index_type==VK_INDEX_TYPE_MAX_ENUM;
    if(item.operation==FileOperation::BindVertex)
        return r.usage==VK_BUFFER_USAGE_VERTEX_BUFFER_BIT && r.stride && r.stride<=2048 && r.index_type==VK_INDEX_TYPE_MAX_ENUM;
    return r.usage==VK_BUFFER_USAGE_INDEX_BUFFER_BIT &&
        ((r.index_type==VK_INDEX_TYPE_UINT16 && r.stride==2) || (r.index_type==VK_INDEX_TYPE_UINT32 && r.stride==4));
}
}
bool prepare_file_plan(const FilePlan& input,ResourceCache& cache,PreparedFilePlan& output,std::string& reason)
{
    reason.clear();
    if(input.items.size()>4096){reason="File plan operation budget exceeded";return false;}
    for(const auto& item:input.items)if(!valid(item)){reason="Invalid file binding contract";return false;}
    PreparedFilePlan next;next.plan_=input;next.requests_.resize(input.items.size());
    for(size_t i=0;i<input.items.size();++i){
        const auto& resource=input.items[i].resource;if(!resource)continue;
        next.plan_.items[i].resource=std::make_shared<const FileResource>(*resource);
        auto request=resource->usage?cache.declare_buffer(resource->path,resource->usage,reason):cache.declare_image(resource->path,reason);
        if(!request)return false;
        next.requests_[i]=std::move(request);
    }
    output=std::move(next);return true;
}
FileReadiness request_file_plan(const PreparedFilePlan& input,ResourceCache& cache,
                               std::vector<ResourceSnapshot>& output,std::string& reason)
{
    reason.clear();
    if(input.plan_.items.size()>4096 || input.requests_.size()!=input.plan_.items.size()){
        reason="Prepared file plan shape mismatch";return FileReadiness::Failed;
    }
    std::vector<ResourceSnapshot> next(input.requests_.size());bool pending=false,failed=false;
    for(size_t i=0;i<input.requests_.size();++i){
        const auto& item=input.plan_.items[i];const auto& request=input.requests_[i];
        if(!valid(item) || bool(item.resource)!=bool(request)){
            failed=true;if(reason.empty())reason="Prepared file binding contract mismatch";continue;
        }
        if(!request)continue;
        const bool accepted=cache.demand(request);auto value=request->snapshot();
        if(!accepted || value.state==ResourceState::Failed || value.state==ResourceState::Stopped){
            failed=true;if(reason.empty())reason=value.reason.empty()?"Resource request is foreign or stopped":value.reason;continue;
        }
        if(value.state!=ResourceState::Ready){pending=true;continue;}
        if((item.resource->usage && (!value.buffer || value.image || value.buffer->usage()!=item.resource->usage ||
                                   value.buffer->size()%item.resource->stride)) ||
           (!item.resource->usage && (!value.image || value.buffer))){
            failed=true;if(reason.empty())reason="Ready resource does not satisfy its binding declaration";continue;
        }
        next[i]=std::move(value);
    }
    if(failed)return FileReadiness::Failed;
    if(pending)return FileReadiness::Pending;
    output=std::move(next);return FileReadiness::Ready;
}
}
