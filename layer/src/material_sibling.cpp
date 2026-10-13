#include "material_sibling.h"

#include <algorithm>
#include <map>
#include <set>

namespace efvk {

bool fragment_image_bindings(const std::vector<uint32_t>& code, std::vector<std::pair<uint32_t, uint32_t>>& out)
{
    if (code.size() < 5 || code.size() > (4u << 20) / 4 || code[0] != 0x07230203) return false;
    enum : uint32_t { OpTypeImage = 25, OpTypeSampledImage = 27, OpTypeArray = 28, OpTypeRuntimeArray = 29,
                      OpTypePointer = 32, OpVariable = 59, OpDecorate = 71, UniformConstant = 0, Binding = 33, DescriptorSet = 34 };
    std::set<uint32_t> images;                                 // image types, and arrays of them
    std::map<uint32_t, uint32_t> pointers;                     // pointer type -> pointee, UniformConstant only
    std::vector<std::pair<uint32_t, uint32_t>> variables;      // variable id, pointer type
    struct Place { bool has_set = false, has_binding = false; uint32_t set = 0, binding = 0; };
    std::map<uint32_t, Place> places;
    for (size_t i = 5; i < code.size();) {
        const uint32_t n = code[i] >> 16, op = code[i] & 0xffff;
        if (!n || n > code.size() - i) return false;
        if ((op == OpTypeImage && n >= 9) || (op == OpTypeSampledImage && n == 3)) images.insert(code[i + 1]);
        else if ((op == OpTypeArray && n == 4) || (op == OpTypeRuntimeArray && n == 3)) {
            if (images.count(code[i + 2])) images.insert(code[i + 1]);
        } else if (op == OpTypePointer && n == 4) {
            if (code[i + 2] == UniformConstant) pointers[code[i + 1]] = code[i + 3];
        } else if (op == OpVariable && n >= 4) {
            if (code[i + 3] == UniformConstant) variables.push_back({code[i + 2], code[i + 1]});
        } else if (op == OpDecorate && n == 4) {
            if (code[i + 2] == DescriptorSet) { places[code[i + 1]].has_set = true; places[code[i + 1]].set = code[i + 3]; }
            else if (code[i + 2] == Binding) { places[code[i + 1]].has_binding = true; places[code[i + 1]].binding = code[i + 3]; }
        }
        i += n;
    }
    std::vector<std::pair<uint32_t, uint32_t>> next;
    for (const auto& v : variables) {
        const auto pointer = pointers.find(v.second);
        if (pointer == pointers.end() || !images.count(pointer->second)) continue;
        const auto place = places.find(v.first);
        if (place == places.end() || !place->second.has_set || !place->second.has_binding) return false;
        next.push_back({place->second.set, place->second.binding});
    }
    std::sort(next.begin(), next.end());
    if (std::adjacent_find(next.begin(), next.end()) != next.end()) return false;
    out = std::move(next);
    return true;
}

bool transfer_material_rules(const std::vector<BoundImage>& sibling, const std::vector<CharacterMaterialRule>& rules,
                             const std::vector<BoundImage>& target, SiblingRules& out)
{
    std::set<uint32_t> held, common;
    for (const auto& image : sibling) if (image.image) held.insert(image.image);
    for (const auto& image : target) if (image.image && held.count(image.image)) common.insert(image.image);
    // An image both sides hold is itself; any other stands for its shape.
    using Token = std::pair<uint32_t, uint64_t>;
    const auto token = [&](const BoundImage& image) { return common.count(image.image) ? Token{1, image.image} : Token{0, image.shape}; };
    std::map<Token, std::vector<const BoundImage*>> from, to;
    uint32_t target_images = 0, matched = 0;
    for (const auto& image : sibling) if (image.image) from[token(image)].push_back(&image);
    for (const auto& image : target) if (image.image) { to[token(image)].push_back(&image); ++target_images; }
    struct Match { const BoundImage* place; bool certain; };
    std::map<std::pair<uint32_t, uint32_t>, Match> moved;
    for (const auto& entry : from) {
        const auto other = to.find(entry.first);
        if (other == to.end() || other->second.size() < entry.second.size()) return false;
        const bool certain = entry.first.first ? entry.second.size() == other->second.size()
                                               : entry.second.size() == 1 && other->second.size() == 1;
        for (size_t k = 0; k < entry.second.size(); ++k)
            moved[{entry.second[k]->set, entry.second[k]->binding}] = {other->second[k], certain};
        matched += static_cast<uint32_t>(entry.second.size());
    }
    SiblingRules next;
    next.extras = target_images - matched;
    for (const auto& rule : rules) {
        CharacterMaterialRule copy = rule;
        if (rule.kind != CharacterMaterialRule::Absent) {
            const auto found = moved.find({rule.set, rule.binding});
            if (found == moved.end() || !found->second.certain) return false;
            copy.set = found->second.place->set; copy.binding = found->second.place->binding;
        }
        next.rules.push_back(copy);
    }
    out = std::move(next);
    return true;
}

} // namespace efvk
