#pragma once
#include "../../layer/src/merged_skeleton.h"
#include <array>
#include <memory>
#include <unordered_map>

namespace efmi::core {
struct LodRemap {
    bool available=false;
    std::vector<uint16_t> indices; // Empty, when available, is the explicit identity map.
};
struct PaletteComponent {
    efvk::BoneComponent range;
    std::array<LodRemap,2> lods; // LOD0 and LOD1 only.
};
struct PaletteDefinition {
    uint32_t bones=0,instances=0;
    std::vector<PaletteComponent> components; // Ids may be sparse, with zero-count entries.
};
struct PaletteSnapshot {
    uint64_t identity=0,update=0;
    std::vector<uint8_t> bones;
    efvk::InstanceEntry entry;
};
// Serial use under the frontend lock. The caller supplies identity epochs from
// the original spatial/Core adapter, never a reusable raw pool slot alone.
// Each identity owns an independent complete master; emitted snapshots compact
// that identity to slot zero and are immutable across updates and retirement.
class PaletteBank {
public:
    bool configure(PaletteDefinition,std::string& reason);
    bool begin_update(uint64_t serial,std::string& reason);
    efvk::BoneImportResult import_component(uint64_t identity,uint32_t component,uint32_t lod,
        const efvk::InstanceEntry&,efvk::BoneWindow current,efvk::BoneWindow previous,float scale,std::string& reason);
    // Conservative completeness: all active components must have supplied each
    // dynamic pose at least once in this identity epoch. Core's previous master
    // remains available for components not yet encountered in the current update.
    bool snapshot(uint64_t identity,const efvk::InstanceEntry&,std::shared_ptr<const PaletteSnapshot>& output,std::string& reason) const;
    void retire(uint64_t identity) {palettes_.erase(identity);}
    size_t live() const {return palettes_.size();}
private:
    struct Palette {
        efvk::MergedSkeleton skeleton;
        std::vector<uint8_t> coverage;
    };
    PaletteDefinition definition_;
    std::vector<efvk::BoneComponent> ordered_;
    std::vector<uint32_t> dense_;
    std::unordered_map<uint64_t,Palette> palettes_;
    uint64_t serial_=0;
    bool begun_=false;
};
}
