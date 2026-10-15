#pragma once
#include "shader_state.h"
#include <vector>

namespace efmi::frontend {
// Immutable original DXBC evidence. Construction validates the container and
// derives the native model/declarations; callers cannot supply unrelated text.
class SourceShaderCode {
public:
    static std::shared_ptr<const SourceShaderCode> create(const void*,size_t);
    const std::vector<uint8_t>& bytes() const {return bytes_;}
    const std::string& model() const {return model_;}
    const std::shared_ptr<const ShaderBindings>& declarations() const {return declarations_;}
private:
    SourceShaderCode()=default;
    std::vector<uint8_t> bytes_;
    std::string model_;
    std::shared_ptr<const ShaderBindings> declarations_;
};
std::string source_shader_disassembly(const SourceShaderCode&,bool patch_cb_offsets,bool custom_data);
// Original signature-aware assembler. Parse notices are retained separately;
// they are nonfatal in the pinned ShaderRegex creation path.
std::vector<uint8_t> source_shader_assembly(const std::string&,std::vector<std::string>& notices);
}
