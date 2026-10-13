// Static contracts shared by the live backend and offline package verification.
#pragma once
#include "character_package.h"
#include "pipeline_variant.h"
#include <set>

namespace efvk {
struct CharacterMaterial {
    bool supported = false, compatibility = false;
    FragmentPatch patch;
    std::vector<uint32_t> assets;
    std::string reason;
};
// One profile against every mesh draw of the package. `counts` gains the image counts its
// supported draws use. Outputs are unchanged if the profile is malformed.
bool prepare_character_profile(const CharacterPackage&, const CharacterMaterialProfile&, std::vector<CharacterMaterial>&,
                               std::set<uint32_t>& counts, std::string& reason);
// Profiles are validated once; unsupported profile/draw combinations retain reasons.
// Outputs are unchanged if a malformed profile prevents preparation.
bool prepare_character_materials(const CharacterPackage&, std::vector<std::vector<CharacterMaterial>>&,
                                 std::set<uint32_t>& image_counts, std::string& reason);
}
