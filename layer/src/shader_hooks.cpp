// What the library's shader hooks stand for, read off SPIR-V (D-113).
#include "shader_hooks.h"
#include <unordered_map>
#include <unordered_set>

namespace efvk {
namespace {
// SPIR-V words of the few instructions read here (SPIR-V 1.0 numbering).
enum Op : uint32_t {
    OpEntryPoint = 15, OpTypeInt = 21, OpTypeFloat = 22, OpTypeVector = 23, OpTypeImage = 25, OpTypeSampler = 26,
    OpTypeSampledImage = 27, OpTypeStruct = 30, OpTypePointer = 32, OpFunction = 54, OpFunctionEnd = 56, OpFunctionCall = 57,
    OpVariable = 59, OpLoad = 61, OpStore = 62, OpAccessChain = 65, OpInBoundsAccessChain = 66, OpDecorate = 71,
    OpSampledImage = 86, OpImageSampleExplicitLod = 88, OpLabel = 248, OpReturn = 253
};
enum Decoration : uint32_t { DecBlock = 2, DecBufferBlock = 3, DecBuiltIn = 11, DecFlat = 14, DecLocation = 30, DecBinding = 33, DecSet = 34 };
enum Storage : uint32_t { SUniformConstant = 0, SInput = 1, SUniform = 2, SOutput = 3, SStorageBuffer = 12 };
constexpr uint32_t kPosition = 0, kInstanceIndex = 43;

struct Module {
    uint32_t model = UINT32_MAX;
    std::unordered_map<uint32_t, std::vector<uint32_t>> types; // result id -> [opcode, operands after the result]
    std::unordered_map<uint32_t, uint32_t> location, builtin;
    std::unordered_set<uint32_t> flat, block, buffer_block;
    struct Var { uint32_t type = 0, storage = 0; };
    std::unordered_map<uint32_t, Var> vars;
    std::vector<std::vector<uint32_t>> body; // function instructions: [opcode, operands...]
    bool parse(const std::vector<uint32_t>& code)
    {
        if (code.size() < 5 || code[0] != 0x07230203u) return false;
        bool in_function = false;
        for (size_t at = 5; at < code.size();) {
            const uint32_t count = code[at] >> 16, op = code[at] & 0xffff;
            if (!count || at + count > code.size()) return false;
            const uint32_t* w = &code[at + 1];
            const uint32_t n = count - 1;
            switch (op) {
            case OpEntryPoint: if (n >= 1 && model == UINT32_MAX) model = w[0]; break;
            case OpDecorate:
                if (n >= 2) {
                    if (w[1] == DecLocation && n >= 3) location[w[0]] = w[2];
                    else if (w[1] == DecBuiltIn && n >= 3) builtin[w[0]] = w[2];
                    else if (w[1] == DecFlat) flat.insert(w[0]);
                    else if (w[1] == DecBlock) block.insert(w[0]);
                    else if (w[1] == DecBufferBlock) buffer_block.insert(w[0]);
                }
                break;
            case OpTypeInt: case OpTypeFloat: case OpTypeVector: case OpTypeImage: case OpTypeSampler: case OpTypeSampledImage:
            case OpTypeStruct: case OpTypePointer:
                if (n >= 1) { std::vector<uint32_t> t{op}; t.insert(t.end(), w + 1, w + n); types[w[0]] = std::move(t); }
                break;
            case OpVariable: if (n >= 3 && !in_function) vars[w[1]] = {w[0], w[2]}; break;
            case OpFunction: in_function = true; break;
            case OpFunctionEnd: in_function = false; break;
            default: break;
            }
            if (in_function) { std::vector<uint32_t> i{op}; i.insert(i.end(), w, w + n); body.push_back(std::move(i)); }
            at += count;
        }
        return model != UINT32_MAX;
    }
    const std::vector<uint32_t>* type(uint32_t id) const { const auto t = types.find(id); return t == types.end() ? nullptr : &t->second; }
    // The type a pointer variable points at.
    uint32_t pointee(uint32_t pointer) const { const auto* t = type(pointer); return t && (*t)[0] == OpTypePointer && t->size() >= 3 ? (*t)[2] : 0; }
    // 'f' float, 'u' unsigned, 'i' signed, with components; 0 otherwise.
    std::pair<char, uint32_t> scalar_vector(uint32_t id) const
    {
        const auto* t = type(id);
        if (!t) return {0, 0};
        if ((*t)[0] == OpTypeVector && t->size() >= 3) { const auto inner = scalar_vector((*t)[1]); return inner.second == 1 ? std::make_pair(inner.first, (*t)[2]) : std::make_pair('\0', 0u); }
        if ((*t)[0] == OpTypeFloat) return {'f', 1};
        if ((*t)[0] == OpTypeInt && t->size() >= 3) return {(*t)[2] ? 'i' : 'u', 1};
        return {0, 0};
    }
};
}

bool lit_character_vertex(const std::vector<uint32_t>& code)
{
    Module m;
    if (!m.parse(code) || m.model != 0) return false;
    // DX11 o1..o9 of the lit vertex shader at locations 0..8: float2, float3, float3, float4, float3 x4, one component.
    static const uint32_t widths[9] = {2, 3, 3, 4, 3, 3, 3, 3, 1};
    bool instance = false, position = false, storage = false, uniform = false;
    uint32_t seen = 0;
    for (const auto& [id, var] : m.vars) {
        const uint32_t pointee = m.pointee(var.type);
        const auto builtin = m.builtin.find(id);
        if (var.storage == SInput && builtin != m.builtin.end() && builtin->second == kInstanceIndex) instance = true;
        if (var.storage == SOutput) {
            if (builtin != m.builtin.end()) { position |= builtin->second == kPosition; continue; }
            const auto location = m.location.find(id);
            if (location == m.location.end() || location->second > 8) return false;
            const auto sv = m.scalar_vector(pointee);
            if (sv.second != widths[location->second] || (location->second < 8 && sv.first != 'f')) return false;
            seen |= 1u << location->second;
        }
        if (var.storage == SStorageBuffer || (var.storage == SUniform && m.buffer_block.count(pointee))) storage = true;
        else if (var.storage == SUniform && m.block.count(pointee)) uniform = true;
    }
    return instance && position && storage && uniform && (seen & 1u) && (seen & (1u << 8));
}

bool copy_fragment(const std::vector<uint32_t>& code)
{
    Module m;
    if (!m.parse(code) || m.model != 4) return false;
    uint32_t input = 0, output = 0, images = 0, samplers = 0, blocks = 0;
    for (const auto& [id, var] : m.vars) {
        const uint32_t pointee = m.pointee(var.type);
        const bool builtin = m.builtin.count(id) != 0;
        if (var.storage == SInput && !builtin) {
            if (input || m.scalar_vector(pointee) != std::make_pair('f', 2u)) return false;
            input = id;
        } else if (var.storage == SOutput) {
            if (builtin || output || m.scalar_vector(pointee) != std::make_pair('f', 4u)) return false;
            const auto location = m.location.find(id);
            if (location == m.location.end() || location->second) return false;
            output = id;
        } else if (var.storage == SUniformConstant) {
            const auto* t = m.type(pointee);
            if (!t) return false;
            if ((*t)[0] == OpTypeImage && t->size() >= 3 && (*t)[2] == 1 && m.scalar_vector((*t)[1]).first == 'f') ++images;
            else if ((*t)[0] == OpTypeSampler) ++samplers;
            else return false;
        } else if (var.storage == SUniform && m.block.count(pointee)) ++blocks;
        else if (var.storage != SInput) return false;
    }
    if (!input || !output || images != 1 || samplers != 1 || blocks > 1) return false;
    // The body: loads, one sampled image, one explicit-LOD sample of the loaded UV, stored as it is.
    std::unordered_map<uint32_t, uint32_t> loaded; // load result -> variable
    uint32_t sample = 0, samples = 0, stores = 0;
    for (const auto& i : m.body) {
        switch (i[0]) {
        case OpFunction: case OpFunctionEnd: case OpLabel: case OpReturn: case OpAccessChain: case OpInBoundsAccessChain: case OpSampledImage: break;
        case OpLoad: if (i.size() >= 4) loaded[i[2]] = i[3]; break;
        case OpImageSampleExplicitLod:
            // result type, result, sampled image, coordinate, image operands (Lod), lod
            if (i.size() < 7 || !(i[5] & 0x2) || loaded.count(i[4]) == 0 || loaded[i[4]] != input) return false;
            sample = i[2]; ++samples; break;
        case OpStore:
            if (i.size() < 3 || i[1] != output || i[2] != sample) return false;
            ++stores; break;
        default: return false;
        }
    }
    return samples == 1 && stores == 1;
}

} // namespace efvk
