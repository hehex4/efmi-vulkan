#include "spatial_palettes.h"
#include "ResourceHash.h"
#include <cmath>
#include <unordered_set>
namespace efmi::frontend {
bool SpatialPalettes::configure(CustomResourcePool& input,core::PaletteDefinition definition,std::string& reason)
{
    reason.clear();auto* pool=input.ResolvePool();const auto size=pool->GetPoolSize();
    if(pool->index_type!=PoolIndexType::SPATIAL || !size || size>4096 ||
       pool->expiration_timeout_frames==UINT32_MAX || !pool->reset_expired_elements || !pool->read_refreshes_expiration){
        reason="Shared palettes require the original expiring, read-refreshed spatial pool";return false;
    }
    std::vector<Lease> leases(size);
    if(!bank_.configure(std::move(definition),reason))return false;
    pool_=pool;leases_=std::move(leases);radius_=pool->spatial_radius;expiration_=pool->expiration_timeout_frames;
    frame_=0;begun_=false;return true;
}
void SpatialPalettes::invalidate()
{
    for(auto& lease:leases_){if(lease.identity)bank_.retire(lease.identity);lease={};}
}
bool SpatialPalettes::observe(uint64_t frame,float seconds,const std::vector<SpatialObservation>& input,
                             std::vector<uint64_t>& identities,std::string& reason)
{
    reason.clear();auto fail=[&](const char* text){invalidate();reason=text;return false;};
    if(!pool_ || pool_->ResolvePool()!=pool_ || pool_->index_type!=PoolIndexType::SPATIAL ||
       pool_->GetPoolSize()!=leases_.size() || pool_->spatial_radius!=radius_ ||
       pool_->expiration_timeout_frames!=expiration_ || !pool_->reset_expired_elements || !pool_->read_refreshes_expiration)
        return fail("Original spatial pool lifetime or configuration changed");
    // Match the original float computation before the integer conversion.
    const float scaled=std::ceil(radius_*(seconds*120.0f));
    if((begun_ && frame<frame_) || !std::isfinite(seconds) || seconds<=0 ||
       !std::isfinite(scaled) || scaled<0 || double(scaled)>UINT32_MAX || input.empty() || input.size()>32)
        return fail("Invalid spatial observation clock or count");
    const auto radius=static_cast<uint32_t>(scaled);
    std::unordered_set<uint32_t> slots;
    for(const auto& item:input)
        if(item.slot>=leases_.size() || !item.hash || item.hash==UINT32_MAX || !slots.insert(item.slot).second)
            return fail("Ambiguous or invalid original spatial result");
    if(next_>UINT64_MAX-input.size())return fail("Spatial identity epoch exhausted");
    auto next=leases_;uint64_t counter=next_;std::vector<uint64_t> retired,output;
    for(auto& lease:next)if(lease.identity && frame-lease.frame>expiration_){
        retired.push_back(lease.identity);lease={};
    }
    for(const auto& item:input){
        auto& lease=next[item.slot];
        // The pool may reuse a full slot without clearing its custom status.
        // Only the original proximity criterion can retain the old bone master.
        if(lease.identity && SpatialDistanceChebyshev(UnpackCellCoords(item.hash),UnpackCellCoords(lease.hash))>radius){
            retired.push_back(lease.identity);lease={};
        }
        if(!lease.identity)lease.identity=counter++;
        lease.frame=frame;lease.hash=item.hash;output.push_back(lease.identity);
    }
    if(!bank_.begin_update(frame,reason)){invalidate();return false;}
    for(auto identity:retired)bank_.retire(identity);
    leases_=std::move(next);identities=std::move(output);next_=counter;frame_=frame;begun_=true;return true;
}
efvk::BoneImportResult SpatialPalettes::import_component(uint64_t identity,uint32_t component,uint32_t lod,
    const efvk::InstanceEntry& entry,efvk::BoneWindow current,efvk::BoneWindow previous,float scale,std::string& reason)
{
    bool active=false;for(const auto& lease:leases_)active|=lease.identity==identity && identity!=0;
    if(!active){reason="Palette identity is no longer active";return efvk::BoneImportResult::Rejected;}
    return bank_.import_component(identity,component,lod,entry,current,previous,scale,reason);
}
bool SpatialPalettes::snapshot(uint64_t identity,const efvk::InstanceEntry& entry,
    std::shared_ptr<const core::PaletteSnapshot>& output,std::string& reason)const
{
    return bank_.snapshot(identity,entry,output,reason);
}
}
