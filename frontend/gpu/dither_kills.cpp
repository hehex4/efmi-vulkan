#include "dither_kills.h"

#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace efmi::gpu {
namespace {
enum : uint32_t {
    OpTypeBool = 20, OpTypeFloat = 22, OpConstantTrue = 41, OpConstantFalse = 42, OpConstant = 43, OpFunction = 54,
    OpVariable = 59, OpLoad = 61, OpAccessChain = 65, OpInBoundsAccessChain = 66, OpPtrAccessChain = 67,
    OpInBoundsPtrAccessChain = 70, OpDecorate = 71, OpCopyObject = 83, OpLabel = 248, OpBranchConditional = 250, OpKill = 252,
    OpTerminateInvocation = 4416, OpDemoteToHelperInvocation = 5380,
    DecorationBinding = 33, DecorationDescriptorSet = 34,
};
constexpr float kNoise = 52.9829189f; // interleaved gradient noise: frac(52.9829 * frac(dot(p, (0.0671, 0.0058))))

// Instructions without a result type and id, or whose operands name no value this analysis follows.
// OpExtInst (12) is followed: GLSL.std.450's Floor and Fract are steps of the noise.
bool no_value(uint32_t op)
{
    return op <= 11 || (op >= 14 && op <= 17) || (op >= 19 && op <= 39) || (op >= 41 && op <= 43) || op == 45 || op == 46 ||
           (op >= 48 && op <= 50) || (op >= 54 && op <= 56) || op == 59 || (op >= 62 && op <= 64) || (op >= 71 && op <= 74) ||
           op == 99 || (op >= 218 && op <= 225) || op == 228 || (op >= 246 && op <= 255) || (op >= 330 && op <= 332) ||
           op == 4416 || op == 5380 || op == 5632 || op == 5633;
}

struct Branch { size_t at; uint32_t condition, kill_when; }; // kill_when: 1 the true side kills, 0 the false side
struct Scan {
    bool ok = false;
    std::vector<Branch> dithers;
    uint32_t bool_type = 0, constant_true = 0, constant_false = 0;
    size_t first_function = 0;
};

Scan scan(const std::vector<uint32_t>& code)
{
    Scan s;
    if (code.size() < 5 || code[0] != 0x07230203) return s;
    std::unordered_map<uint32_t, std::vector<uint32_t>> reads;
    std::unordered_map<uint32_t, uint32_t> base, set, binding;
    std::unordered_set<uint32_t> floats, noise, kills;
    struct Candidate { size_t at; uint32_t condition, t, f; };
    std::vector<Candidate> branches;
    uint32_t label = 0;
    for (size_t i = 5; i < code.size();) {
        const uint32_t op = code[i] & 0xFFFF, n = code[i] >> 16;
        if (!n || i + n > code.size()) return {};
        const uint32_t* o = code.data() + i + 1;
        const uint32_t count = n - 1;
        if (op == OpDecorate && count >= 3 && o[1] == DecorationDescriptorSet) set[o[0]] = o[2];
        else if (op == OpDecorate && count >= 3 && o[1] == DecorationBinding) binding[o[0]] = o[2];
        else if (op == OpTypeBool && count >= 1) s.bool_type = o[0];
        else if (op == OpTypeFloat && count >= 2 && o[1] == 32) floats.insert(o[0]);
        else if (op == OpConstantTrue && count >= 2 && o[0] == s.bool_type) s.constant_true = o[1];
        else if (op == OpConstantFalse && count >= 2 && o[0] == s.bool_type) s.constant_false = o[1];
        else if (op == OpConstant && count == 3 && floats.count(o[0])) {
            float value; std::memcpy(&value, &o[2], 4);
            if (std::fabs(value - kNoise) < 1e-3f) noise.insert(o[1]);
        } else if (op == OpVariable && count >= 2) base[o[1]] = o[1];
        else if (op == OpFunction && !s.first_function) s.first_function = i;
        else if (op == OpLabel && count >= 1) label = o[0];
        else if ((op == OpKill || op == OpTerminateInvocation || op == OpDemoteToHelperInvocation) && label) kills.insert(label);
        else if (op == OpBranchConditional && count >= 3) branches.push_back({i, o[0], o[1], o[2]});
        // A pointer or a value read through it keeps the variable it comes from.
        if ((op == OpLoad || op == OpAccessChain || op == OpInBoundsAccessChain || op == OpPtrAccessChain || op == OpInBoundsPtrAccessChain ||
             op == OpCopyObject) && count >= 3) {
            const auto from = base.find(o[2]);
            if (from != base.end()) base[o[1]] = from->second;
        }
        if (!no_value(op) && count >= 2) reads[o[1]].assign(o + 2, o + count);
        i += n;
    }
    if (!s.first_function) return {};
    std::unordered_set<uint32_t> instance;
    for (const auto& v : set)
        if (v.second == 2) { const auto b = binding.find(v.first); if (b != binding.end() && b->second == 0) instance.insert(v.first); }
    for (const auto& b : branches) {
        const bool kill_true = kills.count(b.t) != 0, kill_false = kills.count(b.f) != 0;
        if (kill_true == kill_false) continue;
        // What the condition is computed from: the noise and the instance window, both.
        bool from_noise = false, from_instance = false;
        std::unordered_set<uint32_t> seen;
        std::vector<uint32_t> stack{b.condition};
        while (!stack.empty() && seen.size() < 65536 && !(from_noise && from_instance)) {
            const uint32_t id = stack.back(); stack.pop_back();
            if (!seen.insert(id).second) continue;
            if (noise.count(id)) from_noise = true;
            const auto from = base.find(id);
            if (from != base.end() && instance.count(from->second)) from_instance = true;
            const auto r = reads.find(id);
            if (r != reads.end()) stack.insert(stack.end(), r->second.begin(), r->second.end());
        }
        if (from_noise && from_instance) s.dithers.push_back({b.at, b.condition, kill_true ? 1u : 0u});
    }
    s.ok = true;
    return s;
}
}

uint32_t dither_kills(const std::vector<uint32_t>& code)
{
    const auto s = scan(code);
    return s.ok ? uint32_t(s.dithers.size()) : 0;
}

bool remove_dither(const std::vector<uint32_t>& code, std::vector<uint32_t>& output, std::string& reason)
{
    const auto s = scan(code);
    if (!s.ok) { reason = "the module cannot be read"; return false; }
    if (s.dithers.empty()) { reason = "the module has no dither kill"; return false; }
    if (!s.bool_type) { reason = "the module declares no boolean type"; return false; }
    std::vector<uint32_t> words = code;
    uint32_t bound = words[3], constant_true = s.constant_true, constant_false = s.constant_false;
    std::vector<uint32_t> declarations;
    auto constant = [&](uint32_t& id, uint32_t op) {
        if (id) return id;
        id = bound++;
        declarations.insert(declarations.end(), {(3u << 16) | op, s.bool_type, id});
        return id;
    };
    // The side that kills is never taken: the condition becomes the other side's constant.
    for (const auto& d : s.dithers)
        words[d.at + 1] = d.kill_when ? constant(constant_false, OpConstantFalse) : constant(constant_true, OpConstantTrue);
    words[3] = bound;
    words.insert(words.begin() + std::ptrdiff_t(s.first_function), declarations.begin(), declarations.end());
    if (dither_kills(words)) { reason = "a dither kill remains after the patch"; return false; }
    output = std::move(words);
    return true;
}
}
