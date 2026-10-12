#include "spirv_repoint.h"

#include <map>
#include <set>

namespace efvk {

bool repoint_vertex(const std::vector<uint32_t>& code, uint32_t own_set, VertexRepoint& out)
{
    out = VertexRepoint(); out.own_set = own_set;
    auto fail = [&](const char* why) { out.reason = why; return false; };
    if (code.size() < 5 || code[0] != 0x07230203 || code[1] > 0x00010600 || code[4] != 0)
        return fail("spirv_header");
    struct Decor { size_t set_word = 0, binding_word = 0; bool block = false, buffer_block = false; };
    struct Pointer { uint32_t storage, pointee; };
    struct Variable { uint32_t pointer, storage; };
    std::map<uint32_t, Decor> decorations;
    std::map<uint32_t, Pointer> pointers;
    std::map<uint32_t, Variable> variables;
    bool vertex_entry = false;
    for (size_t i = 5; i < code.size();) {
        uint32_t n = code[i] >> 16, op = code[i] & 0xffff;
        if (n == 0 || n > code.size() - i) return fail("spirv_instruction_bounds");
        if (op == 15) { // OpEntryPoint; this helper must never be used on a fragment or mixed module.
            if (n < 4 || code[i + 1] != 0) return fail("spirv_not_vertex");
            vertex_entry = true;
        } else if (op == 71) { // OpDecorate
            if (n < 3 || code[i + 1] >= code[3]) return fail("spirv_decoration");
            auto& d = decorations[code[i + 1]];
            uint32_t kind = code[i + 2];
            if (kind == 33 || kind == 34) {
                if (n != 4) return fail("spirv_descriptor_decoration");
                size_t& word = kind == 33 ? d.binding_word : d.set_word;
                if (word) return fail("spirv_duplicate_decoration");
                word = i + 3;
            } else if (kind == 2) d.block = true;
            else if (kind == 3) d.buffer_block = true;
        } else if (op == 73 || op == 74 || op == 75 || op == 332) {
            // Group/ID decorations need full expansion to prove there are no hidden aliases.
            return fail("spirv_indirect_decoration");
        } else if (op == 32) { // OpTypePointer
            if (n != 4 || code[i + 1] >= code[3]) return fail("spirv_pointer");
            pointers[code[i + 1]] = {code[i + 2], code[i + 3]};
        } else if (op == 59) { // OpVariable
            if (n < 4 || code[i + 2] >= code[3]) return fail("spirv_variable");
            variables[code[i + 2]] = {code[i + 1], code[i + 3]};
        }
        i += n;
    }
    if (!vertex_entry) return fail("spirv_vertex_entry_missing");
    uint32_t bone_id = 0, instance_id = 0;
    for (const auto& item : variables) {
        uint32_t id = item.first;
        const auto& v = item.second;
        auto dit = decorations.find(id);
        if (dit == decorations.end() || !dit->second.set_word || !dit->second.binding_word) continue;
        const auto& d = dit->second;
        const uint32_t set = code[d.set_word], binding = code[d.binding_word];
        if (set == own_set && binding <= 1) return fail("spirv_target_collision");
        auto p = pointers.find(v.pointer);
        if (p == pointers.end() || p->second.storage != v.storage) return fail("spirv_variable_pointer");
        auto type = decorations.find(p->second.pointee);
        const bool storage = v.storage == 12 || (v.storage == 2 && type != decorations.end() && type->second.buffer_block);
        const bool uniform = v.storage == 2 && type != decorations.end() && type->second.block;
        if (storage) {
            if (bone_id) return fail("spirv_multiple_storage_buffers");
            bone_id = id; out.bone_set = set; out.bone_binding = binding;
        }
        if (set == 2 && binding == 0) {
            if (!uniform || instance_id) return fail("spirv_instance_ambiguous");
            instance_id = id;
        }
    }
    if (!bone_id || !instance_id || bone_id == instance_id) return fail("spirv_required_buffer_missing");
    // A second variable at a source binding would make moving just one declaration ambiguous.
    for (const auto& item : variables) {
        auto it = decorations.find(item.first);
        if (it == decorations.end() || !it->second.set_word || !it->second.binding_word) continue;
        uint32_t s = code[it->second.set_word], b = code[it->second.binding_word];
        if ((s == out.bone_set && b == out.bone_binding && item.first != bone_id) ||
            (s == 2 && b == 0 && item.first != instance_id)) return fail("spirv_source_alias");
    }
    out.words = code;
    out.words[decorations[bone_id].set_word] = own_set;
    out.words[decorations[bone_id].binding_word] = 0;
    out.words[decorations[instance_id].set_word] = own_set;
    out.words[decorations[instance_id].binding_word] = 1;
    return true;
}

} // namespace efvk
