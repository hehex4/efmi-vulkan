// Material rules for a fragment shader the package has no profile for.
//
// The game draws one component with several fragment shaders that differ by a feature (the
// dithered fade near the camera adds a noise texture and moves the bindings behind it). Such a
// shader samples the textures of a profiled shader, its sibling, at other bindings. Which binding
// holds which texture is read off the images the application has bound: the rule of the sibling
// for a binding moves to the binding of the other shader that holds the same image.
#pragma once

#include "character_package.h"

#include <utility>

namespace efvk {

struct BoundImage {
    uint32_t set = 0, binding = 0;
    uint32_t image = 0; // the tracker's id of the image bound there; 0: nothing known
    uint64_t shape = 0; // view type, format and extent, for an image whose id is on one side only
};

// The image and sampled-image variables of a fragment module in (set, binding) order, arrays
// included, plain samplers left out. False for input this does not understand.
bool fragment_image_bindings(const std::vector<uint32_t>& code, std::vector<std::pair<uint32_t, uint32_t>>& out);

struct SiblingRules {
    uint32_t extras = 0; // images of the target that the sibling does not have
    std::vector<CharacterMaterialRule> rules;
};

// `sibling` and `target` list the image variables of the two shaders with what was bound, in
// (set, binding) order. Every image of the sibling must have a place in the target: the same image
// where both sides hold it, otherwise an image of the same shape, occurrences taken in order. A
// rule moves only through a certain match: the same image the same number of times on both sides,
// or a shape that occurs once on each side. Output is unchanged on failure.
bool transfer_material_rules(const std::vector<BoundImage>& sibling, const std::vector<CharacterMaterialRule>& rules,
                             const std::vector<BoundImage>& target, SiblingRules& out);

} // namespace efvk
