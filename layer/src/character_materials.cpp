#include "character_materials.h"
#include "../../frontend/gpu/material_plan.h"

namespace efvk {
bool prepare_character_profile(const CharacterPackage& package, const CharacterMaterialProfile& source,
                               std::vector<CharacterMaterial>& output, std::set<uint32_t>& counts, std::string& reason)
{
    using namespace efmi;
    {
        gpu::MaterialProfile profile;
        std::vector<gpu::MaterialSlot> rules;
        for (const auto& r : source.rules)
            rules.push_back({r.slot, r.kind == CharacterMaterialRule::Absent ? gpu::MaterialSlotKind::Absent : gpu::MaterialSlotKind::Image,
                             {r.set, r.binding}});
        if (!profile.prepare(source.code, rules, reason)) return false;
        std::vector<CharacterMaterial> plans;
        for (const auto& pixels : package.pixels) {
            std::array<assets::BindingSource, 128> bindings{};
            std::array<uint32_t, 128> indices{};
            CharacterMaterial material;
            for (const auto& pixel : pixels) {
                bindings[pixel.slot] = pixel.asset == kCharacterNone ? assets::BindingSource::Null : assets::BindingSource::File;
                indices[pixel.slot] = pixel.asset;
            }
            for (const auto& r : source.rules) if (r.kind == CharacterMaterialRule::ZeroFile && bindings[r.slot] == assets::BindingSource::File) {
                bindings[r.slot] = assets::BindingSource::Null;
                material.compatibility = true;
            }
            gpu::MaterialContract contract;
            material.supported = profile.select(source.code, bindings, contract, material.reason);
            if (material.supported) {
                material.patch = std::move(contract.patch);
                for (uint32_t slot : contract.image_slots) material.assets.push_back(indices[slot]);
                counts.insert(static_cast<uint32_t>(material.assets.size()));
            }
            plans.push_back(std::move(material));
        }
        output = std::move(plans);
    }
    return true;
}

bool prepare_character_materials(const CharacterPackage& package, std::vector<std::vector<CharacterMaterial>>& output,
                                 std::set<uint32_t>& image_counts, std::string& reason)
{
    reason.clear();
    std::vector<std::vector<CharacterMaterial>> next;
    std::set<uint32_t> counts{0};
    for (const auto& source : package.profiles) {
        std::vector<CharacterMaterial> plans;
        if (!prepare_character_profile(package, source, plans, counts, reason)) return false;
        next.push_back(std::move(plans));
    }
    output = std::move(next); image_counts = std::move(counts); return true;
}
}
