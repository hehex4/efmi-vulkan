// A minimal SPIR-V builder for the two shaders of the sandbox.
//
// There is no shader compiler on the development machine, so the modules are
// assembled word by word. Opcode and enumerant values are those of the SPIR-V
// 1.0 specification.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace spv {

enum Op : uint16_t {
    OpMemoryModel = 14,
    OpEntryPoint = 15,
    OpExecutionMode = 16,
    OpCapability = 17,
    OpTypeVoid = 19,
    OpTypeInt = 21,
    OpTypeFloat = 22,
    OpTypeVector = 23,
    OpTypeStruct = 30,
    OpTypePointer = 32,
    OpTypeFunction = 33,
    OpConstant = 43,
    OpFunction = 54,
    OpFunctionEnd = 56,
    OpVariable = 59,
    OpLoad = 61,
    OpStore = 62,
    OpAccessChain = 65,
    OpDecorate = 71,
    OpMemberDecorate = 72,
    OpCompositeConstruct = 80,
    OpCompositeExtract = 81,
    OpFAdd = 129,
    OpLabel = 248,
    OpReturn = 253,
};

enum : uint32_t {
    CapabilityShader = 1,
    AddressingLogical = 0,
    MemoryGLSL450 = 1,
    ModelVertex = 0,
    ModelFragment = 4,
    ModeOriginUpperLeft = 7,
    StorageInput = 1,
    StorageUniform = 2,
    StorageOutput = 3,
    DecorationBlock = 2,
    DecorationBuiltIn = 11,
    DecorationLocation = 30,
    DecorationBinding = 33,
    DecorationDescriptorSet = 34,
    DecorationOffset = 35,
    BuiltInPosition = 0,
};

class Builder {
public:
    uint32_t id() { return next_++; }

    void op(Op opcode, std::initializer_list<uint32_t> operands)
    {
        words_.push_back((static_cast<uint32_t>(operands.size() + 1) << 16) | opcode);
        words_.insert(words_.end(), operands.begin(), operands.end());
    }

    void entry_point(uint32_t model, uint32_t fn, const char* name, std::initializer_list<uint32_t> interface_ids)
    {
        std::vector<uint32_t> operands = {model, fn};
        uint32_t word = 0;
        int shift = 0;
        for (const char* p = name;; ++p) {
            word |= static_cast<uint32_t>(static_cast<unsigned char>(*p)) << shift;
            shift += 8;
            if (shift == 32) {
                operands.push_back(word);
                word = 0;
                shift = 0;
            }
            if (!*p)
                break;
        }
        if (shift)
            operands.push_back(word);
        operands.insert(operands.end(), interface_ids.begin(), interface_ids.end());
        words_.push_back((static_cast<uint32_t>(operands.size() + 1) << 16) | OpEntryPoint);
        words_.insert(words_.end(), operands.begin(), operands.end());
    }

    std::vector<uint32_t> finish() const
    {
        std::vector<uint32_t> out = {0x07230203u, 0x00010000u, 0u, next_, 0u};
        out.insert(out.end(), words_.begin(), words_.end());
        return out;
    }

private:
    std::vector<uint32_t> words_;
    uint32_t next_ = 1;
};

// layout(location = 0) in vec2 inPos;
// layout(set = 0, binding = 0) uniform U { vec4 offset; vec4 color; } u;
// void main() { gl_Position = vec4(inPos, 0.0, 1.0) + u.offset; }
inline std::vector<uint32_t> vertex_shader()
{
    Builder b;
    uint32_t main = b.id(), in_pos = b.id(), per_vertex = b.id(), pv_var = b.id(), u_type = b.id(), u_var = b.id();
    uint32_t t_void = b.id(), t_fn = b.id(), t_float = b.id(), t_v2 = b.id(), t_v4 = b.id(), t_int = b.id();
    uint32_t p_in_v2 = b.id(), p_out_pv = b.id(), p_uni_u = b.id(), p_uni_v4 = b.id(), p_out_v4 = b.id();
    uint32_t c_i0 = b.id(), c_f0 = b.id(), c_f1 = b.id();
    uint32_t label = b.id(), pos = b.id(), px = b.id(), py = b.id(), pos4 = b.id(), off_ptr = b.id(), off = b.id();
    uint32_t sum = b.id(), out_ptr = b.id();

    b.op(OpCapability, {CapabilityShader});
    b.op(OpMemoryModel, {AddressingLogical, MemoryGLSL450});
    b.entry_point(ModelVertex, main, "main", {in_pos, pv_var});

    b.op(OpDecorate, {in_pos, DecorationLocation, 0});
    b.op(OpMemberDecorate, {per_vertex, 0, DecorationBuiltIn, BuiltInPosition});
    b.op(OpDecorate, {per_vertex, DecorationBlock});
    b.op(OpMemberDecorate, {u_type, 0, DecorationOffset, 0});
    b.op(OpMemberDecorate, {u_type, 1, DecorationOffset, 16});
    b.op(OpDecorate, {u_type, DecorationBlock});
    b.op(OpDecorate, {u_var, DecorationDescriptorSet, 0});
    b.op(OpDecorate, {u_var, DecorationBinding, 0});

    b.op(OpTypeVoid, {t_void});
    b.op(OpTypeFunction, {t_fn, t_void});
    b.op(OpTypeFloat, {t_float, 32});
    b.op(OpTypeVector, {t_v2, t_float, 2});
    b.op(OpTypeVector, {t_v4, t_float, 4});
    b.op(OpTypeInt, {t_int, 32, 1});
    b.op(OpTypeStruct, {per_vertex, t_v4});
    b.op(OpTypeStruct, {u_type, t_v4, t_v4});
    b.op(OpTypePointer, {p_in_v2, StorageInput, t_v2});
    b.op(OpTypePointer, {p_out_pv, StorageOutput, per_vertex});
    b.op(OpTypePointer, {p_uni_u, StorageUniform, u_type});
    b.op(OpTypePointer, {p_uni_v4, StorageUniform, t_v4});
    b.op(OpTypePointer, {p_out_v4, StorageOutput, t_v4});
    b.op(OpConstant, {t_int, c_i0, 0});
    b.op(OpConstant, {t_float, c_f0, 0x00000000u});
    b.op(OpConstant, {t_float, c_f1, 0x3F800000u});
    b.op(OpVariable, {p_in_v2, in_pos, StorageInput});
    b.op(OpVariable, {p_out_pv, pv_var, StorageOutput});
    b.op(OpVariable, {p_uni_u, u_var, StorageUniform});

    b.op(OpFunction, {t_void, main, 0, t_fn});
    b.op(OpLabel, {label});
    b.op(OpLoad, {t_v2, pos, in_pos});
    b.op(OpCompositeExtract, {t_float, px, pos, 0});
    b.op(OpCompositeExtract, {t_float, py, pos, 1});
    b.op(OpCompositeConstruct, {t_v4, pos4, px, py, c_f0, c_f1});
    b.op(OpAccessChain, {p_uni_v4, off_ptr, u_var, c_i0});
    b.op(OpLoad, {t_v4, off, off_ptr});
    b.op(OpFAdd, {t_v4, sum, pos4, off});
    b.op(OpAccessChain, {p_out_v4, out_ptr, pv_var, c_i0});
    b.op(OpStore, {out_ptr, sum});
    b.op(OpReturn, {});
    b.op(OpFunctionEnd, {});
    return b.finish();
}

// layout(location = 0) out vec4 outColor;
// layout(set = 0, binding = 0) uniform U { vec4 offset; vec4 color; } u;
// void main() { outColor = u.color; }
inline std::vector<uint32_t> fragment_shader()
{
    Builder b;
    uint32_t main = b.id(), out_color = b.id(), u_type = b.id(), u_var = b.id();
    uint32_t t_void = b.id(), t_fn = b.id(), t_float = b.id(), t_v4 = b.id(), t_int = b.id();
    uint32_t p_out_v4 = b.id(), p_uni_u = b.id(), p_uni_v4 = b.id(), c_i1 = b.id();
    uint32_t label = b.id(), col_ptr = b.id(), col = b.id();

    b.op(OpCapability, {CapabilityShader});
    b.op(OpMemoryModel, {AddressingLogical, MemoryGLSL450});
    b.entry_point(ModelFragment, main, "main", {out_color});
    b.op(OpExecutionMode, {main, ModeOriginUpperLeft});

    b.op(OpDecorate, {out_color, DecorationLocation, 0});
    b.op(OpMemberDecorate, {u_type, 0, DecorationOffset, 0});
    b.op(OpMemberDecorate, {u_type, 1, DecorationOffset, 16});
    b.op(OpDecorate, {u_type, DecorationBlock});
    b.op(OpDecorate, {u_var, DecorationDescriptorSet, 0});
    b.op(OpDecorate, {u_var, DecorationBinding, 0});

    b.op(OpTypeVoid, {t_void});
    b.op(OpTypeFunction, {t_fn, t_void});
    b.op(OpTypeFloat, {t_float, 32});
    b.op(OpTypeVector, {t_v4, t_float, 4});
    b.op(OpTypeInt, {t_int, 32, 1});
    b.op(OpTypeStruct, {u_type, t_v4, t_v4});
    b.op(OpTypePointer, {p_out_v4, StorageOutput, t_v4});
    b.op(OpTypePointer, {p_uni_u, StorageUniform, u_type});
    b.op(OpTypePointer, {p_uni_v4, StorageUniform, t_v4});
    b.op(OpConstant, {t_int, c_i1, 1});
    b.op(OpVariable, {p_out_v4, out_color, StorageOutput});
    b.op(OpVariable, {p_uni_u, u_var, StorageUniform});

    b.op(OpFunction, {t_void, main, 0, t_fn});
    b.op(OpLabel, {label});
    b.op(OpAccessChain, {p_uni_v4, col_ptr, u_var, c_i1});
    b.op(OpLoad, {t_v4, col, col_ptr});
    b.op(OpStore, {out_color, col});
    b.op(OpReturn, {});
    b.op(OpFunctionEnd, {});
    return b.finish();
}

} // namespace spv
