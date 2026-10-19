#include "source_registers.h"

#include <map>
#include <set>
#include <unordered_map>

namespace efmi::gpu {
namespace {
constexpr uint32_t kMagic = 0x07230203, kMaxWords = 4u << 20;
enum : uint32_t {
    OpTypeImage = 25, OpTypeSampler = 26, OpTypeSampledImage = 27, OpTypeArray = 28, OpTypeRuntimeArray = 29,
    OpTypeStruct = 30, OpTypePointer = 32, OpConstant = 43, OpVariable = 59, OpDecorate = 71, OpMemberDecorate = 72,
    DecorationBlock = 2, DecorationBufferBlock = 3, DecorationNonWritable = 24, DecorationBinding = 33, DecorationDescriptorSet = 34,
    StorageUniformConstant = 0, StorageUniform = 2, StorageStorageBuffer = 12,
};
struct Type { uint32_t op = 0; std::vector<uint32_t> operands; };
} // namespace

bool source_registers(const std::vector<uint32_t>& code, std::vector<SourceRegister>& out, std::string& reason)
{
    reason.clear();
    if (code.size() < 5 || code.size() > kMaxWords || code[0] != kMagic) { reason = "not a SPIR-V module"; return false; }
    std::unordered_map<uint32_t, Type> types;
    std::unordered_map<uint32_t, uint32_t> constants, bindings, sets;
    std::set<uint32_t> nonwritable, buffer_blocks;
    std::unordered_map<uint32_t, std::set<uint32_t>> readonly_members;
    struct Variable { uint32_t id, pointer, storage; };
    std::vector<Variable> variables;
    for (size_t i = 5; i < code.size();) {
        const uint32_t op = code[i] & 0xFFFF, count = code[i] >> 16;
        if (!count || i + count > code.size()) { reason = "malformed SPIR-V instruction"; return false; }
        const uint32_t* o = code.data() + i + 1;
        const uint32_t n = count - 1;
        if (op == OpDecorate && n >= 2) {
            if (o[1] == DecorationBinding && n >= 3) bindings[o[0]] = o[2];
            else if (o[1] == DecorationDescriptorSet && n >= 3) sets[o[0]] = o[2];
            else if (o[1] == DecorationNonWritable) nonwritable.insert(o[0]);
            else if (o[1] == DecorationBufferBlock) buffer_blocks.insert(o[0]);
        } else if (op == OpMemberDecorate && n >= 3 && o[2] == DecorationNonWritable) {
            readonly_members[o[0]].insert(o[1]);
        } else if (op == OpConstant && n >= 3) {
            constants[o[1]] = o[2];
        } else if ((op == OpTypeImage || op == OpTypeSampler || op == OpTypeSampledImage || op == OpTypeArray ||
                    op == OpTypeRuntimeArray || op == OpTypeStruct || op == OpTypePointer) && n >= 1) {
            types[o[0]] = {op, std::vector<uint32_t>(o + 1, o + n)};
        } else if (op == OpVariable && n >= 3) {
            variables.push_back({o[1], o[0], o[2]});
        }
        i += count;
    }
    std::vector<SourceRegister> found;
    for (const auto& v : variables) {
        const auto binding = bindings.find(v.id);
        if (binding == bindings.end()) continue;
        const auto pointer = types.find(v.pointer);
        if (pointer == types.end() || pointer->second.op != OpTypePointer || pointer->second.operands.size() < 2) {
            reason = "resource variable without a pointer type"; return false;
        }
        uint32_t type = pointer->second.operands[1], elements = 1;
        auto t = types.find(type);
        if (t != types.end() && t->second.op == OpTypeRuntimeArray) { reason = "runtime-sized resource array"; return false; }
        if (t != types.end() && t->second.op == OpTypeArray && t->second.operands.size() >= 2) {
            const auto length = constants.find(t->second.operands[1]);
            if (length == constants.end() || !length->second || length->second > 4096) { reason = "resource array of unknown length"; return false; }
            elements = length->second;
            type = t->second.operands[0];
            t = types.find(type);
        }
        if (t == types.end()) { reason = "resource of unknown type"; return false; }
        SourceRegister r;
        r.set = sets.count(v.id) ? sets.at(v.id) : 0;
        r.binding = binding->second;
        r.count = elements;
        const auto& a = t->second.operands;
        if (t->second.op == OpTypeImage || t->second.op == OpTypeSampledImage) {
            const Type* image = &t->second;
            if (t->second.op == OpTypeSampledImage) {
                const auto inner = a.empty() ? types.end() : types.find(a[0]);
                if (inner == types.end() || inner->second.op != OpTypeImage) { reason = "sampled image without an image type"; return false; }
                image = &inner->second;
            }
            if (image->operands.size() < 6) { reason = "malformed image type"; return false; }
            // OpTypeImage: sampled type, Dim, Depth, Arrayed, MS, Sampled (2: storage).
            r.dim = image->operands[1];
            r.arrayed = image->operands[3] != 0;
            r.multisampled = image->operands[4] != 0;
            const bool storage = image->operands[5] == 2;
            r.kind = r.dim == 5 ? (storage ? SourceRegister::TexelUav : SourceRegister::TexelSrv)
                                : (storage ? SourceRegister::ImageUav : SourceRegister::ImageSrv);
        } else if (t->second.op == OpTypeSampler) {
            r.kind = SourceRegister::Sampler;
        } else if (t->second.op == OpTypeStruct) {
            if (v.storage == StorageStorageBuffer || buffer_blocks.count(type)) {
                const auto members = readonly_members.find(type);
                const bool readonly = nonwritable.count(v.id) || nonwritable.count(type) ||
                                      (members != readonly_members.end() && members->second.size() == a.size());
                r.kind = readonly ? SourceRegister::BufferSrv : SourceRegister::BufferUav;
            } else if (v.storage == StorageUniform) {
                r.kind = SourceRegister::ConstantBuffer;
            } else { reason = "block in an unknown storage class"; return false; }
        } else { reason = "resource of unknown type"; return false; }
        found.push_back(r);
    }
    // Numbering: t for shader resource views (buffers first), then b, s, u, each in module order.
    std::vector<SourceRegister> result;
    auto number = [&](char letter, auto&& wanted) {
        uint32_t next = 0;
        for (auto r : found) {
            if (!wanted(r.kind)) continue;
            r.letter = letter; r.index = next; next += r.count;
            result.push_back(r);
        }
    };
    uint32_t t_next = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (auto r : found) {
            const bool buffer = r.kind == SourceRegister::BufferSrv || r.kind == SourceRegister::TexelSrv;
            if (r.kind != SourceRegister::ImageSrv && !buffer) continue;
            if (buffer != (pass == 0)) continue;
            r.letter = 't'; r.index = t_next; t_next += r.count;
            result.push_back(r);
        }
    number('b', [](SourceRegister::Kind k) { return k == SourceRegister::ConstantBuffer; });
    number('s', [](SourceRegister::Kind k) { return k == SourceRegister::Sampler; });
    number('u', [](SourceRegister::Kind k) { return k == SourceRegister::ImageUav || k == SourceRegister::BufferUav || k == SourceRegister::TexelUav; });
    out = std::move(result);
    return true;
}

const SourceRegister* find_source_register(const std::vector<SourceRegister>& registers, char letter, uint32_t index)
{
    for (const auto& r : registers)
        if (r.letter == letter && index >= r.index && index - r.index < r.count) return &r;
    return nullptr;
}

} // namespace efmi::gpu
