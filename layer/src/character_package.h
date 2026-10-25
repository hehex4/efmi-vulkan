// A whole character as data: meshes, bone ranges, LOD remaps and what to draw per matched section.
// Built offline by scripts/package_character.py from one scenario of the hosted frontend's plans.
// Optional static materials; no keys or conditions, not an INI interpreter.
#pragma once
#include "pipeline_recipe.h"

namespace efvk {

constexpr uint32_t kCharacterNone = UINT32_MAX;
// A draw's vertex slot bound to an owned GPU buffer of a live plan (CharacterDraw::owned).
constexpr uint32_t kCharacterOwned = UINT32_MAX - 1;
constexpr uint32_t kCharacterOvershoot = 64;
// DDS images one package may declare: they load only when a draw binds them, so this is a sanity
// budget, not memory (a Mod with a watch GUI declares 83; 64 until 1.8.1).
constexpr uint32_t kCharacterImages = 1024;
// The formats a pipeline declares BLENDINDICES and BLENDWEIGHT with in binding 2: four unsigned
// integers of 8, 16 or 32 bits, and four normalized 16-bit or float weights. A Mod's blend-index
// edit names the replacement format; the game's own may be any of these (BUG-038).
inline bool blend_index_format(VkFormat f)
{
    return f == VK_FORMAT_R8G8B8A8_UINT || f == VK_FORMAT_R16G16B16A16_UINT || f == VK_FORMAT_R32G32B32A32_UINT;
}
inline uint32_t blend_weight_bytes(VkFormat f)
{
    return f == VK_FORMAT_R16G16B16A16_UNORM ? 2 : f == VK_FORMAT_R32G32B32A32_SFLOAT ? 4 : 0;
}

struct CharacterStream {
    enum Kind : uint32_t { Vertex = 0, Index = 1, Remap = 2 };
    uint32_t kind = Vertex, stride = 0;
    uint64_t offset = 0, size = 0; // into the payload
    // For a vertex stream that carries bone indices: byte offsets of four 16-bit indices and,
    // when present, of four 16-bit normalized weights inside one element.
    uint32_t bone_index_offset = kCharacterNone, bone_weight_offset = kCharacterNone;
    uint32_t bone_index_bytes = 2; // width of one bone index: 2 (16-bit), or 4 under a 32-bit blend-index edit
    uint32_t bone_weight_bytes = 2; // width of one weight: 2 (16-bit normalized) or 4 (float, BUG-038)
    uint32_t components = 0; // bit per component whose bones this stream refers to (computed at load)
};
struct CharacterDraw {
    uint32_t index_stream = 0, count = 0, first = 0;
    int32_t base = 0;
    uint32_t vb[4] = {kCharacterNone, kCharacterNone, kCharacterNone, kCharacterNone};
    // A live plan's blend-index format for binding 2 (R16G16B16A16_UINT or R32G32B32A32_UINT);
    // undefined keeps the package's edits. Not part of the package file.
    VkFormat blend = VK_FORMAT_UNDEFINED;
    // A live plan that leaves the application's draw but binds textures for it (D-083): no geometry
    // of the package; the draw is made again with its own geometry and these pixels. Not in the file.
    bool texture_only = false;
    // Element offset edits of a live plan (D-085): the attribute of `binding` at the game's offset
    // `from` reads the Mod's stream at `to`. Not in the file.
    struct Offset { uint32_t binding = 0, from = 0, to = 0; };
    std::vector<Offset> offsets;
    // Vertex slots a live plan binds to a buffer its owned GPU work wrote (P2, D-087): vb[slot] is
    // kCharacterOwned; the Vulkan buffer is bound as it is and `keep` holds it. Not in the file.
    struct OwnedStream { uint32_t slot = 0; uint64_t buffer = 0, offset = 0, size = 0; uint32_t stride = 0; std::shared_ptr<void> keep; };
    std::vector<OwnedStream> owned;
    // A live plan's translucent draw (D-113): not drawn in the pass but captured into a translucency
    // layer at its end, with this opacity, mask image (an index of the image catalogue, or
    // kCharacterNone), channel and inversion. Not in the file.
    bool translucent = false;
    float alpha = 1;
    uint32_t mask_image = kCharacterNone, mask_channel = 0;
    bool mask_invert = false;
    const OwnedStream* owned_stream(uint32_t slot) const
    {
        for (const auto& o : owned) if (o.slot == slot) return &o;
        return nullptr;
    }
};
struct CharacterList {
    uint32_t first = 0, count = 0; // into draws; an empty list hides the original geometry
    uint32_t required = 0;         // components whose bones the list's vertices use (computed at load)
};
struct CharacterComponent {
    uint32_t first = 0, count = 0;     // bone range in the merged skeleton
    bool has_remap = false;
    std::vector<uint16_t> remap_lod1;  // destination bone -> source bone, when has_remap
    uint32_t lists[2] = {0, 0};        // draw list at LOD 0 and LOD 1
    uint32_t remap_stream = kCharacterNone;
};
struct CharacterSection {
    uint32_t hash = 0, match_count = 0;
    uint32_t match_first = kCharacterNone; // none: any first index
    uint32_t component = 0;
    uint32_t lod = kCharacterNone;         // none: the section leaves the mod's LOD variable as it is
};
struct CharacterEdit {
    uint32_t binding = 0;
    VkFormat source = VK_FORMAT_UNDEFINED, format = VK_FORMAT_UNDEFINED;
};
struct CharacterPixel {
    uint32_t slot = 0, asset = kCharacterNone; // none is an explicit null; missing slots inherit
};
struct CharacterMaterialRule {
    // ZeroFile is an explicit, device-bound compatibility policy for invalid source-API
    // image dimensions. It is never inferred merely because a relocation failed.
    enum Kind : uint32_t { Absent = 1, Image = 2, ZeroFile = 3 };
    uint32_t slot = 0, kind = Absent, set = 0, binding = 0;
};
struct CharacterMaterialProfile {
    std::vector<uint32_t> code;
    std::vector<CharacterMaterialRule> rules;
};

struct CharacterPackage {
    uint32_t bones = 0;
    std::vector<CharacterComponent> components; // in bone order, disjoint; bones between them belong to none
    std::vector<CharacterStream> streams;
    std::vector<CharacterSection> sections;
    std::vector<CharacterList> lists;
    std::vector<CharacterDraw> draws;
    std::vector<CharacterEdit> edits;
    std::vector<uint8_t> payload;
    bool materials = false;
    uint32_t material_vendor = 0, material_device = 0;
    std::vector<std::string> images; // absolute UTF-8 paths, immutable after load
    std::vector<CharacterMaterialProfile> profiles;
    std::vector<std::vector<CharacterPixel>> pixels; // one binding snapshot per mesh draw

    bool load(const std::string& utf8_path, std::string& reason);
    // Everything is checked before anything is accepted: sizes, ranges, index bounds of every
    // draw against its vertex streams, and every bone index against the bone count.
    bool decode(const std::vector<uint8_t>& file, std::string& reason);
    // The declaration a replaced draw needs. A stream the draw binds takes the file's stride;
    // an edit changes the format of matching elements of its binding in place. Streams the draw
    // does not bind, the application's default attributes among them, stay as they are.
    // An element may end up to kCharacterOvershoot bytes past the file's stride: the source API
    // binds such a file all the same and the shader reads into the next vertex.
    bool vertex_input(const CharacterDraw&, const VertexInputRecipe& source, VertexInputRecipe& output,
                      std::string& reason) const;
    // A vertex stream found to carry bone indices only at run time (the live plan binds it where the
    // blend-index edit applies): the same checks as decode, then its component mask. Once per stream.
    bool adopt_bones(uint32_t stream, uint32_t index_offset, uint32_t weight_offset, std::string& reason, uint32_t index_bytes = 2,
                     uint32_t weight_bytes = 2);
};

// True when the file starts with the character package magic. No other validation.
bool is_character_package(const std::string& utf8_path);

} // namespace efvk
