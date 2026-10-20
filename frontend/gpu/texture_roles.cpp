#include "texture_roles.h"

#include "source_registers.h"

#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace efmi::gpu {
namespace {
constexpr uint32_t kMagic = 0x07230203, kMaxWords = 4u << 20;
constexpr uint32_t kNear = 100;       // instructions between a role's sample and the normal map's
constexpr unsigned kDecodeDepth = 12; // data-flow steps searched from a sample's result
enum : uint32_t {
    OpExtInst = 12, OpTypeFloat = 22, OpConstant = 43, OpVariable = 59, OpLoad = 61, OpAccessChain = 65,
    OpInBoundsAccessChain = 66, OpDecorate = 71, OpVectorShuffle = 79, OpCompositeConstruct = 80,
    OpCompositeExtract = 81, OpCompositeInsert = 82, OpSampledImage = 86, OpImage = 100, OpBitcast = 124,
    OpFNegate = 127, OpIAdd = 128, OpFAdd = 129, OpISub = 130, OpFSub = 131, OpFMul = 133, OpFDiv = 136,
    OpVectorTimesScalar = 142, OpDot = 148, DecorationBinding = 33, DecorationDescriptorSet = 34,
    GlslSqrt = 31, GlslInverseSqrt = 32,
};
bool sample_op(uint32_t op) { return (op >= 87 && op <= 97) || (op >= 305 && op <= 315); }
// The ids a value-producing instruction reads, for the ones a normal decode passes through.
void flow_operands(uint32_t op, const uint32_t* o, uint32_t n, std::vector<uint32_t>& ids)
{
    ids.clear();
    auto range = [&](uint32_t from, uint32_t to) { for (uint32_t i = from; i < to && i < n; ++i) ids.push_back(o[i]); };
    switch (op) {
    case OpVectorShuffle: case OpCompositeInsert: case OpVectorTimesScalar: case OpDot: range(2, 4); break;
    case OpCompositeExtract: case OpBitcast: range(2, 3); break;
    case OpCompositeConstruct: case OpFNegate: case OpIAdd: case OpFAdd: case OpISub: case OpFSub: case OpFMul: case OpFDiv:
        range(2, n); break;
    case OpExtInst: range(4, n); break;
    default: break;
    }
}
} // namespace

bool role_samples(const std::vector<uint32_t>& code, std::vector<RoleSample>& out, std::string& reason)
{
    reason.clear();
    if (code.size() < 5 || code.size() > kMaxWords || code[0] != kMagic) { reason = "not a SPIR-V module"; return false; }
    std::vector<SourceRegister> registers;
    if (!source_registers(code, registers, reason)) return false;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> t_of;
    for (const auto& r : registers)
        if (r.letter == 't') t_of[{r.set, r.binding}] = r.index;
    struct Inst { uint32_t op; const uint32_t* o; uint32_t n; };
    std::vector<Inst> insts;
    for (size_t i = 5; i < code.size();) {
        const uint32_t op = code[i] & 0xFFFF, count = code[i] >> 16;
        if (!count || i + count > code.size()) { reason = "malformed SPIR-V instruction"; return false; }
        insts.push_back({op, code.data() + i + 1, count - 1});
        i += count;
    }
    std::unordered_set<uint32_t> float32, twos;
    for (const auto& in : insts) {
        if (in.op == OpTypeFloat && in.n > 1 && in.o[1] == 32) float32.insert(in.o[0]);
        else if (in.op == OpConstant && in.n > 2 && float32.count(in.o[0]) && in.o[2] == 0x40000000u) twos.insert(in.o[1]);
    }
    std::unordered_map<uint32_t, std::map<uint32_t, uint32_t>> deco;
    std::unordered_map<uint32_t, uint32_t> origin;
    std::unordered_map<uint32_t, std::vector<uint32_t>> users; // id -> instruction indices reading it
    struct Found { RoleSample sample; uint32_t result; };
    std::vector<Found> found;
    std::vector<uint32_t> ids;
    for (uint32_t at = 0; at < insts.size(); ++at) {
        const auto& in = insts[at];
        const uint32_t* o = in.o;
        if (in.op == OpDecorate && in.n > 2 && (o[1] == DecorationBinding || o[1] == DecorationDescriptorSet)) {
            deco[o[0]][o[1]] = o[2];
        } else if (in.op == OpVariable && in.n > 1) {
            origin[o[1]] = o[1];
        } else if ((in.op == OpLoad || in.op == OpAccessChain || in.op == OpInBoundsAccessChain || in.op == OpSampledImage ||
                    in.op == OpImage) && in.n > 2) {
            const auto from = origin.find(o[2]);
            if (from != origin.end()) origin[o[1]] = from->second;
        }
        if (sample_op(in.op) && in.n > 2) {
            const auto from = origin.find(o[2]);
            if (from != origin.end()) {
                const auto d = deco.find(from->second);
                const uint32_t set = d != deco.end() && d->second.count(DecorationDescriptorSet) ? d->second.at(DecorationDescriptorSet) : 0;
                if (d != deco.end() && d->second.count(DecorationBinding)) {
                    const auto t = t_of.find({set, d->second.at(DecorationBinding)});
                    if (t != t_of.end()) {
                        RoleSample s; s.t = t->second; s.set = set; s.binding = d->second.at(DecorationBinding); s.at = at;
                        found.push_back({s, o[1]});
                    }
                }
            }
        }
        flow_operands(in.op, o, in.n, ids);
        for (uint32_t id : ids) users[id].push_back(at);
    }
    auto decoded = [&](uint32_t start) {
        std::unordered_set<uint32_t> seen{start};
        std::vector<uint32_t> frontier{start}, next;
        bool scaled = false, rooted = false;
        for (unsigned step = 0; step < kDecodeDepth; ++step) {
            next.clear();
            for (uint32_t v : frontier) {
                const auto use = users.find(v);
                if (use == users.end()) continue;
                for (uint32_t index : use->second) {
                    const auto& in = insts[index];
                    if (in.n < 2 || !seen.insert(in.o[1]).second) continue;
                    next.push_back(in.o[1]);
                    if ((in.op == OpFMul || in.op == OpVectorTimesScalar) &&
                        ((in.n > 2 && twos.count(in.o[2])) || (in.n > 3 && twos.count(in.o[3])))) scaled = true;
                    if (in.op == OpExtInst && in.n > 4 && (in.o[3] == GlslSqrt || in.o[3] == GlslInverseSqrt)) rooted = true;
                }
            }
            frontier.swap(next);
        }
        return scaled && rooted;
    };
    out.clear();
    for (auto& f : found) {
        f.sample.decoded = decoded(f.result);
        out.push_back(f.sample);
    }
    return true;
}

char texture_class(const RoleTexture& t)
{
    if (!t.known || !t.width || t.width != t.height || t.levels < 2 || t.levels > 32 || (1ull << (t.levels - 1)) != t.width) return 'o';
    switch (t.format) {
    case 43: case 50: case 57: case 132: case 134: case 136: case 138: case 146: return 'S'; // *_SRGB colour and BC1..BC7
    case 141: case 142: return 'N';                                                         // BC5
    case 131: case 133: case 135: case 137: case 139: case 145: return 'U';                 // BC1..BC4, BC7 UNORM
    default: return 'o';
    }
}

TextureRoles texture_roles(const std::vector<RoleSample>& samples, const std::function<RoleTexture(const RoleSample&)>& texture)
{
    struct Item { uint32_t t; char cls; uint32_t width, height; bool decoded; uint32_t at; };
    std::vector<Item> seq;
    std::set<uint32_t> seen;
    for (const auto& s : samples) {
        if (!seen.insert(s.t).second) continue;
        const RoleTexture tex = texture(s);
        seq.push_back({s.t, texture_class(tex), tex.known ? tex.width : 0, tex.known ? tex.height : 0, s.decoded, s.at});
    }
    TextureRoles roles;
    size_t first = 0;
    while (first < seq.size() && seq[first].cls != 'S') ++first;
    if (first == seq.size()) return roles;
    const Item& diffuse = seq[first];
    roles.diffuse = int32_t(diffuse.t);
    size_t rest = first + 1;
    uint32_t previous = diffuse.at;
    if (rest < seq.size() && seq[rest].cls == 'U' && seq[rest].width == diffuse.width && seq[rest].height == diffuse.height) {
        roles.lightmap = int32_t(seq[rest].t);
        previous = seq[rest].at;
        ++rest;
    }
    const Item* normal = nullptr;
    for (size_t i = rest; i < seq.size() && !normal; ++i)
        if (seq[i].cls == 'N') normal = &seq[i];
    for (size_t i = rest; i < seq.size() && !normal; ++i)
        if (seq[i].decoded && (seq[i].cls == 'S' || seq[i].cls == 'U' || seq[i].cls == 'N') && seq[i].at - previous <= kNear) normal = &seq[i];
    if (!normal && rest < seq.size() && seq[rest].cls == 'U') normal = &seq[rest];
    if (normal) roles.normal = int32_t(normal->t);
    return roles;
}

} // namespace efmi::gpu
