#pragma once
#include "ShaderRegex.h"
#include <array>
#include <memory>

class HackerContext;
namespace efmi::frontend {
class SourceShaderCode;
// Facts about the original tracked source shader, not a temporary CustomShader
// executable. Unknown is distinct from a known unbound stage or unknown hash.
struct SourceShaderBinding {
    bool known=false,bound=false,hash_known=false;
    uint64_t hash=0;
    std::shared_ptr<const ShaderBindings> declarations;
    std::string model; // Proven native model, e.g. vs_5_0; empty means unknown.
    std::shared_ptr<const SourceShaderCode> code; // Optional immutable original DXBC.
};
struct SourceShaderState {
    std::array<SourceShaderBinding,6> stages; // v,h,d,g,p,c
};
std::shared_ptr<const ShaderBindings> source_shader_declarations(const void*,size_t);
std::string source_shader_model(const void*,size_t);

// Installs bounded generation-local facts for the unchanged source evaluator.
// Opaque tokens are map keys only; no D3D object is created or dereferenced.
class SourceShaderContext {
public:
    void install(const SourceShaderState&,HackerContext&);
    bool query(CommandListOperand&,CommandListState&,float&,std::wstring&);
    const SourceShaderBinding& stage(unsigned i) const {return state_.stages.at(i);}
private:
    SourceShaderState state_;
    std::array<unsigned char,6> tokens_{};
};
}
