#pragma once
#include "hlsl_compiler.h"

namespace efmi::gpu {
// Translate exact original or regex-patched SM4/5 bytecode. This is not an HLSL
// recompile and does not infer native shader identity from an application SPIR-V.
class DxbcCompiler {
public:
    explicit DxbcCompiler(const std::filesystem::path& library);
    ~DxbcCompiler();
    DxbcCompiler(const DxbcCompiler&)=delete;
    DxbcCompiler& operator=(const DxbcCompiler&)=delete;
    bool compile(const std::vector<uint8_t>&,char stage,HlslProgram&,std::string& reason);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
