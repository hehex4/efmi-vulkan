// A whole character as data: meshes, bone ranges, LOD remaps and what to draw per matched section.
// Built offline by scripts/package_character.py from one scenario of the hosted frontend's plans.
// No textures, keys or conditions; not an INI interpreter.
#pragma once
#include "pipeline_recipe.h"

namespace efvk {

constexpr uint32_t kCharacterNone = UINT32_MAX;
constexpr uint32_t kCharacterOvershoot = 64;

struct CharacterStream {
    enum Kind : uint32_t { Vertex = 0, Index = 1, Remap = 2 };
    uint32_t kind = Vertex, stride = 0;
    uint64_t offset = 0, size = 0; // into the payload
    // For a vertex stream that carries bone indices: byte offsets of four 16-bit indices and,
    // when present, of four 16-bit normalized weights inside one element.
    uint32_t bone_index_offset = kCharacterNone, bone_weight_offset = kCharacterNone;
    uint32_t components = 0; // bit per component whose bones this stream refers to (computed at load)
};
struct CharacterDraw {
    uint32_t index_stream = 0, count = 0, first = 0;
    int32_t base = 0;
    uint32_t vb[4] = {kCharacterNone, kCharacterNone, kCharacterNone, kCharacterNone};
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

struct CharacterPackage {
    uint32_t bones = 0;
    std::vector<CharacterComponent> components; // in bone order; they partition [0, bones)
    std::vector<CharacterStream> streams;
    std::vector<CharacterSection> sections;
    std::vector<CharacterList> lists;
    std::vector<CharacterDraw> draws;
    std::vector<CharacterEdit> edits;
    std::vector<uint8_t> payload;

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
};

// True when the file starts with the character package magic. No other validation.
bool is_character_package(const std::string& utf8_path);

} // namespace efvk
