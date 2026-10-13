#pragma once
#include "material_plan.h"
#include "../core/palette_bank.h"
#include "../../layer/src/variant_cache.h"

namespace efmi::gpu {
struct TransactionDraw {
    efvk::DrawGeometry geometry;
    efvk::IndexedDraw draw;
    efvk::VertexInputRecipe vertex;
    MaterialDraw material;
    std::shared_ptr<const core::PaletteSnapshot> palette;
    std::vector<std::shared_ptr<const assets::BufferUpload>> buffers;
};
struct DrawTransaction {
    bool skip_original=false;
    efvk::DrawGeometry original;
    std::vector<TransactionDraw> draws;
};
// Compose validated frontend declarations and Ready file owners. The original
// binding range must cover every replaced stream so that restoration is exact.
// Inherited application resources remain the application's lifetime obligation.
bool prepare_draw_transaction(const assets::FileDraws&,const std::vector<efvk::VertexInputRecipe>&,
                              const MaterialPlan&,const efvk::DrawGeometry& original,
                              DrawTransaction&,std::string& reason);
struct VariantTransaction {
    DrawTransaction plan;
    std::vector<std::shared_ptr<const efvk::PipelineVariant>> variants;
};
assets::FileReadiness request_draw_variants(const DrawTransaction&,efvk::VariantCache&,uint64_t pipeline_generation,
    std::shared_ptr<const efvk::GraphicsRecipe>,std::shared_ptr<const efvk::PipelineLayoutInfo>,
    VariantTransaction&,std::string& reason);
class RecordedTransaction {
public:
    bool emit(const efvk::VariantDispatch&,VkCommandBuffer,VkPipeline original_pipeline) const;
    bool skip_original() const {return source_.plan.skip_original;}
private:
    friend bool append_draw_snapshots(const VariantTransaction&,const std::vector<std::shared_ptr<efvk::DrawSnapshots>>&,
                                     RecordedTransaction&,std::string&);
    VariantTransaction source_;
    std::vector<std::shared_ptr<efvk::DrawSnapshots>> arenas_;
    std::vector<efvk::SnapshotSlot> slots_;
};
// Arenas are allocated off the draw path. Partial append failure emits no GPU
// commands and preserves output, but may consume unused slots in those arenas.
// Seal arenas before submit. Retain RecordedTransaction through command reuse/
// resubmission, and release it after completion before destroying the device.
bool append_draw_snapshots(const VariantTransaction&,const std::vector<std::shared_ptr<efvk::DrawSnapshots>>&,
                           RecordedTransaction&,std::string& reason);
}
