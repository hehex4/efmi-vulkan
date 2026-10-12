// Explicit, bounded package for the first self-contained M2 component; not an INI engine.
#pragma once
#include "pipeline_recipe.h"

namespace efvk {
struct M2Mesh {
    uint32_t hash=0, match_count=0, match_first=0, bones=0, component_first=0, component_count=0;
    uint32_t vertices=0, indices=0;
    std::vector<uint8_t> position, texcoord, blend, index;
    bool load(const std::string& utf8_path, std::string& reason);
    bool decode(const std::vector<uint8_t>&, std::string& reason);
    bool vertex_input(const VertexInputRecipe&, VertexInputRecipe& output, std::string& reason) const;
};
} // namespace efvk
