#include "buffer_queries.h"
#include <cmath>
#include <cstring>
#include <climits>
// Pinned original XXMI quantization/packing implementation in ResourceHash.cpp.
uint32_t PackCellCoords(float,float,float,float);
namespace efmi::frontend {
bool BufferQueries::bind(CustomResource* resource,const uint8_t* data,size_t size,std::wstring& reason)
{
    reason.clear();
    if(!resource || !data || !size || size>(1u<<20) || (windows_.size()>=64 && !windows_.count(resource))){
        reason=L"Original buffer window exceeds query limits";return false;
    }
    windows_[resource]={data,size};return true;
}
bool BufferQueries::read(ResourceCopyTarget& target,CommandListState& state,float& output,std::wstring& reason) const
{
    reason.clear();auto fail=[&](const wchar_t* text){reason=text;return false;};
    if(target.type!=ResourceCopyTargetType::CUSTOM_RESOURCE || target.custom_resource_pool ||
       target.evaluation_mode!=ResourceCopyTargetEvaluationMode::RESOURCE_SPATIAL_HASH)
        return fail(L"Unadapted original buffer query");
    const auto found=windows_.find(target.GetCustomResource(&state));
    if(found==windows_.end())return fail(L"Original buffer window unavailable");
    float args[4];
    for(size_t i=0;i<4;++i){
        if(!target.member_args[i].expression)return fail(L"Spatial query arguments incomplete");
        args[i]=target.member_args[i].expression->evaluate(&state);
        if(!std::isfinite(args[i]))return fail(L"Nonfinite spatial query argument");
    }
    if(args[3]<=0)return fail(L"Spatial cell size must be positive");
    const auto& window=found->second;float xyz[3];
    for(size_t i=0;i<3;++i){
        if(args[i]<0 || std::floor(args[i])!=args[i] || double(args[i])*4+4>window.size)
            return fail(L"Spatial query exceeds original buffer window");
        std::memcpy(&xyz[i],window.data+size_t(args[i])*4,4);
        const float cell=std::floor(xyz[i]/args[3]);
        if(!std::isfinite(cell) || double(cell)<INT_MIN || double(cell)>INT_MAX)
            return fail(L"Spatial coordinate exceeds original quantization range");
    }
    const uint32_t hash=PackCellCoords(xyz[0],xyz[1],xyz[2],args[3]);
    if(!hash || hash==UINT32_MAX)return fail(L"Spatial cell collides with an original sentinel");
    std::memcpy(&output,&hash,4);return true;
}
}
