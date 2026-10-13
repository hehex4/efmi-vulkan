#pragma once
#include "../assets/file_draws.h"
#include "../../layer/src/draw_snapshots.h"
#include <array>

namespace efmi::gpu {
enum class MaterialSlotKind { Unknown, Absent, Image };
struct MaterialSlot {
    uint32_t slot=0;
    MaterialSlotKind kind=MaterialSlotKind::Unknown;
    ImageBinding image;
};
struct MaterialContract {
    efvk::FragmentPatch patch;
    std::vector<uint32_t> image_slots;
};
// Explicit API correspondence, selected by exact fragment bytecode. Absent
// means the source API shader does not declare that logical slot, not that a
// Vulkan descriptor happens to be missing. No inference from texture names.
// The owner validates source SPIR-V and supplies evidence for the correspondence;
// prepare checks bounded adapter support, not the truth of an API pairing.
class MaterialProfile {
public:
    bool prepare(const std::vector<uint32_t>& fragment,const std::vector<MaterialSlot>&,
                 std::string& reason);
    // CPU contract selection is independent of file/GPU readiness.
    bool select(const std::vector<uint32_t>& fragment,const std::array<assets::BindingSource,128>&,
                MaterialContract&,std::string& reason) const;
private:
    struct Slot {
        MaterialSlotKind kind=MaterialSlotKind::Unknown;
        ImageBinding image;
        bool file_supported=false,null_supported=false;
    };
    std::vector<uint32_t> code_;
    std::array<Slot,128> slots_{};
};
struct MaterialDraw {
    efvk::FragmentPatch patch;
    // Order is exactly patch.images. Owners survive frontend/cache retirement.
    std::vector<efvk::SnapshotImage> images;
};
struct MaterialPlan {std::vector<MaterialDraw> draws;};
// No GPU commands or file IO. Ready file owners are checked for every draw;
// unknown slots, unsupported types or a late invalid draw refuse atomically.
// This supplies a variant contract, not proof of driver pipeline creation.
bool resolve_material_plan(const assets::FileDraws&,const std::vector<uint32_t>& fragment,
                           const MaterialProfile*,MaterialPlan&,std::string& reason);
}
