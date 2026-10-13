#pragma once
#include "../assets/file_draws.h"
#include "../../layer/src/pipeline_recipe.h"
#include <map>

namespace efmi::frontend {
struct VertexSemantic {uint32_t location=0,index=0;std::string name;};
struct VertexPlan {std::vector<efvk::VertexInputRecipe> draws;};
// Exact-shader semantic correspondence is caller-provided evidence. Stream
// offsets/formats/rates come from the current owned Vulkan pipeline recipe.
class VertexProfile {
public:
    bool prepare(const std::vector<uint32_t>& vertex,const std::vector<VertexSemantic>&,std::string& reason);
    // Calls the pinned original XXMI layout evaluator. Only supported format/
    // offset edits are accepted; semantic relinking and instance-rate files
    // require another adapter. No GPU calls, file IO or partial publication.
    bool resolve(const assets::FileDraws&,const std::vector<uint32_t>& vertex,
                 const efvk::VertexInputRecipe&,VertexPlan&,std::string& reason) const;
private:
    std::vector<uint32_t> code_;
    std::map<uint32_t,VertexSemantic> semantics_;
};
}
