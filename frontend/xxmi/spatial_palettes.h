#pragma once
#include "draw_plan.h"
#include "../core/palette_bank.h"

namespace efmi::frontend {
struct SpatialObservation {
    uint32_t slot=0,hash=0; // Slot is the original Core result, never selected here.
};
// One owner per retained original spatial pool. Observe every completed Core
// identification immediately; invalidate after any incomplete execution or pool
// reset. This conservatively validates continuity, not physical entity identity.
class SpatialPalettes {
public:
    bool configure(CustomResourcePool&,core::PaletteDefinition,std::string& reason);
    bool observe(uint64_t frame,float frame_seconds,const std::vector<SpatialObservation>&,
                 std::vector<uint64_t>& identities,std::string& reason);
    void invalidate();
    efvk::BoneImportResult import_component(uint64_t identity,uint32_t component,uint32_t lod,
        const efvk::InstanceEntry&,efvk::BoneWindow current,efvk::BoneWindow previous,float scale,std::string& reason);
    bool snapshot(uint64_t identity,const efvk::InstanceEntry& entry,
                  std::shared_ptr<const core::PaletteSnapshot>& output,std::string& reason)const;
private:
    struct Lease {uint64_t identity=0,frame=0;uint32_t hash=0;};
    core::PaletteBank bank_;
    CustomResourcePool* pool_=nullptr;
    std::vector<Lease> leases_;
    uint32_t radius_=0,expiration_=0;
    uint64_t next_=1,frame_=0;
    bool begun_=false;
};
}
